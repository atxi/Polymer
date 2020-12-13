#include "packet_interpreter.h"

#include "gamestate.h"
#include "inflate.h"
#include "protocol.h"
#include <cassert>
#include <cstdio>

namespace polymer {

PacketInterpreter::PacketInterpreter(GameState* game)
    : game(game), compression(false), inflate_buffer(*game->perm_arena, 65536 * 32) {}

void PacketInterpreter::InterpretPlay(RingBuffer* rb, u64 pkt_id, size_t pkt_size) {
  MemoryArena* trans_arena = game->trans_arena;
  Connection* connection = &game->connection;

  PlayProtocol type = (PlayProtocol)pkt_id;

  assert(type < PlayProtocol::Count);

  switch (type) {
  case PlayProtocol::ChatMessage: {
    SizedString sstr;
    sstr.str = memory_arena_push_type_count(trans_arena, char, 32767);
    sstr.size = 32767;

    size_t length = rb->ReadString(&sstr);

    if (length > 0) {
      printf("%.*s\n", (int)length, sstr.str);
    }
  } break;
  case PlayProtocol::Disconnect: {
    SizedString sstr;
    sstr.str = memory_arena_push_type_count(trans_arena, char, 32767);
    sstr.size = 32767;

    size_t length = rb->ReadString(&sstr);

    if (length > 0) {
      printf("Disconnected: %.*s\n", (int)length, sstr.str);
    }
  } break;
  case PlayProtocol::Explosion: {
    float x = rb->ReadFloat();
    float y = rb->ReadFloat();
    float z = rb->ReadFloat();
    float strength = rb->ReadFloat();
    u32 records = rb->ReadU32();

    for (u32 i = 0; i < records; ++i) {
      s8 x_offset = rb->ReadU8();
      s8 y_offset = rb->ReadU8();
      s8 z_offset = rb->ReadU8();

      game->OnBlockChange((s32)x + x_offset, (s32)y + y_offset, (s32)z + z_offset, 0);
    }
  } break;
  case PlayProtocol::KeepAlive: {
    u64 id = rb->ReadU64();

    connection->SendKeepAlive(id);
    printf("Sending keep alive %llu\n", id);
    fflush(stdout);
  } break;
  case PlayProtocol::PlayerPositionAndLook: {
    double x = rb->ReadDouble();
    double y = rb->ReadDouble();
    double z = rb->ReadDouble();
    float yaw = rb->ReadFloat();
    float pitch = rb->ReadFloat();
    u8 flags = rb->ReadU8();

    u64 teleport_id;
    rb->ReadVarInt(&teleport_id);

    connection->SendTeleportConfirm(teleport_id);
    // TODO: Relative/Absolute
    printf("Position: (%f, %f, %f)\n", x, y, z);
  } break;
  case PlayProtocol::UpdateHealth: {
    float health = rb->ReadFloat();

    printf("Health: %f\n", health);
    if (health <= 0.0f) {
      connection->SendClientStatus(Connection::ClientStatusAction::Respawn);
      printf("Sending respawn packet.\n");
    }
  } break;
  case PlayProtocol::BlockChange: {
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

    game->OnBlockChange(x, y, z, (u32)new_bid);
  } break;
  case PlayProtocol::MultiBlockChange: {
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

      game->OnBlockChange(chunk_x * 16 + relative_x, chunk_y * 16 + relative_y, chunk_z * 16 + relative_z, new_bid);
    }
  } break;
  case PlayProtocol::ChunkData: {
    s32 chunk_x = rb->ReadU32();
    s32 chunk_z = rb->ReadU32();
    bool is_full = rb->ReadU8();
    u64 bitmask;
    rb->ReadVarInt(&bitmask);

    SizedString sstr;
    sstr.str = memory_arena_push_type_count(trans_arena, char, 32767);
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

          palette = memory_arena_push_type_count(trans_arena, u64, (size_t)palette_length);

          for (u64 i = 0; i < palette_length; ++i) {
            u64 palette_data;
            rb->ReadVarInt(&palette_data);
            palette[i] = palette_data;
          }
        }

        ChunkSection* section = &game->chunks[GetChunkCacheIndex(chunk_x)][GetChunkCacheIndex(chunk_z)];
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
          BlockState* state = game->block_states + block_state_id;

          printf("Block at %llu, %llu, %llu - %s\n", x, y, z, state->name);
        }
      }

      // Delay the chunk load call until the entire section is loaded.
      for (u64 chunk_y = 0; chunk_y < 16; ++chunk_y) {
        if (!(bitmask & (1LL << chunk_y))) {
          continue;
        }

        game->OnChunkLoad(chunk_x, (s32)chunk_y, chunk_z);
      }
    }

    // Jump to after the data because the data_size can be larger than actual chunk data sent according to
    // documentation.
    rb->read_offset = new_offset;
    u64 block_entity_count;
    rb->ReadVarInt(&block_entity_count);

#if 0
    if (block_entity_count > 0) {
      printf("Block entity count: %llu in chunk (%d, %d)\n", block_entity_count, chunk_x, chunk_z);
    }
#endif
  } break;
  default:
    break;
  }
}

void PacketInterpreter::InterpretLogin(RingBuffer* rb, u64 pkt_id, size_t pkt_size) {
  MemoryArena* trans_arena = game->trans_arena;
  Connection* connection = &game->connection;

  LoginProtocol type = (LoginProtocol)pkt_id;

  assert(type < LoginProtocol::Count);

  switch (type) {
  case LoginProtocol::Disconnect: {
    SizedString sstr;
    sstr.str = memory_arena_push_type_count(trans_arena, char, 32767);
    sstr.size = 32767;

    size_t length = rb->ReadString(&sstr);

    if (length > 0) {
      printf("Disconnect reason: %.*s\n", (int)length, sstr.str);
    }

    connection->Disconnect();
  } break;
  case LoginProtocol::EncryptionRequest: {
    printf("EncryptionRequest. Not yet implemented\n");
    connection->Disconnect();
  } break;
  case LoginProtocol::LoginSuccess: {
    printf("Login success\n");
    connection->protocol_state = ProtocolState::Play;
  } break;
  case LoginProtocol::SetCompression: {
    compression = true;
  } break;
  default:
    break;
  }
}

void PacketInterpreter::InterpretStatus(RingBuffer* rb, u64 pkt_id, size_t pkt_size) {
  MemoryArena* trans_arena = game->trans_arena;
  Connection* connection = &game->connection;

  StatusProtocol type = (StatusProtocol)pkt_id;

  assert(type < StatusProtocol::Count);
}

void PacketInterpreter::Interpret() {
  MemoryArena* trans_arena = game->trans_arena;
  Connection* connection = &game->connection;
  RingBuffer* rb = &connection->read_buffer;

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

    switch (connection->protocol_state) {
    case ProtocolState::Status:
      this->InterpretStatus(rb, pkt_id, (size_t)pkt_size);
      break;
    case ProtocolState::Login:
      this->InterpretLogin(rb, pkt_id, (size_t)pkt_size);
      break;
    case ProtocolState::Play:
      this->InterpretPlay(rb, pkt_id, (size_t)pkt_size);
      break;
    default:
      break;
    }

    // Always skip to the next packet in case some data wasn't read.
    rb->read_offset = target_offset;

    rb = &connection->read_buffer;
  } while (rb->read_offset != rb->write_offset);
}

} // namespace polymer
