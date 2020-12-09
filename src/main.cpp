#include "connection.h"
#include "gamestate.h"
#include "inflate.h"
#include "memory.h"
#include "packet_interpreter.h"
#include "types.h"

#include <cstdio>
#include <cassert>

#pragma comment(lib, "ws2_32.lib")

#define WIN32_LEAN_AND_MEAN
#include <WS2tcpip.h>
#include <Windows.h>

namespace polymer {

int run() {
  constexpr size_t kMirrorBufferSize = 65536 * 32;
  constexpr size_t kPermanentSize = gigabytes(1);
  constexpr size_t kTransientSize = megabytes(32);

  u8* perm_memory = (u8*)VirtualAlloc(NULL, kPermanentSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  u8* trans_memory = (u8*)VirtualAlloc(NULL, kTransientSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

  MemoryArena perm_arena(perm_memory, kPermanentSize);
  MemoryArena trans_arena(trans_memory, kTransientSize);

  printf("Polymer\n");

  GameState* game = memory_arena_construct_type(&perm_arena, GameState, &perm_arena, &trans_arena);
  PacketInterpreter interpreter(game);
  Connection* connection = &game->connection;

  connection->interpreter = &interpreter;

  game->LoadBlocks();

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

  connection->SendHandshake(754, "127.0.0.1", 25565, ProtocolState::Login);
  connection->SendLoginStart("polymer");

  while (connection->connected) {
    trans_arena.Reset();

    Connection::TickResult result = connection->Tick();

    if (result == Connection::TickResult::ConnectionClosed) {
      fprintf(stderr, "Connection closed by server.\n");
    }
  }

  return 0;
}

} // namespace polymer

int main(int argc, char* argv[]) {
  return polymer::run();
}
