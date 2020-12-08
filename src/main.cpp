#include "connection.h"
#include "inflate.h"
#include "memory.h"
#include "polymer.h"
#include "json.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cmath>

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
  constexpr size_t kMirrorBufferSize = 65536 * 32;
  constexpr size_t kPermanentSize = megabytes(32);
  constexpr size_t kTransientSize = megabytes(32);

  u8* perm_memory = (u8*)VirtualAlloc(NULL, kPermanentSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  u8* trans_memory = (u8*)VirtualAlloc(NULL, kTransientSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

  MemoryArena perm_arena(perm_memory, kPermanentSize);
  MemoryArena trans_arena(trans_memory, kTransientSize);

  printf("Polymer\n");

  Connection* connection = memory_arena_construct_type(&perm_arena, Connection, perm_arena);

  // Allocate mirrored ring buffers so they can always be inflated
  connection->read_buffer.size = kMirrorBufferSize;
  connection->read_buffer.data = AllocateMirroredBuffer(connection->read_buffer.size);
  connection->write_buffer.size = kMirrorBufferSize;
  connection->write_buffer.data = AllocateMirroredBuffer(connection->write_buffer.size);

  assert(connection->read_buffer.data);
  assert(connection->write_buffer.data);

  // Inflate buffer doesn't need mirrored because it always operates from byte zero.
  RingBuffer inflate_buffer(perm_arena, kMirrorBufferSize);

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

  SendHandshake(connection, 754, "127.0.0.1", 25565, ProtocolState::Login);
  SendLoginStart(connection, "polymer");

  bool compression = false;

  while (connection->connected) {
    trans_arena.Reset();

    RingBuffer* rb = &connection->read_buffer;
    RingBuffer* wb = &connection->write_buffer;

    while (wb->write_offset != wb->read_offset) {
      int bytes_sent = 0;

      if (wb->write_offset > wb->read_offset) {
        // Send up to the write offset
        bytes_sent =
            send(connection->fd, (char*)wb->data + wb->read_offset, (int)(wb->write_offset - wb->read_offset), 0);
      } else if (wb->write_offset < wb->read_offset) {
        // Send up to the end of the buffer then loop again to send the remaining up to the write offset
        bytes_sent = send(connection->fd, (char*)wb->data + wb->read_offset, (int)(wb->size - wb->read_offset), 0);
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

      fprintf(stderr, "Unexpected socket error: %d\n", err);
      connection->Disconnect();
      break;
    } else if (bytes_recv > 0) {
      rb->write_offset = (rb->write_offset + bytes_recv) % rb->size;

      do {
        size_t offset_snapshot = rb->read_offset;
        u64 pkt_size = 0;
        u64 pkt_id = 0;

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

            mz_ulong mz_size = (mz_ulong)inflate_buffer.size;
            int result = mz_uncompress((u8*)inflate_buffer.data, (mz_ulong*)&mz_size, (u8*)rb->data + rb->read_offset,
                                       (mz_ulong)payload_size);

            assert(result == MZ_OK);
            rb->read_offset = target_offset;
            rb = &inflate_buffer;
          }
        }

        bool id_read = rb->ReadVarInt(&pkt_id);
        assert(id_read);

        if (pkt_id == 0x03) {
          compression = true;
        } else if (pkt_id == 0x0E) {
          sized_string sstr;
          sstr.str = memory_arena_push_type_count(&trans_arena, char, 32767);
          sstr.size = 32767;

          size_t length = rb->ReadString(&sstr);

          if (length > 0) {
            printf("%.*s\n", (int)length, sstr.str);
          }
        } else if (pkt_id == 0x1F) { // Keep-alive
          u64 id = rb->ReadU64();

          SendKeepAlive(connection, id);
          printf("Sending keep alive %llu\n", id);
          fflush(stdout);
        } else if (pkt_id == 0x20) { // Chunk data
          s32 chunk_x = rb->ReadU32();
          s32 chunk_z = rb->ReadU32();
          bool is_full = rb->ReadU8();
          u64 bitmask;
          rb->ReadVarInt(&bitmask);

          sized_string sstr;
          sstr.str = memory_arena_push_type_count(&trans_arena, char, 32767);
          sstr.size = 32767;

          // TODO: Implement simple NBT parsing api
          // Tag Compound
          u8 nbt_type = rb->ReadU8();
          assert(nbt_type == 10);

          u16 length = rb->ReadU16();

          // Root compound name - empty
          rb->ReadRawString(&sstr, length);

          for (int i = 0; i < 2; ++i) {
            // Tag Long Array
            nbt_type = rb->ReadU8();
            assert(nbt_type == 12);

            length = rb->ReadU16();
            // LongArray tag name
            rb->ReadRawString(&sstr, length);

            u32 count = rb->ReadU32();
            for (u32 j = 0; j < count; ++j) {
              u64 data = rb->ReadU64();
            }
          }
          u8 end_tag = rb->ReadU8();
          assert(end_tag == 0);

          if (is_full) {
            u64 biome_length;
            rb->ReadVarInt(&biome_length);

            for (u64 i = 0; i < biome_length; ++i) {
              u64 biome;

              rb->ReadVarInt(&biome);
            }
          }
          u64 data_size;
          rb->ReadVarInt(&data_size);

          size_t new_offset = (rb->read_offset + data_size) % rb->size;

          if (data_size > 0) {
            for (u64 i = 0; i < 16; ++i) {
              if (!(bitmask & (1LL << i))) {
                continue;
              }

              // Read Chunk data here
              u16 block_count = rb->ReadU16();
              u8 bpb = rb->ReadU8();

              if (bpb < 4) {
                bpb = 4;
              }

              u64* palette = nullptr;
              u64 palette_length = 0;
              if (bpb < 9) {
                rb->ReadVarInt(&palette_length);

                palette = memory_arena_push_type_count(&trans_arena, u64, (size_t)palette_length);

                for (u64 i = 0; i < palette_length; ++i) {
                  u64 palette_data;
                  rb->ReadVarInt(&palette_data);
                  palette[i] = palette_data;
                }
              }

              u64 base_x = chunk_x * 16LL;
              u64 base_y = i * 16;
              u64 base_z = chunk_z * 16LL;

              u32* chunk = memory_arena_push_type_count(&trans_arena, u32, 16 * 16 * 16);

              u64 data_array_length;
              rb->ReadVarInt(&data_array_length);
              u64 id_mask = (u64)std::pow(2, bpb) - 1;
              u64 block_index = 0;

              for (u64 i = 0; i < data_array_length; ++i) {
                u64 data_value = rb->ReadU64();

                for (u64 j = 0; j < 64 / bpb; ++j) {
                  size_t palette_index = (size_t)((data_value >> (j * bpb)) & id_mask);

                  if (palette) {
                    chunk[block_index++] = (u32)palette[palette_index];
                  } else {
                    chunk[block_index++] = palette_index;
                  }
                }
              }
            }
          }

          // Jump to after the data because the data_size can be larger than actual chunk data sent according to
          // documentation.
          rb->read_offset = new_offset;
          u64 block_entity_count;
          rb->ReadVarInt(&block_entity_count);

          if (block_entity_count > 0) {
            printf("Block entity count: %llu in chunk (%d, %d)\n", block_entity_count, chunk_x, chunk_z);
          }
        }

        // skip every packet until they are implemented
        rb->read_offset = target_offset;

        rb = &connection->read_buffer;
        // printf("%llu\n", pkt_id);
      } while (rb->read_offset != rb->write_offset);
    }
  }

  return 0;
}

} // namespace polymer

int main(int argc, char* argv[]) {
  return polymer::run();
}
