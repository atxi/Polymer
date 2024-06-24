#ifndef POLYMER_CONNECTION_H_
#define POLYMER_CONNECTION_H_

#include <polymer/buffer.h>
#include <polymer/math.h>
#include <polymer/memory.h>
#include <polymer/protocol.h>
#include <polymer/types.h>

namespace polymer {

enum class ConnectResult { Success, ErrorSocket, ErrorAddrInfo, ErrorConnect };

#ifdef _WIN64
using SocketType = long long;
#else
using SocketType = int;
#endif

struct PacketBuilder {
  enum BuildFlag {
    BuildFlag_Compression = (1 << 0),
    BuildFlag_OmitCompress = (1 << 1),
  };
  using BuildFlags = u32;

  RingBuffer buffer;
  BuildFlags flags;

  PacketBuilder(MemoryArena& arena) : buffer(arena, 32767), flags(BuildFlag_OmitCompress) {}

  inline void Commit(RingBuffer& out, u32 pid) {
    size_t compress_length_size = (flags & BuildFlag_OmitCompress) ? 0 : GetVarIntSize(0);
    size_t total_size = buffer.write_offset + compress_length_size + GetVarIntSize(pid);

    out.WriteVarInt(total_size);

    if (!(flags & BuildFlag_OmitCompress)) {
      // TODO: Implement compression
      out.WriteVarInt(0);
    }

    out.WriteVarInt(pid);

    if (buffer.write_offset > 0) {
      out.WriteRawString(String((char*)buffer.data, buffer.write_offset));
      buffer.write_offset = 0;
    }
  }

  inline void WriteU8(u8 value) {
    buffer.WriteU8(value);
  }

  inline void WriteU16(u16 value) {
    buffer.WriteU16(value);
  }

  inline void WriteU32(u32 value) {
    buffer.WriteU32(value);
  }

  inline void WriteU64(u64 value) {
    buffer.WriteU64(value);
  }

  inline void WriteVarInt(u64 value) {
    buffer.WriteVarInt(value);
  }

  inline void WriteFloat(float value) {
    buffer.WriteFloat(value);
  }

  inline void WriteDouble(double value) {
    buffer.WriteDouble(value);
  }

  inline void WriteString(const String& str) {
    buffer.WriteString(str);
  }

  inline void WriteString(const char* str, size_t size) {
    buffer.WriteString(str, size);
  }

  inline void WriteRawString(const String& str) {
    buffer.WriteRawString(str);
  }

  inline void WriteRawString(const char* str, size_t size) {
    buffer.WriteRawString(str, size);
  }
};

struct Connection {
  enum class TickResult { Success, ConnectionClosed, ConnectionError };

  SocketType fd = -1;
  bool connected = false;
  ProtocolState protocol_state = ProtocolState::Handshake;

  RingBuffer read_buffer;
  RingBuffer write_buffer;

  PacketBuilder builder;

  struct PacketInterpreter* interpreter;

  Connection(MemoryArena& arena);

  ConnectResult Connect(const char* ip, u16 port);
  void Disconnect();
  void SetBlocking(bool blocking);

  TickResult Tick();
};

} // namespace polymer

#endif
