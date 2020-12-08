#include "connection.h"
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

  SendHandshake(connection, 754, "127.0.0.1", 25565, ProtocolState::Status);
  SendPingRequest(connection);

  while (connection->connected) {
    trans_arena.Reset();

    RingBuffer* rb = &connection->write_buffer;
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
      fprintf(stderr, "Bytes recv zero\n");
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

      size_t offset_snapshot = rb->read_offset;
      u64 pkt_size;
      u64 pkt_id;

      if (!rb->ReadVarInt(&pkt_size)) {
        continue;
      }

      if (rb->GetReadAmount() < pkt_size) {
        continue;
      }

      assert(rb->ReadVarInt(&pkt_id));

      sized_string sstr;
      sstr.size = 65535;
      sstr.str = memory_arena_push_type_count(&trans_arena, char, sstr.size);

      size_t ping_size = rb->ReadString(&sstr);

      printf("%.*s\n", ping_size, sstr.str);

      rb->read_offset = (rb->read_offset + bytes_recv) % rb->size;
      connection->Disconnect();
    }
  }

  return 0;
}

} // namespace polymer

int main(int argc, char* argv[]) {
  return polymer::run();
}
