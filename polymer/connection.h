#ifndef POLYMER_CONNECTION_H_
#define POLYMER_CONNECTION_H_

#include <polymer/buffer.h>
#include <polymer/math.h>
#include <polymer/memory.h>
#include <polymer/types.h>

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
  ProtocolState protocol_state = ProtocolState::Handshake;

  RingBuffer read_buffer;
  RingBuffer write_buffer;

  PacketInterpreter* interpreter;

  Connection(MemoryArena& arena);

  ConnectResult Connect(const char* ip, u16 port);
  void Disconnect();
  void SetBlocking(bool blocking);

  TickResult Tick();

  void SendHandshake(u32 version, const String& address, u16 port, ProtocolState state_request);
  void SendPingRequest();
  void SendLoginStart(const String& username);
  void SendKeepAlive(u64 id);
  void SendTeleportConfirm(u64 id);
  void SendPlayerPositionAndRotation(const Vector3f& position, float yaw, float pitch, bool on_ground);
  void SendChatMessage(const String& message);
  void SendChatCommand(const String& message);

  enum class ClientStatusAction { Respawn, Stats };
  void SendClientStatus(ClientStatusAction action);
};

} // namespace polymer

#endif
