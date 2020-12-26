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

void FaceTextureMap::Insert(const char* name, const char* value) {
  u32 bucket = Hash(name) & (kTextureMapBuckets - 1);

  FaceTextureElement* element = elements[bucket];
  while (element) {
    if (strcmp(element->name, name) == 0) {
      break;
    }
    element = element->next;
  }

  if (element == nullptr) {
    element = Allocate();

    assert(strlen(name) < polymer_array_count(element->name));
    assert(strlen(value) < polymer_array_count(element->value));

    strcpy(element->name, name);
    strcpy(element->value, value);

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

bool AssetLoader::OpenArchive(const char* filename) {
  snapshot = arena->GetSnapshot();

  return archive.Open(filename);
}

void AssetLoader::CloseArchive() {
  archive.Close();
  arena->Revert(snapshot);
}

size_t AssetLoader::ParseBlockModels() {
  

  return 0;
}

} // namespace polymer
