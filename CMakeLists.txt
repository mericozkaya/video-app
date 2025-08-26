cmake_minimum_required(VERSION 3.14)
project(video-app C CXX)
set(CMAKE_CXX_STANDARD 14)

add_subdirectory(lib/FFmpeg)
add_subdirectory(lib/glfw)

add_definitions(-DGL_SILENCE_DEPRECATION)

# SDL2 (pkg-config)
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

# portable-file-dialogs (header-only)
include_directories(${CMAKE_SOURCE_DIR}/lib/portable-file-dialogs)

# Dear ImGui (OpenGL2 + GLFW backend -> fixed pipeline ile uyumlu)
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/lib/imgui)
include_directories(${IMGUI_DIR} ${IMGUI_DIR}/backends)
set(IMGUI_SRC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl2.cpp
)

list(APPEND SOURCES
    src/player.cpp
    src/main.cpp
    src/video_reader.cpp
    src/sound_reader.cpp
    ${IMGUI_SRC}
)

add_executable(video-app ${SOURCES})
target_link_libraries(video-app FFmpeg glfw avformat avcodec avutil swscale swresample ${SDL2_LIBRARIES} ${EXTRA_LIBS})

# İsteğe bağlı: uyarıları azalt
# add_compile_options(-Wno-deprecated-declarations)
