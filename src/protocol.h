#ifndef POLYMER_PROTOCOL_H_
#define POLYMER_PROTOCOL_H_

namespace polymer {

enum class StatusProtocol { Response, Pong, Count };

enum class LoginProtocol { Disconnect, EncryptionRequest, LoginSuccess, SetCompression, LoginPluginRequest, Count };

enum class PlayProtocol {
  SpawnEntity,
  SpawnExperienceOrb,
  SpawnLivingEntity,
  SpawnPainting,
  SpawnPlayer,
  EntityAnimation,
  Statistics,
  AcknowledgePlayerDigging,
  BlockBreakAnimation,
  BlockEntityData,
  BlockAction,
  BlockChange,
  BossBar,
  ServerDifficulty,
  ChatMessage,
  TabComplete,
  DeclareCommands,
  WindowConfirmation,
  CloseWindow,
  WindowItems,
  WindowProperty,
  SetSlot,
  SetCooldown,
  PluginMessage,
  NamedSoundEffect,
  Disconnect,
  EntityStatus,
  Explosion,
  UnloadChunk,
  ChangeGameState,
  OpenHorseWindow,
  KeepAlive,
  ChunkData,
  Effect,
  Particle,
  UpdateLight,
  JoinGame,
  MapData,
  TradeList,
  EntityPosition,
  EntityPositionAndRotation,
  EntityRotation,
  EntityMovement,
  VehicleMove,
  OpenBook,
  OpenWindow,
  OpenSignEditor,
  CraftRecipeResponse,
  PlayerAbilities,
  CombatEvent,
  PlayerInfo,
  FacePlayer,
  PlayerPositionAndLook,
  UnlockRecipes,
  DestroyEntities,
  RemoveEntityEffect,
  ResourcePackSend,
  Respawn,
  EntityHeadLook,
  MultiBlockChange,
  SelectAdvancementTab,
  WorldBorder,
  Camera,
  HeldItemChange,
  UpdateViewPosition,
  UpdateViewDistance,
  SpawnPosition,
  DisplayScoreboard,
  EntityMetadata,
  AttachEntity,
  EntityVelocity,
  EntityEquipment,
  SetExperience,
  UpdateHealth,
  ScoreboardObjective,
  SetPassengers,
  Teams,
  UpdateScore,
  TimeUpdate,
  Title,
  EntitySoundEffect,
  SoundEffect,
  StopSound,
  PlayerListHeadAndFooter,
  NBTQueryResponse,
  CollectItem,
  EntityTeleport,
  Advancements,
  EntityProperties,
  EntityEffect,
  DeclareEcipes,
  Tags,

  Count
};

} // namespace polymer

#endif
