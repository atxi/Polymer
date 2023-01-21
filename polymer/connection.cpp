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

Connection::Connection(MemoryArena& arena) : read_buffer(arena, 0), write_buffer(arena, 0), interpreter(nullptr) {}

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
  RingBuffer& wb = write_buffer;
  u32 pid = 0;

  size_t size = GetVarIntSize(pid) + GetVarIntSize(version) + GetVarIntSize(address.size) + address.size + sizeof(u16) +
                GetVarIntSize((u64)state_request);

  wb.WriteVarInt(size);
  wb.WriteVarInt(pid);

  wb.WriteVarInt(version);
  wb.WriteString(address);
  wb.WriteU16(port);
  wb.WriteVarInt((u64)state_request);

  this->protocol_state = state_request;
}

void Connection::SendPingRequest() {
  RingBuffer& wb = write_buffer;

  u32 pid = 0;
  size_t size = GetVarIntSize(pid);

  wb.WriteVarInt(size);
  wb.WriteVarInt(pid);
}

void Connection::SendLoginStart(const String& username) {
  RingBuffer& wb = write_buffer;

  u32 pid = 0;
  size_t size = GetVarIntSize(pid) + GetVarIntSize(username.size) + username.size + 1;

  wb.WriteVarInt(size);
  wb.WriteVarInt(pid);
  wb.WriteString(username);
  wb.WriteU8(0); // HasPlayerUUID
}

void Connection::SendKeepAlive(u64 id) {
  RingBuffer& wb = write_buffer;

  u32 pid = 0x11;
  size_t size = GetVarIntSize(pid) + GetVarIntSize(0) + sizeof(id);

  wb.WriteVarInt(size);
  wb.WriteVarInt(0); // compression
  wb.WriteVarInt(pid);

  wb.WriteU64(id);
}

void Connection::SendTeleportConfirm(u64 id) {
  RingBuffer& wb = write_buffer;
  u32 pid = 0x00;

  size_t size = GetVarIntSize(pid) + GetVarIntSize(0) + GetVarIntSize(id);

  wb.WriteVarInt(size);
  wb.WriteVarInt(0);
  wb.WriteVarInt(pid);

  wb.WriteVarInt(id);
}

void Connection::SendPlayerPositionAndRotation(const Vector3f& position, float yaw, float pitch, bool on_ground) {
  RingBuffer& wb = write_buffer;
  u32 pid = 0x14;

  size_t size = GetVarIntSize(pid) + GetVarIntSize(0) + sizeof(double) + sizeof(double) + sizeof(double) +
                sizeof(float) + sizeof(float) + 1;

  wb.WriteVarInt(size);
  wb.WriteVarInt(0);
  wb.WriteVarInt(pid);

  wb.WriteDouble(position.x);
  wb.WriteDouble(position.y);
  wb.WriteDouble(position.z);

  wb.WriteFloat(yaw);
  wb.WriteFloat(pitch);
  wb.WriteU8(on_ground);
}

void Connection::SendChatCommand(const String& message) {
  u64 timestamp = 0;
  u64 salt = 0;
  u64 array_length = 0;
  u64 message_count = 0;

  RingBuffer& wb = write_buffer;
  u32 pid = 0x04;

  size_t size = GetVarIntSize(pid) + GetVarIntSize(0) + GetVarIntSize(message.size) + message.size + sizeof(u64) +
                sizeof(u64) + GetVarIntSize(array_length) + GetVarIntSize(message_count) + 3;

  wb.WriteVarInt(size);
  wb.WriteVarInt(0);
  wb.WriteVarInt(pid);

  wb.WriteString(message);
  wb.WriteU64(timestamp);
  wb.WriteU64(salt);
  wb.WriteVarInt(array_length);
  wb.WriteVarInt(message_count);
  // TODO: This doesn't match what wiki says, maybe because it's unclear about its mixed usage of BitSet term.
  // Seems to work fine for insecure chatting.
  wb.WriteU8(0);
  wb.WriteU8(0);
  wb.WriteU8(0);
}

void Connection::SendChatMessage(const String& message) {
  u64 timestamp = 0;
  u64 salt = 0;
  u64 message_count = 0;

  RingBuffer& wb = write_buffer;
  u32 pid = 0x05;

  size_t size = GetVarIntSize(pid) + GetVarIntSize(0) + GetVarIntSize(message.size) + message.size + sizeof(u64) +
                sizeof(u64) + 1 + GetVarIntSize(message_count) + 3;

  wb.WriteVarInt(size);
  wb.WriteVarInt(0);
  wb.WriteVarInt(pid);

  wb.WriteString(message);
  wb.WriteU64(timestamp);
  wb.WriteU64(salt);
  wb.WriteU8(0);
  wb.WriteVarInt(message_count);
  // TODO: This doesn't match what wiki says, maybe because it's unclear about its mixed usage of BitSet term.
  // Seems to work fine for insecure chatting.
  wb.WriteU8(0);
  wb.WriteU8(0);
  wb.WriteU8(0);
}

void Connection::SendClientStatus(ClientStatusAction action) {
  RingBuffer& wb = write_buffer;
  u32 pid = 0x06;

  size_t size = GetVarIntSize(pid) + GetVarIntSize(0) + GetVarIntSize((u64)action);

  wb.WriteVarInt(size);
  wb.WriteVarInt(0);
  wb.WriteVarInt(pid);

  wb.WriteVarInt((u64)action);
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
