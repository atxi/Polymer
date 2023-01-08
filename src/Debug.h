#ifndef POLYMER_DEBUG_H_
#define POLYMER_DEBUG_H_

#include "math.h"
#include "render/font_renderer.h"

#include <stdarg.h>
#include <stdio.h>

namespace polymer {

enum class DebugTextAlignment { Left, Right, Center };

struct DebugTextSystem {
  render::FontRenderer& font_renderer;
  Vector2f position;
  Vector4f color;
  DebugTextAlignment alignment;

  DebugTextSystem(render::FontRenderer& renderer) : font_renderer(renderer) {}

  void Write(const char* fmt, ...) {
    char buffer[2048];

    va_list args;

    va_start(args, fmt);
    size_t size = vsprintf_s(buffer, fmt, args);
    va_end(args);

    render::FontStyleFlags style = render::FontStyle_Background | render::FontStyle_DropShadow;

    font_renderer.RenderText(Vector3f(position.x, position.y, 0), String(buffer, size), style, color);
    position.y += 16;
  }
};

} // namespace polymer

#endif
