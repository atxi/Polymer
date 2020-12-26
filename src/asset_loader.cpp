#include "asset_loader.h"

#include <cstring>

namespace polymer {

u32 Hash(const char* str) {
  u32 hash = 5381;
  char c;

  while (c = *str++) {
    hash = hash * 33 ^ c;
  }

  return hash;
}

u32 Hash(const char* str, size_t len) {
  u32 hash = 5381;
  
  for (size_t i = 0; i < len; ++i) {
    hash = hash * 33 ^ str[i];
  }

  return hash;
}

TextureIdMap::TextureIdMap(MemoryArena* arena) : arena(arena), free(nullptr) {
  for (size_t i = 0; i < kTextureIdBuckets; ++i) {
    elements[i] = nullptr;
  }

  for (size_t i = 0; i < 1024; ++i) {
    TextureIdElement* element = memory_arena_push_type(arena, TextureIdElement);
    element->next = free;
    free = element;
  }
}

TextureIdElement* TextureIdMap::Allocate() {
  TextureIdElement* result = nullptr;

  if (free) {
    result = free;
    free = free->next;
  } else {
    result = memory_arena_push_type(arena, TextureIdElement);
  }

  result->next = nullptr;
  return result;
}

void TextureIdMap::Insert(const char* name, u32 value) {
  u32 bucket = Hash(name) & (kTextureIdBuckets - 1);

  TextureIdElement* element = elements[bucket];
  while (element) {
    if (strcmp(element->name, name) == 0) {
      break;
    }
    element = element->next;
  }

  if (element == nullptr) {
    element = Allocate();

    assert(strlen(name) < polymer_array_count(element->name));

    strcpy(element->name, name);
    element->value = value;

    element->next = elements[bucket];
    elements[bucket] = element;
  }
}

u32* TextureIdMap::Find(const char* name) {
  u32 bucket = Hash(name) & (kTextureIdBuckets - 1);
  TextureIdElement* element = elements[bucket];

  while (element) {
    if (strcmp(element->name, name) == 0) {
      return &element->value;
    }
    element = element->next;
  }

  return nullptr;
}

FaceTextureMap::FaceTextureMap(MemoryArena* arena) : arena(arena), free(nullptr) {
  for (size_t i = 0; i < kTextureMapBuckets; ++i) {
    elements[i] = nullptr;
  }

  for (size_t i = 0; i < 32; ++i) {
    FaceTextureElement* element = memory_arena_push_type(arena, FaceTextureElement);
    element->next = free;
    free = element;
  }
}

FaceTextureElement* FaceTextureMap::Allocate() {
  FaceTextureElement* result = nullptr;

  if (free) {
    result = free;
    free = free->next;
  } else {
    result = memory_arena_push_type(arena, FaceTextureElement);
  }

  result->next = nullptr;
  return result;
}

void FaceTextureMap::Insert(const char* name, size_t namelen, const char* value, size_t valuelen) {
  u32 bucket = Hash(name, namelen) & (kTextureMapBuckets - 1);

  FaceTextureElement* element = elements[bucket];
  while (element) {
    if (strcmp(element->name, name) == 0) {
      break;
    }
    element = element->next;
  }

  if (element == nullptr) {
    element = Allocate();

    assert(namelen < polymer_array_count(element->name));
    assert(valuelen < polymer_array_count(element->value));

    strncpy(element->name, name, namelen);
    strncpy(element->value, value, valuelen);

    element->next = elements[bucket];
    elements[bucket] = element;
  }
}

const char* FaceTextureMap::Find(const char* name) {
  u32 bucket = Hash(name) & (kTextureMapBuckets - 1);
  FaceTextureElement* element = elements[bucket];

  while (element) {
    if (strcmp(element->name, name) == 0) {
      return element->value;
    }
    element = element->next;
  }

  return nullptr;
}

void ParsedBlockModel::InsertTextureMap(FaceTextureMap* map) {
  json_object_element_s* root_element = root->start;

  while (root_element) {
    if (strcmp(root_element->name->string, "textures") == 0) {
      json_object_element_s* texture_element = json_value_as_object(root_element->value)->start;

      while (texture_element) {
        json_string_s* value_string = json_value_as_string(texture_element->value);

        map->Insert(texture_element->name->string, texture_element->name->string_size, value_string->string,
                    value_string->string_size);

        texture_element = texture_element->next;
      }
      break;
    }

    root_element = root_element->next;
  }
}

bool AssetLoader::OpenArchive(const char* filename) {
  snapshot = arena->GetSnapshot();

  return archive.Open(filename);
}

void AssetLoader::CloseArchive() {
  archive.Close();
  arena->Revert(snapshot);
}

size_t AssetLoader::ParseBlockModels() {
  size_t count;
  ZipArchiveElement* files = archive.ListFiles(arena, "assets/minecraft/models/block", &count);

  models = memory_arena_push_type_count(arena, ParsedBlockModel, count);

  for (size_t i = 0; i < count; ++i) {
    size_t size = 0;
    char* data = archive.ReadFile(arena, files[i].name, &size);

    assert(data);

    models[i].root_value = json_parse(data, size);
    assert(models[i].root_value->type == json_type_object);

    models[i].root = json_value_as_object(models[i].root_value);
  }

  model_count = count;
  return count;
}

void AssetLoader::Cleanup() {
  for (size_t i = 0; i < model_count; ++i) {
    free(models[i].root_value);
  }
  model_count = 0;

  CloseArchive();
}

} // namespace polymer
