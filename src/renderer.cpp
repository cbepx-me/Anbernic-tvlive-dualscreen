#include "renderer.hpp"
#include "config.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>

#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace tv {

DualUIRenderer::DualUIRenderer() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "[UI] SDL_Init failed: %s\n", SDL_GetError());
        return;
    }
    if (TTF_Init() != 0) {
        std::fprintf(stderr, "[UI] TTF_Init failed: %s\n", TTF_GetError());
    }

    int n = SDL_GetNumVideoDisplays();
    std::fprintf(stderr, "[UI] displays = %d\n", n);
    int want = std::min(n, 2);

    for (int i = 0; i < want; i++) {
        SDL_Rect b;
        SDL_GetDisplayBounds(i, &b);
        int w = b.w > 0 ? b.w : 640;
        int h = b.h > 0 ? b.h : 480;

        Screen s;
        s.w = w;
        s.h = h;
        s.display_idx = i;
        s.pixels.assign((size_t)w * h * 4, 0);

        s.window = SDL_CreateWindow(
            "TVLive",
            SDL_WINDOWPOS_CENTERED_DISPLAY(i),
            SDL_WINDOWPOS_CENTERED_DISPLAY(i),
            w, h,
            SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN
        );
        if (!s.window) {
            std::fprintf(stderr, "[UI] window %d failed: %s\n", i, SDL_GetError());
            continue;
        }

        s.renderer = SDL_CreateRenderer(s.window, -1, SDL_RENDERER_ACCELERATED);
        if (!s.renderer) {
            s.renderer = SDL_CreateRenderer(s.window, -1, SDL_RENDERER_SOFTWARE);
        }
        if (!s.renderer) {
            std::fprintf(stderr, "[UI] renderer %d failed: %s\n", i, SDL_GetError());
            SDL_DestroyWindow(s.window);
            continue;
        }

        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        std::fprintf(stderr, "[UI] screen %d ready (%dx%d)\n", i, w, h);
        screens_.push_back(std::move(s));
    }

    target_ = (screens_.size() >= 2) ? 1 : 0;
    font_path_ = tv::FONT_FILE;
}

DualUIRenderer::~DualUIRenderer() {
    for (auto& kv : fonts_) if (kv.second) TTF_CloseFont(kv.second);
    fonts_.clear();
    TTF_Quit();
    for (auto& s : screens_) {
        if (s.renderer) SDL_DestroyRenderer(s.renderer);
        if (s.window)   SDL_DestroyWindow(s.window);
    }
    SDL_Quit();
}

bool DualUIRenderer::ok() const {
    return !screens_.empty() && screens_[0].renderer != nullptr;
}

void DualUIRenderer::set_target(int idx) {
    if (idx >= 0 && idx < (int)screens_.size()) target_ = idx;
}

DualUIRenderer::Screen* DualUIRenderer::get(int idx) {
    if (idx < 0) idx = target_;
    if (idx < 0 || idx >= (int)screens_.size()) return nullptr;
    return &screens_[idx];
}

std::pair<int,int> DualUIRenderer::size(int idx) const {
    int i = (idx < 0) ? target_ : idx;
    if (i < 0 || i >= (int)screens_.size()) return {640, 480};
    return {screens_[i].w, screens_[i].h};
}

void DualUIRenderer::clear(int idx) {
    Screen* s = get(idx);
    if (!s) return;
    std::fill(s->pixels.begin(), s->pixels.end(), 0);
}

void DualUIRenderer::fill_rect(int x0, int y0, int x1, int y1,
                               uint32_t color, int idx) {
    Screen* s = get(idx);
    if (!s) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    if (x0 >= x1 || y0 >= y1) return;
    for (int y = y0; y < y1; y++) {
        uint32_t* row = reinterpret_cast<uint32_t*>(&s->pixels[(size_t)y * s->w * 4]);
        for (int x = x0; x < x1; x++) row[x] = color;
    }
}

void DualUIRenderer::fill_gradient_v(int x0, int y0, int x1, int y1,
                                     uint32_t top, uint32_t bottom, int idx) {
    Screen* s = get(idx);
    if (!s) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    if (x0 >= x1 || y0 >= y1) return;

    int h = y1 - y0;
    uint8_t r1 = top & 0xFF, g1 = (top >> 8) & 0xFF, b1 = (top >> 16) & 0xFF;
    uint8_t r2 = bottom & 0xFF, g2 = (bottom >> 8) & 0xFF, b2 = (bottom >> 16) & 0xFF;
    for (int y = y0; y < y1; y++) {
        float t = (float)(y - y0) / h;
        uint8_t r = (uint8_t)(r1 + (r2 - r1) * t);
        uint8_t g = (uint8_t)(g1 + (g2 - g1) * t);
        uint8_t b = (uint8_t)(b1 + (b2 - b1) * t);
        uint32_t c = rgba(r, g, b);
        uint32_t* row = reinterpret_cast<uint32_t*>(&s->pixels[(size_t)y * s->w * 4]);
        for (int x = x0; x < x1; x++) row[x] = c;
    }
}

