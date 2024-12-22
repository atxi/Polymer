#ifndef POLYMER_PROTOCOL_H_
#define POLYMER_PROTOCOL_H_

#include <polymer/types.h>

namespace polymer {

// TODO: Generate protocol ids and packet data from some standardized source.

struct Vector3f;

constexpr u32 kProtocolVersion = 769;

enum class ProtocolState { Handshake, Status, Login, Configuration, Play };

enum class ClientStatusAction { Respawn, Stats };

struct Connection;

enum PlayerMoveFlag {
  PlayerMoveFlag_Position = (1 << 0),
  PlayerMoveFlag_Look = (1 << 1),
};
typedef u8 PlayerMoveFlags;

enum TeleportFlag {
  TeleportFlag_RelativeX = (1 << 0),
  TeleportFlag_RelativeY = (1 << 1),
  TeleportFlag_RelativeZ = (1 << 2),
  TeleportFlag_RelativeYaw = (1 << 3),
  TeleportFlag_RelativePitch = (1 << 4),
  TeleportFlag_RelativeVelocityX = (1 << 5),
  TeleportFlag_RelativeVelocityY = (1 << 6),
  TeleportFlag_RelativeVelocityZ = (1 << 7),
  TeleportFlag_RotateDelta = (1 << 8),
};
typedef u32 TeleportFlags;

namespace inbound {
namespace status {

enum class ProtocolId { Response, Pong, Count };

} // namespace status

namespace login {

enum class ProtocolId {
  Disconnect,
  EncryptionRequest,
  LoginSuccess,
  SetCompression,
  LoginPluginRequest,
  CookieRequest,
  Count
};

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
  ServerLinks,
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
  TeleportEntity,
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
  MoveMinecart,
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
  PlayerRotation,
  RecipeBookAdd,
  RecipeBookRemove,
  RecipeBookSettings,
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
  SetCenterChunk,
  SetRenderDistance,
  SetCursorItem,
  SetDefaultSpawnPosition,
  DisplayObjective,
  EntityMetadata,
  LinkEntities,
  EntityVelocity,
  EntityEquipment,
  SetExperience,
  UpdateHealth,
  SetHeldItem,
  UpdateObjectives,
  SetPassengers,
  SetPlayerInventorySlot,
  UpdateTeams,
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
  SynchronizeVehiclePosition,
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
  KeepAlive = 0x1A,
  PlayPositionAndRotation = 0x1D,
  ChatMessage = 0x07,
  ChatCommand = 0x05,
  ChunkBatchReceived = 0x09,
  ClientStatus = 0x0A,
  Count
};

void SendKeepAlive(Connection& connection, u64 id);
void SendTeleportConfirm(Connection& connection, u64 id);
void SendPlayerPositionAndRotation(Connection& connection, const Vector3f& position, float yaw, float pitch,
                                   PlayerMoveFlags flags);
void SendChatMessage(Connection& connection, const String& message);
void SendChatCommand(Connection& connection, const String& message);
void SendChunkBatchReceived(Connection& connection, float chunks_per_tick);
void SendClientStatus(Connection& connection, ClientStatusAction action);

} // namespace play
} // namespace outbound

} // namespace polymer

#endif
