#pragma once

#include <polymer/hashmap.h>
#include <polymer/network_queue.h>
#include <polymer/platform/platform.h>
#include <polymer/types.h>
#include <polymer/util.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct json_object_s;

namespace polymer {
namespace asset {

enum class AssetType : u8 { VersionDescriptor, Index, Object, Client };

struct AssetInfo {
  String name;
  HashSha1 hash;
  AssetType type;
};

// Keeps a local asset store synchronized with the remote store by using the index.
// The store begins by checking if the local index exists, if it doesn't, then it kicks it off to the network queue.
// When the netqueue finishes downloading, it will callback to the store to continue processing the index and kicking
// off any missing assets in the index to the netqueue.
//
// The netqueue will need to be completely empty before assets are considered fully downloaded.
struct AssetStore {
  Platform& platform;
  MemoryArena& perm_arena;
  MemoryArena& trans_arena;
  NetworkQueue& net_queue;
  HashMap<String, HashSha1, MapStringHasher> asset_hash_map;
  String path;

  AssetStore(Platform& platform, MemoryArena& perm_arena, MemoryArena& trans_arena, NetworkQueue& net_queue)
      : platform(platform), perm_arena(perm_arena), trans_arena(trans_arena), asset_hash_map(perm_arena),
        net_queue(net_queue) {
    path = platform.GetAssetStorePath(trans_arena);
  }

  void Initialize();

  bool HasAsset(AssetInfo& info);

  char* GetClientPath(MemoryArena& arena);

  String LoadObject(MemoryArena& arena, String name);

private:
  void ProcessVersionDescriptor(const char* path);
  void ProcessIndex(const char* path);
};

} // namespace asset
} // namespace polymer
