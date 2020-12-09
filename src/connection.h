#ifndef POLYMER_CONNECTION_H_
#define POLYMER_CONNECTION_H_

#include "buffer.h"
#include "memory.h"
#include "types.h"

namespace polymer {

enum class ConnectResult { Success, ErrorSocket, ErrorAddrInfo, ErrorConnect };
enum class ProtocolState { Handshake, Status, Login, Play };

#ifdef _WIN64
using SocketType = long long;
#else
using SocketType = int;
#endif

struct PacketInterpreter;

struct Connection {
  enum class TickResult { Success, ConnectionClosed, ConnectionError };

  SocketType fd = -1;
  bool connected = false;

  RingBuffer read_buffer;
  RingBuffer write_buffer;

  PacketInterpreter* interpreter;

  Connection(MemoryArena& arena);

  ConnectResult Connect(const char* ip, u16 port);
  void Disconnect();
  void SetBlocking(bool blocking);

  TickResult Tick();

  void SendHandshake(u32 version, const char* address, u16 port, ProtocolState state_request);
  void SendPingRequest();
  void SendLoginStart(const char* username);
  void SendKeepAlive(u64 id);
  void SendTeleportConfirm(u64 id);
};

} // namespace polymer

#endif
