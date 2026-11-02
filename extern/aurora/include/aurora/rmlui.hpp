#pragma once

#ifdef AURORA_ENABLE_RMLUI

#include <aurora/input.hpp>

#include <RmlUi/Core/Context.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace aurora::rmlui {

enum class InputType {
  Text,
  Number,
};

inline constexpr const char* TouchStartEvent = "touchstart";
inline constexpr const char* TouchMoveEvent = "touchmove";
inline constexpr const char* TouchEndEvent = "touchend";
inline constexpr const char* TouchCancelEvent = "touchcancel";

Rml::Context* get_context() noexcept;
bool is_initialized() noexcept;
void set_input_type(InputType type) noexcept;
void set_ui_scale(float scale) noexcept;
float get_ui_scale() noexcept;
void set_glass_light_dir(float x, float y) noexcept;

struct InputResult {
  bool handled = false;           // An element stopped propagation
  Rml::Element* target = nullptr; // The element hit, if any
};
InputResult process_input(const input::InputEvent& event) noexcept;

struct RuntimeTexture {
  uint32_t width = 0;
  uint32_t height = 0;
  std::span<const std::byte> rgba8;
  bool premultipliedAlpha = false;
  bool generateMipmaps = false;
};

using TextureProvider = std::function<std::optional<RuntimeTexture>(std::string_view)>;

void register_texture_provider(std::string scheme, TextureProvider provider);
void unregister_texture_provider(std::string_view scheme) noexcept;

/* desktop-melee: a second, normal window with its own RmlUi context, drawn and
 * presented with the game's frames (lib/rmlui_window.cpp). Its input goes to
 * its context, never to the game. Main thread only. */
using WindowCallback = void (*)(void* userdata);
/* Opens the window, or raises it when already open. Returns its context
 * (named name), or nullptr on failure. */
Rml::Context* open_window(const char* name, const char* title, int width, int height) noexcept;
/* Closes it; documents loaded into its context are destroyed with it. */
void close_window() noexcept;
/* The open window's context, or nullptr. */
Rml::Context* get_window_context() noexcept;
/* Called after the player closes the window (its close button). */
void set_window_closed_callback(WindowCallback callback, void* userdata) noexcept;
/* Called once a frame while it is open, before its context updates. */
void set_window_frame_callback(WindowCallback callback, void* userdata) noexcept;

} // namespace aurora::rmlui

#endif
