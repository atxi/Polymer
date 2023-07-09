#include "asset_store.h"

#include <polymer/render/util.h>

#include <stdarg.h>
#include <stdio.h>

#include <polymer/json.h>

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

static json_object_s* FindJsonObjectElement(json_object_s* obj, const char* name) {
  json_object_element_s* element = obj->start;

  while (element) {
    if (element->value->type == json_type_object) {
      String element_name(element->name->string, element->name->string_size);

      if (strncmp(element_name.data, name, element_name.size) == 0) {
        return json_value_as_object(element->value);
      }
    }

    element = element->next;
  }

  return nullptr;
}

static String FindJsonStringValue(json_object_s* obj, const char* name) {
  json_object_element_s* element = obj->start;

  while (element) {
    if (element->value->type == json_type_string) {
      String element_name(element->name->string, element->name->string_size);

      if (strncmp(element_name.data, name, element_name.size) == 0) {
        json_string_s* str = json_value_as_string(element->value);

        return String(str->string, str->string_size);
      }
    }

    element = element->next;
  }

  return {};
}

HashSha1 GetSha1(const String& contents) {
  HashSha1 result = {};
  hash_state state = {};

  sha1_init(&state);
  sha1_process(&state, (u8*)contents.data, (unsigned long)contents.size);
  sha1_done(&state, result.hash);

  return result;
}

HashSha1 GetFileSha1(MemoryArena& trans_arena, const char* name) {
  String entire_file = ReadEntireFile(name, trans_arena);

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

  String contents = ReadEntireFile(info.path, trans_arena);

  json_value_s* root_value = json_parse(contents.data, contents.size);
  if (!root_value || root_value->type != json_type_object) {
    fprintf(stderr, "AssetStore: Failed to parse version descriptor json.\n");
    exit(1);
  }

  json_object_s* root = json_value_as_object(root_value);

  {
    json_object_s* downloads_obj = FindJsonObjectElement(root, "downloads");

    if (!downloads_obj) {
      fprintf(stderr, "AssetStore: Invalid 'downloads' element of version descriptor. Expected object.\n");
      exit(1);
    }

    json_object_s* client_obj = FindJsonObjectElement(downloads_obj, "client");
    if (!client_obj) {
      fprintf(stderr, "AssetStore: Invalid 'downloads.client' element of version descriptor. Expected object.\n");
      exit(1);
    }

    String sha1_str = FindJsonStringValue(client_obj, "sha1");
    if (sha1_str.size == 0) {
      fprintf(stderr, "AssetStore: Invalid 'downloads.client.sha1' element of version descriptor. Expected string.\n");
      exit(1);
    }

    AssetInfo client_info = {};
    client_info.hash = HashSha1(sha1_str.data, sha1_str.size);
    client_info.type = AssetType::Client;

    if (!HasAsset(client_info)) {
      String url = FindJsonStringValue(client_obj, "url");
      if (url.size == 0) {
        fprintf(stderr, "AssetStore: Invalid 'downloads.client.url' element of version descriptor. Expected string.\n");
        exit(1);
      }

      net_queue.PushRequest(url, this, [](NetworkRequest* request, NetworkResponse* response) {
        AssetStore* store = (AssetStore*)request->userp;

        char* filename = GetAbsolutePath(store->trans_arena, store->path, "versions\\%s", kVersionJar);

        response->SaveToFile(filename);
      });
    }
  }

  {
    json_object_s* assetindex_obj = FindJsonObjectElement(root, "assetIndex");
    if (!assetindex_obj) {
      fprintf(stderr, "AssetStore: Invalid 'assetIndex' element of version descriptor. Expected object.\n");
      exit(1);
    }

    String sha1_str = FindJsonStringValue(assetindex_obj, "sha1");

    AssetInfo index_info = {};
    index_info.hash = HashSha1(sha1_str.data, sha1_str.size);
    index_info.type = AssetType::Index;

    if (!HasAsset(index_info)) {
      String url = FindJsonStringValue(assetindex_obj, "url");
      if (url.size == 0) {
        fprintf(stderr, "AssetStore: Invalid 'downloads.client.url' element of version descriptor. Expected string.\n");
        exit(1);
      }

      net_queue.PushRequest(url, this, [](NetworkRequest* request, NetworkResponse* response) {
        AssetStore* store = (AssetStore*)request->userp;

        char* filename = GetAbsolutePath(store->trans_arena, store->path, "index\\%s", kVersionIndex);

        response->SaveToFile(filename);

        AssetInfo index_info = {};

        index_info.type = AssetType::Index;
        strcpy(index_info.path, filename);

        store->ProcessIndex(index_info);
      });
    } else {
      char* filename = GetAbsolutePath(trans_arena, path, "index\\%s", kVersionIndex);

      strcpy(index_info.path, filename);

      ProcessIndex(index_info);
    }
  }
}

void AssetStore::ProcessIndex(const AssetInfo& info) {
  printf("TODO: Process index. (%s)\n", info.path);
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

char* AssetStore::GetClientPath(MemoryArena& arena) {
  return GetAbsolutePath(arena, path, "versions\\%s", kVersionJar);
}

} // namespace asset
} // namespace polymer
