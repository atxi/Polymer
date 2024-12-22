#include "util.h"

#include <polymer/memory.h>
#include <polymer/platform/platform.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace polymer {

FILE* CreateAndOpenFileImpl(char* filename, const char* mode) {
  char* current = filename;

  while (*current++) {
    char c = *current;
    if (c == '/' || c == '\\') {
      // Null terminate up to the current folder so it gets checked as existing.
      char prev = *(current + 1);
      *(current + 1) = 0;

      if (!g_Platform.FolderExists(filename)) {
        if (!g_Platform.CreateFolder(filename)) {
          fprintf(stderr, "Failed to create folder '%s'.\n", filename);
          *(current + 1) = prev;
          return nullptr;
        }
      }

      // Revert to previous value so it can keep processing.
      *(current + 1) = prev;
    }
  }

  return fopen(filename, mode);
}

FILE* CreateAndOpenFile(String filename, const char* mode) {
  char* mirror = (char*)malloc(filename.size + 1);

  if (!mirror) return nullptr;

  memcpy(mirror, filename.data, filename.size);
  mirror[filename.size] = 0;

  FILE* f = CreateAndOpenFileImpl(mirror, mode);

  free(mirror);

  return f;
}

FILE* CreateAndOpenFile(const char* filename, const char* mode) {
  size_t name_len = strlen(filename);
  char* mirror = (char*)malloc(name_len + 1);

  if (!mirror) return nullptr;

  memcpy(mirror, filename, name_len);
  mirror[name_len] = 0;

  FILE* f = CreateAndOpenFileImpl(mirror, mode);

  free(mirror);

  return f;
}

String ReadEntireFile(const char* filename, MemoryArena& arena) {
  String result = {};
  FILE* f = fopen(filename, "rb");

  if (!f) {
    return result;
  }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  char* buffer = memory_arena_push_type_count(&arena, char, size);

  long total_read = 0;
  while (total_read < size) {
    total_read += (long)fread(buffer + total_read, 1, size - total_read, f);
  }

  fclose(f);

  result.data = buffer;
  result.size = size;

  return result;
}

struct Sha1 {
  static constexpr size_t kDigestSize = 20;

  struct Context {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t buffer[64];
  };

  typedef uint8_t Digest[kDigestSize];

  static void Init(Context* context);
  static void Update(Context* context, const uint8_t* data, const size_t len);
  static void Final(Context* context, uint8_t digest[kDigestSize]);
};

/*
SHA-1
Based on the public domain implementation by Steve Reid <sreid@sea-to-sky.net>.
*/

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

static void SHA1_Transform(uint32_t state[5], const uint8_t buffer[64]);

#define blk0(i)                                                                                                        \
  (block.l[i] =                                                                                                        \
       ((block.c[i * 4] << 24) | (block.c[i * 4 + 1] << 16) | (block.c[i * 4 + 2] << 8) | (block.c[i * 4 + 3])))

#define blk(i)                                                                                                         \
  (block.l[i & 15] = rol(block.l[(i + 13) & 15] ^ block.l[(i + 8) & 15] ^ block.l[(i + 2) & 15] ^ block.l[i & 15], 1))

/* (R0+R1), R2, R3, R4 are the different operations used in SHA1 */
#define R0(v, w, x, y, z, i)                                                                                           \
  z += ((w & (x ^ y)) ^ y) + blk0(i) + 0x5A827999 + rol(v, 5);                                                         \
  w = rol(w, 30);
#define R1(v, w, x, y, z, i)                                                                                           \
  z += ((w & (x ^ y)) ^ y) + blk(i) + 0x5A827999 + rol(v, 5);                                                          \
  w = rol(w, 30);
#define R2(v, w, x, y, z, i)                                                                                           \
  z += (w ^ x ^ y) + blk(i) + 0x6ED9EBA1 + rol(v, 5);                                                                  \
  w = rol(w, 30);
#define R3(v, w, x, y, z, i)                                                                                           \
  z += (((w | x) & y) | (w & x)) + blk(i) + 0x8F1BBCDC + rol(v, 5);                                                    \
  w = rol(w, 30);
#define R4(v, w, x, y, z, i)                                                                                           \
  z += (w ^ x ^ y) + blk(i) + 0xCA62C1D6 + rol(v, 5);                                                                  \
  w = rol(w, 30);

