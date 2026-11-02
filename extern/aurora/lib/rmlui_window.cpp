/* desktop-melee: a second, normal window with its own RmlUi context.
 *
 * The game's RmlUi context draws into the game's window. This one has its
 * own SDL window, WebGPU surface, render interface and context:
 *  - main thread: open/close, its SDL events (window.cpp hands over every
 *    event for a window that isn't the game's), and recording its UI with the
 *    game's frame (aurora.cpp's end_frame) into its own render target;
 *  - render thread: configuring the surface, drawing that target onto it
 *    inside the frame's command encoder, presenting it after the submit, and
 *    releasing it once the window closes.
 * Only the render thread touches the surface; the main thread asks for a
 * size or a close through WindowPresentState, and destroys the SDL window
 * only after the render thread has released the surface. */
#include "rmlui.hpp"

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>

#include <RmlUi/Core.h>
#include <RmlUi_Platform_SDL.h>
#include <SDL3/SDL.h>

#include <aurora/webgpu.hpp>
#include "internal.hpp"
#include "rmlui/WebGPURenderInterface.hpp"
#include "webgpu/gpu.hpp"

namespace aurora::rmlui {

struct WindowPresentState {
  std::mutex mutex;
  wgpu::Surface surface;
  uint32_t width = 0; // wanted size in pixels (main thread writes)
  uint32_t height = 0;
  bool closing = false;  // main thread: release the surface
  bool released = false; // render thread: released; the window may go
  // Render thread only:
  wgpu::SurfaceConfiguration config{};
  bool configured = false;
  wgpu::Texture texture; // acquired in window_encode, presented in window_present
};

namespace {
Module Log("aurora::rmlui::window");

SDL_Window* s_window = nullptr;
Rml::Context* s_context = nullptr;
std::string s_name;
std::shared_ptr<WindowPresentState> s_state;
/* A closed window, hidden, waiting for the render thread to release its
 * surface before SDL_DestroyWindow. */
SDL_Window* s_closingWindow = nullptr;
std::shared_ptr<WindowPresentState> s_closingState;
/* Created once and kept for the program's life: RmlUi holds on to a render
 * interface's resources until Rml::Shutdown. */
WebGPURenderInterface* s_renderInterface = nullptr;
webgpu::TextureWithSampler s_target;
wgpu::BindGroup s_targetBindGroup;
WindowCallback s_closedCallback = nullptr;
void* s_closedUserdata = nullptr;
WindowCallback s_frameCallback = nullptr;
void* s_frameUserdata = nullptr;

Rml::Vector2i pixel_size() noexcept {
  int width = 0;
  int height = 0;
  SDL_GetWindowSizeInPixels(s_window, &width, &height);
  return {std::max(1, width), std::max(1, height)};
}

void request_size(Rml::Vector2i size) noexcept {
  std::lock_guard lock{s_state->mutex};
  s_state->width = static_cast<uint32_t>(size.x);
  s_state->height = static_cast<uint32_t>(size.y);
}

void sync_context(Rml::Vector2i size) noexcept {
  if (s_context->GetDimensions() != size) {
    s_context->SetDimensions(size);
  }
  const float scale = SDL_GetWindowDisplayScale(s_window);
  if (scale > 0.f && s_context->GetDensityIndependentPixelRatio() != scale) {
    s_context->SetDensityIndependentPixelRatio(scale);
  }
}

void ensure_target(Rml::Vector2i size) noexcept {
  const auto width = static_cast<uint32_t>(size.x);
  const auto height = static_cast<uint32_t>(size.y);
  if (s_target.view && s_target.size.width == width && s_target.size.height == height) {
    return;
  }
  s_target = webgpu::create_render_texture(width, height, false);
  s_targetBindGroup = webgpu::create_copy_bind_group(s_target);
}

/* Destroys the closed window once the render thread let go of it. */
void reap_closing() noexcept {
  if (s_closingState == nullptr) {
    return;
  }
  {
    std::lock_guard lock{s_closingState->mutex};
    if (!s_closingState->released) {
      return;
    }
  }
  SDL_DestroyWindow(s_closingWindow);
  s_closingWindow = nullptr;
  s_closingState.reset();
}

void begin_close() noexcept {
  if (s_context != nullptr) {
    Rml::RemoveContext(s_name);
    s_context = nullptr;
  }
  if (s_window == nullptr) {
    return;
  }
  SDL_HideWindow(s_window);
  {
    std::lock_guard lock{s_state->mutex};
    s_state->closing = true;
  }
  /* One window at a time: an older one still closing is destroyed first. */
  if (s_closingState != nullptr) {
    SDL_DestroyWindow(s_closingWindow);
  }
  s_closingWindow = s_window;
  s_closingState = std::move(s_state);
  s_window = nullptr;
  s_target = {};
  s_targetBindGroup = {};
}

/* Render thread, under the state's lock. */
void release_surface(WindowPresentState& state) noexcept {
  state.texture = {};
  if (state.configured) {
    state.surface.Unconfigure();
    state.configured = false;
  }
  state.surface = {};
  state.released = true;
}
} // namespace

Rml::Context* open_window(const char* name, const char* title, int width, int height) noexcept {
  if (s_window != nullptr) {
    SDL_RaiseWindow(s_window);
    return s_context;
  }
  if (!is_initialized() || !webgpu::g_device) {
    Log.error("open_window: RmlUi or the GPU isn't ready");
    return nullptr;
  }
  reap_closing();
  s_window = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (s_window == nullptr) {
    Log.error("open_window: {}", SDL_GetError());
    return nullptr;
  }
  s_state = std::make_shared<WindowPresentState>();
  s_state->surface = webgpu::create_window_surface(webgpu::g_instance, s_window, "RmlUi window surface");
  if (!s_state->surface) {
    Log.error("open_window: no surface for the window");
    SDL_DestroyWindow(s_window);
    s_window = nullptr;
    s_state.reset();
    return nullptr;
  }
  if (s_renderInterface == nullptr) {
    s_renderInterface = new WebGPURenderInterface();
    s_renderInterface->SetRenderTargetFormat(webgpu::g_graphicsConfig.surfaceConfiguration.format);
    s_renderInterface->CreateDeviceObjects();
  }
  const Rml::Vector2i size = pixel_size();
  request_size(size);
  s_name = name;
  s_context = Rml::CreateContext(s_name, size, s_renderInterface);
  if (s_context == nullptr) {
    Log.error("open_window: no RmlUi context named {}", s_name);
    begin_close();
    return nullptr;
  }
  sync_context(size);
  Log.info("opened \"{}\" ({}x{} px)", title, size.x, size.y);
  return s_context;
}

void close_window() noexcept { begin_close(); }

Rml::Context* get_window_context() noexcept { return s_context; }

void set_window_closed_callback(WindowCallback callback, void* userdata) noexcept {
  s_closedCallback = callback;
  s_closedUserdata = userdata;
}

void set_window_frame_callback(WindowCallback callback, void* userdata) noexcept {
  s_frameCallback = callback;
  s_frameUserdata = userdata;
}

bool window_handle_event(SDL_Event& event) noexcept {
  SDL_Window* target = SDL_GetWindowFromEvent(&event);
  if (target == nullptr) {
    return false;
  }
  if (target == s_closingWindow) {
    return true; // hidden and on its way out
  }
  if (target != s_window) {
    return false;
  }
  switch (event.type) {
  case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    begin_close();
    if (s_closedCallback != nullptr) {
      s_closedCallback(s_closedUserdata);
    }
    return true;
  case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
  case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: {
    const Rml::Vector2i size = pixel_size();
    request_size(size);
    sync_context(size);
    return true;
  }
  default:
    break;
  }
  if (s_context != nullptr) {
    RmlSDL::InputEventHandler(s_context, s_window, event);
  }
  return true;
}

WindowFrame window_record_frame() noexcept {
  reap_closing();
  if (s_context == nullptr || s_renderInterface == nullptr) {
    /* A closing window still needs a frame for the render thread to
     * release its surface on. */
    return {.state = s_closingState};
  }
  if (s_frameCallback != nullptr) {
    s_frameCallback(s_frameUserdata);
    if (s_context == nullptr) {
      return {.state = s_closingState}; // the callback closed it
    }
  }
  if ((SDL_GetWindowFlags(s_window) & SDL_WINDOW_MINIMIZED) != 0) {
    return {.state = s_state};
  }
  const Rml::Vector2i size = pixel_size();
  ensure_target(size);
  sync_context(size);
  s_context->Update();
  s_renderInterface->SetWindowSize(size);
  s_renderInterface->BeginFrame(s_target, s_target, BaseLayerContent::Transparent);
  s_context->Render();
  const bool rendered = s_renderInterface->EndFrame();
  return {
      .state = s_state,
      .bindGroup = rendered ? s_targetBindGroup : wgpu::BindGroup{},
  };
}

void window_encode(const WindowFrame& frame, const wgpu::CommandEncoder& encoder) noexcept {
  if (!frame.state) {
    return;
  }
  auto& state = *frame.state;
  std::lock_guard lock{state.mutex};
  state.texture = {};
  if (state.closing) {
    if (!state.released) {
      release_surface(state);
    }
    return;
  }
  if (!state.surface || state.width == 0 || state.height == 0) {
    return;
  }
  if (!state.configured || state.config.width != state.width || state.config.height != state.height) {
    /* The game surface's format, so the copy pipelines draw onto it;
     * opaque, and FIFO, which every surface supports. */
    state.config = webgpu::g_graphicsConfig.surfaceConfiguration;
    state.config.device = webgpu::g_device;
    state.config.width = state.width;
    state.config.height = state.height;
    state.config.alphaMode = wgpu::CompositeAlphaMode::Auto;
    state.config.presentMode = wgpu::PresentMode::Fifo;
    state.surface.Configure(&state.config);
    state.configured = true;
  }
  wgpu::SurfaceTexture surfaceTexture;
  state.surface.GetCurrentTexture(&surfaceTexture);
  if (surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal &&
      surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal) {
    return;
  }
  state.texture = std::move(surfaceTexture.texture);
  const auto view = state.texture.CreateView();
  const std::array attachments{
      wgpu::RenderPassColorAttachment{
          .view = view,
          .loadOp = wgpu::LoadOp::Clear,
          .storeOp = wgpu::StoreOp::Store,
          .clearValue = {0.08, 0.08, 0.10, 1.0},
      },
  };
  const wgpu::RenderPassDescriptor descriptor{
      .label = "RmlUi window pass",
      .colorAttachmentCount = attachments.size(),
      .colorAttachments = attachments.data(),
  };
  const auto pass = encoder.BeginRenderPass(&descriptor);
  if (frame.bindGroup) {
    pass.SetViewport(0.f, 0.f, static_cast<float>(state.config.width), static_cast<float>(state.config.height), 0.f,
                     1.f);
    pass.SetPipeline(webgpu::g_CopyPremultipliedAlphaPipeline);
    pass.SetBindGroup(0, frame.bindGroup, 0, nullptr);
    pass.Draw(3);
  }
  pass.End();
}

void window_present(const WindowFrame& frame) noexcept {
  if (!frame.state) {
    return;
  }
  auto& state = *frame.state;
  std::lock_guard lock{state.mutex};
  if (state.texture && state.configured) {
    state.surface.Present();
  }
  state.texture = {};
}

/* At aurora's shutdown the render thread has stopped: release directly. */
void window_shutdown() noexcept {
  begin_close();
  if (s_closingState != nullptr) {
    {
      std::lock_guard lock{s_closingState->mutex};
      if (!s_closingState->released) {
        release_surface(*s_closingState);
      }
    }
    SDL_DestroyWindow(s_closingWindow);
    s_closingWindow = nullptr;
    s_closingState.reset();
  }
}

} // namespace aurora::rmlui
