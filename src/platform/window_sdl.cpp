#include "platform/window_sdl.h"

#include <SDL.h>

#include "core/log.h"

namespace vf {

struct Window::Impl {
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  SDL_Texture* texture = nullptr;
  int texture_width = 0;
  int texture_height = 0;
};

Window::~Window() { close(); }

bool Window::open(int width, int height, const std::string& title) {
  impl_ = new Impl();
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
    LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
    std::fprintf(stderr,
                 "could not open a window: %s\n"
                 "  (on a headless machine use `vlcflix render --screen home --png out.png`)\n",
                 SDL_GetError());
    delete impl_;
    impl_ = nullptr;
    return false;
  }
  impl_->window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (!impl_->window) {
    LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
    std::fprintf(stderr, "could not create a window: %s\n", SDL_GetError());
    SDL_Quit();
    delete impl_;
    impl_ = nullptr;
    return false;
  }
  impl_->renderer = SDL_CreateRenderer(impl_->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!impl_->renderer) {
    LOG_WARN("accelerated renderer unavailable (%s); falling back to software", SDL_GetError());
    impl_->renderer = SDL_CreateRenderer(impl_->window, -1, SDL_RENDERER_SOFTWARE);
  }
  if (!impl_->renderer) {
    LOG_ERROR("SDL_CreateRenderer failed: %s", SDL_GetError());
    std::fprintf(stderr, "could not create a renderer: %s\n", SDL_GetError());
    SDL_DestroyWindow(impl_->window);
    SDL_Quit();
    delete impl_;
    impl_ = nullptr;
    return false;
  }
  SDL_SetRenderDrawColor(impl_->renderer, 11, 11, 15, 255);
  SDL_StartTextInput();
  window_ = impl_->window;
  width_ = width;
  height_ = height;
  LOG_INFO("window opened %dx%d (%s)", width, height, title.c_str());
  return true;
}

void Window::close() {
  if (!impl_) return;
  if (impl_->texture) SDL_DestroyTexture(impl_->texture);
  if (impl_->renderer) SDL_DestroyRenderer(impl_->renderer);
  if (impl_->window) SDL_DestroyWindow(impl_->window);
  SDL_Quit();
  delete impl_;
  impl_ = nullptr;
  window_ = nullptr;
  LOG_INFO("window closed");
}

void Window::present(const Canvas& canvas) {
  if (!impl_ || !impl_->renderer) return;
  if (!impl_->texture || impl_->texture_width != canvas.width() ||
      impl_->texture_height != canvas.height()) {
    if (impl_->texture) SDL_DestroyTexture(impl_->texture);
    impl_->texture = SDL_CreateTexture(impl_->renderer, SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_STREAMING, canvas.width(), canvas.height());
    impl_->texture_width = canvas.width();
    impl_->texture_height = canvas.height();
  }
  if (!impl_->texture) return;
  SDL_UpdateTexture(impl_->texture, nullptr, canvas.pixels().data(), canvas.width() * 4);
  SDL_RenderClear(impl_->renderer);
  SDL_RenderCopy(impl_->renderer, impl_->texture, nullptr, nullptr);
  SDL_RenderPresent(impl_->renderer);
}

void Window::toggle_fullscreen() {
  if (!impl_ || !impl_->window) return;
  fullscreen_ = !fullscreen_;
  SDL_SetWindowFullscreen(impl_->window, fullscreen_ ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  LOG_INFO("window: fullscreen %s", fullscreen_ ? "on" : "off");
}

bool Window::poll(ui::InputEvent* event, int timeout_ms) {
  if (!impl_) return false;
  SDL_Event sdl_event;
  if (SDL_PollEvent(&sdl_event) == 0) {
    if (SDL_WaitEventTimeout(&sdl_event, timeout_ms) == 0) return true;  // no input yet
  }
  do {
    switch (sdl_event.type) {
      case SDL_QUIT:
        return false;
      case SDL_WINDOWEVENT:
        if (sdl_event.window.event == SDL_WINDOWEVENT_CLOSE) return false;
        if (sdl_event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
          width_ = sdl_event.window.data1;
          height_ = sdl_event.window.data2;
          event->type = ui::InputEvent::Type::Resize;
          event->x = static_cast<float>(width_);
          event->y = static_cast<float>(height_);
          return true;
        }
        break;
      case SDL_MOUSEMOTION:
        event->type = ui::InputEvent::Type::MouseMove;
        event->x = static_cast<float>(sdl_event.motion.x);
        event->y = static_cast<float>(sdl_event.motion.y);
        return true;
      case SDL_MOUSEBUTTONDOWN:
        if (sdl_event.button.button == SDL_BUTTON_LEFT) {
          event->type = ui::InputEvent::Type::MouseDown;
          event->x = static_cast<float>(sdl_event.button.x);
          event->y = static_cast<float>(sdl_event.button.y);
          return true;
        }
        break;
      case SDL_MOUSEWHEEL:
        event->type = ui::InputEvent::Type::Wheel;
        event->wheel = static_cast<float>(sdl_event.wheel.y);
        return true;
      case SDL_KEYDOWN:
        event->type = ui::InputEvent::Type::Key;
        event->key = sdl_event.key.keysym.sym;
        event->ctrl = (sdl_event.key.keysym.mod & KMOD_CTRL) != 0;
        if (sdl_event.key.keysym.sym == SDLK_BACKSPACE && sdl_event.key.repeat == 0) {
          // Handled by the app as a text edit as well as a key.
        }
        return true;
      case SDL_TEXTINPUT:
        event->type = ui::InputEvent::Type::Text;
        event->text = sdl_event.text.text;
        return true;
      default:
        break;
    }
  } while (SDL_PollEvent(&sdl_event));
  return true;
}

}  // namespace vf
