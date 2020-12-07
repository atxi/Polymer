#ifndef POLYMER_CONNECTION_H_
#define POLYMER_CONNECTION_H_

#include "memory.h"
#include "polymer.h"

namespace polymer {

enum class ConnectResult { Success, ErrorSocket, ErrorAddrInfo, ErrorConnect };

struct Connection {
  int fd = -1;
  bool connected = false;

  RingBuffer buffer;

  Connection(MemoryArena& arena);

  ConnectResult Connect(const char* ip, u16 port);
  void Disconnect();
};

} // namespace polymer

#endif
