#pragma once

#include "aurora/aurora.h"
#include "webgpu/gpu.hpp"

#include <SDL3/SDL_events.h>
#include <aurora/rmlui.hpp>
#include <dawn/webgpu_cpp.h>

#include <memory>

namespace aurora::rmlui {

struct RecordedFrame {
  wgpu::BindGroup bindGroup;
  bool overlay = false;
};

void initialize(const AuroraWindowSize& size) noexcept;
void handle_window_event(const SDL_Event& event) noexcept;
RecordedFrame record_frame(const webgpu::Viewport& presentViewport) noexcept;
void shutdown() noexcept;

/* desktop-melee: the second window (lib/rmlui_window.cpp). */
struct WindowPresentState;
struct WindowFrame {
  std::shared_ptr<WindowPresentState> state; // null when no window is open
  wgpu::BindGroup bindGroup;                  // its UI this frame, or null
};
/* Main thread: whether the event belonged to the second window (handled). */
bool window_handle_event(SDL_Event& event) noexcept;
/* Main thread, with the game's frame: updates and records its UI. */
WindowFrame window_record_frame() noexcept;
/* Render thread, inside the frame's command encoder: draws it. */
void window_encode(const WindowFrame& frame, const wgpu::CommandEncoder& encoder) noexcept;
/* Render thread, after the queue submit: presents it. */
void window_present(const WindowFrame& frame) noexcept;
void window_shutdown() noexcept;

} // namespace aurora::rmlui
