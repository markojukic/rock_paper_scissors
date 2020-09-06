add_executable(${PROJECT_NAME} main.cpp
    util.hpp util.cpp
    queue.hpp
    network.hpp network.cpp
    game.hpp game.cpp
)

target_link_libraries(${PROJECT_NAME} Threads::Threads OpenSSL::Crypto)