#CMakeLists.txt

cmake_minimum_required(VERSION 3.14)
project(video-app C CXX)
set(CMAKE_CXX_STANDARD 14)

add_subdirectory(lib/FFmpeg)
add_subdirectory(lib/glfw)

add_definitions(-DGL_SILENCE_DEPRECATION)

# SDL2 (pkg-config üzerinden)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
include_directories(${SDL2_INCLUDE_DIRS})

if(APPLE)
    list(APPEND EXTRA_LIBS "-framework OpenGL")
elseif(WIN32)
    list(APPEND EXTRA_LIBS "-lglu32 -lopengl32")
    set(CMAKE_EXE_LINKER_FLAGS "-std=gnu99 -static -static-libgcc -static-libstdc++ -mwindows")
else()
    list(APPEND EXTRA_LIBS "-lGL -lGLU -lX11")
endif()

list(APPEND SOURCES
    src/player.cpp
    src/main.cpp
    src/video_reader.cpp
    src/load_frame.cpp
    src/sound_reader.cpp    # <-- yeni
)

add_executable(video-app ${SOURCES})
# swresample + SDL2 eklendi
target_link_libraries(video-app FFmpeg glfw avformat avcodec avutil swscale swresample ${SDL2_LIBRARIES} ${EXTRA_LIBS})

target_include_directories(video-app PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/portable-file-dialogs
)
