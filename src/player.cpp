#include "player.hpp"
#include "video_reader.hpp"
#include "sound_reader.hpp"

#include <GLFW/glfw3.h>
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdint>
#include <algorithm>

int run_player(const char* filename) {
    // --- GLFW / OpenGL ---
    if (!glfwInit()) {
        std::printf("Couldn't init GLFW\n");
        return 1;
    }
    GLFWwindow* window = glfwCreateWindow(640, 480, "Video Player", nullptr, nullptr);
    if (!window) {
        std::printf("Couldn't open window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // VSYNC

    // --- Video ---
    VideoReaderState vr{};
    if (!video_reader_open(&vr, filename)) {
        std::printf("Couldn't open video file (video)\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    const int frame_width  = vr.width;
    const int frame_height = vr.height;
    uint8_t* frame_data = new uint8_t[frame_width * frame_height * 4]; // RGBA

    // OpenGL texture (bir kere allocate et, her kare subimage ile güncelle)
    GLuint tex_handle = 0;
    glGenTextures(1, &tex_handle);
    glBindTexture(GL_TEXTURE_2D, tex_handle);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame_width, frame_height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    // --- SDL2 / Audio ---
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        std::printf("SDL_Init audio failed: %s\n", SDL_GetError());
        delete[] frame_data;
        glDeleteTextures(1, &tex_handle);
        video_reader_close(&vr);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    SoundReaderState sr{};
    const int AUDIO_SR = 48000;
    const int AUDIO_CH = 2;
    if (!sound_reader_open(&sr, filename, AUDIO_SR, AUDIO_CH, AV_SAMPLE_FMT_S16)) {
        std::printf("Couldn't open audio stream\n");
        SDL_Quit();
        delete[] frame_data;
        glDeleteTextures(1, &tex_handle);
        video_reader_close(&vr);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    SDL_AudioSpec want{};
    want.freq = AUDIO_SR;
    want.channels = AUDIO_CH;
    want.format = AUDIO_S16SYS;
    want.samples = 1024;
    want.callback = nullptr; // queue modeli

    SDL_AudioSpec have{};
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!dev) {
        std::printf("SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        sound_reader_close(&sr);
        SDL_Quit();
        delete[] frame_data;
        glDeleteTextures(1, &tex_handle);
        video_reader_close(&vr);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    const int BYTES_PER_SEC = have.freq * have.channels * (SDL_AUDIO_BITSIZE(have.format) / 8);

    // --- Prebuffer ~300ms ---
    double audio_pts_base = 0.0;
    double audio_end_pts  = 0.0;
    bool   audio_started  = false;

    auto prebuffer_audio = [&]() {
        audio_started = false;
        audio_end_pts = 0.0;
        audio_pts_base = 0.0;

        while (SDL_GetQueuedAudioSize(dev) < (Uint32)(0.3 * BYTES_PER_SEC)) {
            uint8_t* data = nullptr; int nbytes = 0;
            double a_start = 0.0, a_end = 0.0;
            if (!sound_reader_read(&sr, &data, &nbytes, &a_start, &a_end)) break;
            if (!audio_started) { audio_pts_base = a_start; audio_started = true; }
            audio_end_pts = a_end;
            SDL_QueueAudio(dev, data, nbytes);
            delete[] data;
        }
    };
    prebuffer_audio();
    SDL_PauseAudioDevice(dev, 0); // oynat

    // --- Senkron ---
    bool first_video = true;
    double video_pts_base = 0.0;

    // --- Kontroller ---
    bool paused = false;
    bool prevSpace = false, prevLeft = false, prevRight = false;

    auto get_audio_clock = [&]() -> double {
        double queued = (double)SDL_GetQueuedAudioSize(dev) / (double)BYTES_PER_SEC;
        return audio_end_pts - queued; // absolute audio clock (sec)
    };

    auto do_seek = [&](double target_sec) {
        if (target_sec < 0.0) target_sec = 0.0;

        SDL_PauseAudioDevice(dev, 1);
        SDL_ClearQueuedAudio(dev);

        if (!sound_reader_seek(&sr, target_sec)) std::printf("audio seek failed\n");
        if (!video_reader_seek(&vr, target_sec)) std::printf("video seek failed\n");

        prebuffer_audio();
        first_video = true; // video bazını ilk frame’de yeniden al

        SDL_PauseAudioDevice(dev, paused ? 1 : 0);
    };

    // FPS ölçümü (opsiyonel)
    uint32_t fps_t0 = SDL_GetTicks();
    int frames_drawn = 0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // --- Klavye kontrolleri ---
        bool sp = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (sp && !prevSpace) {
            paused = !paused;
            SDL_PauseAudioDevice(dev, paused ? 1 : 0);
        }
        prevSpace = sp;

        bool left = glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS;
        if (left && !prevLeft) do_seek(get_audio_clock() - 5.0);
        prevLeft = left;

        bool right = glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS;
        if (right && !prevRight) do_seek(get_audio_clock() + 5.0);
        prevRight = right;

        // Ses kuyruğunu besle (pause değilken)
        if (!paused) {
            while (SDL_GetQueuedAudioSize(dev) < (Uint32)(0.3 * BYTES_PER_SEC)) {
                uint8_t* data = nullptr; int nbytes = 0;
                double a_start = 0.0, a_end = 0.0;
                if (!sound_reader_read(&sr, &data, &nbytes, &a_start, &a_end)) {
                    break; // EOF
                }
                audio_end_pts = a_end;
                SDL_QueueAudio(dev, data, nbytes);
                delete[] data;
            }
        }

        // Video frame oku (pause değilken)
        int64_t vpts_i64 = 0;
        double vpts_sec = 0.0;
        if (!paused) {
            if (!video_reader_read_frame(&vr, frame_data, &vpts_i64)) {
                break; // EOF
            }
            vpts_sec = vpts_i64 * (double)vr.time_base.num / (double)vr.time_base.den;
            if (first_video) { video_pts_base = vpts_sec; first_video = false; }
        }

        // Senkron bekleme (audio master), sadece pause değilken
        if (!paused) {
            double queued_sec = (double)SDL_GetQueuedAudioSize(dev) / (double)BYTES_PER_SEC;
            double audio_clock_rel = (audio_end_pts - audio_pts_base) - queued_sec;
            double video_rel = vpts_sec - video_pts_base;

            while (video_rel > audio_clock_rel) {
                glfwPollEvents();
                SDL_Delay(1);
                queued_sec = (double)SDL_GetQueuedAudioSize(dev) / (double)BYTES_PER_SEC;
                audio_clock_rel = (audio_end_pts - audio_pts_base) - queued_sec;
            }
        }

        // --- Render ---
        int ww, wh;
        glfwGetFramebufferSize(window, &ww, &wh);
        glViewport(0, 0, ww, wh);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, ww, 0, wh, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glBindTexture(GL_TEXTURE_2D, tex_handle);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame_width, frame_height,
                        GL_RGBA, GL_UNSIGNED_BYTE, frame_data);

        // Aspect fit
        double sx = (double)ww / (double)frame_width;
        double sy = (double)wh / (double)frame_height;
        double scale = (sx < sy) ? sx : sy;
        int draw_w = (int)(frame_width  * scale + 0.5);
        int draw_h = (int)(frame_height * scale + 0.5);
        int x0 = (ww - draw_w) / 2;
        int y0 = (wh - draw_h) / 2;
        int x1 = x0 + draw_w;
        int y1 = y0 + draw_h;

        glColor4f(1.f, 1.f, 1.f, 1.f);
        glEnable(GL_TEXTURE_2D);
        glBegin(GL_QUADS);
            glTexCoord2d(0, 1); glVertex2i(x0, y0);
            glTexCoord2d(1, 1); glVertex2i(x1, y0);
            glTexCoord2d(1, 0); glVertex2i(x1, y1);
            glTexCoord2d(0, 0); glVertex2i(x0, y1);
        glEnd();
        glDisable(GL_TEXTURE_2D);

        glfwSwapBuffers(window);

        // FPS log (opsiyonel)
        frames_drawn++;
        uint32_t now = SDL_GetTicks();
        if (now - fps_t0 >= 1000) {
            double draw_fps = (double)frames_drawn * 1000.0 / (double)(now - fps_t0);
            char title[128];
            std::snprintf(title, sizeof(title), "Video Player | %.1f FPS  [Space:Play/Pause, ←/→:±5s]", draw_fps);
            glfwSetWindowTitle(window, title);
            frames_drawn = 0;
            fps_t0 = now;
        }
    }

    // --- Temizlik ---
    delete[] frame_data;
    glDeleteTextures(1, &tex_handle);
    video_reader_close(&vr);
    SDL_CloseAudioDevice(dev);
    sound_reader_close(&sr);
    SDL_Quit();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
