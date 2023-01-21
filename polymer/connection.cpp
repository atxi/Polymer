#include <polymer/connection.h>

#include <polymer/packet_interpreter.h>

#include <chrono>
#include <thread>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <WS2tcpip.h>

namespace polymer {

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

  int bytes_recv = recv(fd, (char*)rb->data + rb->write_offset, (u32)rb->GetFreeSize(), 0);

  if (bytes_recv == 0) {
    this->connected = false;
    return TickResult::ConnectionClosed;
  } else if (bytes_recv < 0) {
    int err = WSAGetLastError();

    if (err == WSAEWOULDBLOCK) {
      return TickResult::Success;
    }

    fprintf(stderr, "Unexpected socket error: %d\n", err);
    this->Disconnect();
    return TickResult::ConnectionError;
  } else if (bytes_recv > 0) {
    rb->write_offset = (rb->write_offset + bytes_recv) % rb->size;

    assert(interpreter);

    interpreter->Interpret();
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

void Connection::SendHandshake(u32 version, const String& address, u16 port, ProtocolState state_request) {
  builder.WriteVarInt(version);
  builder.WriteString(address);
  builder.WriteU16(port);
  builder.WriteVarInt((u64)state_request);

  builder.Commit(write_buffer, 0x00);

  this->protocol_state = state_request;
}

void Connection::SendPingRequest() {
  builder.Commit(write_buffer, 0x00);
}

void Connection::SendLoginStart(const String& username) {
  builder.WriteString(username);
  builder.WriteU8(0); // HasPlayerUUID

  builder.Commit(write_buffer, 0x00);
}

void Connection::SendKeepAlive(u64 id) {
  builder.WriteU64(id);

  builder.Commit(write_buffer, 0x11);
}

void Connection::SendTeleportConfirm(u64 id) {
  builder.WriteVarInt(id);

  builder.Commit(write_buffer, 0x00);
}

void Connection::SendPlayerPositionAndRotation(const Vector3f& position, float yaw, float pitch, bool on_ground) {
  builder.WriteDouble(position.x);
  builder.WriteDouble(position.y);
  builder.WriteDouble(position.z);

  builder.WriteFloat(yaw);
  builder.WriteFloat(pitch);
  builder.WriteU8(on_ground);

  builder.Commit(write_buffer, 0x14);
}

void Connection::SendChatCommand(const String& message) {
  u64 timestamp = 0;
  u64 salt = 0;
  u64 array_length = 0;
  u64 message_count = 0;

  builder.WriteString(message);
  builder.WriteU64(timestamp);
  builder.WriteU64(salt);
  builder.WriteVarInt(array_length);
  builder.WriteVarInt(message_count);
  // TODO: This doesn't match what wiki says, maybe because it's unclear about its mixed usage of BitSet term.
  // Seems to work fine for insecure chatting.
  builder.WriteU8(0);
  builder.WriteU8(0);
  builder.WriteU8(0);

  builder.Commit(write_buffer, 0x04);
}

void Connection::SendChatMessage(const String& message) {
  u64 timestamp = 0;
  u64 salt = 0;
  u64 message_count = 0;

  builder.WriteString(message);
  builder.WriteU64(timestamp);
  builder.WriteU64(salt);
  builder.WriteU8(0);
  builder.WriteVarInt(message_count);
  // TODO: This doesn't match what wiki says, maybe because it's unclear about its mixed usage of BitSet term.
  // Seems to work fine for insecure chatting.
  builder.WriteU8(0);
  builder.WriteU8(0);
  builder.WriteU8(0);

  builder.Commit(write_buffer, 0x05);
}

void Connection::SendClientStatus(ClientStatusAction action) {
  builder.WriteVarInt((u64)action);

  builder.Commit(write_buffer, 0x06);
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
