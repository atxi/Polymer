#include <polymer/connection.h>
#include <polymer/gamestate.h>
#include <polymer/memory.h>
#include <polymer/polymer.h>
#include <polymer/types.h>

#include <chrono>

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace polymer {

static Polymer* g_application;
static InputState g_input = {};
static bool g_display_cursor = false;

struct CursorPosition {
  int x = 0;
  int y = 0;
};

CursorPosition g_last_cursor;
bool g_frame_chat_open = false;

inline void ToggleCursor() {
  GLFWwindow* window = (GLFWwindow*)g_application->window;
  g_display_cursor = !g_display_cursor;

  int mode = g_display_cursor ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED;
  glfwSetInputMode(window, GLFW_CURSOR, mode);
}

static void OnWindowResize(GLFWwindow* window, int width, int height) {
  g_application->renderer.invalid_swapchain = true;
}

static void OnCursorPosition(GLFWwindow* window, double xpos, double ypos) {
  if (!g_display_cursor) {
    int dx = xpos - g_last_cursor.x;
    int dy = ypos - g_last_cursor.y;

    g_application->game->OnWindowMouseMove(dx, dy);

    g_last_cursor.x = xpos;
    g_last_cursor.y = ypos;
  }
}

void OnCharacterPress(GLFWwindow* window, unsigned int codepoint) {
  if (g_frame_chat_open) {
    g_frame_chat_open = false;
    return;
  }

  g_frame_chat_open = false;

  if (g_application->game->chat_window.display_full) {
    g_application->game->chat_window.OnInput(codepoint);
  }
}

static void OnKeyDown(GLFWwindow* window, int key, int scancode, int action, int mods) {
  GameState* game = g_application->game;

  switch (key) {
  case GLFW_KEY_ESCAPE: {
    if (action == GLFW_PRESS) {
      ToggleCursor();
      game->chat_window.ToggleDisplay();
      g_frame_chat_open = true;
      memset(&g_input, 0, sizeof(g_input));
    }
  } break;
  }

  if (!game->chat_window.display_full) {
    switch (key) {
    case GLFW_KEY_SLASH:
    case GLFW_KEY_T: {
      if (action == GLFW_PRESS && !game->chat_window.display_full) {
        ToggleCursor();
        game->chat_window.ToggleDisplay();
        g_frame_chat_open = true;
        memset(&g_input, 0, sizeof(g_input));

        if (key == GLFW_KEY_SLASH) {
          game->chat_window.input.active = true;
          game->chat_window.OnInput('/');
        }
      }
    } break;
    case GLFW_KEY_W: {
      g_input.forward = action != GLFW_RELEASE;
    } break;
    case GLFW_KEY_S: {
      g_input.backward = action != GLFW_RELEASE;
    } break;
    case GLFW_KEY_A: {
      g_input.left = action != GLFW_RELEASE;
    } break;
    case GLFW_KEY_D: {
      g_input.right = action != GLFW_RELEASE;
    } break;
    case GLFW_KEY_SPACE: {
      g_input.climb = action != GLFW_RELEASE;
    } break;
    case GLFW_KEY_LEFT_SHIFT:
    case GLFW_KEY_RIGHT_SHIFT: {
      g_input.fall = action != GLFW_RELEASE;
    } break;
    case GLFW_KEY_LEFT_CONTROL:
    case GLFW_KEY_RIGHT_CONTROL: {
      g_input.sprint = action != GLFW_RELEASE;
    } break;
    case GLFW_KEY_TAB: {
      g_input.display_players = action != GLFW_RELEASE;
    } break;
    }
  } else if (action != GLFW_RELEASE) {
    switch (key) {
    case GLFW_KEY_ENTER: {
      fflush(stdout);
      ToggleCursor();
      game->chat_window.SendInput(game->connection);
      game->chat_window.ToggleDisplay();
    } break;
    case GLFW_KEY_LEFT: {
      game->chat_window.MoveCursor(ui::ChatMoveDirection::Left);
    } break;
    case GLFW_KEY_RIGHT: {
      game->chat_window.MoveCursor(ui::ChatMoveDirection::Right);
    } break;
    case GLFW_KEY_HOME: {
      game->chat_window.MoveCursor(ui::ChatMoveDirection::Home);
    } break;
    case GLFW_KEY_END: {
      game->chat_window.MoveCursor(ui::ChatMoveDirection::End);
    } break;
    case GLFW_KEY_DELETE: {
      game->chat_window.OnDelete();
    } break;
    case GLFW_KEY_BACKSPACE: {
      game->chat_window.OnInput(0x08);
    } break;
    }
  }
}

static const char* UnixGetPlatformName() {
  return "Linux";
}

static PolymerWindow UnixWindowCreate(int width, int height) {
  GLFWwindow* window = glfwCreateWindow(width, height, "Polymer", NULL, NULL);
  if (!window) {
    fprintf(stderr, "Failed to create glfw window\n");
    glfwTerminate();
    return nullptr;
  }

  glfwSetWindowSizeCallback(window, OnWindowResize);
  glfwSetKeyCallback(window, OnKeyDown);
  glfwSetCursorPosCallback(window, OnCursorPosition);
  glfwSetCharCallback(window, OnCharacterPress);

  if (glfwRawMouseMotionSupported()) {
    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
  }

  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  return (PolymerWindow)window;
}

static bool UnixWindowCreateSurface(PolymerWindow window, VkSurfaceKHR* surface) {
  VkResult result = glfwCreateWindowSurface(g_application->renderer.instance, (GLFWwindow*)window, nullptr, surface);

  return result == VK_SUCCESS;
}

static IntRect UnixWindowGetRect(PolymerWindow window) {
  IntRect rect = {};

  glfwGetWindowSize((GLFWwindow*)window, &rect.right, &rect.bottom);

  return rect;
}

static void UnixWindowPump(PolymerWindow window) {
  glfwPollEvents();

  if (glfwWindowShouldClose((GLFWwindow*)window)) {
    g_application->game->connection.Disconnect();
  }
}

static const char* kDeviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
// static const char* kValidationLayers[] = {"VK_LAYER_KHRONOS_validation"};
static const char* kValidationLayers[] = {};

static ExtensionRequest UnixGetExtensionRequest() {
  uint32_t count;
  const char** extensions = glfwGetRequiredInstanceExtensions(&count);

  ExtensionRequest extension_request;

  extension_request.extensions = extensions;
  extension_request.extension_count = count;

  extension_request.device_extension_count = polymer_array_count(kDeviceExtensions);
  extension_request.device_extensions = kDeviceExtensions;

  extension_request.validation_layer_count = polymer_array_count(kValidationLayers);
  extension_request.validation_layers = kValidationLayers;

  return extension_request;
}

} // namespace polymer

int main(int argc, char* argv[]) {
  using namespace polymer;

  constexpr size_t kPermanentSize = Gigabytes(1);
  constexpr size_t kTransientSize = Megabytes(32);

  u8* perm_memory = (u8*)malloc(kPermanentSize);
  u8* trans_memory = (u8*)malloc(kTransientSize);

  MemoryArena perm_arena(perm_memory, kPermanentSize);
  MemoryArena trans_arena(trans_memory, kTransientSize);

  Polymer* polymer = perm_arena.Construct<Polymer>(perm_arena, trans_arena, argc, argv);

  polymer->platform = {UnixGetPlatformName, UnixWindowCreate, UnixWindowCreateSurface,
                       UnixWindowGetRect,   UnixWindowPump,   UnixGetExtensionRequest};

  g_application = polymer;

  if (!glfwInit()) {
    fprintf(stderr, "Failed to initialize glfw.\n");
    return 1;
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  return polymer->Run(&g_input);
}
