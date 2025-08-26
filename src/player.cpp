#include "player.hpp"
#include "video_reader.hpp"
#include "sound_reader.hpp"

#include <GLFW/glfw3.h>
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <string>

// ImGui
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl2.h"

// küçük yardımcılar
static inline std::string fmt_time(double sec) {
    if (sec < 0) sec = 0;
    int s = (int)(sec + 0.5);
    int h = s / 3600; s %= 3600;
    int m = s / 60;   s %= 60;
    char buf[32];
    if (h > 0) std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    else       std::snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    return buf;
}
static inline void apply_volume_s16(uint8_t* data, int nbytes, float vol01) {
    if (!data) return;
    if (vol01 < 0.f) vol01 = 0.f;
    if (vol01 > 1.f) vol01 = 1.f;
    int16_t* p = (int16_t*)data;
    int count = nbytes / 2;
    for (int i = 0; i < count; ++i) {
        int v = (int)std::lround(p[i] * vol01);
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        p[i] = (int16_t)v;
    }
}

int run_player(const char* filename) {
    // --- GLFW / OpenGL ---
    if (!glfwInit()) { std::printf("Couldn't init GLFW\n"); return 1; }
    GLFWwindow* window = glfwCreateWindow(960, 540, "Video Player", nullptr, nullptr);
    if (!window) { std::printf("Couldn't open window\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // VSYNC

    // --- ImGui ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();

    // --- Video ---
    VideoReaderState vr{};
    if (!video_reader_open(&vr, filename)) {
        std::printf("Couldn't open video file (video)\n");
        ImGui_ImplOpenGL2_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
        glfwDestroyWindow(window); glfwTerminate(); return 1;
    }
    const int frame_width  = vr.width;
    const int frame_height = vr.height;
    uint8_t* frame_data = new uint8_t[frame_width * frame_height * 4];

    // GL texture
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
        delete[] frame_data; glDeleteTextures(1, &tex_handle);
        video_reader_close(&vr);
        ImGui_ImplOpenGL2_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
        glfwDestroyWindow(window); glfwTerminate(); return 1;
    }

    SoundReaderState sr{};
    const int AUDIO_SR = 48000, AUDIO_CH = 2;
    if (!sound_reader_open(&sr, filename, AUDIO_SR, AUDIO_CH, AV_SAMPLE_FMT_S16)) {
        std::printf("Couldn't open audio stream\n");
        SDL_Quit(); delete[] frame_data; glDeleteTextures(1, &tex_handle);
        video_reader_close(&vr);
        ImGui_ImplOpenGL2_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
        glfwDestroyWindow(window); glfwTerminate(); return 1;
    }
    SDL_AudioSpec want{}; want.freq=AUDIO_SR; want.channels=AUDIO_CH;
    want.format=AUDIO_S16SYS; want.samples=1024; want.callback=nullptr;
    SDL_AudioSpec have{}; SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr,0,&want,&have,0);
    if (!dev) {
        std::printf("SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        sound_reader_close(&sr); SDL_Quit(); delete[] frame_data; glDeleteTextures(1, &tex_handle);
        video_reader_close(&vr);
        ImGui_ImplOpenGL2_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
        glfwDestroyWindow(window); glfwTerminate(); return 1;
    }
    const int BYTES_PER_SEC = have.freq * have.channels * (SDL_AUDIO_BITSIZE(have.format)/8);

    // --- Prebuffer ~300ms ---
    double audio_pts_base = 0.0, audio_end_pts = 0.0; bool audio_started = false;
    double file_start_sec = 0.0; bool have_file_start = false; // dosya başlangıcı (sabit)

    // UI state
    bool paused = false, prevSpace=false, prevLeft=false, prevRight=false;
    bool seeking_slider = false;
    float volume01 = 1.0f; // <-- HEP buradan uygulanacak (prebuffer + feed)

    auto prebuffer_audio = [&]() {
        audio_started = false; audio_end_pts = 0.0; audio_pts_base = 0.0;
        while (SDL_GetQueuedAudioSize(dev) < (Uint32)(0.3 * BYTES_PER_SEC)) {
            uint8_t* data = nullptr; int nbytes = 0; double a_start = 0.0, a_end = 0.0;
            if (!sound_reader_read(&sr, &data, &nbytes, &a_start, &a_end)) break;
            if (!audio_started) {
                audio_pts_base = a_start; audio_started = true;
                if (!have_file_start) { file_start_sec = a_start; have_file_start = true; }
            }
            audio_end_pts = a_end;
            apply_volume_s16(data, nbytes, volume01); // <-- VOLUME UYGULA (prebuffer)
            SDL_QueueAudio(dev, data, nbytes);
            delete[] data;
        }
    };
    prebuffer_audio();
    SDL_PauseAudioDevice(dev, 0);

    // --- Senkron ---
    bool first_video = true; double video_pts_base = 0.0;

    auto get_audio_clock_abs = [&]() -> double {
        double queued = (double)SDL_GetQueuedAudioSize(dev) / (double)BYTES_PER_SEC;
        return audio_end_pts - queued; // absolute sec
    };
    auto get_pos_rel = [&]() -> double { // UI için 0 = dosya başlangıcı
        return get_audio_clock_abs() - file_start_sec;
    };
    auto do_seek_rel = [&](double rel_sec) { // UI’den gelen göreli saniye
        if (rel_sec < 0.0) rel_sec = 0.0;
        double duration_sec = video_reader_get_duration_sec(&vr);
        if (duration_sec > 0.0 && rel_sec > duration_sec) rel_sec = duration_sec;

        double target_abs_sec = file_start_sec + rel_sec; // mutlak saniye
        SDL_PauseAudioDevice(dev, 1);
        SDL_ClearQueuedAudio(dev);
        if (!sound_reader_seek(&sr, target_abs_sec)) std::printf("audio seek failed\n");
        if (!video_reader_seek(&vr,  target_abs_sec)) std::printf("video seek failed\n");
        prebuffer_audio();            // volume burada da uygulanır
        first_video = true;           // video bazını ilk frame’de yeniden al
        SDL_PauseAudioDevice(dev, paused ? 1 : 0);
    };

    double duration_sec = video_reader_get_duration_sec(&vr);

    // FPS ölçümü (opsiyonel)
    uint32_t fps_t0 = SDL_GetTicks(); int frames_drawn = 0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // --- Input ---
        bool sp = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (sp && !prevSpace) { paused = !paused; SDL_PauseAudioDevice(dev, paused ? 1 : 0); }
        prevSpace = sp;
        bool left = glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS;
        if (left && !prevLeft) do_seek_rel(get_pos_rel() - 5.0);
        prevLeft = left;
        bool right = glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS;
        if (right && !prevRight) do_seek_rel(get_pos_rel() + 5.0);
        prevRight = right;

        // --- Ses kuyruğu ---
        if (!paused && !seeking_slider) {
            while (SDL_GetQueuedAudioSize(dev) < (Uint32)(0.3 * BYTES_PER_SEC)) {
                uint8_t* data = nullptr; int nbytes = 0; double a_start = 0.0, a_end = 0.0;
                if (!sound_reader_read(&sr, &data, &nbytes, &a_start, &a_end)) break;
                audio_end_pts = a_end;
                apply_volume_s16(data, nbytes, volume01);  // <-- VOLUME UYGULA (normal besleme)
                SDL_QueueAudio(dev, data, nbytes);
                delete[] data;
            }
        }

        // --- Video frame ---
        int64_t vpts_i64 = 0; double vpts_sec = 0.0;
        if (!paused && !seeking_slider) {
            if (!video_reader_read_frame(&vr, frame_data, &vpts_i64)) break; // EOF
            vpts_sec = vpts_i64 * (double)vr.time_base.num / (double)vr.time_base.den;
            if (first_video) { video_pts_base = vpts_sec; first_video = false; }
        }

        // --- Senkron (audio master) ---
        if (!paused && !seeking_slider) {
            double queued_sec = (double)SDL_GetQueuedAudioSize(dev) / (double)BYTES_PER_SEC;
            double audio_clock_rel = (audio_end_pts - audio_pts_base) - queued_sec; // prebuffer bazına göre
            double video_rel = vpts_sec - video_pts_base;
            while (video_rel > audio_clock_rel) {
                glfwPollEvents();
                SDL_Delay(1);
                queued_sec = (double)SDL_GetQueuedAudioSize(dev) / (double)BYTES_PER_SEC;
                audio_clock_rel = (audio_end_pts - audio_pts_base) - queued_sec;
            }
        }

        // --- Render video ---
        int ww, wh; glfwGetFramebufferSize(window, &ww, &wh);
        glViewport(0, 0, ww, wh);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, ww, 0, wh, -1, 1);
        glMatrixMode(GL_MODELVIEW);  glLoadIdentity();

        glBindTexture(GL_TEXTURE_2D, tex_handle);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame_width, frame_height,
                        GL_RGBA, GL_UNSIGNED_BYTE, frame_data);

        double sx = (double)ww / (double)frame_width;
        double sy = (double)wh / (double)frame_height;
        double scale = (sx < sy) ? sx : sy;
        int draw_w = (int)(frame_width  * scale + 0.5);
        int draw_h = (int)(frame_height * scale + 0.5);
        int x0 = (ww - draw_w) / 2; int y0 = (wh - draw_h) / 2;
        int x1 = x0 + draw_w;       int y1 = y0 + draw_h;

        glColor4f(1.f, 1.f, 1.f, 1.f);
        glEnable(GL_TEXTURE_2D);
        glBegin(GL_QUADS);
            glTexCoord2d(0, 1); glVertex2i(x0, y0);
            glTexCoord2d(1, 1); glVertex2i(x1, y0);
            glTexCoord2d(1, 0); glVertex2i(x1, y1);
            glTexCoord2d(0, 0); glVertex2i(x0, y1);
        glEnd();
        glDisable(GL_TEXTURE_2D);

        // --- ImGui ---
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const float bar_h = 96.0f;
        ImGui::SetNextWindowPos(ImVec2(0, wh - bar_h), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2((float)ww, bar_h), ImGuiCond_Always);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin("ControlBar", nullptr, flags)) {
            ImGui::PushItemWidth(-1);

            ImGui::Columns(3, nullptr, false);
            if (ImGui::Button(paused ? "Play (Space)" : "Pause (Space)", ImVec2(150, 32))) {
                paused = !paused;
                SDL_PauseAudioDevice(dev, paused ? 1 : 0);
            }
            ImGui::NextColumn();

            double cur_rel = get_pos_rel();
            std::string time_left = fmt_time(cur_rel);
            std::string time_total = (duration_sec > 0) ? fmt_time(duration_sec) : "--:--";
            ImGui::Text("  %s / %s", time_left.c_str(), time_total.c_str());
            ImGui::NextColumn();

            ImGui::Text("Volume");
            ImGui::SameLine();
            ImGui::SliderFloat("##vol", &volume01, 0.0f, 1.0f, "%.2f");
            ImGui::Columns(1);

            // Timeline slider — SADECE etkileşim yokken playhead'den güncelle
            float slider_w = ImGui::GetContentRegionAvail().x;
            if (duration_sec > 0.0) {
                static float slider_val = 0.0f;
                if (!seeking_slider) slider_val = (float)cur_rel;  // <-- FIX: override etme
                ImGui::PushItemWidth(slider_w);
                ImGui::SliderFloat("##timeline", &slider_val, 0.0f, (float)duration_sec, "");
                if (ImGui::IsItemActive()) seeking_slider = true;
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    seeking_slider = false;
                    do_seek_rel((double)slider_val); // göreli -> mutlak
                }
                ImGui::PopItemWidth();
            } else {
                ImGui::ProgressBar(0.f, ImVec2(slider_w, 12.0f));
            }

            ImGui::PopItemWidth();
        }
        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);

        // FPS başlık (ASCII oklar, UTF-8 uyarısı yok)
        frames_drawn++;
        uint32_t now = SDL_GetTicks();
        if (now - fps_t0 >= 1000) {
            double fps = (double)frames_drawn * 1000.0 / (double)(now - fps_t0);
            char title[160];
            std::snprintf(title, sizeof(title),
                          "Video Player  |  %.1f FPS   [Space: Play/Pause, <-/->: +/-5s]",
                          fps);
            glfwSetWindowTitle(window, title);
            frames_drawn = 0; fps_t0 = now;
        }
    }

    // --- Temizlik ---
    delete[] frame_data;
    glDeleteTextures(1, &tex_handle);
    video_reader_close(&vr);
    SDL_CloseAudioDevice(dev);
    sound_reader_close(&sr);
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    SDL_Quit();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
