#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <tuple>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Surface;
struct _TTF_Font;
using TTF_Font = _TTF_Font;

namespace tv {

inline constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}

class DualUIRenderer {
public:
    DualUIRenderer();
    ~DualUIRenderer();

    bool ok() const;
    int  num_screens() const { return (int)screens_.size(); }

    void set_target(int idx);
    int  target() const { return target_; }

    std::pair<int,int> size(int idx = -1) const;

    void clear(int idx = -1);
    void fill_rect(int x0, int y0, int x1, int y1, uint32_t color, int idx = -1);
    void fill_rounded_rect(int x0, int y0, int x1, int y1, int radius, uint32_t color, int idx = -1);
    void fill_gradient_v(int x0, int y0, int x1, int y1,
                         uint32_t top, uint32_t bottom, int idx = -1);
    void fill_circle(int cx, int cy, int r, uint32_t color, int idx = -1);

    void text(int x, int y, const std::string& s,
              int font_size = 22, uint32_t color = 0xFFFFFFFF,
              const std::string& anchor = "lt", bool shadow = false, int idx = -1);

    void draw_image_fit(SDL_Surface* surf, int cx, int cy,
                        int max_w, int max_h, int idx = -1);

    std::tuple<int,int> measure_text(const std::string& s, int font_size);

    void paint(int idx = -1);
    void paint_all();
    void draw_end();

private:
    struct Screen {
        SDL_Window*   window   = nullptr;
        SDL_Renderer* renderer = nullptr;
        std::vector<uint8_t> pixels;
        int w = 0, h = 0;
        int display_idx = 0;
    };

    Screen* get(int idx);
    TTF_Font* get_font(int size);
    void blit_surface(SDL_Surface* surf, int dx, int dy, int idx);
    void blit_surface_scaled(SDL_Surface* src, int dx, int dy, int dw, int dh, int idx);

    std::vector<Screen> screens_;
    int target_ = 0;
    std::string font_path_;
    std::map<int, TTF_Font*> fonts_;
};

} // namespace tv