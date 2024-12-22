#include "protocol.h"

#include <polymer/connection.h>
#include <polymer/math.h>

namespace polymer {

namespace outbound {
namespace handshake {

void SendHandshake(Connection& connection, u32 version, const char* address, size_t address_size, u16 port,
                   ProtocolState state_request) {
  auto& builder = connection.builder;

  builder.WriteVarInt(version);
  builder.WriteString(address, address_size);
  builder.WriteU16(port);
  builder.WriteVarInt((u64)state_request);

  builder.Commit(connection.write_buffer, 0x00);
  connection.protocol_state = state_request;
}

} // namespace handshake

namespace login {

void SendLoginStart(Connection& connection, const char* username, size_t username_size) {
  auto& builder = connection.builder;

  builder.WriteString(username, username_size);
  builder.WriteU64(0); // UUID start
  builder.WriteU64(0); // UUID end

  builder.Commit(connection.write_buffer, (u32)ProtocolId::LoginStart);
}

void SendAcknowledged(Connection& connection) {
  auto& builder = connection.builder;

  builder.Commit(connection.write_buffer, (u32)ProtocolId::LoginAcknowledged);
}

} // namespace login

namespace configuration {

void SendClientInformation(Connection& connection, u8 view_distance, u8 skin_bitmask, u8 main_hand) {
  auto& builder = connection.builder;

  builder.WriteString(POLY_STR("en_GB")); // Locale
  builder.WriteU8(view_distance);
  builder.WriteVarInt(0); // ChatMode enabled
  builder.WriteU8(1);     // Chat Colors
  builder.WriteU8(skin_bitmask);
  builder.WriteVarInt(main_hand);
  builder.WriteU8(0); // Text filtering
  builder.WriteU8(1); // Allow listing

  enum class ParticleMode {
    All,
    Decreased,
    Minimal,
  };

  ParticleMode particle_mode = ParticleMode::All;

  builder.WriteVarInt((u64)particle_mode);

  builder.Commit(connection.write_buffer, (u32)ProtocolId::ClientInformation);
}

void SendKeepAlive(Connection& connection, u64 id) {
  auto& builder = connection.builder;

  builder.WriteU64(id);

  builder.Commit(connection.write_buffer, (u32)ProtocolId::KeepAlive);
}

void SendPong(Connection& connection, u32 id) {
  auto& builder = connection.builder;

  builder.WriteU32(id);

  builder.Commit(connection.write_buffer, (u32)ProtocolId::Pong);
}

void SendFinish(Connection& connection) {
  auto& builder = connection.builder;

  builder.Commit(connection.write_buffer, (u32)ProtocolId::AcknowledgeFinish);
}

void SendKnownPacks(Connection& connection) {
  auto& builder = connection.builder;

  builder.WriteVarInt(1);
  builder.WriteString(POLY_STR("minecraft"));
  builder.WriteString(POLY_STR("core"));
  builder.WriteString(POLY_STR("1.21"));

  builder.Commit(connection.write_buffer, (u32)ProtocolId::KnownPacks);
}

} // namespace configuration

namespace play {

void SendKeepAlive(Connection& connection, u64 id) {
  auto& builder = connection.builder;

  builder.WriteU64(id);

  builder.Commit(connection.write_buffer, (u32)ProtocolId::KeepAlive);
}

void SendTeleportConfirm(Connection& connection, u64 id) {
  auto& builder = connection.builder;

  builder.WriteVarInt(id);

  builder.Commit(connection.write_buffer, (u32)ProtocolId::TeleportConfirm);
}

void SendPlayerPositionAndRotation(Connection& connection, const Vector3f& position, float yaw, float pitch,
                                   PlayerMoveFlags flags) {
  auto& builder = connection.builder;

  builder.WriteDouble(position.x);
  builder.WriteDouble(position.y);
  builder.WriteDouble(position.z);

  builder.WriteFloat(yaw);
  builder.WriteFloat(pitch);

  builder.WriteU8(flags);

  builder.Commit(connection.write_buffer, (u32)ProtocolId::PlayPositionAndRotation);
}

void SendChatMessage(Connection& connection, const String& message) {
  auto& builder = connection.builder;

  u64 timestamp = 0;
  u64 salt = 0;
  u64 message_count = 0;

  builder.WriteString(message);
  builder.WriteU64(timestamp);
  builder.WriteU64(salt);
  builder.WriteU8(0); // HasSignature
  builder.WriteVarInt(message_count);

  const u32 kBitsetSize = 20;
  const u32 kEmptyBitsetBytes = (kBitsetSize + 8) / 8;

  for (size_t i = 0; i < kEmptyBitsetBytes; ++i) {
    builder.WriteU8(0);
  }

  builder.Commit(connection.write_buffer, (u32)ProtocolId::ChatMessage);
}

void SendChatCommand(Connection& connection, const String& message) {
  auto& builder = connection.builder;

  builder.WriteString(message);

  builder.Commit(connection.write_buffer, (u32)ProtocolId::ChatCommand);
}

void SendChunkBatchReceived(Connection& connection, float chunks_per_tick) {
  auto& builder = connection.builder;

  builder.WriteFloat(chunks_per_tick);

  builder.Commit(connection.write_buffer, (u32)ProtocolId::ChunkBatchReceived);
}

void SendClientStatus(Connection& connection, ClientStatusAction action) {
  auto& builder = connection.builder;

  builder.WriteVarInt((u64)action);

  builder.Commit(connection.write_buffer, (u32)ProtocolId::ClientStatus);
}

} // namespace play

} // namespace outbound

} // namespace polymer
