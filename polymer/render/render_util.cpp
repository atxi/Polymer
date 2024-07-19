#include <polymer/render/render_util.h>

#include <polymer/memory.h>

#include <math.h>
#include <stdio.h>

namespace polymer {
namespace render {

inline float GetColorGamma(int color) {
  return powf((color & 0xFF) / 255.0f, 2.2f);
}

inline float GetColorGamma(int a, int b, int c, int d) {
  float an = a / 255.0f;
  float bn = b / 255.0f;
  float cn = c / 255.0f;
  float dn = d / 255.0f;

  return (powf(an, 2.2f) + powf(bn, 2.2f) + powf(cn, 2.2f) + powf(dn, 2.2f)) / 4.0f;
}

// Blend four samples into a final result after doing gamma conversions
inline int GammaBlend(int a, int b, int c, int d) {
  float result = powf(GetColorGamma(a, b, c, d), 1.0f / 2.2f);

  return static_cast<int>(255.0f * result);
}

inline u32 GetLinearColor(u32 c) {
  int a = (int)(powf(((c >> 24) & 0xFF) / 255.0f, 2.2f) * 255.0f);
  int b = (int)(powf(((c >> 16) & 0xFF) / 255.0f, 2.2f) * 255.0f);
  int g = (int)(powf(((c >> 8) & 0xFF) / 255.0f, 2.2f) * 255.0f);
  int r = (int)(powf(((c >> 0) & 0xFF) / 255.0f, 2.2f) * 255.0f);

  return a << 24 | b << 16 | g << 8 | r << 0;
}

// Perform blend in linear space by multiplying the samples by their alpha and dividing by the accumulated alpha.
inline int AlphaBlend(int c0, int c1, int c2, int c3, int a0, int a1, int a2, int a3, int f, int d, int shift) {
  int t = ((c0 >> shift & 0xFF) * a0 + (c1 >> shift & 0xFF) * a1 + (c2 >> shift & 0xFF) * a2 +
           (c3 >> shift & 0xFF) * a3 + f);
  return (t / d);
}

void BoxFilterMipmap(u8* previous, u8* data, size_t data_size, size_t dim, bool brighten_mipping) {
  size_t size_per_tex = dim * dim * 4;
  size_t count = data_size / size_per_tex;
  size_t prev_dim = dim * 2;

  bool has_transparent = false;

  if (brighten_mipping) {
    for (size_t i = 0; i < data_size; i += 4) {
      if (data[i + 3] == 0) {
        has_transparent = true;
        break;
      }
    }
  }

  unsigned int* pixel = (unsigned int*)data;
  for (size_t i = 0; i < count; ++i) {
    unsigned char* prev_tex = previous + i * (prev_dim * prev_dim * 4);

    Mipmap source(prev_tex, prev_dim);

    for (size_t y = 0; y < dim; ++y) {
      for (size_t x = 0; x < dim; ++x) {
        int red, green, blue, alpha;

        const size_t red_index = 0;
        const size_t green_index = 1;
        const size_t blue_index = 2;
        const size_t alpha_index = 3;

        if (has_transparent) {
          u32 full_samples[4] = {source.SampleFull(x * 2, y * 2), source.SampleFull(x * 2 + 1, y * 2),
                                 source.SampleFull(x * 2, y * 2 + 1), source.SampleFull(x * 2 + 1, y * 2 + 1)};
          // Convert the fetched samples into linear space
          u32 c[4] = {
              GetLinearColor(full_samples[0]),
              GetLinearColor(full_samples[1]),
              GetLinearColor(full_samples[2]),
              GetLinearColor(full_samples[3]),
          };

          int a0 = (c[0] >> 24) & 0xFF;
          int a1 = (c[1] >> 24) & 0xFF;
          int a2 = (c[2] >> 24) & 0xFF;
          int a3 = (c[3] >> 24) & 0xFF;

          int alpha_sum = a0 + a1 + a2 + a3;

          int d;
          if (alpha_sum != 0) {
            d = alpha_sum;
          } else {
            d = 4;
            a3 = a2 = a1 = a0 = 1;
          }

          int f = (d + 1) / 2;

          u32 la = (alpha_sum + 2) / 4;
          u32 lb = AlphaBlend(c[0], c[1], c[2], c[3], a0, a1, a2, a3, f, d, 16);
          u32 lg = AlphaBlend(c[0], c[1], c[2], c[3], a0, a1, a2, a3, f, d, 8);
          u32 lr = AlphaBlend(c[0], c[1], c[2], c[3], a0, a1, a2, a3, f, d, 0);

          // Convert back into gamma space
          alpha = (u32)(powf(la / 255.0f, 1.0f / 2.2f) * 255.0f);
          red = (u32)(powf(lr / 255.0f, 1.0f / 2.2f) * 255.0f);
          green = (u32)(powf(lg / 255.0f, 1.0f / 2.2f) * 255.0f);
          blue = (u32)(powf(lb / 255.0f, 1.0f / 2.2f) * 255.0f);
        } else {
          red = GammaBlend(source.Sample(x * 2, y * 2, red_index), source.Sample(x * 2 + 1, y * 2, red_index),
                           source.Sample(x * 2, y * 2 + 1, red_index), source.Sample(x * 2 + 1, y * 2 + 1, red_index));

          green = GammaBlend(source.Sample(x * 2, y * 2, green_index), source.Sample(x * 2 + 1, y * 2, green_index),
                             source.Sample(x * 2, y * 2 + 1, green_index),
                             source.Sample(x * 2 + 1, y * 2 + 1, green_index));

          blue =
              GammaBlend(source.Sample(x * 2, y * 2, blue_index), source.Sample(x * 2 + 1, y * 2, blue_index),
                         source.Sample(x * 2, y * 2 + 1, blue_index), source.Sample(x * 2 + 1, y * 2 + 1, blue_index));

          alpha = GammaBlend(source.Sample(x * 2, y * 2, alpha_index), source.Sample(x * 2 + 1, y * 2, alpha_index),
                             source.Sample(x * 2, y * 2 + 1, alpha_index),
                             source.Sample(x * 2 + 1, y * 2 + 1, alpha_index));
        }

        // AA BB GG RR
        *pixel = ((alpha & 0xFF) << 24) | ((blue & 0xFF) << 16) | ((green & 0xFF) << 8) | (red & 0xFF);
        ++pixel;
      }
    }
  }
}

VkShaderModule CreateShaderModule(VkDevice device, String code) {
  VkShaderModuleCreateInfo create_info{};

  create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  create_info.codeSize = code.size;
  create_info.pCode = (u32*)code.data;

  VkShaderModule shader;

  if (vkCreateShaderModule(device, &create_info, nullptr, &shader) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create shader module.\n");
  }

  return shader;
}

} // namespace render
} // namespace polymer
