#include <stdio.h>
#include <GLFW/glfw3.h>
#include "video_reader.hpp"


bool load_frame(const char* filename, int* width,int* height, unsigned char** data);

int main(int argc, const char** argv)   {
    GLFWwindow* window;

    //glf başladı
    if(!glfwInit()){
        printf("Couldn't init GLFW\n");
        return 1;
    }

    //glf penceresi oluşturuldu 
    // Pencereyi yeniden boyutlandırılabilir ve dekorlu olarak oluştur
    
    window =glfwCreateWindow(640,280, "Hello World",NULL, NULL);
    if(!window) {
        printf("Couldn't open window\n");
        return 1;
    }

/*
    unsigned char* data = new unsigned char[100 * 100 * 3];
    for (int y = 0; y < 100; ++y) {
        for (int x=0; x<100; ++x) {
            data[y*100*3+x*3] = 0xff;
            data[y*100*3+x*3+1] = 0x00;
            data[y*100*3+x*3+2] = 0x00; 
        }""
    }

    for (int y = 25; y < 75; ++y) {
        for (int x=25; x<75; ++x) {
            data[y*100*3+x*3] = 0x00;
            data[y*100*3+x*3+1] = 0x00;
            data[y*100*3+x*3+2] = 0xff; 
        }  
    }
*/

    VideoReaderState vr_state;
    if (!video_reader_open(&vr_state, "/home/meric/vestel/video-app/ugwey.mp4")){
        printf("Couldn't open video file\n");
        return 1;
    }

    const int frame_width = vr_state.width;
    const int frame_height = vr_state.height;
    uint8_t* frame_data = new uint8_t[frame_width * frame_height * 4];

    if(!video_reader_read_frame(&vr_state, frame_data)) {
        printf("Couldn't load video frame\n");
        return 1;
    }
    video_reader_close(&vr_state);

    //Gözükmeme sorunu için aşağıdaki komutun yeri değiştirildi.
    glfwMakeContextCurrent(window);

    GLuint tex_handle;
    /*int tex_width = 100;
    int tex_height = 100;*/
    
    glGenTextures(1, &tex_handle);
    glBindTexture(GL_TEXTURE_2D, tex_handle);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    // Hem dahili format hem de format GL_RGBA olmalı (4 kanal için)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame_width, frame_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame_data);
    

    //pencere kullanıcı tarafından kapanana kadar while döngüsü
    
    while (!glfwWindowShouldClose(window)){
        int window_width, window_height;
        glfwGetFramebufferSize(window, &window_width, &window_height);
        glViewport(0, 0, window_width, window_height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, window_width, 0, window_height, -1, 1);
        glMatrixMode(GL_MODELVIEW);

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex_handle);
        glBegin(GL_QUADS);
            glTexCoord2d(0,1); glVertex2i(0,0); // sol alt
            glTexCoord2d(1,1); glVertex2i(window_width,0); // sağ alt
            glTexCoord2d(1,0); glVertex2i(window_width, window_height); // sağ üst
            glTexCoord2d(0,0); glVertex2i(0, window_height); // sol üst
        glEnd();
        glDisable(GL_TEXTURE_2D);

        //Çizim yapılmış Buffer'ı öne alırız
        glfwSwapBuffers(window);
        glfwWaitEvents();
    }

    return 0;
}


//Kodun çalışıp hiç bir şeyin ekrana gelmemesi
//sorunu gerekli kütüphane kurulumları ile
//düzeldi kod aşağıdadir
//g++ main.cpp -o video-app -lglfw -lGL -ldl -lpthread
