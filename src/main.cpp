#include "player.hpp"
#include "portable-file-dialogs.h"
#include <cstdio>
#include <string>
#include <vector>
#include <iostream>

static std::string pick_video_path()
{
    try {
        // Başlangıç klasörü: mevcut dizin
        auto files = pfd::open_file(
            "Bir video seçin",
            ".",
            {
                "Video dosyaları", "*.mp4 *.mkv *.mov *.avi *.webm",
                "Tümü", "*"
            },
            pfd::opt::none // çoklu seçim istemiyoruz
        ).result();

        if (!files.empty())
            return files[0]; // seçilen ilk dosya
    } catch (...) {
        // GUI yoksa (ör. WSL’de zenity/kdialog yok) terminale düş
    }

    // Terminal fallback
    std::string path;
    std::cout << "Video yolu girin (örn. ../test.mp4): ";
    std::getline(std::cin, path);
    return path;
}

int main(int argc, const char** argv)
{
    std::string path;

    // 1) Argüman geldiyse onu kullan
    if (argc >= 2) {
        path = argv[1];
    } else {
        // 2) Gelmediyse dosya seçici aç
        path = pick_video_path();
    }

    if (path.empty()) {
        std::fprintf(stderr, "Dosya seçilmedi.\n");
        return 1;
    }

    std::printf("Playing: %s\n", path.c_str());
    return run_player(path.c_str());
}
