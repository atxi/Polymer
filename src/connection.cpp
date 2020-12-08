#include "connection.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>

#define WIN32_LEAN_AND_MEAN
#include <WS2tcpip.h>
#include <Windows.h>

namespace polymer {

Connection::Connection(MemoryArena& arena) : buffer(arena, kilobytes(64)) {}

ConnectResult Connection::Connect(const char* ip, u16 port) {
  addrinfo hints = {0}, *result = nullptr;

  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  this->fd = socket(hints.ai_family, hints.ai_socktype, hints.ai_protocol);

  if (this->fd < 0) {
    return ConnectResult::ErrorSocket;
  }

  char service[32];

  sprintf(service, "%hu", port);

  if (getaddrinfo(ip, service, &hints, &result) != 0) {
    return ConnectResult::ErrorAddrInfo;
  }

  addrinfo* info = nullptr;

  for (info = result; info != nullptr; info = info->ai_next) {
    sockaddr_in* sockaddr = (sockaddr_in*)info->ai_addr;

    if (::connect(this->fd, (struct sockaddr*)sockaddr, sizeof(sockaddr_in)) == 0) {
      break;
    }
  }

  freeaddrinfo(result);

  if (!info) {
    return ConnectResult::ErrorConnect;
  }

  this->connected = true;

  return ConnectResult::Success;
}

void Connection::Disconnect() {
  assert(this->fd >= 0);
  assert(this->connected);

  closesocket(this->fd);
  this->connected = false;
}

void Connection::SetBlocking(bool blocking) {
  unsigned long mode = blocking ? 0 : 1;

#ifdef _WIN32
  ioctlsocket(this->fd, FIONBIO, &mode);
#else
  int flags = fcntl(this->fd, F_GETFL, 0);

  flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);

  fcntl(this->fd, F_SETFL, flags);
#endif
}

#ifdef _WIN32
struct NetworkInitializer {
  NetworkInitializer() {
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
      fprintf(stderr, "Error WSAStartup: %d\n", WSAGetLastError());
      exit(1);
    }
  }
};
NetworkInitializer _net_init;
#endif

} // namespace polymer
