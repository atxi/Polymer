#include <polymer/packet_interpreter.h>

#include <polymer/bitset.h>
#include <polymer/gamestate.h>
#include <polymer/miniz.h>
#include <polymer/nbt.h>
#include <polymer/protocol.h>
#include <polymer/unicode.h>

#include <assert.h>
#include <stdio.h>

#define LOG_PACKET_ID 0

using polymer::world::ChunkSection;
using polymer::world::ChunkSectionInfo;
using polymer::world::DimensionCodec;
using polymer::world::DimensionType;
using polymer::world::kChunkColumnCount;

namespace polymer {

PacketInterpreter::PacketInterpreter(GameState* game)
    : game(game), compression(false), inflate_buffer(*game->perm_arena, 65536 * 32) {}

void PacketInterpreter::InterpretPlay(RingBuffer* rb, u64 pkt_id, size_t pkt_size) {
  MemoryArena* trans_arena = game->trans_arena;
  Connection* connection = &game->connection;

  using inbound::play::ProtocolId;

  ProtocolId type = (ProtocolId)pkt_id;

  assert(type < ProtocolId::Count);

#if LOG_PACKET_ID
  printf("InterpetPlay: %lld\n", pkt_id);
#endif

  switch (type) {
  case ProtocolId::ChunkBatchStart: {
    // TODO: Measure
  } break;
  case ProtocolId::ChunkBatchFinished: {
    outbound::play::SendChunkBatchReceived(*connection, 16.0f);
  } break;
  case ProtocolId::SystemChatMessage: {
    MemoryRevert revert = trans_arena->GetReverter();

    nbt::TagCompound* msg_nbt = memory_arena_push_type(trans_arena, nbt::TagCompound);

    if (nbt::Parse(true, *rb, *trans_arena, msg_nbt)) {
      for (size_t i = 0; i < msg_nbt->ntags; ++i) {
        String tag_name(msg_nbt->tags[i].name, msg_nbt->tags[i].name_length);

        // Grab the translate key and output it for now.

        if (poly_strcmp(tag_name, POLY_STR("translate")) == 0) {
          if (msg_nbt->tags[i].type == nbt::TagType::String) {
            nbt::TagString* str_tag = (nbt::TagString*)msg_nbt->tags[i].tag;
            printf("System: %.*s\n", (u32)str_tag->length, str_tag->data);
          }
        }
      }
    } else {
      fprintf(stderr, "PlayProtocol::SystemChatMessage: Failed to parse NBT.\n");
    }
  } break;
  case ProtocolId::PlayerChatMessage: {
    String sender_uuid = rb->ReadAllocRawString(*trans_arena, 16);

    if (sender_uuid.size != 16) {
      fprintf(stderr, "Failed to read PlayerChatMessage::sender_uuid\n");
      break;
    }

    Player* sender = game->player_manager.GetPlayerByUuid(sender_uuid);

    if (!sender) {
      fprintf(stderr, "Failed to find player with sender uuid in PlayerChatMessage\n");
      break;
    }

    u64 index = 0;
    if (!rb->ReadVarInt(&index)) {
      fprintf(stderr, "Failed to read PlayerChatMessage::index\n");
      break;
    }

    bool has_mesg_signature = rb->ReadU8();

    if (has_mesg_signature) {
      u64 mesg_signature_size = 0;

      rb->ReadVarInt(&mesg_signature_size);

      String mesg_signature = rb->ReadAllocRawString(*trans_arena, mesg_signature_size);

      if (mesg_signature.size != mesg_signature_size) {
        fprintf(stderr, "Failed to read PlayerChatMessage::mesg_signature\n");
        break;
      }
    }

    String message = rb->ReadAllocString(*trans_arena);

    if (message.size > 0) {
      wchar output_text[1024];
      size_t output_length = 0;

      if (sender) {
        size_t namelen = strlen(sender->name);

        output_text[0] = '<';

        for (size_t i = 0; i < namelen; ++i) {
          output_text[i + 1] = sender->name[i];
        }

        output_text[namelen + 1] = '>';
        output_text[namelen + 2] = ' ';

        output_length += namelen + 3;
      }

      WString wmessage = Unicode::FromUTF8(*trans_arena, message);

      for (size_t i = 0; i < wmessage.length; ++i) {
        output_text[output_length++] = wmessage.data[i];
      }

      game->chat_window.PushMessage(output_text, output_length);
    }

    u64 timestamp = rb->ReadU64();
    u64 salt = rb->ReadU64();
  } break;
  case ProtocolId::Disconnect: {
    String reason = rb->ReadAllocString(*trans_arena);

    if (reason.size > 0) {
      printf("Disconnected: %.*s\n", (int)reason.size, reason.data);
    }
  } break;
  case ProtocolId::Explosion: {
    double x = rb->ReadDouble();
    double y = rb->ReadDouble();
    double z = rb->ReadDouble();
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
  case ProtocolId::UnloadChunk: {
    s32 chunk_z = rb->ReadU32();
    s32 chunk_x = rb->ReadU32();

    game->OnChunkUnload(chunk_x, chunk_z);
  } break;
  case ProtocolId::KeepAlive: {
    u64 id = rb->ReadU64();

    outbound::play::SendKeepAlive(*connection, id);
  } break;
  case ProtocolId::PlayerPositionAndLook: {
    double x = rb->ReadDouble();
    double y = rb->ReadDouble();
    double z = rb->ReadDouble();
    float yaw = rb->ReadFloat();
    float pitch = rb->ReadFloat();
    u8 flags = rb->ReadU8();

    u64 teleport_id;
    rb->ReadVarInt(&teleport_id);

    outbound::play::SendTeleportConfirm(*connection, teleport_id);
    // TODO: Relative/Absolute
    printf("Position: (%f, %f, %f)\n", x, y, z);
    game->OnPlayerPositionAndLook(Vector3f((float)x, (float)y, (float)z), yaw, pitch);
  } break;
  case ProtocolId::UpdateHealth: {
    float health = rb->ReadFloat();

    printf("Health: %f\n", health);
    if (health <= 0.0f) {
      outbound::play::SendClientStatus(*connection, ClientStatusAction::Respawn);
      printf("Sending respawn packet.\n");
    }
  } break;
  case ProtocolId::BlockUpdate: {
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
  case ProtocolId::Login: {
    u32 entity_id = rb->ReadU32();
    bool is_hardcore = rb->ReadU8();
    u64 dimension_count = 0;

    rb->ReadVarInt(&dimension_count);

    // Read all of the dimensions
    for (size_t i = 0; i < dimension_count; ++i) {
      String dimension_name = rb->ReadAllocString(*trans_arena);
    }

    u64 max_players = 0, view_distance = 0, simulation_distance = 0;

    rb->ReadVarInt(&max_players);
    rb->ReadVarInt(&view_distance);
    rb->ReadVarInt(&simulation_distance);

    u8 reduced_debug_info = rb->ReadU8();
    u8 enable_respawn_screen = rb->ReadU8();
    u8 limited_crafting = rb->ReadU8();

    u64 dimension_type_id = 0;

    rb->ReadVarInt(&dimension_type_id);

    DimensionType* dimension_type = game->dimension_codec.GetDimensionTypeById((s32)dimension_type_id);

    if (dimension_type) {
      printf("PlayProtocol::Login: Dimension set to %.*s\n", (u32)dimension_type->name.size, dimension_type->name.data);
      game->dimension = *dimension_type;
    } else {
      fprintf(stderr, "Failed to find dimension with id %d in codec.\n", (s32)dimension_type_id);
    }

    String dimension_identifier = rb->ReadAllocString(*trans_arena);

    if (dimension_identifier.size > 0) {
      printf("Dimension: %.*s\n", (u32)dimension_identifier.size, dimension_identifier.data);
    }

    printf("Entered dimension with height range of %d to %d\n", game->dimension.min_y,
           (game->dimension.height + game->dimension.min_y));
  } break;
  case ProtocolId::Respawn: {
    u64 dimension_type_id = 0;

    rb->ReadVarInt(&dimension_type_id);

    String dimension_name = rb->ReadAllocString(*trans_arena);

    DimensionType* dimension_type = game->dimension_codec.GetDimensionTypeById((s32)dimension_type_id);

    if (dimension_type) {
      game->dimension = *dimension_type;

      printf("Entered dimension with height range of %d to %d\n", game->dimension.min_y,
             (game->dimension.height + game->dimension.min_y));
    } else {
      fprintf(stderr, "Failed to find dimension type %d in codec.\n", (s32)dimension_type_id);
    }

    game->OnDimensionChange();
  } break;
  case ProtocolId::UpdateSectionBlocks: {
    u64 xzy = rb->ReadU64();

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
  case ProtocolId::ChunkData: {
    s32 chunk_x = rb->ReadU32();
    s32 chunk_z = rb->ReadU32();

    // This is some scratch space for reading byte arrays below.
    String scratch_str;
    scratch_str.data = memory_arena_push_type_count(trans_arena, char, 32767);
    scratch_str.size = 32767;

    nbt::TagCompound* nbt = memory_arena_push_type(trans_arena, nbt::TagCompound);

    if (!nbt::Parse(true, *rb, *trans_arena, nbt)) {
      fprintf(stderr, "Failed to parse chunk nbt.\n");
      fflush(stderr);
    }

    u64 data_size;
    rb->ReadVarInt(&data_size);

    size_t new_offset = (rb->read_offset + data_size) % rb->size;

    u32 x_index = world::GetChunkCacheIndex(chunk_x);
    u32 z_index = world::GetChunkCacheIndex(chunk_z);

    ChunkSection* section = &game->world.chunks[z_index][x_index];
    ChunkSectionInfo* section_info = &game->world.chunk_infos[z_index][x_index];

    for (size_t i = 0; i < kChunkColumnCount; ++i) {
      if (section->chunks[i]) {
        game->world.chunk_pool.Free(section->chunks[i]);
        section->chunks[i] = nullptr;
      }
    }

    section_info->ClearQueued();
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

        u64 data_array_length;
        rb->ReadVarInt(&data_array_length);

        u64 id_mask = (1LL << bpb) - 1;
        u64 block_index = 0;

        u32* chunk = nullptr;
        
        if (block_count > 0) {
          section->chunks[chunk_y] = game->world.chunk_pool.Allocate();
          chunk = (u32*)section->chunks[chunk_y]->blocks;

          // Fill out entire chunk with the one block palette
          if (data_array_length == 0 && bpb == 0) {
            for (int i = 0; i < 16 * 16 * 16; ++i) {
              chunk[i] = (u32)single_palette;
            }
          }
        }

        for (u64 i = 0; i < data_array_length; ++i) {
          u64 data_value = rb->ReadU64();

          if (block_count > 0) {
            for (u64 j = 0; j < 64 / bpb; ++j) {
              size_t palette_index = (size_t)((data_value >> (j * bpb)) & id_mask);

              if (palette) {
                chunk[block_index++] = (u32)palette[palette_index];
              } else {
                chunk[block_index++] = (u32)palette_index;
              }
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

    for (size_t i = 0; i < block_entity_count; ++i) {
      u8 packed_xz = rb->ReadU8();
      s16 y = rb->ReadU16();

      u64 type;
      rb->ReadVarInt(&type);

      ArenaSnapshot snapshot = trans_arena->GetSnapshot();
      nbt::TagCompound* block_entity_nbt = memory_arena_push_type(trans_arena, nbt::TagCompound);

      if (!nbt::Parse(true, *rb, *trans_arena, block_entity_nbt)) {
        fprintf(stderr, "Failed to parse block entity nbt.\n");
        fflush(stderr);
      }

      trans_arena->Revert(snapshot);
    }

    BitSet skylight_mask;
    if (!skylight_mask.Read(*trans_arena, *rb)) {
      fprintf(stderr, "Failed to read skylight mask\n");
      fflush(stderr);
      break;
    }

    BitSet blocklight_mask;
    if (!blocklight_mask.Read(*trans_arena, *rb)) {
      fprintf(stderr, "Failed to read blocklight mask\n");
      fflush(stderr);
      break;
    }

    BitSet empty_skylight_mask;
    if (!empty_skylight_mask.Read(*trans_arena, *rb)) {
      fprintf(stderr, "Failed to read empty skylight mask\n");
      fflush(stderr);
      break;
    }

    BitSet empty_blocklight_mask;
    if (!empty_blocklight_mask.Read(*trans_arena, *rb)) {
      fprintf(stderr, "Failed to read empty blocklight mask\n");
      fflush(stderr);
      break;
    }

    for (size_t i = 0; i < kChunkColumnCount; ++i) {
      if (section->chunks[i]) {
        memset(section->chunks[i]->lightmap, 0, sizeof(section->chunks[i]->lightmap));
      }
    }

    u64 skylight_array_count = 0;
    rb->ReadVarInt(&skylight_array_count);

    constexpr size_t kRecvSections = kChunkColumnCount + 2;

    s32 column_offset = (game->dimension.min_y + 64) / 16;

    for (size_t i = 0; i < kRecvSections; ++i) {
      if (!skylight_mask.IsSet(i)) continue;

      u64 skylight_length = 0;

      rb->ReadVarInt(&skylight_length);
      rb->ReadRawString(&scratch_str, skylight_length);

      if (i == 0 || i == kRecvSections - 1) continue;

      size_t chunk_y = i - 1 + column_offset;

      for (size_t index = 0; index < skylight_length; ++index) {
        size_t block_data_index = index * 2;

        if (section->chunks[chunk_y]) {
          u8* lightmap = (u8*)section->chunks[chunk_y]->lightmap;

          lightmap[block_data_index] = scratch_str.data[index] & 0x0F;
          lightmap[block_data_index + 1] = (scratch_str.data[index] & 0xF0) >> 4;
        }
      }
    }

    u64 blocklight_array_count = 0;
    rb->ReadVarInt(&blocklight_array_count);

    for (size_t i = 0; i < kRecvSections + 2; ++i) {
      if (!blocklight_mask.IsSet(i)) continue;

      u64 blocklight_length = 0;

      rb->ReadVarInt(&blocklight_length);
      rb->ReadRawString(&scratch_str, blocklight_length);

      if (i == 0 || i == kRecvSections - 1) continue;

      size_t chunk_y = i - 1 + column_offset;

      for (size_t index = 0; index < blocklight_length; ++index) {
        size_t block_data_index = index * 2;

        if (section->chunks[chunk_y]) {
          u8* lightmap = (u8*)section->chunks[chunk_y]->lightmap;

          // Merge the block lightmap into the packed chunk lightmap
          lightmap[block_data_index] |= (scratch_str.data[index] & 0x0F) << 4;
          lightmap[block_data_index + 1] |= (scratch_str.data[index] & 0xF0);
        }
      }
    }
  } break;
  case ProtocolId::PlayerInfoUpdate: {
    u8 action_bitmask = rb->ReadU8();

    u64 action_count = 0;
    if (!rb->ReadVarInt(&action_count)) {
      fprintf(stderr, "Failed to read PlayerInfoUpdate::action_count\n");
      break;
    }

    for (u64 i = 0; i < action_count; ++i) {
      ArenaSnapshot snapshot = trans_arena->GetSnapshot();

      String uuid_string = rb->ReadAllocRawString(*trans_arena, 16);

      if (uuid_string.size != 16) {
        fprintf(stderr, "Failed to read PlayerInfoUpdate::uuid\n");
        break;
      }

      enum ActionFlags {
        AddAction = (1 << 0),
        ChatAction = (1 << 1),
        GamemodeAction = (1 << 2),
        ListedAction = (1 << 3),
        LatencyAction = (1 << 4),
        DisplayNameAction = (1 << 5),
      };

      if (action_bitmask & AddAction) {
        String name = rb->ReadAllocString(*trans_arena);

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

        game->player_manager.AddPlayer(name, uuid_string, 0, 0);
      }

      Player* player = game->player_manager.GetPlayerByUuid(uuid_string);

      if (action_bitmask & ChatAction) {
        bool has_signature = rb->ReadU8();

        if (has_signature) {
          String chat_session_id = rb->ReadAllocRawString(*trans_arena, 16);

          if (chat_session_id.size != 16) {
            fprintf(stderr, "Failed to read PlayerInfoAction::InitializeChat::chat_session_id\n");
            break;
          }

          u64 public_key_expiry_time = rb->ReadU64();
          u64 encoded_public_key_size = 0;

          if (!rb->ReadVarInt(&encoded_public_key_size)) {
            fprintf(stderr, "Failed to read PlayerInfoAction::InitializeChat::encoded_public_key_size\n");
            break;
          }

          String encoded_public_key = rb->ReadAllocRawString(*trans_arena, encoded_public_key_size);

          if (encoded_public_key.size != encoded_public_key_size) {
            fprintf(stderr, "Failed to read PlayerInfoAction::InitializeChat::encoded_public_key\n");
            break;
          }

          u64 public_key_sig_size = 0;

          if (!rb->ReadVarInt(&public_key_sig_size)) {
            fprintf(stderr, "Failed to read PlayerInfoAction::InitializeChat::public_key_sig_size\n");
            break;
          }

          String public_key_sig = rb->ReadAllocRawString(*trans_arena, public_key_sig_size);

          if (public_key_sig.size != public_key_sig_size) {
            fprintf(stderr, "Failed to read PlayerInfoAction::InitializeChat::public_key_sig\n");
            break;
          }
        }
      }

      if (action_bitmask & GamemodeAction) {
        u64 gamemode = 0;

        if (!rb->ReadVarInt(&gamemode)) {
          fprintf(stderr, "Failed to read PlayerInfoAction::gamemode\n");
          break;
        }

        if (player) {
          player->gamemode = (u8)gamemode;
        }
      }

      if (action_bitmask & ListedAction) {
        player->listed = rb->ReadU8();
      }

      if (action_bitmask & LatencyAction) {
        u64 latency = 0;

        if (!rb->ReadVarInt(&latency)) {
          fprintf(stderr, "Failed to read PlayerInfoAction::ping\n");
          break;
        }

        if (player) {
          player->ping = (u8)latency;
        }
      }

      if (action_bitmask & DisplayNameAction) {
        bool has_display_name = rb->ReadU8();

        if (has_display_name) {
          String display_name = rb->ReadAllocString(*trans_arena);
        }
      }

      trans_arena->Revert(snapshot);
    }
  } break;
  case ProtocolId::PlayerInfoRemove: {
    u64 player_count = 0;

    rb->ReadVarInt(&player_count);

    String uuid_string;

    uuid_string.data = memory_arena_push_type_count(trans_arena, char, 16);
    uuid_string.size = 16;

    for (u64 i = 0; i < player_count; ++i) {
      rb->ReadRawString(&uuid_string, 16);
      game->player_manager.RemovePlayer(uuid_string);
    }
  } break;
  case ProtocolId::TimeUpdate: {
    u64 world_age = rb->ReadU64();
    s64 time_tick = rb->ReadU64();

    // TODO: Fixed time with negative values
    game->world.world_tick = (u32)time_tick % 24000;
  } break;
  default:
    break;
  }
}

void PacketInterpreter::InterpretConfiguration(RingBuffer* rb, u64 pkt_id, size_t pkt_size) {
  MemoryArena* trans_arena = game->trans_arena;
  Connection* connection = &game->connection;

  using inbound::configuration::ProtocolId;

  ProtocolId type = (ProtocolId)pkt_id;

  assert(type < ProtocolId::Count);

#if LOG_PACKET_ID
  printf("InterpetConfiguration: %lld\n", pkt_id);
#endif

  switch (type) {
  case ProtocolId::CookieRequest: {
    String cookie_request = rb->ReadAllocString(*trans_arena);

    printf("ConfigurationProtocol::CookieRequest: %.*s\n", (int)cookie_request.size, cookie_request.data);
  } break;
  case ProtocolId::PluginMessage: {
    String channel = rb->ReadAllocString(*trans_arena);

    if (channel.size > 0) {
      printf("ConfigurationProtocol::PluginMessage on channel %.*s\n", (int)channel.size, channel.data);
    }
  } break;
  case ProtocolId::Disconnect: {
    String reason = rb->ReadAllocString(*trans_arena);

    if (reason.size > 0) {
      printf("ConfigurationProtocol::Disconnect: %.*s\n", (int)reason.size, reason.data);
    } else {
      printf("ConfigurationProtocol::Disconnect: No reason specified.\n");
    }
  } break;
  case ProtocolId::Finish: {
    printf("LoginConfiguration::Finish: Transitioning to PlayProtocol.\n");

    outbound::configuration::SendFinish(*connection);
    connection->protocol_state = ProtocolState::Play;
  } break;
  case ProtocolId::KeepAlive: {
    u64 alive_id = rb->ReadU64();

    outbound::configuration::SendKeepAlive(*connection, alive_id);
  } break;
  case ProtocolId::Ping: {
    u32 ping_id = rb->ReadU32();

    outbound::configuration::SendPong(*connection, ping_id);
  } break;
  case ProtocolId::RegistryData: {
    // This registry type holds what kind of registry is being received. Biome, banner, dimensions, etc.
    String registry_type = rb->ReadAllocString(*trans_arena);

    u64 entry_count = 0;

    rb->ReadVarInt(&entry_count);

    printf("ConfigurationProtocol::RegistryData: Received data for %.*s with %d entries\n", (u32)registry_type.size,
           registry_type.data, (s32)entry_count);

    // We only care to process dimension data.
    if (poly_strcmp(registry_type, POLY_STR("minecraft:dimension_type")) == 0) {
      game->dimension_codec.Initialize(*game->perm_arena, entry_count);

      for (u64 i = 0; i < entry_count; ++i) {
        MemoryRevert reverter = trans_arena->GetReverter();
        DimensionType* dimension_type = game->dimension_codec.types + i;

        dimension_type->id = (s32)i;
        dimension_type->name = rb->ReadAllocString(*game->perm_arena);

        if (rb->ReadU8()) {
          nbt::TagCompound* dimension_codec_nbt = memory_arena_push_type(trans_arena, nbt::TagCompound);

          if (!nbt::Parse(true, *rb, *trans_arena, dimension_codec_nbt)) {
            fprintf(stderr, "ConfigurationProtocol::RegistryData: Failed to parse dimension codec nbt.\n");
            exit(1);
          }

          game->dimension_codec.ParseType(*game->perm_arena, *dimension_codec_nbt, *dimension_type);
        } else {
          // If we didn't receive data about this dimension then it is one of the core ones.
          printf("Receiving default data about dim %d\n", (s32)i);
          game->dimension_codec.ParseDefaultType(*game->perm_arena, i);
        }
      }

      printf("ConfigurationProtocol::RegistryData: Received %u dimension types.\n", (u32)entry_count);
    }
  } break;
  case ProtocolId::RemoveResourcePack: {
    //
  } break;
  case ProtocolId::AddResourcePack: {
    //
  } break;
  case ProtocolId::FeatureFlags: {
    //
  } break;
  case ProtocolId::UpdateTags: {
    //
  } break;
  case ProtocolId::KnownPacks: {
    u64 pack_count = 0;

    rb->ReadVarInt(&pack_count);

    for (u64 i = 0; i < pack_count; ++i) {
      String namespace_id = rb->ReadAllocString(*trans_arena);
      String pack_id = rb->ReadAllocString(*trans_arena);
      String version = rb->ReadAllocString(*trans_arena);

      //
    }

    outbound::configuration::SendKnownPacks(*connection);
  } break;
  default: {

  } break;
  }
}

void PacketInterpreter::InterpretLogin(RingBuffer* rb, u64 pkt_id, size_t pkt_size) {
  MemoryArena* trans_arena = game->trans_arena;
  Connection* connection = &game->connection;

  using inbound::login::ProtocolId;

  ProtocolId type = (ProtocolId)pkt_id;

  assert(type < ProtocolId::Count);

#if LOG_PACKET_ID
  printf("InterpetLogin: %lld\n", pkt_id);
#endif

  switch (type) {
  case ProtocolId::Disconnect: {
    String reason = rb->ReadAllocString(*trans_arena);

    if (reason.size > 0) {
      printf("LoginProtocol::Disconnect: %.*s\n", (int)reason.size, reason.data);
    }

    connection->Disconnect();
  } break;
  case ProtocolId::EncryptionRequest: {
    printf("LoginProtocol::EncryptionRequest: online-mode=true (server.properties) is not yet implemented.\n");
    connection->Disconnect();
  } break;
  case ProtocolId::LoginSuccess: {
    printf("LoginProtocol::LoginSuccess: Transitioning to ConfigurationProtocol.\n");

    outbound::login::SendAcknowledged(*connection);
    connection->protocol_state = ProtocolState::Configuration;

    u8 view_distance = 16;
#ifdef _DEBUG
    view_distance = 3;
#endif

    outbound::configuration::SendClientInformation(*connection, view_distance, 0x7F, 1);
  } break;
  case ProtocolId::SetCompression: {
    compression = true;
    connection->builder.flags &= ~(PacketBuilder::BuildFlag_OmitCompress);
  } break;
  default:
    break;
  }
}

void PacketInterpreter::InterpretStatus(RingBuffer* rb, u64 pkt_id, size_t pkt_size) {
  MemoryArena* trans_arena = game->trans_arena;
  Connection* connection = &game->connection;

  using inbound::status::ProtocolId;

  ProtocolId type = (ProtocolId)pkt_id;

  assert(type < ProtocolId::Count);

#if LOG_PACKET_ID
  printf("InterpetStatus: %lld\n", pkt_id);
#endif
}

size_t PacketInterpreter::Interpret() {
  MemoryArena* trans_arena = game->trans_arena;
  Connection* connection = &game->connection;
  RingBuffer* rb = &connection->read_buffer;

  size_t processed_count = 0;

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

    MemoryRevert memory_snapshot = trans_arena->GetReverter();

    switch (connection->protocol_state) {
    case ProtocolState::Status:
      this->InterpretStatus(rb, pkt_id, (size_t)pkt_size);
      break;
    case ProtocolState::Login:
      this->InterpretLogin(rb, pkt_id, (size_t)pkt_size);
      break;
    case ProtocolState::Configuration: {
      this->InterpretConfiguration(rb, pkt_id, (size_t)pkt_size);
    } break;
    case ProtocolState::Play:
      this->InterpretPlay(rb, pkt_id, (size_t)pkt_size);
      break;
    default:
      break;
    }

    rb = &connection->read_buffer;

    // Always skip to the next packet in case some data wasn't read.
    rb->read_offset = target_offset;
    ++processed_count;
  } while (rb->read_offset != rb->write_offset);

  return processed_count;
}

} // namespace polymer
