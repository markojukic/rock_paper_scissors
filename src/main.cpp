#include <iostream>
#include "game.hpp"

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cout << "Usage: ./rock_paper_scissors <your port> <opponent's host> <opponent's port>\n";
        return 1;
    }
    Game game(argv[1], argv[2], argv[3]);
    game.run();
    return 0;
}