void DualUIRenderer::fill_rounded_rect(int x0, int y0, int x1, int y1,
                                       int radius, uint32_t color, int idx) {
    Screen* s = get(idx);
    if (!s) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    if (x0 >= x1 || y0 >= y1) return;

    int max_r = std::min({radius, (x1 - x0) / 2, (y1 - y0) / 2});
    if (max_r < 1) { fill_rect(x0, y0, x1, y1, color, idx); return; }

    for (int y = y0; y < y1; y++) {
        int left = x0, right = x1;
        int dy_top = (y0 + max_r) - y;
        int dy_bot = y - (y1 - max_r - 1);
        if (dy_top > 0) {
            int dx = (int)std::sqrt((double)max_r * max_r - (double)dy_top * dy_top);
            left  = x0 + max_r - dx;
            right = x1 - max_r + dx;
        } else if (dy_bot > 0) {
            int dx = (int)std::sqrt((double)max_r * max_r - (double)dy_bot * dy_bot);
            left  = x0 + max_r - dx;
            right = x1 - max_r + dx;
        }
        if (left < x0) left = x0;
        if (right > x1) right = x1;
        if (left < right) {
            uint32_t* row = reinterpret_cast<uint32_t*>(&s->pixels[(size_t)y * s->w * 4]);
            for (int x = left; x < right; x++) row[x] = color;
        }
    }
}

void DualUIRenderer::fill_circle(int cx, int cy, int r, uint32_t color, int idx) {
    Screen* s = get(idx);
    if (!s || r <= 0) return;
    int r2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)std::sqrt((double)(r2 - dy * dy));
        int y = cy + dy;
        if (y < 0 || y >= s->h) continue;
        int x0 = cx - dx, x1 = cx + dx + 1;
        if (x0 < 0) x0 = 0;
        if (x1 > s->w) x1 = s->w;
        if (x0 >= x1) continue;
        uint32_t* row = reinterpret_cast<uint32_t*>(&s->pixels[(size_t)y * s->w * 4]);
        for (int x = x0; x < x1; x++) row[x] = color;
    }
}

TTF_Font* DualUIRenderer::get_font(int size) {
    auto it = fonts_.find(size);
    if (it != fonts_.end()) return it->second;
    TTF_Font* f = nullptr;
    if (!font_path_.empty()) {
        f = TTF_OpenFont(font_path_.c_str(), size);
        if (!f) {
            std::fprintf(stderr, "[UI] TTF_OpenFont(%s,%d) failed: %s\n",
                         font_path_.c_str(), size, TTF_GetError());
        }
    }
    fonts_[size] = f;
    return f;
}

std::tuple<int,int> DualUIRenderer::measure_text(const std::string& s, int font_size) {
    TTF_Font* f = get_font(font_size);
    if (!f) return {0, 0};
    int w = 0, h = 0;
    if (TTF_SizeUTF8(f, s.c_str(), &w, &h) != 0) return {0, 0};
    return {w, h};
}

void DualUIRenderer::blit_surface(SDL_Surface* surf, int dx, int dy, int idx) {
    Screen* s = get(idx);
    if (!s || !surf) return;

    SDL_Surface* rgba = surf;
    bool need_free = false;
    if (surf->format->format != SDL_PIXELFORMAT_RGBA32) {
        rgba = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGBA32, 0);
        need_free = true;
        if (!rgba) return;
    }

    const uint8_t* spix = (const uint8_t*)rgba->pixels;
    int spitch = rgba->pitch;

    for (int y = 0; y < rgba->h; y++) {
        int py = dy + y;
        if (py < 0 || py >= s->h) continue;
        const uint8_t* srow = spix + y * spitch;
        uint8_t* drow = &s->pixels[(size_t)py * s->w * 4];

        for (int x = 0; x < rgba->w; x++) {
            int px = dx + x;
            if (px < 0 || px >= s->w) continue;
            const uint8_t* sp = srow + x * 4;
            uint8_t sa = sp[3];
            if (sa == 0) continue;
            uint8_t* dp = drow + px * 4;
            if (sa == 255) {
                dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = 255;
            } else {
                dp[0] = (sp[0]*sa + dp[0]*(255-sa))/255;
                dp[1] = (sp[1]*sa + dp[1]*(255-sa))/255;
                dp[2] = (sp[2]*sa + dp[2]*(255-sa))/255;
                dp[3] = 255;
            }
        }
    }
    if (need_free) SDL_FreeSurface(rgba);
}

