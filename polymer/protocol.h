#ifndef POLYMER_PROTOCOL_H_
#define POLYMER_PROTOCOL_H_

namespace polymer {

// TODO: Generate protocol ids and packet data from some standardized source.

constexpr u32 kProtocolVersion = 765;

enum class StatusProtocol { Response, Pong, Count };

enum class LoginProtocol { Disconnect, EncryptionRequest, LoginSuccess, SetCompression, LoginPluginRequest, Count };

enum class ConfigurationProtocol {
  PluginMessage,
  Disconnect,
  Finish,
  KeepAlive,
  Ping,
  RegistryData,
  RemoveResourcePack,
  AddResourcePack,
  FeatureFlags,
  UpdateTags,

  Count
};

enum class PlayProtocol {
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
  SetCooldown,
  ChatSuggestions,
  PluginMessage,
  DamageEvent,
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
  SystemChatMessage,
  PlayerListHeaderAndFooter,
  NBTQueryResponse,
  CollectItem,
  EntityTeleport,
  SetTickingState,
  StepTick,
  UpdateAdvancements,
  UpdateAttributes,
  EntityEffect,
  UpdateRecipes,
  Tags,

  Count
};

} // namespace polymer

#endif
