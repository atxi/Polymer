#include "connection.h"
#include "inflate.h"
#include "memory.h"
#include "polymer.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>

#pragma comment(lib, "ws2_32.lib")

#define WIN32_LEAN_AND_MEAN
#include <WS2tcpip.h>
#include <Windows.h>

namespace polymer {

// Allocate virtual pages that mirror the first half
u8* AllocateMirrorRingBuffer(size_t size) {
  SYSTEM_INFO sys_info = {0};

  GetSystemInfo(&sys_info);

  size_t granularity = sys_info.dwAllocationGranularity;

  if (((size / granularity) * granularity) != size) {
    fprintf(stderr, "Incorrect size. Must be multiple of %zd\n", granularity);
    return 0;
  }

  HANDLE map_handle =
      CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE | SEC_COMMIT, 0, (DWORD)size * 2, NULL);

  if (map_handle == NULL) {
    return 0;
  }

  u8* buffer = (u8*)VirtualAlloc(NULL, size * 2, MEM_RESERVE, PAGE_READWRITE);

  if (buffer == NULL) {
    CloseHandle(map_handle);
    return 0;
  }

  VirtualFree(buffer, 0, MEM_RELEASE);

  u8* view = (u8*)MapViewOfFileEx(map_handle, FILE_MAP_ALL_ACCESS, 0, 0, size, buffer);

  if (view == NULL) {
    u32 error = GetLastError();
    CloseHandle(map_handle);
    return 0;
  }

  u8* mirror_view = (u8*)MapViewOfFileEx(map_handle, FILE_MAP_ALL_ACCESS, 0, 0, size, buffer + size);

  if (mirror_view == NULL) {
    u32 error = GetLastError();

    UnmapViewOfFile(view);
    CloseHandle(map_handle);
    return 0;
  }

  return view;
}

enum class ProtocolState { Handshake, Status, Login, Play };

void SendHandshake(Connection* connection, u32 version, const char* address, u16 port, ProtocolState state_request) {
  RingBuffer& wb = connection->write_buffer;

  sized_string sstr;
  sstr.str = (char*)address;
  sstr.size = strlen(address);

  u32 pid = 0;

  size_t size = GetVarIntSize(pid) + GetVarIntSize(version) + GetVarIntSize(sstr.size) + sstr.size + sizeof(u16) +
                GetVarIntSize((u64)state_request);

  wb.WriteVarInt(size);
  wb.WriteVarInt(pid);

  wb.WriteVarInt(version);
  wb.WriteString(sstr);
  wb.WriteU16(port);
  wb.WriteVarInt((u64)state_request);
}

void SendPingRequest(Connection* connection) {
  RingBuffer& wb = connection->write_buffer;

  u32 pid = 0;
  size_t size = GetVarIntSize(pid);

  wb.WriteVarInt(size);
  wb.WriteVarInt(pid);
}

void SendLoginStart(Connection* connection, const char* username) {
  RingBuffer& wb = connection->write_buffer;

  sized_string sstr;
  sstr.str = (char*)username;
  sstr.size = strlen(username);

  u32 pid = 0;
  size_t size = GetVarIntSize(pid) + GetVarIntSize(sstr.size) + sstr.size;

  wb.WriteVarInt(size);
  wb.WriteVarInt(pid);
  wb.WriteString(sstr);
}

void SendKeepAlive(Connection* connection, u64 id) {
  RingBuffer& wb = connection->write_buffer;

  u32 pid = 0x10;
  size_t size = GetVarIntSize(pid) + GetVarIntSize(0) + sizeof(id);

  wb.WriteVarInt(size);
  wb.WriteVarInt(0); // compression
  wb.WriteVarInt(pid);

  wb.WriteU64(id);
}

