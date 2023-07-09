#include "asset_store.h"

#include <polymer/render/util.h>

#include <stdarg.h>
#include <stdio.h>

#pragma warning(disable : 4273)
#include <tomcrypt.h>

namespace polymer {
namespace asset {

constexpr const char* kVersionJar = "1.20.1.jar";
constexpr const char* kVersionDescriptor = "1.20.1.json";
constexpr const char* kVersionIndex = "1.20.1.json";
constexpr const char* kVersionDescriptorUrl =
    "https://piston-meta.mojang.com/v1/packages/715ccf3330885e75b205124f09f8712542cbe7e0/1.20.1.json";

const HashSha1 kVersionDescriptorHash("715ccf3330885e75b205124f09f8712542cbe7e0");

HashSha1 GetSha1(const String& contents) {
  HashSha1 result = {};
  hash_state state = {};

  sha1_init(&state);
  sha1_process(&state, (u8*)contents.data, (unsigned long)contents.size);
  sha1_done(&state, result.hash);

  return result;
}

HashSha1 GetFileSha1(MemoryArena& trans_arena, const char* name) {
  String entire_file = ReadEntireFile(name, &trans_arena);

  if (entire_file.size == 0) {
    return {};
  }

  return GetSha1(entire_file);
}

inline char* GetAbsolutePath(MemoryArena& arena, const String& path, const char* fmt, ...) {
  char* buffer = (char*)arena.Allocate(path.size + 2048);

  memcpy(buffer, path.data, path.size);

  va_list args;

  va_start(args, fmt);
  size_t size = vsprintf(buffer + path.size, fmt, args);
  va_end(args);

  return buffer;
}

inline bool GetOrCreateFolder(Platform& platform, const char* path) {
  if (!platform.FolderExists(path)) {
    if (!platform.CreateFolder(path)) {
      fprintf(stderr, "Failed to create folder '%s'\n", path);
      return false;
    }
  }

  return true;
}

void AssetStore::Initialize() {
  AssetInfo version_info = {};
  version_info.hash = kVersionDescriptorHash;
  version_info.type = AssetType::VersionDescriptor;

  // Check local store for version descriptor file
  if (HasAsset(version_info)) {
    ProcessVersionDescriptor(version_info);
  } else {
    net_queue.PushRequest(kVersionDescriptorUrl, this, [](NetworkRequest* request, NetworkResponse* response) {
      AssetStore* store = (AssetStore*)request->userp;

      char* filename = GetAbsolutePath(store->trans_arena, store->path, "versions\\%s", kVersionDescriptor);

      response->SaveToFile(filename);
      AssetInfo info;

      info.type = AssetType::VersionDescriptor;
      info.exists = true;
      strcpy(info.path, filename);

      store->ProcessVersionDescriptor(info);
    });
  }
}

void AssetStore::ProcessVersionDescriptor(const AssetInfo& info) {
  //
  // Load json and grab client/index

  // If not client, net_queue request
  // if not index, net_queue request
}

bool AssetStore::HasAsset(AssetInfo& info) {
  switch (info.type) {
  case AssetType::Client: {
    char* versions_folder = GetAbsolutePath(trans_arena, path, "versions");
    if (!platform.FolderExists(versions_folder)) return false;

    char* filename = GetAbsolutePath(trans_arena, path, "versions\\%s", kVersionJar);
    HashSha1 existing_hash = GetFileSha1(trans_arena, filename);

    return existing_hash == info.hash;
  } break;
  case AssetType::VersionDescriptor: {
    char* index_folder = GetAbsolutePath(trans_arena, path, "versions");
    if (!platform.FolderExists(index_folder)) return false;

    char* filename = GetAbsolutePath(trans_arena, path, "versions\\%s", kVersionDescriptor);
    HashSha1 existing_hash = GetFileSha1(trans_arena, filename);

    if (existing_hash != info.hash) {
      return false;
    }

    strcpy(info.path, filename);
    return true;
  } break;
  case AssetType::Index: {
    char* index_folder = GetAbsolutePath(trans_arena, path, "index");
    if (!platform.FolderExists(index_folder)) return false;

    char* filename = GetAbsolutePath(trans_arena, path, "index\\%s", kVersionIndex);
    HashSha1 existing_hash = GetFileSha1(trans_arena, filename);

    return existing_hash == info.hash;
  } break;
  case AssetType::Object: {
    char hash_str[3] = {};
    u8 hash_value = info.hash.hash[0];

    sprintf(hash_str, "%02x", hash_value);

    char* objects_folder = GetAbsolutePath(trans_arena, path, "objects");
    if (!platform.FolderExists(objects_folder)) return false;

    char* hash_folder = GetAbsolutePath(trans_arena, path, "objects\\%s", hash_str);
    if (!platform.FolderExists(hash_folder)) return false;

    char* filename =
        GetAbsolutePath(trans_arena, path, "objects\\%s\\%.*s", hash_str, (u32)info.name.size, info.name.data);

    HashSha1 existing_hash = GetFileSha1(trans_arena, filename);

    return existing_hash == info.hash;
  } break;
  default: {
  } break;
  }

  return false;
}

} // namespace asset
} // namespace polymer
