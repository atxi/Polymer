#ifndef POLYMER_CONNECTION_H_
#define POLYMER_CONNECTION_H_

#include "buffer.h"
#include "memory.h"
#include "polymer.h"

namespace polymer {

enum class ConnectResult { Success, ErrorSocket, ErrorAddrInfo, ErrorConnect };

#ifdef _WIN64
using SocketType = long long;
#else
using SocketType = int;
#endif

struct Connection {
  SocketType fd = -1;
  bool connected = false;

  RingBuffer read_buffer;
  RingBuffer write_buffer;

  Connection(MemoryArena& arena);

  ConnectResult Connect(const char* ip, u16 port);
  void Disconnect();
  void SetBlocking(bool blocking);
};

} // namespace polymer

#endif
