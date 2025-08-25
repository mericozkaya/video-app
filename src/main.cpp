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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    const int frame_width  = vr_state.width;
    const int frame_height = vr_state.height;
    uint8_t* frame_data = new uint8_t[frame_width * frame_height * 4];

    // Döngü
    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        int window_width, window_height;
        glfwGetFramebufferSize(window, &window_width, &window_height);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, window_width, 0, window_height, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        // Yeni frame oku
        if (!video_reader_read_frame(&vr_state, frame_data)) {
            printf("Couldn't load video frame\n");
            break; // video bitti / hata
        }

        // Texture'a yükle (klasik yöntem)
        glBindTexture(GL_TEXTURE_2D, tex_handle);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     frame_width, frame_height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, frame_data);

        // Renk modülasyonu yapma
        glColor4f(1.f, 1.f, 1.f, 1.f);

        // PENCEREYİ TAM DOLDUR (stretch) + DİKEY FLIP (v koordinatlarını ters çevir)
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex_handle);
        glBegin(GL_QUADS);
            // DİKKAT: (v) 1↔0 çevrildi -> ters görüntü düzelir
            glTexCoord2d(0, 1); glVertex2i(0,               0);                // sol-alt
            glTexCoord2d(1, 1); glVertex2i(window_width,    0);                // sağ-alt
            glTexCoord2d(1, 0); glVertex2i(window_width,    window_height);    // sağ-üst
            glTexCoord2d(0, 0); glVertex2i(0,               window_height);    // sol-üst
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

// Kodun çalışıp hiç bir şeyin ekrana gelmemesi
// sorunu gerekli kütüphane kurulumları ile
// düzeldi kod aşağıdadır
// g++ main.cpp -o video-app -lglfw -lGL -ldl -lpthread
