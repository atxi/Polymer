#pragma once

#include <polymer/input.h>
#include <polymer/memory.h>
#include <polymer/platform/args.h>
#include <polymer/render/render.h>

#include <polymer/platform/platform.h>

namespace polymer {

struct GameState;

struct Polymer {
  MemoryArena& perm_arena;
  MemoryArena& trans_arena;
  Platform platform = {};
  PolymerWindow window = nullptr;

  render::VulkanRenderer renderer;
  GameState* game = nullptr;

  polymer::LaunchArgs args;

  Polymer(MemoryArena& perm_arena, MemoryArena& trans_arena, int argc, char** argv);

  int Run(InputState* input);
};

} // namespace polymer
