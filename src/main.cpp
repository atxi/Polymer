#include "connection.h"
#include "inflate.h"
#include "json.h"
#include "memory.h"
#include "polymer.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

#define WIN32_LEAN_AND_MEAN
#include <WS2tcpip.h>
#include <Windows.h>

namespace polymer {

struct BlockState {
  u32 id;
  char* name;
};

constexpr size_t kChunkCacheSize = 48;

struct Chunk {
  u32 blocks[16][16][16];
};

struct ChunkSection {
  s32 x;
  s32 z;

  Chunk chunks[16];
};

u32 GetChunkCacheIndex(s32 v) {
  return ((v % (s32)kChunkCacheSize) + (s32)kChunkCacheSize) % (s32)kChunkCacheSize;
}

struct Polymer {
  MemoryArena* perm_arena;
  MemoryArena* trans_arena;

  Connection connection;

  size_t block_name_count = 0;
  char block_names[32768][32];

  size_t block_state_count = 0;
  BlockState block_states[32768];

  // Chunk cache
  ChunkSection chunks[kChunkCacheSize][kChunkCacheSize];

  Polymer(MemoryArena* perm_arena, MemoryArena* trans_arena)
      : perm_arena(perm_arena), trans_arena(trans_arena), connection(*perm_arena) {}

