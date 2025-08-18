#include <stdio.h>
#include <GLFW/glfw3.h>

int main(int argc, const char** argv)   {
    GLFWwindow* window;

    //glf başladı
    if(!glfwInit()){
        printf("Couldn't init GLFW\n");
        return 1;
    }

    //glf penceresi oluşturuldu 
    window =glfwCreateWindow(640,280, "Hello World",NULL, NULL);
    if(!window) {
        printf("Couldn't open window\n");
        return 1;
    }


    unsigned char* data = new unsigned char[100 * 100 * 3];
    for (int y = 0; y < 100; ++y) {
        for (int x=0; x<100; ++x) {
            data[y*100*3+x*3] = 0xff;
            data[y*100*3+x*3+1] = 0x00;
            data[y*100*3+x*3+2] = 0x00; 
        }
        
    }

    //pencere kullanıcı tarafından kapanana kadar while döngüsü
    glfwMakeContextCurrent(window);
    while (!glfwWindowShouldClose(window)){
        
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDrawPixels(100,100, GL_RGB, GL_UNSIGNED_BYTE, data);
        
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
