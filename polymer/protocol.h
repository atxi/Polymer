#ifndef POLYMER_PROTOCOL_H_
#define POLYMER_PROTOCOL_H_

#include <polymer/types.h>

namespace polymer {

// TODO: Generate protocol ids and packet data from some standardized source.

struct Vector3f;

constexpr u32 kProtocolVersion = 767;

enum class ProtocolState { Handshake, Status, Login, Configuration, Play };

enum class ClientStatusAction { Respawn, Stats };

struct Connection;

namespace inbound {
namespace status {

enum class ProtocolId { Response, Pong, Count };

} // namespace status

namespace login {

enum class ProtocolId { Disconnect, EncryptionRequest, LoginSuccess, SetCompression, LoginPluginRequest, Count };

} // namespace login

namespace configuration {
enum class ProtocolId {
  CookieRequest,
  PluginMessage,
  Disconnect,
  Finish,
  KeepAlive,
  Ping,
  ResetChat,
  RegistryData,
  RemoveResourcePack,
  AddResourcePack,
  StoreCookie,
  Transfer,
  FeatureFlags,
  UpdateTags,
  KnownPacks,
  CustomReportDetails,
  Count
};

} // namespace configuration

namespace play {

enum class ProtocolId {
  BundleDelimiter,
  SpawnEntity,
  SpawnExperienceOrb,
  EntityAnimation,
  AwardStatistics,
  AcknowledgeBlockChange,
  SetBlockDestroyStage,
  BlockEntityData,
  BlockAction,
  BlockUpdate,
  BossBar,
  ChangeDifficulty,
  ChunkBatchFinished,
  ChunkBatchStart,
  ChunkBiomes,
  ClearTitles,
  CommandSuggestionsResponse,
  Commands,
  CloseContainer,
  SetContainerContent,
  SetContainerProperty,
  SetContainerSlot,
  CookieRequest,
  SetCooldown,
  ChatSuggestions,
  PluginMessage,
  DamageEvent,
  DebugSample,
  DeleteMessage,
  Disconnect,
  DisguisedChatMessage,
  EntityEvent,
  Explosion,
  UnloadChunk,
  GameEvent,
  OpenHorseScreen,
  HurtAnimation,
  InitializeWorldBorder,
  KeepAlive,
  ChunkData,
  WorldEvent,
  Particle,
  UpdateLight,
  Login,
  MapData,
  MerchantOffers,
  EntityPosition,
  EntityPositionAndRotation,
  EntityRotation,
  VehicleMove,
  OpenBook,
  OpenScreen,
  OpenSignEditor,
  Ping,
  PingResponse,
  PlaceGhostRecipe,
  PlayerAbilities,
  PlayerChatMessage,
  EndCombatEvent,
  EnterCombatEvent,
  DeathCombatEvent,
  PlayerInfoRemove,
  PlayerInfoUpdate,
  LookAt,
  PlayerPositionAndLook,
  UpdateRecipeBook,
  RemoveEntities,
  RemoveEntityEffect,
  ResetScore,
  RemoveResourcePack,
  AddResourcePack,
  Respawn,
  SetHeadRotation,
  UpdateSectionBlocks,
  SelectAdvancementTab,
  ServerData,
  SetActionBarText,
  WorldBorderCenter,
  WorldBorderLerpSize,
  WorldBorderSize,
  WorldBorderWarningDelay,
  WorldBorderWarningDistance,
  Camera,
  SetHeldItem,
  SetCenterChunk,
  SetRenderDistance,
  SetDefaultSpawnPosition,
  DisplayObjective,
  EntityMetadata,
  LinkEntities,
  EntityVelocity,
  EntityEquipment,
  SetExperience,
  UpdateHealth,
  UpdateObjectives,
  SetPassengers,
  Teams,
  UpdateScore,
  UpdateSimulationDistance,
  SetSubtitleText,
  TimeUpdate,
  SetTitleText,
  SetTitleAnimationTimes,
  EntitySoundEffect,
  SoundEffect,
  StartConfiguration,
  StopSound,
  StoreCookie,
  SystemChatMessage,
  PlayerListHeaderAndFooter,
  NBTQueryResponse,
  CollectItem,
  EntityTeleport,
  SetTickingState,
  StepTick,
  Transfer,
  UpdateAdvancements,
  UpdateAttributes,
  EntityEffect,
  UpdateRecipes,
  Tags,
  ProjectilePower,
  CustomReportDetails,
  ServerLinks,

  Count
};

} // namespace play
} // namespace inbound

namespace outbound {
namespace handshake {

enum class ProtocolId { Handshake, Count };

void SendHandshake(Connection& connection, u32 version, const char* address, size_t address_size, u16 port,
                   ProtocolState state_request);

} // namespace handshake

namespace login {

enum class ProtocolId { LoginStart, EncryptionResponse, LoginPluginResponse, LoginAcknowledged, CookieResponse, Count };

void SendLoginStart(Connection& connection, const char* username, size_t username_size);
void SendAcknowledged(Connection& connection);

} // namespace login

namespace configuration {

enum class ProtocolId {
  ClientInformation,
  CookieResponse,
  PluginMessage,
  AcknowledgeFinish,
  KeepAlive,
  Pong,
  ResourcePack,
  KnownPacks,
  Count
};

void SendClientInformation(Connection& connection, u8 view_distance, u8 skin_bitmask, u8 main_hand);
void SendKeepAlive(Connection& connection, u64 id);
void SendPong(Connection& connection, u32 id);
void SendFinish(Connection& connection);
void SendKnownPacks(Connection& connection);

} // namespace configuration

namespace play {

enum class ProtocolId {
  TeleportConfirm = 0x00,
  KeepAlive = 0x18,
  PlayPositionAndRotation = 0x1B,
  ChatMessage = 0x06,
  ChatCommand = 0x04,
  ChunkBatchReceived = 0x08,
  ClientStatus = 0x09,
  Count
};

void SendKeepAlive(Connection& connection, u64 id);
void SendTeleportConfirm(Connection& connection, u64 id);
void SendPlayerPositionAndRotation(Connection& connection, const Vector3f& position, float yaw, float pitch,
                                   bool on_ground);
void SendChatMessage(Connection& connection, const String& message);
void SendChatCommand(Connection& connection, const String& message);
void SendChunkBatchReceived(Connection& connection, float chunks_per_tick);
void SendClientStatus(Connection& connection, ClientStatusAction action);

} // namespace play
} // namespace outbound

} // namespace polymer

#endif