int run() {
  constexpr size_t perm_size = megabytes(32);
  u8* perm_memory = (u8*)VirtualAlloc(NULL, perm_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

  constexpr size_t trans_size = megabytes(32);
  u8* trans_memory = (u8*)VirtualAlloc(NULL, trans_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

  MemoryArena perm_arena(perm_memory, perm_size);
  MemoryArena trans_arena(trans_memory, trans_size);

  printf("Polymer\n");

  Connection* connection = memory_arena_construct_type(&perm_arena, Connection, perm_arena);

  ConnectResult connect_result = connection->Connect("127.0.0.1", 25565);

  // Allocate mirrored ring buffers so they can always be inflated
  connection->read_buffer.size = 65536 * 32;
  connection->read_buffer.data = AllocateMirrorRingBuffer(connection->read_buffer.size);
  connection->write_buffer.size = 65536 * 32;
  connection->write_buffer.data = AllocateMirrorRingBuffer(connection->write_buffer.size);

  assert(connection->read_buffer.data);
  assert(connection->write_buffer.data);

  switch (connect_result) {
  case ConnectResult::ErrorSocket: {
    fprintf(stderr, "Failed to create socket\n");
    return 1;
  }
  case ConnectResult::ErrorAddrInfo: {
    fprintf(stderr, "Failed to get address info\n");
    return 1;
  }
  case ConnectResult::ErrorConnect: {
    fprintf(stderr, "Failed to connect\n");
    return 1;
  }
  default:
    break;
  }

  printf("Connected to server.\n");

  connection->SetBlocking(false);

  SendHandshake(connection, 753, "127.0.0.1", 25565, ProtocolState::Login);
  // SendPingRequest(connection);

  SendLoginStart(connection, "polymer");

  bool compression = false;

  RingBuffer inflate_buffer(perm_arena, 65536 * 32);

  while (connection->connected) {
    trans_arena.Reset();

    RingBuffer* rb = &connection->read_buffer;
    RingBuffer* wb = &connection->write_buffer;

    while (wb->write_offset != wb->read_offset) {
      int bytes_sent = 0;

      if (wb->write_offset > wb->read_offset) {
        // Send up to the write offset
        bytes_sent = send(connection->fd, (char*)wb->data + wb->read_offset, wb->write_offset - wb->read_offset, 0);
      } else if (wb->write_offset < wb->read_offset) {
        // Send up to the end of the buffer then loop again to send the remaining up to the write offset
        bytes_sent = send(connection->fd, (char*)wb->data + wb->read_offset, wb->size - wb->read_offset, 0);
      }

      if (bytes_sent > 0) {
        wb->read_offset = (wb->read_offset + bytes_sent) % wb->size;
      }
    }

    int bytes_recv = recv(connection->fd, (char*)rb->data + rb->write_offset, (u32)rb->GetFreeSize(), 0);

    if (bytes_recv == 0) {
      fprintf(stderr, "Connection closed by server.\n");
      connection->connected = false;
      break;
    } else if (bytes_recv < 0) {
      int err = WSAGetLastError();

      if (err == WSAEWOULDBLOCK) {
        continue;
      }

      fprintf(stderr, "Error: %d\n", err);
      connection->Disconnect();
      break;
    } else if (bytes_recv > 0) {
      rb->write_offset = (rb->write_offset + bytes_recv) % rb->size;

      do {
        size_t offset_snapshot = rb->read_offset;
        u64 pkt_size;
        u64 pkt_id;

        if (!rb->ReadVarInt(&pkt_size)) {
          break;
        }

        if (rb->GetReadAmount() < pkt_size) {
          rb->read_offset = offset_snapshot;
          break;
        }

        size_t target_offset = (rb->read_offset + pkt_size) % rb->size;

        if (compression) {
          u64 payload_size;

          rb->ReadVarInt(&payload_size);

          if (payload_size > 0) {
            inflate_buffer.write_offset = inflate_buffer.read_offset = 0;
            
            mz_ulong mz_size = inflate_buffer.size;
            int result = mz_uncompress((u8*)inflate_buffer.data, (mz_ulong*)&mz_size,
                                       (u8*)rb->data + rb->read_offset, (mz_ulong)payload_size);

            assert(result == MZ_OK);
            rb->read_offset = target_offset;
            rb = &inflate_buffer;
            int b = 0;
          }
        }

        assert(rb->ReadVarInt(&pkt_id));

        if (pkt_id == 0x03) {
          compression = true;
        } else if (pkt_id == 0x0E) {
          sized_string sstr;
          sstr.str = memory_arena_push_type_count(&trans_arena, char, 32767);
          sstr.size = 32767;

          size_t length = rb->ReadString(&sstr);

          if (length > 0) {
            printf("%.*s\n", length, sstr.str);
          }
        } else if (pkt_id == 0x1F) { // Keep-alive
          u64 id = rb->ReadU64();

          SendKeepAlive(connection, id);
          printf("Sending keep alive\n");
        }

        // skip every packet until they are implemented
        rb->read_offset = target_offset;

        rb = &connection->read_buffer;
        //printf("%llu\n", pkt_id);
      } while (rb->read_offset != rb->write_offset);
    }
  }

  return 0;
}

} // namespace polymer

int main(int argc, char* argv[]) {
  return polymer::run();
}
