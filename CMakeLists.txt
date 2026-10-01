cmake_minimum_required(VERSION 3.16)
project(tvlive CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)

pkg_check_modules(SDL2       REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL2_TTF   REQUIRED IMPORTED_TARGET SDL2_ttf)
pkg_check_modules(SDL2_IMAGE REQUIRED IMPORTED_TARGET SDL2_image)

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

set(SOURCES
    src/main.cpp
    src/app.cpp
    src/config.cpp
    src/input.cpp
    src/logo_loader.cpp
    src/player.cpp
    src/renderer.cpp
    src/scanner.cpp
    src/touch.cpp
    src/translator.cpp
)

add_executable(tvlive ${SOURCES})

target_link_libraries(tvlive PRIVATE
    PkgConfig::SDL2
    PkgConfig::SDL2_TTF
    PkgConfig::SDL2_IMAGE
    CURL::libcurl
    Threads::Threads
)

target_include_directories(tvlive PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)