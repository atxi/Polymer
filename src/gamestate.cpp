#include "gamestate.h"

#include "json.h"
#include "math.h"
#include "stb_image.h"
#include "zip_archive.h"

#include "render/block_mesher.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <unordered_map>

using polymer::render::kRenderLayerCount;
using polymer::render::RenderLayer;
using polymer::world::ChunkMesh;
using polymer::world::ChunkSection;
using polymer::world::ChunkSectionInfo;
using polymer::world::kChunkCacheSize;
using polymer::world::kChunkColumnCount;

namespace polymer {

GameState::GameState(render::VulkanRenderer* renderer, MemoryArena* perm_arena, MemoryArena* trans_arena)
    : perm_arena(perm_arena), trans_arena(trans_arena), connection(*perm_arena), renderer(renderer),
      block_registry(*perm_arena), block_mesher(*trans_arena) {
  for (u32 chunk_z = 0; chunk_z < kChunkCacheSize; ++chunk_z) {
    for (u32 chunk_x = 0; chunk_x < kChunkCacheSize; ++chunk_x) {
      ChunkSection* section = &world.chunks[chunk_z][chunk_x];
      ChunkSectionInfo* section_info = &world.chunk_infos[chunk_z][chunk_x];

      section->info = section_info;

      ChunkMesh* meshes = world.meshes[chunk_z][chunk_x];

      for (u32 chunk_y = 0; chunk_y < kChunkColumnCount; ++chunk_y) {
        for (s32 i = 0; i < kRenderLayerCount; ++i) {
          meshes[chunk_y].meshes[i].vertex_count = 0;
        }
      }
    }
  }

  camera.near = 0.1f;
  camera.far = 1024.0f;
  camera.fov = Radians(80.0f);

  position_sync_timer = 0.0f;
  animation_accumulator = 0.0f;
  time_accumulator = 0.0f;
  world_tick = 0;
}

void GameState::Update(float dt, InputState* input) {
  ProcessMovement(dt, input);

  if (input->display_players) {
    player_manager.RenderPlayerList(*renderer);
  }

  chat_manager.Update(*renderer, dt);

  animation_accumulator += dt;
  time_accumulator += dt;

  if (animation_accumulator >= 128.0f) {
    animation_accumulator -= 128.0f;
  }

  if (time_accumulator >= 1.0f / 20.0f) {
    time_accumulator -= 1.0f / 20.0f;

    if (world_tick++ >= 24000) {
      world_tick = 0;
    }
  }

  ProcessBuildQueue();
  RenderFrame();
}

void GameState::ProcessMovement(float dt, InputState* input) {
  const float kMoveSpeed = 20.0f;
  const float kSprintModifier = 1.3f;

  Vector3f movement;

  if (input->forward) {
    movement += camera.GetForward();
  }

  if (input->backward) {
    movement -= camera.GetForward();
  }

  if (input->left) {
    Vector3f forward = camera.GetForward();
    Vector3f right = Normalize(forward.Cross(Vector3f(0, 1, 0)));
    movement -= right;
  }

  if (input->right) {
    Vector3f forward = camera.GetForward();
    Vector3f right = Normalize(forward.Cross(Vector3f(0, 1, 0)));
    movement += right;
  }

  if (input->climb) {
    movement += Vector3f(0, 1, 0);
  }

  if (input->fall) {
    movement -= Vector3f(0, 1, 0);
  }

  if (movement.LengthSq() > 0) {
    float modifier = kMoveSpeed;

    if (input->sprint) {
      modifier *= kSprintModifier;
    }

    camera.position += Normalize(movement) * (dt * modifier);
  }

  position_sync_timer += dt;

  // Send position packets when in spectator for testing chunk loading.
  // TODO: Implement for real
  if (player_manager.client_player && player_manager.client_player->gamemode == 3) {
    if (position_sync_timer >= (50.0f / 1000.0f)) {
      connection.SendPlayerPositionAndRotation(camera.position - Vector3f(0, 1.8f, 0.0f), 0.0f, 0.0f, false);
      position_sync_timer = 0.0f;
    }
  }
}

void GameState::ProcessBuildQueue() {
  for (size_t i = 0; i < build_queue.count;) {
    s32 chunk_x = build_queue.data[i].x;
    s32 chunk_z = build_queue.data[i].z;

    // TODO: Put a cap on the distance away so it's not constantly grabbing the neighbors of the outer unmeshed chunks.
    render::ChunkBuildContext ctx(chunk_x, chunk_z);

    if (ctx.GetNeighbors(&world)) {
      BuildChunkMesh(&ctx);
      build_queue.data[i] = build_queue.data[--build_queue.count];
    } else {
      ++i;
    }
  }
}

void GameState::RenderFrame() {
  camera.aspect_ratio = (float)renderer->swap_extent.width / renderer->swap_extent.height;

  render::ChunkRenderUBO ubo;
  void* data = nullptr;

  ubo.mvp = camera.GetProjectionMatrix() * camera.GetViewMatrix();
  ubo.frame = (u32)(animation_accumulator * 8.0f);
  ubo.sunlight = GetSunlight();

  render::ChunkRenderer& chunk_renderer = renderer->chunk_renderer;

  chunk_renderer.sunlight = ubo.sunlight;

  vmaMapMemory(renderer->allocator, chunk_renderer.uniform_allocations[renderer->current_frame], &data);
  memcpy(data, &ubo, sizeof(ubo));
  vmaUnmapMemory(renderer->allocator, chunk_renderer.uniform_allocations[renderer->current_frame]);

  Frustum frustum = camera.GetViewFrustum();

  VkDeviceSize offsets[] = {0};
  VkDeviceSize offset = {};

#if DISPLAY_PERF_STATS
  stats.Reset();
#endif

  VkCommandBuffer buffers[] = {
      renderer->chunk_renderer.block_renderer.command_buffers[renderer->current_frame],
      renderer->chunk_renderer.flora_renderer.command_buffers[renderer->current_frame],
      renderer->chunk_renderer.leaf_renderer.command_buffers[renderer->current_frame],
      renderer->chunk_renderer.alpha_renderer.command_buffers[renderer->current_frame],
  };

  for (s32 chunk_z = 0; chunk_z < (s32)kChunkCacheSize; ++chunk_z) {
    for (s32 chunk_x = 0; chunk_x < (s32)kChunkCacheSize; ++chunk_x) {
      ChunkSectionInfo* section_info = &world.chunk_infos[chunk_z][chunk_x];

      if (!section_info->loaded) {
        continue;
      }

      ChunkMesh* meshes = world.meshes[chunk_z][chunk_x];

      for (s32 chunk_y = 0; chunk_y < kChunkColumnCount; ++chunk_y) {
        ChunkMesh* mesh = meshes + chunk_y;

        if ((section_info->bitmask & (1 << chunk_y))) {
          Vector3f chunk_min(section_info->x * 16.0f, chunk_y * 16.0f - 64.0f, section_info->z * 16.0f);
          Vector3f chunk_max(section_info->x * 16.0f + 16.0f, chunk_y * 16.0f - 48.0f, section_info->z * 16.0f + 16.0f);

          if (frustum.Intersects(chunk_min, chunk_max)) {
#if DISPLAY_PERF_STATS
            bool rendered = false;
#endif
            for (s32 i = 0; i < render::kRenderLayerCount; ++i) {
              render::RenderMesh* layer_mesh = &mesh->meshes[i];

              if (layer_mesh->vertex_count > 0) {
                VkCommandBuffer command_buffer = buffers[i];

                vkCmdBindVertexBuffers(command_buffer, 0, 1, &layer_mesh->vertex_buffer, offsets);
                vkCmdBindIndexBuffer(command_buffer, layer_mesh->index_buffer, offset, VK_INDEX_TYPE_UINT16);
                vkCmdDrawIndexed(command_buffer, layer_mesh->index_count, 1, 0, 0, 0);
#if DISPLAY_PERF_STATS
                stats.vertex_counts[i] += layer_mesh->vertex_count;
                rendered = true;
#endif
              }
            }

#if DISPLAY_PERF_STATS
            if (rendered) {
              ++stats.chunk_render_count;
            }
#endif
          }
        }
      }
    }
  }
}

void GameState::OnWindowMouseMove(s32 dx, s32 dy) {
  const float kSensitivity = 0.005f;
  constexpr float kMaxPitch = Radians(89.0f);

  camera.yaw += dx * kSensitivity;
  camera.pitch -= dy * kSensitivity;

  if (camera.pitch > kMaxPitch) {
    camera.pitch = kMaxPitch;
  } else if (camera.pitch < -kMaxPitch) {
    camera.pitch = -kMaxPitch;
  }
}

void GameState::OnPlayerPositionAndLook(const Vector3f& position, float yaw, float pitch) {
  camera.position = position + Vector3f(0, 1.8f, 0);
  camera.yaw = Radians(yaw + 90.0f);
  camera.pitch = -Radians(pitch);
}

void GameState::BuildChunkMesh(render::ChunkBuildContext* ctx, s32 chunk_x, s32 chunk_y, s32 chunk_z) {
  u8* arena_snapshot = trans_arena->current;

  render::ChunkVertexData vertex_data = block_mesher.CreateMesh(assets, block_registry, ctx, chunk_y);

  ChunkMesh* meshes = world.meshes[ctx->z_index][ctx->x_index];

  for (s32 i = 0; i < kRenderLayerCount; ++i) {
    if (meshes[chunk_y].meshes[i].vertex_count > 0) {
      // TODO: This should be done in a better way
      renderer->WaitForIdle();
      renderer->FreeMesh(&meshes[chunk_y].meshes[i]);
      meshes[chunk_y].meshes[i].vertex_count = 0;
    }

    if (vertex_data.vertex_count[i] > 0) {
      assert(vertex_data.vertex_count[i] <= 0xFFFFFFFF);

      const size_t data_size = sizeof(render::ChunkVertex) * vertex_data.vertex_count[i];

      meshes[chunk_y].meshes[i].vertex_count = (u32)vertex_data.vertex_count[i];
      meshes[chunk_y].meshes[i] =
          renderer->AllocateMesh(vertex_data.vertices[i], data_size, vertex_data.vertex_count[i],
                                 vertex_data.indices[i], vertex_data.index_count[i]);
    }
  }

  // Reset the arena to where it was before this allocation. The data was already sent to the GPU so it's no longer
  // useful.
  trans_arena->current = arena_snapshot;
  block_mesher.Reset();
}

void GameState::BuildChunkMesh(render::ChunkBuildContext* ctx) {
  // TODO:
  // This should probably be done on a separate thread or in a compute shader ideally.
  // Either an index buffer or face merging should be done to reduce buffer size.

  u32 x_index = world.GetChunkCacheIndex(ctx->chunk_x);
  u32 z_index = world.GetChunkCacheIndex(ctx->chunk_z);

  ChunkSectionInfo* section_info = &world.chunk_infos[z_index][x_index];

  renderer->BeginMeshAllocation();

  ChunkMesh* meshes = world.meshes[ctx->z_index][ctx->x_index];

  for (s32 chunk_y = 0; chunk_y < kChunkColumnCount; ++chunk_y) {
    if (!(section_info->bitmask & (1 << chunk_y))) {
      for (s32 i = 0; i < kRenderLayerCount; ++i) {
        meshes[chunk_y].meshes[i].vertex_count = 0;
      }

      continue;
    }

    BuildChunkMesh(ctx, ctx->chunk_x, chunk_y, ctx->chunk_z);
  }

  renderer->EndMeshAllocation();
}

void GameState::OnDimensionChange() {
  renderer->WaitForIdle();

  for (s32 chunk_z = 0; chunk_z < kChunkCacheSize; ++chunk_z) {
    for (s32 chunk_x = 0; chunk_x < kChunkCacheSize; ++chunk_x) {
      ChunkSectionInfo* section_info = &world.chunk_infos[chunk_z][chunk_x];
      ChunkMesh* meshes = world.meshes[chunk_z][chunk_x];

      section_info->loaded = false;
      section_info->bitmask = 0;

      for (s32 chunk_y = 0; chunk_y < kChunkColumnCount; ++chunk_y) {
        ChunkMesh* mesh = meshes + chunk_y;

        for (s32 i = 0; i < kRenderLayerCount; ++i) {
          if (mesh->meshes[i].vertex_count > 0) {
            renderer->FreeMesh(&mesh->meshes[i]);
            mesh->meshes[i].vertex_count = 0;
          }
        }
      }
    }
  }

  build_queue.Clear();
}

void GameState::OnChunkLoad(s32 chunk_x, s32 chunk_z) {
  u32 x_index = world.GetChunkCacheIndex(chunk_x);
  u32 z_index = world.GetChunkCacheIndex(chunk_z);

  ChunkSectionInfo* section_info = &world.chunk_infos[z_index][x_index];
  ChunkMesh* meshes = world.meshes[z_index][x_index];

  if (section_info->loaded) {
    renderer->WaitForIdle();

    build_queue.Dequeue(section_info->x, section_info->z);

    // Force clear any existing meshes
    for (s32 chunk_y = 0; chunk_y < kChunkColumnCount; ++chunk_y) {
      ChunkMesh* mesh = meshes + chunk_y;

      for (s32 i = 0; i < kRenderLayerCount; ++i) {
        if (mesh->meshes[i].vertex_count > 0) {
          renderer->FreeMesh(&mesh->meshes[i]);
          mesh->meshes[i].vertex_count = 0;
        }
      }
    }
  }

  section_info->loaded = true;
  section_info->x = chunk_x;
  section_info->z = chunk_z;

  build_queue.Enqueue(chunk_x, chunk_z);
}

void GameState::OnChunkUnload(s32 chunk_x, s32 chunk_z) {
  u32 x_index = world.GetChunkCacheIndex(chunk_x);
  u32 z_index = world.GetChunkCacheIndex(chunk_z);
  ChunkSection* section = &world.chunks[z_index][x_index];
  ChunkSectionInfo* section_info = &world.chunk_infos[z_index][x_index];

  build_queue.Dequeue(chunk_x, chunk_z);

  // It's possible to receive an unload packet after receiving a new chunk that would take this chunk's position in
  // the cache, so it needs to be checked before anything is changed in the cache.
  if (section_info->x != chunk_x || section_info->z != chunk_z) {
    return;
  }

  section_info->bitmask = 0;
  section_info->loaded = false;

  for (s32 chunk_y = 0; chunk_y < kChunkColumnCount; ++chunk_y) {
    if (section_info->bitmask & (1 << chunk_y)) {
      memset(section->chunks[chunk_y].blocks, 0, sizeof(section->chunks[chunk_y].blocks));
    }
  }

  ChunkMesh* meshes = world.meshes[z_index][x_index];

  renderer->WaitForIdle();

  for (s32 chunk_y = 0; chunk_y < kChunkColumnCount; ++chunk_y) {
    for (s32 i = 0; i < kRenderLayerCount; ++i) {
      if (meshes[chunk_y].meshes[i].vertex_count > 0) {
        renderer->FreeMesh(&meshes[chunk_y].meshes[i]);
        meshes[chunk_y].meshes[i].vertex_count = 0;
      }
    }
  }
}

void GameState::OnBlockChange(s32 x, s32 y, s32 z, u32 new_bid) {
  s32 chunk_x = (s32)std::floor(x / 16.0f);
  s32 chunk_z = (s32)std::floor(z / 16.0f);
  s32 chunk_y = (s32)std::floor(y / 16.0f) + 4;

  ChunkSection* section = &world.chunks[world.GetChunkCacheIndex(chunk_z)][world.GetChunkCacheIndex(chunk_x)];

  s32 relative_x = x % 16;
  s32 relative_y = y % 16;
  s32 relative_z = z % 16;

  if (relative_x < 0) {
    relative_x += 16;
  }

  if (relative_y < 0) {
    relative_y += 16;
  }

  if (relative_z < 0) {
    relative_z += 16;
  }

  u32 old_bid = section->chunks[chunk_y].blocks[relative_y][relative_z][relative_x];

  section->chunks[chunk_y].blocks[relative_y][relative_z][relative_x] = (u32)new_bid;

  if (new_bid != 0) {
    ChunkSectionInfo* section_info =
        &world.chunk_infos[world.GetChunkCacheIndex(chunk_z)][world.GetChunkCacheIndex(chunk_x)];
    section_info->bitmask |= (1 << chunk_y);
  }

  // TODO: Block changes should be batched to update a chunk once in the frame when it changes
  renderer->BeginMeshAllocation();

  render::ChunkBuildContext ctx(chunk_x, chunk_z);
  ImmediateRebuild(&ctx, chunk_y);

  if (relative_x == 0) {
    // Rebuild west
    render::ChunkBuildContext nearby_ctx(chunk_x - 1, chunk_z);
    ImmediateRebuild(&nearby_ctx, chunk_y);
  } else if (relative_x == 15) {
    // Rebuild east
    render::ChunkBuildContext nearby_ctx(chunk_x + 1, chunk_z);
    ImmediateRebuild(&nearby_ctx, chunk_y);
  }

  if (relative_z == 0) {
    // Rebuild north
    render::ChunkBuildContext nearby_ctx(chunk_x, chunk_z - 1);
    ImmediateRebuild(&nearby_ctx, chunk_y);
  } else if (relative_z == 15) {
    // Rebuild south
    render::ChunkBuildContext nearby_ctx(chunk_x, chunk_z + 1);
    ImmediateRebuild(&nearby_ctx, chunk_y);
  }

  if (relative_y == 0 && chunk_y > 0) {
    // Rebuild below
    render::ChunkBuildContext nearby_ctx(chunk_x, chunk_z);
    ImmediateRebuild(&nearby_ctx, chunk_y - 1);
  } else if (relative_y == 15 && chunk_y < 15) {
    // Rebuild above
    render::ChunkBuildContext nearby_ctx(chunk_x, chunk_z);
    ImmediateRebuild(&nearby_ctx, chunk_y + 1);
  }

  renderer->EndMeshAllocation();
}

void GameState::ImmediateRebuild(render::ChunkBuildContext* ctx, s32 chunk_y) {
  s32 chunk_x = ctx->chunk_x;
  s32 chunk_z = ctx->chunk_z;

  if (build_queue.IsInQueue(chunk_x, chunk_z)) return;
  // It should always have neighbors if it's not in the build queue, but sanity check anyway.
  if (!ctx->GetNeighbors(&world)) return;

  BuildChunkMesh(ctx, chunk_x, chunk_y, chunk_z);
}

void GameState::FreeMeshes() {
  for (u32 chunk_z = 0; chunk_z < kChunkCacheSize; ++chunk_z) {
    for (u32 chunk_x = 0; chunk_x < kChunkCacheSize; ++chunk_x) {
      ChunkMesh* meshes = world.meshes[chunk_z][chunk_x];

      for (u32 chunk_y = 0; chunk_y < kChunkColumnCount; ++chunk_y) {
        ChunkMesh* mesh = meshes + chunk_y;

        for (s32 i = 0; i < kRenderLayerCount; ++i) {
          if (mesh->meshes[i].vertex_count > 0) {
            renderer->FreeMesh(&mesh->meshes[i]);
            mesh->meshes[i].vertex_count = 0;
          }
        }
      }
    }
  }
}

void PlayerManager::AddPlayer(const String& name, const String& uuid, u8 ping, u8 gamemode) {
  Player* new_player = nullptr;

  for (size_t i = 0; i < player_count; ++i) {
    Player* player = players + i;
    String player_uuid(player->uuid, 16);

    if (poly_strcmp(player_uuid, uuid) == 0) {
      new_player = player;
      break;
    }
  }

  if (!new_player) {
    if (player_count >= polymer_array_count(players)) return;

    new_player = players + player_count++;
  }

  assert(name.size <= polymer_array_count(new_player->name));

  memcpy(new_player->name, name.data, name.size);
  new_player->name[name.size] = 0;

  assert(uuid.size <= polymer_array_count(new_player->uuid));
  memcpy(new_player->uuid, uuid.data, uuid.size);

  new_player->ping = ping;
  new_player->gamemode = gamemode;

  if (poly_strcmp(name, String(client_name)) == 0) {
    client_player = new_player;
  }
}

void PlayerManager::RemovePlayer(const String& uuid) {
  for (size_t i = 0; i < player_count; ++i) {
    Player* player = players + i;
    String player_uuid(player->uuid, 16);

    if (poly_strcmp(player_uuid, uuid) == 0) {
      Player* swap_player = players + --player_count;

      // Swap the pointer to the new slot if the removed player is the client player
      if (poly_strcmp(swap_player->name, String(client_name)) == 0) {
        client_player = players + i;
      }

      players[i] = *swap_player;
      return;
    }
  }
}

Player* PlayerManager::GetPlayerByUuid(const String& uuid) {
  for (size_t i = 0; i < player_count; ++i) {
    Player* player = players + i;
    String player_uuid(player->uuid, 16);

    if (poly_strcmp(player_uuid, uuid) == 0) {
      return player;
    }
  }

  return nullptr;
}

void PlayerManager::RenderPlayerList(render::VulkanRenderer& renderer) {
  float center_x = renderer.swap_extent.width / 2.0f;

  render::FontStyleFlags style = render::FontStyle_DropShadow;

  float max_width = 0;

  // Find the width of the longest player name
  for (size_t i = 0; i < player_count; ++i) {
    Player* player = players + i;
    String player_name(player->name);

    float text_width = (float)renderer.font_renderer.GetTextWidth(player_name);

    if (text_width > max_width) {
      max_width = text_width;
    }
  }

  Vector3f position(center_x - max_width / 2.0f, 16, 0);

  for (size_t i = 0; i < player_count; ++i) {
    Player* player = players + i;
    String player_name(player->name);

    // Add extra pixels to the last displayed name because the glyph is not vertically centered
    float height = (i == player_count - 1) ? 18.0f : 16.0f;

    // Render the background with the size determined by the longest name. Add some horizontal padding.
    renderer.font_renderer.RenderBackground(position + Vector3f(-4, 0, 0), Vector2f(max_width + 8, height));
    renderer.font_renderer.RenderText(position, player_name, style);

    position.y += 16;
  }
}

void ChatManager::Update(render::VulkanRenderer& renderer, float dt) {
  constexpr size_t kChatMessageQueueSize = polymer_array_count(chat_message_queue);

  for (int i = 0; i < polymer_array_count(chat_message_queue); ++i) {
    ChatMessagePopup* popup = chat_message_queue + i;

    if (popup->remaining_time > 0) {
      popup->remaining_time -= dt;
    }
  }

  for (int i = 0; i < polymer_array_count(chat_message_queue); ++i) {
    // Index backwards into the queue from current index - 1 so they come out from bottom up
    size_t index = (chat_message_index - i - 1 + kChatMessageQueueSize) % kChatMessageQueueSize;

    ChatMessagePopup* popup = chat_message_queue + index;

    if (popup->remaining_time <= 0.0f) {
      continue;
    }

    float y = (float)(renderer.swap_extent.height - 24 - i * 18);
    Vector3f position(8, y, 0);

    render::FontStyleFlags style = render::FontStyle_DropShadow;

    float alpha = 1.0f;

    if (popup->remaining_time < 1.0f) {
      alpha = popup->remaining_time;
    }

    Vector4f color(1, 1, 1, alpha);
    Vector4f bg_color(0, 0, 0, 0.4f * alpha);

    float background_width = 660;
    if (position.x + background_width > renderer.swap_extent.width) {
      background_width = (float)renderer.swap_extent.width - 8;
    }

    renderer.font_renderer.RenderBackground(position + Vector3f(-4, 0, 0), Vector2f(background_width, 18), bg_color);
    renderer.font_renderer.RenderText(position, String(popup->message, popup->message_size), style, color);
  }
}

void ChatManager::PushMessage(const char* mesg, size_t mesg_size, float display_time) {
  ChatMessagePopup* popup = chat_message_queue + chat_message_index;

  chat_message_index = (chat_message_index + 1) % polymer_array_count(chat_message_queue);

  memcpy(popup->message, mesg, mesg_size);
  popup->message_size = mesg_size;
  popup->remaining_time = display_time;
}

} // namespace polymer
