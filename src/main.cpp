#include "player.hpp"
#include <cstdio>

int main(int argc, const char** argv) {
    const char* filename = (argc >= 2) ? argv[1] : "test.mp4"; // istersen varsayılanı kaldır
    std::printf("Playing: %s\n", filename);
    return run_player(filename);
}
