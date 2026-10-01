#include <cstdio>
#include "config.hpp"
#include "app.hpp"

int main(int, char**) {
    setvbuf(stderr, NULL, _IONBF, 0);
    tv::init_paths();

    tv::TVApp app;
    try {
        app.run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
    return 0;
}