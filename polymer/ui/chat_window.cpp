#include <polymer/ui/chat_window.h>

#include <polymer/connection.h>
#include <polymer/math.h>
#include <polymer/render/font_renderer.h>

#include <chrono>

namespace polymer {
namespace ui {

const size_t kChatMessageQueueSize = polymer_array_count(ChatWindow::messages);
const u64 kSecondNanoseconds = 1000LL * 1000LL * 1000LL;
const u64 kDisplayNanoseconds = kSecondNanoseconds * 10LL;
const int kLineHeight = 18;

inline static u64 GetNow() {
  return std::chrono::high_resolution_clock::now().time_since_epoch().count();
}

void ChatWindow::RenderSlice(render::FontRenderer& font_renderer, size_t start_index, size_t count, bool fade) {
  const int kEmptyLinesBelow = 4;
  u64 now = GetNow();

  for (size_t i = 0; i < count && i < message_count; ++i) {
    size_t index = (start_index - i - 1 + kChatMessageQueueSize) % kChatMessageQueueSize;

    ChatMessage* chat_message = messages + index;

    float y = (float)(font_renderer.renderer->GetExtent().height - 8 - (i + kEmptyLinesBelow) * kLineHeight);
    Vector3f position(8, y, 0);

    render::FontStyleFlags style = render::FontStyle_DropShadow;

    float alpha = 1.0f;

    if (fade) {
      u64 delta_ns = now - chat_message->timestamp;

      // Stop rendering anything outside of display time range.
      if (delta_ns > kDisplayNanoseconds) break;

      if (kDisplayNanoseconds - delta_ns < kSecondNanoseconds) {
        alpha = (kDisplayNanoseconds - delta_ns) / (float)kSecondNanoseconds;
      }
    }

    Vector4f color(1, 1, 1, alpha);
    Vector4f bg_color(0, 0, 0, 0.4f * alpha);

    float background_width = 660;
    if (position.x + background_width > font_renderer.renderer->GetExtent().width) {
      background_width = (float)font_renderer.renderer->GetExtent().width - 8;
    }

    font_renderer.RenderBackground(position + Vector3f(-4, 0, 0), Vector2f(background_width, kLineHeight), bg_color);
    font_renderer.RenderText(position, String(chat_message->message, chat_message->message_size), style, color);
  }
}

void ChatWindow::Update(render::FontRenderer& font_renderer) {
  if (display_full) {
    RenderSlice(font_renderer, message_index, 20, false);

    float bottom = (float)font_renderer.renderer->GetExtent().height - 22.0f;
    float background_width = (float)font_renderer.renderer->GetExtent().width - 8;

    render::FontStyleFlags style = render::FontStyle_DropShadow;
    Vector4f color(1, 1, 1, 1);
    Vector4f bg_color(0, 0, 0, 0.4f);

    font_renderer.RenderBackground(Vector3f(4, bottom, 0), Vector2f(background_width, kLineHeight), bg_color);
    font_renderer.RenderText(Vector3f(8, bottom, 0), String(input.message, input.size), style, color);

    u64 now_ms = GetNow() / (1000 * 1000);

    if (now_ms % 500 < 250) {
      float text_width = (float)font_renderer.GetTextWidth(String(input.message, input_cursor_index));
      float left_spacing = 12;

      if (input_cursor_index >= input.size) {
        if (input_cursor_index == 0) {
          left_spacing = 8;
        }
        font_renderer.RenderText(Vector3f(left_spacing + text_width, bottom, 0), POLY_STR("_"), style, color);
      } else {
        font_renderer.RenderText(Vector3f(left_spacing - 4 + text_width, bottom, 0), POLY_STR("|"), style, color);
      }
    }

    input.active = true;
  } else {
    RenderSlice(font_renderer, message_index, 10, true);
  }
}

void ChatWindow::OnDelete() {
  if (input_cursor_index >= input.size || input.size == 0) return;

  memmove(input.message + input_cursor_index, input.message + input_cursor_index + 1, input.size - input_cursor_index);

  --input.size;
}

void ChatWindow::SendInput(Connection& connection) {
  if (input.size <= 0) return;

  if (input.message[0] == '/') {
    if (input.size > 1) {
      connection.SendChatCommand(String(input.message + 1, input.size - 1));
    }
  } else {
    connection.SendChatMessage(String(input.message, input.size));
  }

  input_cursor_index = 0;
  input.Clear();
}

void ChatWindow::OnInput(u32 codepoint) {
  // TODO: Handle more than ascii
  // TODO: Remove magic numbers

  if (!input.active) return;

  if (input_cursor_index > 0 && codepoint == 0x08) {
    // Backspace
    memmove(input.message + input_cursor_index - 1, input.message + input_cursor_index,
            input.size - input_cursor_index);
    --input.size;
    --input_cursor_index;
    input.message[input.size] = 0;
  }

  if (codepoint < 0x20 || codepoint > 0x7E) return;

  if (input.size < polymer_array_count(input.message)) {
    InsertCodepoint(codepoint);
  }
}

void ChatWindow::MoveCursor(ChatMoveDirection direction) {
  if (direction == ChatMoveDirection::Left) {
    if (input_cursor_index > 0) {
      --input_cursor_index;
    }
  } else if (direction == ChatMoveDirection::Right) {
    if (input_cursor_index < input.size) {
      ++input_cursor_index;
    }
  } else if (direction == ChatMoveDirection::Home) {
    input_cursor_index = 0;
  } else if (direction == ChatMoveDirection::End) {
    input_cursor_index = input.size;
  }
}

void ChatWindow::InsertCodepoint(u32 codepoint) {
  if (input.size >= polymer_array_count(input.message)) return;

  if (input_cursor_index < input.size) {
    memmove(input.message + input_cursor_index + 1, input.message + input_cursor_index,
            input.size - input_cursor_index);
  }

  input.message[input_cursor_index] = (char)codepoint;

  ++input_cursor_index;
  ++input.size;
}

bool ChatWindow::ToggleDisplay() {
  display_full = !display_full;

  if (display_full) {
    input.Clear();
    input_cursor_index = 0;
  } else {
    input.active = false;
  }

  return display_full;
}

void ChatWindow::PushMessage(const char* mesg, size_t mesg_size) {
  ChatMessage* chat_message = nullptr;

  if (message_count < polymer_array_count(messages)) {
    chat_message = messages + message_count++;
  } else {
    chat_message = messages + message_index;
  }

  message_index = (message_index + 1) % polymer_array_count(messages);

  if (mesg_size > polymer_array_count(chat_message->message)) {
    mesg_size = polymer_array_count(chat_message->message);
  }

  memcpy(chat_message->message, mesg, mesg_size);
  chat_message->message_size = mesg_size;
  chat_message->timestamp = GetNow();
}

} // namespace ui
} // namespace polymer
