#include <polymer/asset/unihex_font.h>

#include <stdio.h>
#include <stdlib.h>

#include <polymer/util.h>

namespace polymer {
namespace asset {

bool UnihexFont::Load(const char* filename, MemoryArena& perm_arena, MemoryArena& trans_arena) {
  String file_data = ReadEntireFile(filename, trans_arena);
  if (!file_data.data) {
    fprintf(stderr, "Failed to load font file '%s'.\n", filename);
    return false;
  }

  return Load(perm_arena, trans_arena, file_data);
}

bool UnihexFont::Load(MemoryArena& perm_arena, MemoryArena& trans_arena, String file_data) {
  unifont_data = file_data.data;
  unifont_end = file_data.data + file_data.size;

  images = (u8*)trans_arena.Allocate(glyph_page_width * glyph_page_height * glyph_page_count);

  codepoint_str = String(unifont_data, 0);

  // TODO: Refactor font renderer to have indirect lookups so it can handle higher than 0xFFFF codepoints.
  while (unifont_data < unifont_end) {
    char c = *unifont_data++;

    if (state == ReadState::Codepoint) {
      if (!ProcessCodepoint(c)) {
        fprintf(stderr, "UnihexFont: Invalid format while processing codepoint data.\n");
        return false;
      }
    } else {
      if (!ProcessData(c)) {
        fprintf(stderr, "UnihexFont: Invalid format while processing image data.\n");
        return false;
      }
    }
  }

  return true;
}

bool UnihexFont::ProcessCodepoint(char c) {
  if (c == '\n') {
    // Invalid font file.
    return false;
  }

  if (c == ':') {
    codepoint = strtol(codepoint_str.data, nullptr, 16);
    state = ReadState::Data;

    data_str.data = unifont_data;
    data_str.size = 0;
  }

  return true;
}

bool UnihexFont::ProcessData(char c) {
  if (c == ':') {
    // Invalid font file.
    return false;
  }

  if ((c == '\n' || c == 0)) {
    // Skip over handling any high codepoints since the system doesn't currently support them.
    if (codepoint > 0xFFFF) {
      codepoint_str.data = unifont_data;
      state = ReadState::Codepoint;
      return true;
    }

    size_t page_index = codepoint / 256;
    size_t relative_index = codepoint % 256;
    u8* page_start = images + (256 * 256) * page_index;
    u8* glyph_start = page_start + (16 * 16) * relative_index;

    int glyph_start_x = ((int)relative_index % 16) * 16;
    int glyph_start_y = ((int)relative_index / 16) * 16;

    if (data_str.size % 2 != 0) {
      // Invalid font file.
      return false;
    }

    // Height of all glyphs is 16.
    int height = 16;
    // Width is calculated with number of bits used divided by the required height of 16.
    int width = (((int)data_str.size / 2) * 8) / height;

    // Only 8 and 16 are supported here.
    if (width <= 16) {
      // Convert hex string into bitstream.
      int glyph_x = 0;
      int glyph_y = 0;

      int min_glyph_x = 15;
      int max_glyph_x = 0;

      for (int i = 0; i < data_str.size; i += 2) {
        char temp[3] = {};

        temp[0] = data_str.data[i];
        temp[1] = data_str.data[i + 1];
        temp[2] = 0;

        u8 value = (u8)strtol(temp, nullptr, 16);

        for (int j = 0; j < 8; ++j) {
          int absolute_x = glyph_start_x + glyph_x;
          int absolute_y = glyph_start_y + glyph_y;

          bool has_value = (value & (1 << (7 - j)));
          page_start[absolute_y * glyph_page_width + absolute_x] = has_value ? 0xFF : 0;

          if (has_value) {
            if (glyph_x < min_glyph_x) {
              min_glyph_x = glyph_x;
            }

            if (glyph_x > max_glyph_x) {
              max_glyph_x = glyph_x;
            }
          }

          if (++glyph_x >= width) {
            glyph_x = 0;
            ++glyph_y;
          }
        }
      }

      glyph_size_table[codepoint] = (min_glyph_x << 4) | max_glyph_x;
    }

    if (c == 0) {
      return false;
    }

    codepoint_str.data = unifont_data;
    state = ReadState::Codepoint;
  }

  ++data_str.size;
  return true;
}

} // namespace asset
} // namespace polymer
