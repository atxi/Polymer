#ifndef POLYMER_UI_CHAT_WINDOW_H_
#define POLYMER_UI_CHAT_WINDOW_H_

#include <polymer/types.h>

namespace polymer {

struct Connection;

namespace render {

struct FontRenderer;

} // namespace render

namespace ui {

struct ChatMessage {
  char message[1024];
  size_t message_size;
  u64 timestamp;
};

// TODO: Handle all of the different ways of manipulating the input.
struct ChatInput {
  char message[256];
  size_t size;
  bool active;

  ChatInput() {
    message[0] = 0;
    size = 0;
    active = false;
  }

  void Clear() {
    message[0] = 0;
    size = 0;
  }
};

enum class ChatMoveDirection { Left, Right, Home, End };

// TODO: Generalize all of this to input controls.
// TODO: Handle modifiers such as control so entire words can be erased at once.
// TODO: Selection ranges and highlighting.
struct ChatWindow {
  ChatMessage messages[50];
  size_t message_count;

  // The index of the next chat message.
  size_t message_index;

  bool display_full;
  ChatInput input;
  size_t input_cursor_index;

  ChatWindow() {
    message_count = 0;
    message_index = 0;
    display_full = false;
    input_cursor_index = 0;
  }

  void Update(render::FontRenderer& font_renderer);
  void PushMessage(const char* mesg, size_t mesg_size);

  bool ToggleDisplay();

  void OnInput(u32 codepoint);
  void SendInput(Connection& connection);

  void MoveCursor(ChatMoveDirection direction);
  void OnDelete();

private:
  void RenderSlice(render::FontRenderer& font_renderer, size_t start_index, size_t count, bool fade);
  void InsertCodepoint(u32 codepoint);
};

} // namespace ui
} // namespace polymer

#endif
