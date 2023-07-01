#ifndef POLYMER_ASSET_UNIHEX_FONT_H_
#define POLYMER_ASSET_UNIHEX_FONT_H_

#include <polymer/memory.h>
#include <polymer/types.h>

namespace polymer {
namespace asset {

struct UnihexFont {
  enum class ReadState { Codepoint, Data };

  char* unifont_data = nullptr;
  char* unifont_end = nullptr;
  u8* images = nullptr;

  u8* glyph_size_table;
  size_t glyph_page_width;
  size_t glyph_page_height;
  size_t glyph_page_count;

  String codepoint_str;
  String data_str;
  u32 codepoint = 0;
  ReadState state = ReadState::Codepoint;

  UnihexFont(u8* glyph_size_table, size_t glyph_page_width, size_t glyph_page_height, size_t glyph_page_count)
      : glyph_size_table(glyph_size_table), glyph_page_width(glyph_page_width), glyph_page_height(glyph_page_height),
        glyph_page_count(glyph_page_count) {}

  bool Load(const char* filename, MemoryArena& perm_arena, MemoryArena& trans_arena);

private:
  bool ProcessCodepoint(char c);

  bool ProcessData(char c);
};

} // namespace asset
} // namespace polymer

#endif
