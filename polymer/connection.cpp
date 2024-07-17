#include <polymer/connection.h>

#include <polymer/packet_interpreter.h>

#include <chrono>
#include <thread>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <WS2tcpip.h>
#include <Windows.h>
#define POLY_EWOULDBLOCK WSAEWOULDBLOCK
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#define closesocket close

#include <fcntl.h>
#define POLY_EWOULDBLOCK EWOULDBLOCK
#endif

namespace polymer {

static int GetLastErrorCode() {
#if defined(_WIN32) || defined(WIN32)
  int err = WSAGetLastError();
#else
  int err = errno;
#endif

  return err;
}

Connection::Connection(MemoryArena& arena)
    : read_buffer(arena, 0), write_buffer(arena, 0), interpreter(nullptr), builder(arena) {}

Connection::TickResult Connection::Tick() {
  RingBuffer* rb = &read_buffer;
  RingBuffer* wb = &write_buffer;

  while (wb->write_offset != wb->read_offset) {
    int bytes_sent = 0;

    if (wb->write_offset > wb->read_offset) {
      // Send up to the write offset
      bytes_sent = send(fd, (char*)wb->data + wb->read_offset, (int)(wb->write_offset - wb->read_offset), 0);
    } else if (wb->write_offset < wb->read_offset) {
      // Send up to the end of the buffer then loop again to send the remaining up to the write offset
      bytes_sent = send(fd, (char*)wb->data + wb->read_offset, (int)(wb->size - wb->read_offset), 0);
    }

    if (bytes_sent > 0) {
      wb->read_offset = (wb->read_offset + bytes_sent) % wb->size;
    }
  }

  int bytes_recv = 1;
  while (bytes_recv > 0) {
    bytes_recv = recv(fd, (char*)rb->data + rb->write_offset, (u32)rb->GetFreeSize(), 0);

    if (bytes_recv == 0) {
      this->connected = false;
      return TickResult::ConnectionClosed;
    } else if (bytes_recv < 0) {
      int err = GetLastErrorCode();

      if (err == POLY_EWOULDBLOCK) {
        return TickResult::Success;
      }

      fprintf(stderr, "Unexpected socket error: %d\n", err);
      this->Disconnect();
      return TickResult::ConnectionError;
    } else if (bytes_recv > 0) {
      rb->write_offset = (rb->write_offset + bytes_recv) % rb->size;

      assert(interpreter);

      if (interpreter->Interpret() == 0) break;
    }
  }

  return TickResult::Success;
}

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