void DualUIRenderer::text(int x, int y, const std::string& s,
                          int font_size, uint32_t color,
                          const std::string& anchor, bool shadow, int idx) {
    if (s.empty()) return;
    TTF_Font* f = get_font(font_size);
    if (!f) return;

    SDL_Color c = {
        (uint8_t)(color & 0xFF),
        (uint8_t)((color >> 8) & 0xFF),
        (uint8_t)((color >> 16) & 0xFF),
        255
    };
    SDL_Surface* srf = TTF_RenderUTF8_Blended(f, s.c_str(), c);
    if (!srf) return;

    int tw = srf->w, th = srf->h;
    int dx = x, dy = y;
    if (!anchor.empty()) {
        char h = anchor[0];
        char v = (anchor.size() >= 2) ? anchor[1] : 't';
        if      (h == 'l') dx = x;
        else if (h == 'm' || h == 'c') dx = x - tw / 2;
        else if (h == 'r') dx = x - tw;
        if      (v == 't') dy = y;
        else if (v == 'm') dy = y - th / 2;
        else if (v == 'b') dy = y - th;
    }

    if (shadow) {
        SDL_Color bc = {0, 0, 0, 255};
        SDL_Surface* ss = TTF_RenderUTF8_Blended(f, s.c_str(), bc);
        if (ss) {
            blit_surface(ss, dx + 1, dy + 1, idx);
            blit_surface(ss, dx + 1, dy - 1, idx);
            blit_surface(ss, dx - 1, dy + 1, idx);
            blit_surface(ss, dx - 1, dy - 1, idx);
            SDL_FreeSurface(ss);
        }
    }
    blit_surface(srf, dx, dy, idx);
    SDL_FreeSurface(srf);
}

void DualUIRenderer::blit_surface_scaled(SDL_Surface* src, int dx, int dy,
                                         int dw, int dh, int idx) {
    if (!src || dw <= 0 || dh <= 0) return;
    SDL_Surface* rgba = src;
    bool need_free = false;
    if (src->format->format != SDL_PIXELFORMAT_RGBA32) {
        rgba = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_RGBA32, 0);
        need_free = true;
        if (!rgba) return;
    }

    SDL_Surface* scaled = SDL_CreateRGBSurfaceWithFormat(
        0, dw, dh, 32, SDL_PIXELFORMAT_RGBA32);
    if (!scaled) {
        if (need_free) SDL_FreeSurface(rgba);
        return;
    }
    SDL_BlitScaled(rgba, nullptr, scaled, nullptr);
    blit_surface(scaled, dx, dy, idx);
    SDL_FreeSurface(scaled);
    if (need_free) SDL_FreeSurface(rgba);
}

void DualUIRenderer::draw_image_fit(SDL_Surface* surf, int cx, int cy,
                                    int max_w, int max_h, int idx) {
    if (!surf) return;
    int sw = surf->w, sh = surf->h;
    if (sw <= 0 || sh <= 0) return;
    float scale = std::min((float)max_w / sw, (float)max_h / sh);
    int tw = std::max(1, (int)(sw * scale));
    int th = std::max(1, (int)(sh * scale));
    int dx = cx - tw / 2;
    int dy = cy - th / 2;
    blit_surface_scaled(surf, dx, dy, tw, th, idx);
}

void DualUIRenderer::paint(int idx) {
    Screen* s = get(idx);
    if (!s) return;

    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
        s->pixels.data(), s->w, s->h, 32, s->w * 4,
        SDL_PIXELFORMAT_RGBA32);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(s->renderer, surf);
    SDL_FreeSurface(surf);
    if (!tex) return;

    SDL_RenderClear(s->renderer);
    SDL_RenderCopy(s->renderer, tex, nullptr, nullptr);
    SDL_RenderPresent(s->renderer);
    SDL_DestroyTexture(tex);
}

void DualUIRenderer::paint_all() {
    for (int i = 0; i < (int)screens_.size(); i++) paint(i);
}

void DualUIRenderer::draw_end() {}

} // namespace tv