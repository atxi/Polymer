#ifndef POLYMER_PACKET_INTERPRETER_H_
#define POLYMER_PACKET_INTERPRETER_H_

#include "buffer.h"
#include "types.h"

namespace polymer {

struct GameState;

struct PacketInterpreter {
  GameState* game;
  bool compression;

  RingBuffer inflate_buffer;

  PacketInterpreter(GameState* game);

  void Interpret();
};

} // namespace polymer

#endif
