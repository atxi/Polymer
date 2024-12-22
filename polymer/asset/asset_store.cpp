#include "asset_store.h"

#include <polymer/render/render_util.h>

#include <stdarg.h>
#include <stdio.h>

#include <lib/json.h>

namespace polymer {
namespace asset {

constexpr const char* kVersionJar = "1.21.jar";
constexpr const char* kVersionDescriptor = "1.21.json";
constexpr const char* kVersionIndex = "1.21.json";
constexpr const char* kVersionDescriptorUrl =
    "https://piston-meta.mojang.com/v1/packages/177e49d3233cb6eac42f0495c0a48e719870c2ae/1.21.json";

const HashSha1 kVersionDescriptorHash("177e49d3233cb6eac42f0495c0a48e719870c2ae");
const String kResourceApi = POLY_STR("https://resources.download.minecraft.net/");

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

HashSha1 GetFileSha1(MemoryArena& trans_arena, const char* name) {
  String entire_file = ReadEntireFile(name, trans_arena);

  if (entire_file.size == 0) {
    return {};
  }

  return Sha1(entire_file);
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

inline char* GetObjectUrl(MemoryArena& arena, HashSha1 hash) {
  char hash_str[3] = {};
  u8 hash_value = hash.hash[0];

  sprintf(hash_str, "%02x", hash_value);

  char full_hash[41];
  hash.ToString(full_hash);

  char* buffer = (char*)arena.Allocate(kResourceApi.size + sizeof(full_hash) + sizeof(hash_str) + 1);

  sprintf(buffer, "%.*s%s/%s", (u32)kResourceApi.size, kResourceApi.data, hash_str, full_hash);

  return buffer;
}

void AssetStore::Initialize() {
  AssetInfo version_info = {};
  version_info.hash = kVersionDescriptorHash;
  version_info.type = AssetType::VersionDescriptor;

  // Check local store for version descriptor file
  if (HasAsset(version_info)) {
    char* filename = GetAbsolutePath(trans_arena, path, "versions/%s", kVersionDescriptor);

    ProcessVersionDescriptor(filename);
  } else {
    net_queue.PushRequest(kVersionDescriptorUrl, this, [](NetworkRequest* request, NetworkResponse* response) {
      AssetStore* store = (AssetStore*)request->userp;

      char* filename = GetAbsolutePath(store->trans_arena, store->path, "versions/%s", kVersionDescriptor);

      response->SaveToFile(filename);
      store->ProcessVersionDescriptor(filename);
    });
  }
}

void AssetStore::ProcessVersionDescriptor(const char* path) {
  String contents = ReadEntireFile(path, trans_arena);

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

        char* filename = GetAbsolutePath(store->trans_arena, store->path, "versions/%s", kVersionJar);

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

        char* filename = GetAbsolutePath(store->trans_arena, store->path, "index/%s", kVersionIndex);

        response->SaveToFile(filename);
        store->ProcessIndex(filename);
      });
    } else {
      char* filename = GetAbsolutePath(trans_arena, this->path, "index/%s", kVersionIndex);

      ProcessIndex(filename);
    }
  }
}