/* Hash a single 512-bit block. This is the core of the algorithm. */
static void SHA1_Transform(uint32_t state[5], const uint8_t buffer[64]) {
  uint32_t a, b, c, d, e;

  typedef union {
    uint8_t c[64];
    uint32_t l[16];

  } CHAR64LONG16;
  CHAR64LONG16 block;

  memcpy(&block, buffer, 64);

  /* Copy context->state[] to working vars */
  a = state[0];
  b = state[1];
  c = state[2];
  d = state[3];
  e = state[4];

  /* 4 rounds of 20 operations each. Loop unrolled. */
  R0(a, b, c, d, e, 0);
  R0(e, a, b, c, d, 1);
  R0(d, e, a, b, c, 2);
  R0(c, d, e, a, b, 3);
  R0(b, c, d, e, a, 4);
  R0(a, b, c, d, e, 5);
  R0(e, a, b, c, d, 6);
  R0(d, e, a, b, c, 7);
  R0(c, d, e, a, b, 8);
  R0(b, c, d, e, a, 9);
  R0(a, b, c, d, e, 10);
  R0(e, a, b, c, d, 11);
  R0(d, e, a, b, c, 12);
  R0(c, d, e, a, b, 13);
  R0(b, c, d, e, a, 14);
  R0(a, b, c, d, e, 15);
  R1(e, a, b, c, d, 16);
  R1(d, e, a, b, c, 17);
  R1(c, d, e, a, b, 18);
  R1(b, c, d, e, a, 19);
  R2(a, b, c, d, e, 20);
  R2(e, a, b, c, d, 21);
  R2(d, e, a, b, c, 22);
  R2(c, d, e, a, b, 23);
  R2(b, c, d, e, a, 24);
  R2(a, b, c, d, e, 25);
  R2(e, a, b, c, d, 26);
  R2(d, e, a, b, c, 27);
  R2(c, d, e, a, b, 28);
  R2(b, c, d, e, a, 29);
  R2(a, b, c, d, e, 30);
  R2(e, a, b, c, d, 31);
  R2(d, e, a, b, c, 32);
  R2(c, d, e, a, b, 33);
  R2(b, c, d, e, a, 34);
  R2(a, b, c, d, e, 35);
  R2(e, a, b, c, d, 36);
  R2(d, e, a, b, c, 37);
  R2(c, d, e, a, b, 38);
  R2(b, c, d, e, a, 39);
  R3(a, b, c, d, e, 40);
  R3(e, a, b, c, d, 41);
  R3(d, e, a, b, c, 42);
  R3(c, d, e, a, b, 43);
  R3(b, c, d, e, a, 44);
  R3(a, b, c, d, e, 45);
  R3(e, a, b, c, d, 46);
  R3(d, e, a, b, c, 47);
  R3(c, d, e, a, b, 48);
  R3(b, c, d, e, a, 49);
  R3(a, b, c, d, e, 50);
  R3(e, a, b, c, d, 51);
  R3(d, e, a, b, c, 52);
  R3(c, d, e, a, b, 53);
  R3(b, c, d, e, a, 54);
  R3(a, b, c, d, e, 55);
  R3(e, a, b, c, d, 56);
  R3(d, e, a, b, c, 57);
  R3(c, d, e, a, b, 58);
  R3(b, c, d, e, a, 59);
  R4(a, b, c, d, e, 60);
  R4(e, a, b, c, d, 61);
  R4(d, e, a, b, c, 62);
  R4(c, d, e, a, b, 63);
  R4(b, c, d, e, a, 64);
  R4(a, b, c, d, e, 65);
  R4(e, a, b, c, d, 66);
  R4(d, e, a, b, c, 67);
  R4(c, d, e, a, b, 68);
  R4(b, c, d, e, a, 69);
  R4(a, b, c, d, e, 70);
  R4(e, a, b, c, d, 71);
  R4(d, e, a, b, c, 72);
  R4(c, d, e, a, b, 73);
  R4(b, c, d, e, a, 74);
  R4(a, b, c, d, e, 75);
  R4(e, a, b, c, d, 76);
  R4(d, e, a, b, c, 77);
  R4(c, d, e, a, b, 78);
  R4(b, c, d, e, a, 79);

  /* Add the working vars back into context.state[] */
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  /* Wipe variables */
  a = b = c = d = e = 0;
}

/* SHA1Init - Initialize new context */
void Sha1::Init(Context* context) {
  /* SHA1 initialization constants */
  context->state[0] = 0x67452301;
  context->state[1] = 0xEFCDAB89;
  context->state[2] = 0x98BADCFE;
  context->state[3] = 0x10325476;
  context->state[4] = 0xC3D2E1F0;
  context->count[0] = context->count[1] = 0;
}

/* Run your data through this. */
void Sha1::Update(Context* context, const uint8_t* data, const size_t len_) {
  unsigned int i, j;

  uint32_t len = (uint32_t)len_;
  j = (context->count[0] >> 3) & 63;
  if ((context->count[0] += len << 3) < (len << 3)) context->count[1]++;
  context->count[1] += (len >> 29);
  if ((j + len) > 63) {
    memcpy(&context->buffer[j], data, (i = 64 - j));
    SHA1_Transform(context->state, context->buffer);
    for (; i + 63 < len; i += 64) {
      SHA1_Transform(context->state, &data[i]);
    }
    j = 0;
  } else
    i = 0;
  memcpy(&context->buffer[j], &data[i], len - i);
}

/* Add padding and return the message digest. */
void Sha1::Final(Context* context, uint8_t digest[kDigestSize]) {
  uint32_t i;
  uint8_t finalcount[8];

  for (i = 0; i < 8; i++) {
    finalcount[i] = (uint8_t)((context->count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) & 255); /* Endian independent */
  }
  Sha1::Update(context, (uint8_t*)"\200", 1);
  while ((context->count[0] & 504) != 448) {
    Sha1::Update(context, (uint8_t*)"\0", 1);
  }
  /* Should cause a SHA1_Transform() */
  Sha1::Update(context, finalcount, 8);
  for (i = 0; i < kDigestSize; i++) {
    digest[i] = (uint8_t)((context->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
  }
  /* Wipe variables */
  i = 0;
  memset(context->buffer, 0, 64);
  memset(context->state, 0, kDigestSize);
  memset(context->count, 0, 8);
  memset(&finalcount, 0, 8);
}

HashSha1 Sha1(const String& contents) {
  HashSha1 result = {};
  Sha1::Context ctx;

  Sha1::Init(&ctx);
  Sha1::Update(&ctx, (uint8_t*)contents.data, contents.size);
  Sha1::Final(&ctx, result.hash);

  return result;
}

} // namespace polymer
