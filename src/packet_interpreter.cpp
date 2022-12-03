#include "packet_interpreter.h"

#include "gamestate.h"
#include "miniz.h"
#include "nbt.h"
#include "protocol.h"

#include <cassert>
#include <cstdio>

#define LOG_PACKET_ID 0

using polymer::world::DimensionCodec;
using polymer::world::DimensionType;
using polymer::world::ChunkSection;
using polymer::world::ChunkSectionInfo;
using polymer::world::kChunkColumnCount;

namespace polymer {

PacketInterpreter::PacketInterpreter(GameState* game)
    : game(game), compression(false), inflate_buffer(*game->perm_arena, 65536 * 32) {}

void PacketInterpreter::InterpretPlay(RingBuffer* rb, u64 pkt_id, size_t pkt_size) {
  MemoryArena* trans_arena = game->trans_arena;
  Connection* connection = &game->connection;

  PlayProtocol type = (PlayProtocol)pkt_id;

  assert(type < PlayProtocol::Count);

#if LOG_PACKET_ID
  printf("InterpetPlay: %lld\n", pkt_id);
#endif

  switch (type) {
  case PlayProtocol::SystemChatMessage: {
    String sstr;
    sstr.data = memory_arena_push_type_count(trans_arena, char, 32767);
    sstr.size = 32767;

    size_t length = rb->ReadString(&sstr);

    printf("System: %.*s\n", (int)length, sstr.data);

    u64 type = 0;
    rb->ReadVarInt(&type);
    printf("Type: %lld\n", type);
  } break;
  case PlayProtocol::PlayerChatMessage: {
    u64 mesg_signature_size = 0;
    bool has_mesg_signature = rb->ReadU8();

    if (has_mesg_signature) {
      rb->ReadVarInt(&mesg_signature_size);

      String mesg_signature;
      mesg_signature.data = memory_arena_push_type_count(trans_arena, char, mesg_signature_size);
      mesg_signature.size = mesg_signature_size;

      rb->ReadRawString(&mesg_signature, mesg_signature_size);
    }

    String sstr;
    sstr.data = memory_arena_push_type_count(trans_arena, char, 32767);
    sstr.size = 32767;

    // Read and discard Sender UUID
    rb->ReadRawString(&sstr, 16);

    // Read and discard header signature
    u64 header_sig_size = 0;
    rb->ReadVarInt(&header_sig_size);
    assert(header_sig_size <= sstr.size);
    rb->ReadRawString(&sstr, header_sig_size);

    // Read plain message
    size_t mesg_length = rb->ReadString(&sstr);

    bool has_formatted_mesg = rb->ReadU8();

    if (has_formatted_mesg) {
      mesg_length = rb->ReadString(&sstr);
    }

    if (mesg_length > 0) {
      printf("%.*s\n", (int)mesg_length, sstr.data);
    }

    u64 timestamp = rb->ReadU64();
    u64 salt = rb->ReadU64();
  } break;
  case PlayProtocol::Disconnect: {
    String sstr;
    sstr.data = memory_arena_push_type_count(trans_arena, char, 32767);
    sstr.size = 32767;

    size_t length = rb->ReadString(&sstr);

    if (length > 0) {
      printf("Disconnected: %.*s\n", (int)length, sstr.data);
    }
  } break;
  case PlayProtocol::Explosion: {
    float x = rb->ReadFloat();
    float y = rb->ReadFloat();
    float z = rb->ReadFloat();
    float strength = rb->ReadFloat();

    u64 records;
    rb->ReadVarInt(&records);

    for (u64 i = 0; i < records; ++i) {
      s8 x_offset = rb->ReadU8();
      s8 y_offset = rb->ReadU8();
      s8 z_offset = rb->ReadU8();

      game->OnBlockChange((s32)x + x_offset, (s32)y + y_offset, (s32)z + z_offset, 0);
    }

    float velocity_x = rb->ReadFloat();
    float velocity_y = rb->ReadFloat();
    float velocity_z = rb->ReadFloat();
  } break;
  case PlayProtocol::UnloadChunk: {
    s32 chunk_x = rb->ReadU32();
    s32 chunk_z = rb->ReadU32();

    game->OnChunkUnload(chunk_x, chunk_z);
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
    game->OnPlayerPositionAndLook(Vector3f((float)x, (float)y, (float)z), yaw, pitch);
  } break;
  case PlayProtocol::UpdateHealth: {
    float health = rb->ReadFloat();

    printf("Health: %f\n", health);
    if (health <= 0.0f) {
      connection->SendClientStatus(Connection::ClientStatusAction::Respawn);
      printf("Sending respawn packet.\n");
    }
  } break;
  case PlayProtocol::BlockUpdate: {
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
  case PlayProtocol::Login: {
    u32 entity_id = rb->ReadU32();
    bool is_hardcore = rb->ReadU8();
    u8 gamemode = rb->ReadU8();
    u8 previous_gamemode = rb->ReadU8();

    u64 world_count = 0;
    rb->ReadVarInt(&world_count);

    String sstr;
    sstr.data = memory_arena_push_type_count(trans_arena, char, 32767);
    sstr.size = 32767;

    // Read all of the dimensions
    for (size_t i = 0; i < world_count; ++i) {
      size_t length = rb->ReadString(&sstr);
    }

    nbt::TagCompound dimension_codec_nbt;

    if (!nbt::Parse(*rb, *trans_arena, &dimension_codec_nbt)) {
      fprintf(stderr, "Failed to parse dimension codec nbt.\n");
    }

    game->dimension_codec.Parse(*game->perm_arena, dimension_codec_nbt);

    String dimension_type_string;
    dimension_type_string.data = memory_arena_push_type_count(trans_arena, char, 32767);
    dimension_type_string.size = 32767;

    dimension_type_string.size = rb->ReadString(&dimension_type_string);

    DimensionType* dimension_type = game->dimension_codec.GetDimensionType(dimension_type_string);

    if (dimension_type) {
      game->dimension = *dimension_type;
    } else {
      fprintf(stderr, "Failed to find dimension type %.*s in codec.\n", (u32)dimension_type_string.size,
              dimension_type_string.data);
    }

    String dimension_identifier;
    dimension_identifier.data = memory_arena_push_type_count(trans_arena, char, 32767);
    dimension_identifier.size = 32767;

    rb->ReadString(&dimension_identifier);

    if (dimension_identifier.size > 0) {
      printf("Dimension: %.*s\n", (u32)dimension_identifier.size, dimension_identifier.data);
    }

    printf("Entered dimension with height range of %d to %d\n", game->dimension.min_y,
           (game->dimension.height + game->dimension.min_y));
  } break;
  case PlayProtocol::Respawn: {
    String dimension_type_string;

    dimension_type_string.data = memory_arena_push_type_count(trans_arena, char, 32767);
    dimension_type_string.size = 32767;

    dimension_type_string.size = rb->ReadString(&dimension_type_string);

    DimensionType* dimension_type = game->dimension_codec.GetDimensionType(dimension_type_string);

    if (dimension_type) {
      game->dimension = *dimension_type;
    } else {
      fprintf(stderr, "Failed to find dimension type %.*s in codec.\n", (u32)dimension_type_string.size,
              dimension_type_string.data);
    }

    printf("Entered dimension with height range of %d to %d\n", game->dimension.min_y,
           (game->dimension.height + game->dimension.min_y));

    game->OnDimensionChange();
  } break;
  case PlayProtocol::UpdateSectionBlocks: {
    u64 xzy = rb->ReadU64();
    bool inverse = rb->ReadU8();

    s32 chunk_x = xzy >> (22 + 20);
    s32 chunk_z = (xzy >> 20) & ((1 << 22) - 1);
    s32 chunk_y = xzy & ((1 << 20) - 1);

    if (chunk_x >= (1 << 21)) {
      chunk_x -= (1 << 22);
    }

    if (chunk_y >= (1 << 19)) {
      chunk_y -= (1 << 20);
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

    String sstr;
    sstr.data = memory_arena_push_type_count(trans_arena, char, 32767);
    sstr.size = 32767;

    nbt::TagCompound nbt;

    if (!nbt::Parse(*rb, *trans_arena, &nbt)) {
      fprintf(stderr, "Failed to parse chunk nbt.\n");
      fflush(stderr);
    }

    u64 data_size;
    rb->ReadVarInt(&data_size);

    size_t new_offset = (rb->read_offset + data_size) % rb->size;

    u32 x_index = game->world.GetChunkCacheIndex(chunk_x);
    u32 z_index = game->world.GetChunkCacheIndex(chunk_z);

    ChunkSection* section = &game->world.chunks[z_index][x_index];
    ChunkSectionInfo* section_info = &game->world.chunk_infos[z_index][x_index];

    section_info->x = chunk_x;
    section_info->z = chunk_z;
    section_info->bitmask = 0;

    if (data_size > 0) {
      u32 end_y = kChunkColumnCount;
      u64 start_y = 0;

      if (game->dimension.height > 0) {
        end_y = game->dimension.height / 16;
        start_y = (game->dimension.min_y / 16) + (64 / 16);
      }

      for (u64 chunk_y = start_y; chunk_y < end_y; ++chunk_y) {
        // Read Chunk data here
        u16 block_count = rb->ReadU16();
        u8 bpb = rb->ReadU8();

        if (block_count > 0) {
          section_info->bitmask |= (1 << chunk_y);
        }

        u64* palette = nullptr;
        u64 single_palette = 0;

        if (bpb == 0) {
          rb->ReadVarInt(&single_palette);
          palette = &single_palette;
        } else if (bpb < 9) {
          if (bpb < 4) bpb = 4;

          u64 palette_length = 0;
          rb->ReadVarInt(&palette_length);

          palette = memory_arena_push_type_count(trans_arena, u64, (size_t)palette_length);

          for (u64 i = 0; i < palette_length; ++i) {
            u64 palette_data;
            rb->ReadVarInt(&palette_data);
            palette[i] = palette_data;
          }
        }

        u32* chunk = (u32*)section->chunks[chunk_y].blocks;

        u64 data_array_length;
        rb->ReadVarInt(&data_array_length);

        u64 id_mask = (1LL << bpb) - 1;
        u64 block_index = 0;

        // Fill out entire chunk with the one block palette
        if (data_array_length == 0 && bpb == 0) {
          for (int i = 0; i < 16 * 16 * 16; ++i) {
            chunk[i] = (u32)single_palette;
          }
        }

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

        u8 biome_bpe = rb->ReadU8();

        u64* biome_palette = nullptr;
        u64 single_biome_palette = 0;

        if (biome_bpe == 0) {
          rb->ReadVarInt(&single_biome_palette);
          biome_palette = &single_biome_palette;
        } else if (biome_bpe < 9) {
          u64 biome_palette_length = 0;
          rb->ReadVarInt(&biome_palette_length);

          biome_palette = memory_arena_push_type_count(trans_arena, u64, (size_t)biome_palette_length);

          for (u64 i = 0; i < biome_palette_length; ++i) {
            u64 biome_palette_data;
            rb->ReadVarInt(&biome_palette_data);
            biome_palette[i] = biome_palette_data;
          }
        }

        u64 biome_data_array_length;
        rb->ReadVarInt(&biome_data_array_length);

        u64 biome_id_mask = (1LL << biome_bpe) - 1;
        u64 biome_index = 0;

        for (u64 i = 0; i < biome_data_array_length; ++i) {
          u64 data_value = rb->ReadU64();

          for (u64 j = 0; j < 64 / biome_bpe; ++j) {
            size_t palette_index = (size_t)((data_value >> (j * biome_bpe)) & biome_id_mask);

            if (biome_palette) {
              // u32 biome_id = (u32)biome_palette[palette_index];
            } else {
              // u32 biome_id = (u32)palette_index;
            }

            ++biome_index;
          }
        }
      }
    }

    // Delay the chunk load call until the entire section is loaded.
    game->OnChunkLoad(chunk_x, chunk_z);

    // Jump to after the data because the data_size can be larger than actual chunk data sent according to
    // documentation.
    rb->read_offset = new_offset;
    u64 block_entity_count;
    rb->ReadVarInt(&block_entity_count);
  } break;
  case PlayProtocol::PlayerInfo: {
    u64 action_value, player_count;

    if (!rb->ReadVarInt(&action_value)) {
      fprintf(stderr, "Failed to read PlayerInfo action varint.\n");
      break;
    }

    if (!rb->ReadVarInt(&player_count)) {
      fprintf(stderr, "Failed to read PlayerInfo player count varint.\n");
      break;
    }

    for (u64 i = 0; i < player_count; ++i) {
      ArenaSnapshot snapshot = trans_arena->GetSnapshot();

      String uuid_string;

      uuid_string.data = memory_arena_push_type_count(trans_arena, char, 16);
      uuid_string.size = 16;

      rb->ReadRawString(&uuid_string, 16);

      enum class PlayerInfoAction { Add, UpdateGamemode, UpdateLatency, UpdateDisplayName, Remove, Count };

      if (action_value >= (u64)PlayerInfoAction::Count) {
        fprintf(stderr, "Failed to read valid PlayerInfo action.\n");
        break;
      }
      PlayerInfoAction action = (PlayerInfoAction)action_value;

      switch (action) {
        case PlayerInfoAction::Add: {
          String name;

          name.data = memory_arena_push_type_count(trans_arena, char, 16);
          name.size = 16;

          name.size = rb->ReadString(&name);

          String property_name;
          property_name.data = memory_arena_push_type_count(trans_arena, char, 32767);
          property_name.size = 32767;

          String property_value;
          property_value.data = memory_arena_push_type_count(trans_arena, char, 32767);
          property_value.size = 32767;

          String signature;
          signature.data = memory_arena_push_type_count(trans_arena, char, 32767);
          signature.size = 32767;

          u64 property_count;
          rb->ReadVarInt(&property_count);

          for (size_t i = 0; i < property_count; ++i) {
            property_name.size = rb->ReadString(&property_name);
            property_value.size = rb->ReadString(&property_value);

            u8 is_signed = rb->ReadU8();

            if (is_signed) {
              signature.size = rb->ReadString(&signature);
            }
          }

          u64 gamemode, ping;
          rb->ReadVarInt(&gamemode);
          rb->ReadVarInt(&ping);

          u8 has_display_name = rb->ReadU8();
          if (has_display_name) {
            String display_name;

            display_name.data = memory_arena_push_type_count(trans_arena, char, 32767);
            display_name.size = 32767;
            display_name.size = rb->ReadString(&display_name);
          }

          u8 has_sig_data = rb->ReadU8();

          if (has_sig_data) {
            u64 timestamp = rb->ReadU64();
            u64 public_key_size;

            rb->ReadVarInt(&public_key_size);

            String public_key;
            public_key.data = memory_arena_push_type_count(trans_arena, char, public_key_size);
            public_key.size = public_key_size;

            rb->ReadRawString(&public_key, public_key_size);

            u64 signature_size;

            rb->ReadVarInt(&signature_size);

            String signature;
            signature.data = memory_arena_push_type_count(trans_arena, char, signature_size);
            signature.size = signature_size;

            rb->ReadRawString(&signature, signature_size);
          }

          printf("PlayerInfo add: %.*s\n", (u32)name.size, name.data);
        } break;
        case PlayerInfoAction::UpdateGamemode: {
          u64 gamemode;

          rb->ReadVarInt(&gamemode);
          // TODO: Update player's gamemode for the provided UUID.
        } break;
        case PlayerInfoAction::UpdateLatency: {
          u64 latency;

          rb->ReadVarInt(&latency);
          // TODO: Update player's latency for the provided UUID.
        } break;
        case PlayerInfoAction::UpdateDisplayName: {
          // TODO: Update display name
        } break;
        case PlayerInfoAction::Remove: {
          // TODO: Remove player from player list
        } break;
        default: {
        } break;
      }

      trans_arena->Revert(snapshot);
    }
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

#if LOG_PACKET_ID
  printf("InterpetLogin: %lld\n", pkt_id);
#endif

  switch (type) {
  case LoginProtocol::Disconnect: {
    String sstr;
    sstr.data = memory_arena_push_type_count(trans_arena, char, 32767);
    sstr.size = 32767;

    size_t length = rb->ReadString(&sstr);

    if (length > 0) {
      printf("Disconnect reason: %.*s\n", (int)length, sstr.data);
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
    fflush(stdout);
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

#if LOG_PACKET_ID
  printf("InterpetStatus: %lld\n", pkt_id);
#endif
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
        // Use the entire remaining packet as source length
        mz_ulong source_len = (mz_ulong)(pkt_size - GetVarIntSize(payload_size));

        // The connection read buffer is mirrored in virtual memory, so it is free to read off the end of the buffer for
        // uncompressing.
        int result =
            mz_uncompress((u8*)inflate_buffer.data, (mz_ulong*)&mz_size, (u8*)rb->data + rb->read_offset, source_len);

        if (result == MZ_OK) {
          // Swap to inflate buffer and set pkt_size to new decompressed size.
          rb = &inflate_buffer;
          pkt_size = mz_size;
        } else {
          // Decompression failed.
          fprintf(stderr, "Failed to decompress packet. Skipping.\n");
          fflush(stderr);
          rb->read_offset = target_offset;
          continue;
        }
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

    rb = &connection->read_buffer;

    // Always skip to the next packet in case some data wasn't read.
    rb->read_offset = target_offset;
  } while (rb->read_offset != rb->write_offset);
}

} // namespace polymer
