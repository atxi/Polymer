#ifndef POLYMER_PACKET_INTERPRETER_H_
#define POLYMER_PACKET_INTERPRETER_H_

#include <polymer/buffer.h>
#include <polymer/types.h>

namespace polymer {

struct GameState;

struct PacketInterpreter {
  GameState* game;
  bool compression;

  RingBuffer inflate_buffer;

  PacketInterpreter(GameState* game);

  void Interpret();

private:
  void InterpretStatus(RingBuffer* rb, u64 pkt_id, size_t pkt_size);
  void InterpretLogin(RingBuffer* rb, u64 pkt_id, size_t pkt_size);
  void InterpretPlay(RingBuffer* rb, u64 pkt_id, size_t pkt_size);
};

} // namespace polymer

#endif
