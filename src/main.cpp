// main.cpp
#include <stdio.h>
#include <GLFW/glfw3.h>
#include "video_reader.hpp"

int main(int argc, const char** argv) {
    GLFWwindow* window;

    // GLFW başlat
    if (!glfwInit()) {
        printf("Couldn't init GLFW\n");
        return 1;
    }

    // Pencere oluştur
    window = glfwCreateWindow(640, 480, "Hello World", NULL, NULL);
    if (!window) {
        printf("Couldn't open window\n");
        glfwTerminate();
        return 1;
    }

    // Video aç
    VideoReaderState vr_state{};
    if (!video_reader_open(&vr_state, "/home/meric/vestel/video-app/ugwey.mp4")) {
        printf("Couldn't open video file\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // GL context aktif
    glfwMakeContextCurrent(window);

    // Texture oluştur (klasik yöntem, her karede glTexImage2D ile güncelleyeceğiz)
    GLuint tex_handle;
    glGenTextures(1, &tex_handle);
    glBindTexture(GL_TEXTURE_2D, tex_handle);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // repeat yerine clamp
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    const int frame_width  = vr_state.width;
    const int frame_height = vr_state.height;
    uint8_t* frame_data = new uint8_t[frame_width * frame_height * 4];

    bool first_frame = true;

    // Döngü
    while (!glfwWindowShouldClose(window)) {
        int window_width, window_height;
        glfwGetFramebufferSize(window, &window_width, &window_height);

        // Viewport'u pencere boyutuna eşitle (kritik)
        glViewport(0, 0, window_width, window_height);

        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, window_width, 0, window_height, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        // Yeni frame oku
        int64_t pts;
        if (!video_reader_read_frame(&vr_state, frame_data, &pts)) {
            printf("Couldn't load video frame (eof or error)\n");
            break; // video bitti / hata
        }

        if (first_frame) {
            glfwSetTime(0.0);
            first_frame = false;
        }

        double pt_in_seconds = pts * (double)vr_state.time_base.num / (double)vr_state.time_base.den;
        while (pt_in_seconds > glfwGetTime()) {
            // basit zamanlama
        }

        // Texture'a yükle (klasik yöntem)
        glBindTexture(GL_TEXTURE_2D, tex_handle);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     frame_width, frame_height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, frame_data);

        // Renk modülasyonu yapma
        glColor4f(1.f, 1.f, 1.f, 1.f);

        // --- ASPECT KORUYARAK ÇİZ ---
        // pencereye sığacak ölçeği bul (letterbox/pillarbox)
        double sx = (double)window_width  / (double)frame_width;
        double sy = (double)window_height / (double)frame_height;
        double scale = (sx < sy) ? sx : sy;

        int draw_w = (int)(frame_width  * scale + 0.5);
        int draw_h = (int)(frame_height * scale + 0.5);
        int x0 = (window_width  - draw_w) / 2;
        int y0 = (window_height - draw_h) / 2;
        int x1 = x0 + draw_w;
        int y1 = y0 + draw_h;

        // Dikey flip: v koordinatlarını ters çeviriyoruz
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex_handle);
        glBegin(GL_QUADS);
            // sol-alt, sağ-alt, sağ-üst, sol-üst
            glTexCoord2d(0, 1); glVertex2i(x0, y0);
            glTexCoord2d(1, 1); glVertex2i(x1, y0);
            glTexCoord2d(1, 0); glVertex2i(x1, y1);
            glTexCoord2d(0, 0); glVertex2i(x0, y1);
        glEnd();
        glDisable(GL_TEXTURE_2D);

        // Çizimi göster
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Temizlik
    delete[] frame_data;
    glDeleteTextures(1, &tex_handle);
    video_reader_close(&vr_state);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