void AssetStore::ProcessIndex(const char* filename) {
  String contents = ReadEntireFile(filename, trans_arena);

  json_value_s* root_value = json_parse(contents.data, contents.size);
  if (!root_value || root_value->type != json_type_object) {
    fprintf(stderr, "AssetStore: Failed to parse version index json.\n");
    exit(1);
  }

  json_object_s* root = json_value_as_object(root_value);
  json_object_s* objects = FindJsonObjectElement(root, "objects");

  if (!objects) {
    fprintf(stderr, "AssetStore: Invalid 'objects' element of version index. Expected object.\n");
    exit(1);
  }

  json_object_element_s* object_element = objects->start;
  while (object_element) {
    String element_name(object_element->name->string, object_element->name->string_size);

    json_object_element_s* current_obj_ele = object_element;

    object_element = object_element->next;

    // Skip over objects that aren't currently necessary.
    if (poly_contains(element_name, POLY_STR("sound"))) continue;
    if (poly_contains(element_name, POLY_STR("/lang/"))) continue;
    if (poly_contains(element_name, POLY_STR("icons/"))) continue;
    if (poly_contains(element_name, POLY_STR("/resourcepacks/"))) continue;

    if (current_obj_ele->value->type == json_type_object) {
      json_object_s* obj = json_value_as_object(current_obj_ele->value);

      String obj_hash_str = FindJsonStringValue(obj, "hash");

      if (obj_hash_str.size > 0) {
        HashSha1 hash(obj_hash_str.data, obj_hash_str.size);

        AssetInfo info = {};
        info.type = AssetType::Object;
        info.hash = hash;

        asset_hash_map.Insert(element_name, hash);

        // Check local store for item.
        // If not found, request from server.
        if (!HasAsset(info)) {
          char* url = GetObjectUrl(trans_arena, hash);

          net_queue.PushRequest(url, this, [](NetworkRequest* request, NetworkResponse* response) {
            AssetStore* store = (AssetStore*)request->userp;

            char* relative_name = request->url + kResourceApi.size;
            char* filename = GetAbsolutePath(store->trans_arena, store->path, "objects/%s", relative_name);

            response->SaveToFile(filename);
          });
        }
      }
    }
  }
}

String AssetStore::LoadObject(MemoryArena& arena, String name) {
  HashSha1* hash = asset_hash_map.Find(name);

  if (hash) {
    char hash_str[3] = {};
    u8 hash_value = hash->hash[0];

    sprintf(hash_str, "%02x", hash_value);

    char* objects_folder = GetAbsolutePath(trans_arena, path, "objects");
    if (platform.FolderExists(objects_folder)) {
      char* hash_folder = GetAbsolutePath(trans_arena, path, "objects/%s", hash_str);

      if (platform.FolderExists(hash_folder)) {
        char fullhash[41];

        hash->ToString(fullhash);

        char* filename = GetAbsolutePath(trans_arena, path, "objects/%s/%s", hash_str, fullhash);

        return ReadEntireFile(filename, arena);
      }
    }
  }

  fprintf(stderr, "AssetStore::GetAsset failed to find asset with name %.*s\n", (u32)name.size, name.data);
  return {};
}

bool AssetStore::HasAsset(AssetInfo& info) {
  char* filename = nullptr;

  ArenaSnapshot snapshot = trans_arena.GetSnapshot();

  switch (info.type) {
  case AssetType::Client: {
    char* versions_folder = GetAbsolutePath(trans_arena, path, "versions");
    if (!platform.FolderExists(versions_folder)) return false;

    filename = GetAbsolutePath(trans_arena, path, "versions/%s", kVersionJar);
  } break;
  case AssetType::VersionDescriptor: {
    char* index_folder = GetAbsolutePath(trans_arena, path, "versions");
    if (!platform.FolderExists(index_folder)) return false;

    filename = GetAbsolutePath(trans_arena, path, "versions/%s", kVersionDescriptor);
  } break;
  case AssetType::Index: {
    char* index_folder = GetAbsolutePath(trans_arena, path, "index");
    if (!platform.FolderExists(index_folder)) return false;

    filename = GetAbsolutePath(trans_arena, path, "index/%s", kVersionIndex);
  } break;
  case AssetType::Object: {
    char minihash[3] = {};
    u8 hash_value = info.hash.hash[0];

    sprintf(minihash, "%02x", hash_value);
    char fullhash[41];
    info.hash.ToString(fullhash);

    char* objects_folder = GetAbsolutePath(trans_arena, path, "objects");
    if (!platform.FolderExists(objects_folder)) return false;

    char* hash_folder = GetAbsolutePath(trans_arena, path, "objects/%s", minihash);
    if (!platform.FolderExists(hash_folder)) return false;

    filename = GetAbsolutePath(trans_arena, path, "objects/%s/%s", minihash, fullhash);
  } break;
  default: {
  } break;
  }

  bool exists = false;

  if (filename) {
    HashSha1 existing_hash = GetFileSha1(trans_arena, filename);
    exists = existing_hash == info.hash;
  }

  trans_arena.Revert(snapshot);

  return exists;
}

char* AssetStore::GetClientPath(MemoryArena& arena) {
  return GetAbsolutePath(arena, path, "versions/%s", kVersionJar);
}

} // namespace asset
} // namespace polymer