  void LoadBlocks();
};

void Polymer::LoadBlocks() {
  block_state_count = 0;

  FILE* f = fopen("blocks.json", "r");
  fseek(f, 0, SEEK_END);
  long file_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  char* buffer = memory_arena_push_type_count(trans_arena, char, file_size);

  fread(buffer, 1, file_size, f);
  fclose(f);

  json_value_s* root = json_parse(buffer, file_size);
  assert(root->type == json_type_object);

  json_object_s* root_obj = json_value_as_object(root);
  assert(root_obj);

  json_object_element_s* element = root_obj->start;
  while (element) {
    json_object_s* block_obj = json_value_as_object(element->value);
    assert(block_obj);

    char* block_name = block_names[block_name_count++];
    memcpy(block_name, element->name->string, element->name->string_size);

    json_object_element_s* block_element = block_obj->start;
    while (block_element) {
      if (strncmp(block_element->name->string, "states", block_element->name->string_size) == 0) {
        json_array_s* states = json_value_as_array(block_element->value);
        json_array_element_s* state_array_element = states->start;

        while (state_array_element) {
          json_object_s* state_obj = json_value_as_object(state_array_element->value);

          json_object_element_s* state_element = state_obj->start;
          while (state_element) {
            if (strncmp(state_element->name->string, "id", state_element->name->string_size) == 0) {
              block_states[block_state_count].name = block_name;

              long block_id = strtol(json_value_as_number(state_element->value)->number, nullptr, 10);

              block_states[block_state_count].id = block_id;

              ++block_state_count;
            }
            state_element = state_element->next;
          }

          state_array_element = state_array_element->next;
        }
      }

      block_element = block_element->next;
    }

    element = element->next;
  }

  free(root);
}

enum class ProtocolState { Handshake, Status, Login, Play };

void SendHandshake(Connection* connection, u32 version, const char* address, u16 port, ProtocolState state_request) {
  RingBuffer& wb = connection->write_buffer;

  SizedString sstr;
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

  SizedString sstr;
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

void SendTeleportConfirm(Connection* connection, u64 id) {
  RingBuffer& wb = connection->write_buffer;
  u32 pid = 0x00;

  size_t size = GetVarIntSize(pid) + GetVarIntSize(0) + GetVarIntSize(id);

  wb.WriteVarInt(size);
  wb.WriteVarInt(0);
  wb.WriteVarInt(pid);

  wb.WriteVarInt(id);
}

void OnBlockChange(Polymer* polymer, s32 x, s32 y, s32 z, u32 new_bid) {
  s32 chunk_x = (s32)std::floor(x / 16.0f);
  s32 chunk_z = (s32)std::floor(z / 16.0f);

  ChunkSection* section = &polymer->chunks[GetChunkCacheIndex(chunk_x)][GetChunkCacheIndex(chunk_z)];

  // It should be in the loaded cache otherwise the server is sending about an unloaded chunk.
  assert(section->x == chunk_x);
  assert(section->z == chunk_z);

  s32 relative_x = x % 16;
  s32 relative_z = z % 16;

  if (relative_x < 0) {
    relative_x += 16;
  }

  if (relative_z < 0) {
    relative_z += 16;
  }

  u32 old_bid = section->chunks[y / 16].blocks[y % 16][relative_z][relative_x];

  section->chunks[y / 16].blocks[y % 16][relative_z][relative_x] = (u32)new_bid;

  printf("Block changed at (%d, %d, %d) from %s to %s\n", x, y, z, polymer->block_states[old_bid].name,
         polymer->block_states[new_bid].name);
}

int run() {
  constexpr size_t kMirrorBufferSize = 65536 * 32;
  constexpr size_t kPermanentSize = gigabytes(1);
  constexpr size_t kTransientSize = megabytes(32);

  u8* perm_memory = (u8*)VirtualAlloc(NULL, kPermanentSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  u8* trans_memory = (u8*)VirtualAlloc(NULL, kTransientSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

  MemoryArena perm_arena(perm_memory, kPermanentSize);
  MemoryArena trans_arena(trans_memory, kTransientSize);

  printf("Polymer\n");

  Polymer* polymer = memory_arena_construct_type(&perm_arena, Polymer, &perm_arena, &trans_arena);

  polymer->LoadBlocks();

  Connection* connection = &polymer->connection;

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
        // TODO: Remove this once rendering is started. This is just to reduce cpu usage for now.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
          SizedString sstr;
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
        } else if (pkt_id == 0x34) { // PlayerPositionAndLook
          double x = rb->ReadDouble();
          double y = rb->ReadDouble();
          double z = rb->ReadDouble();
          float yaw = rb->ReadFloat();
          float pitch = rb->ReadFloat();
          u8 flags = rb->ReadU8();

          u64 teleport_id;
          rb->ReadVarInt(&teleport_id);

          SendTeleportConfirm(connection, teleport_id);
          // TODO: Relative/Absolute
          printf("Position: (%f, %f, %f)\n", x, y, z);
        } else if (pkt_id == 0x0B) { // BlockChange
          u64 position_data = rb->ReadU64();

          u64 new_bid;
          rb->ReadVarInt(&new_bid);

          s32 x = (position_data >> (26 + 12)) & 0x3FFFFFF;
          s32 y = position_data & 0x0FFF;
          s32 z = (position_data >> 12) & 0x3FFFFFF;

          if (x >= 0x2000000) {
            x -= 0x4000000;
          }
          if (y >= 0x800) {
            y -= 0x1000;
          }
          if (z >= 0x2000000) {
            z -= 0x4000000;
          }

          OnBlockChange(polymer, x, y, z, (u32)new_bid);
        } else if (pkt_id == 0x3B) { // MultiBlockChange
          u64 xzy = rb->ReadU64();
          bool inverse = rb->ReadU8();

          s32 chunk_x = xzy >> (22 + 20);
          s32 chunk_z = (xzy >> 20) & ((1 << 22) - 1);
          s32 chunk_y = xzy & ((1 << 20) - 1);

          if (chunk_x >= (1 << 21)) {
            chunk_x -= (1 << 22);
          }

          if (chunk_z >= (1 << 21)) {
            chunk_z -= (1 << 22);
          }

          u64 count;
          rb->ReadVarInt(&count);

          for (size_t i = 0; i < count; ++i) {
            u64 data;

            rb->ReadVarInt(&data);

            u32 new_bid = (u32)(data >> 12);
            u32 relative_x = (data >> 8) & 0x0F;
            u32 relative_z = (data >> 4) & 0x0F;
            u32 relative_y = data & 0x0F;

            OnBlockChange(polymer, chunk_x * 16 + relative_x, chunk_y * 16 + relative_y, chunk_z * 16 + relative_z, new_bid);
          }

        } else if (pkt_id == 0x20) { // ChunkData
          s32 chunk_x = rb->ReadU32();
          s32 chunk_z = rb->ReadU32();
          bool is_full = rb->ReadU8();
          u64 bitmask;
          rb->ReadVarInt(&bitmask);

          SizedString sstr;
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
            for (u64 chunk_y = 0; chunk_y < 16; ++chunk_y) {
              if (!(bitmask & (1LL << chunk_y))) {
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

              ChunkSection* section = &polymer->chunks[GetChunkCacheIndex(chunk_x)][GetChunkCacheIndex(chunk_z)];
              section->x = chunk_x;
              section->z = chunk_z;
              u32* chunk = (u32*)section->chunks[chunk_y].blocks;

              u64 data_array_length;
              rb->ReadVarInt(&data_array_length);
              u64 id_mask = (1LL << bpb) - 1;
              u64 block_index = 0;

              for (u64 i = 0; i < data_array_length; ++i) {
                u64 data_value = rb->ReadU64();

                for (u64 j = 0; j < 64 / bpb; ++j) {
                  size_t palette_index = (size_t)((data_value >> (j * bpb)) & id_mask);

                  if (palette) {
                    chunk[block_index++] = (u32)palette[palette_index];
                  } else {
                    chunk[block_index++] = (u32)palette_index;
                  }
                }
              }

              if (chunk_x == 11 && chunk_y == 4 && chunk_z == 21) {
                u64 x = chunk_x * 16LL + 7;  // 183
                u64 y = chunk_y * 16 + 3;    // 67
                u64 z = chunk_z * 16LL + 10; // 346

                size_t index = 3 * 16 * 16 + 10 * 16 + 7;
                u32 block_state_id = chunk[index];
                BlockState* state = polymer->block_states + block_state_id;

                printf("Block at %llu, %llu, %llu - %s\n", x, y, z, state->name);
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
