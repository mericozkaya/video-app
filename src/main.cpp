#include "player.hpp"
#include "portable-file-dialogs.h"
#include <cstdio>
#include <string>
#include <vector>
#include <iostream>

static std::string pick_video_path()
{
    try {
        auto files = pfd::open_file(
            "Bir video seçin",
            ".",
            { "Video dosyaları", "*.mp4 *.mkv *.mov *.avi *.webm", "Tümü", "*" },
            pfd::opt::none
        ).result();
        if (!files.empty()) return files[0];
    } catch (...) {
        // GUI yoksa terminal fallback
    }
    std::string path;
    std::cout << "Video yolu girin (örn. ../test.mp4): ";
    std::getline(std::cin, path);
    return path;
}

int main(int argc, const char** argv) {
    std::string path;
    if (argc >= 2) {
        path = argv[1];
    } else {
        path = pick_video_path();
    }
    if (path.empty()) {
        std::fprintf(stderr, "Dosya seçilmedi.\n");
        return 1;
    }
    std::printf("Playing: %s\n", path.c_str());
    return run_player(path.c_str());
}
