#pragma once

#include <polymer/hashmap.h>
#include <polymer/network_queue.h>
#include <polymer/platform/platform.h>
#include <polymer/types.h>

#include <stdlib.h>
#include <string.h>

namespace polymer {
namespace asset {

struct HashSha1 {
  u8 hash[20] = {};

  HashSha1() {}

  HashSha1(const char* hex, size_t len) {
    char temp[3] = {};

    for (size_t i = 0; i < len; i += 2) {
      temp[0] = hex[i];
      temp[1] = hex[i + 1];
      u8 value = (u8)strtol(temp, nullptr, 16);

      hash[i / 2] = value;
    }
  }

  HashSha1(const char* hex) : HashSha1(hex, strlen(hex)) {}

  bool operator==(const HashSha1& other) const {
    return memcmp(hash, other.hash, sizeof(hash)) == 0;
  }

  bool operator!=(const HashSha1& other) const {
    return !(*this == other);
  }
};

enum class AssetType : u8 { VersionDescriptor, Index, Object, Client };

struct AssetInfo {
  String name;
  HashSha1 hash;
  char path[2048];
  size_t size;

  AssetType type;
  bool exists;
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
  HashMap<String, AssetInfo, MapStringHasher> asset_map;
  String path;

  AssetStore(Platform& platform, MemoryArena& perm_arena, MemoryArena& trans_arena, NetworkQueue& net_queue,
             String path)
      : platform(platform), perm_arena(perm_arena), trans_arena(trans_arena), asset_map(perm_arena),
        net_queue(net_queue), path(path) {}

  void Initialize();

  bool HasAsset(AssetInfo& info);
  bool StoreAsset(const AssetInfo& info);

  void ProcessVersionDescriptor(const AssetInfo& info);
};

} // namespace asset
} // namespace polymer
