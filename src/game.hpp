#pragma once

#include <mutex>
#include <condition_variable>
#include <thread>
#include <optional>
#include "network.hpp"
#include "queue.hpp"

#define SHA256              1
#define SHA512              2
#define SHA256_DIGEST_SIZE  32
#define SHA512_DIGEST_SIZE  64

#define SECRET_LENGTH   64      // Length of randomly generated secret keys
#define CHF             SHA256  // Cryptographic hash function

#if CHF == SHA256
    #define DIGEST_SIZE SHA256_DIGEST_SIZE
    #define EVP_DIGEST EVP_sha256
#elif CHF == SHA512
    #define DIGEST_SIZE SHA512_DIGEST_SIZE
    #define EVP_DIGEST EVP_sha512
#endif

enum class Choice {
    rock,
    paper,
    scissors,
    invalid,
};

enum MessageType: std::uint8_t {
    choice_made,
    choice_reveal,
};

// Data for revealing player's choice
struct ChoiceReveal {
    Choice choice;
    unsigned char secret[SECRET_LENGTH]; // Secret key

    ChoiceReveal() = default;

    // Generates a cryptographically secure random secret key
    ChoiceReveal(Choice choice);
};

// Data for announcing that player has made a choice
struct ChoiceMade {
    unsigned char hash[DIGEST_SIZE]; // HMAC generated from choice and secret using SHA512

    ChoiceMade() = default;      

    // Create a choice-made announcement from choice_reveal
    ChoiceMade(const ChoiceReveal& choice_reveal);
};

// Messages sent over TCP connections
struct Message {
    MessageType message_type;
    union {
        ChoiceReveal choice_reveal;
        ChoiceMade choice_made;
    } data;
};

enum EventType {
    server_connected,
    server_disconnected,
    client_connected,
    client_disconnected,
    user_choice,
    message_received,
};


// Events sent to Game::run()
struct Event {
    EventType type;
    union {
        Choice choice;
        Message message;
    } data;
};

// Rock paper scissors game
class Game {
    // Game states
    using State = unsigned; 
    const static State condition_server_connected   = 1 << 0;
    const static State condition_client_connected   = 1 << 1;
    const static State condition_user_choice_made   = 1 << 2;
    const static State condition_user_announced     = 1 << 3;
    const static State condition_opponent_announced = 1 << 4;
    const static State condition_user_revealed      = 1 << 5;
    const static State condition_opponent_revealed  = 1 << 6;

    const char *server_port, *client_host, *client_port;
    std::unique_ptr<Server> server;     // Server for incoming messages
    std::unique_ptr<Connection> client; // Connection for outgoing messages
    std::thread server_thread, client_thread, ui_thread;
    State state = 0;                    // Current state
    // std::mutex state_m;                 // Mutex for state variable
    // std::condition_variable state_cv;   // Notifications about state changes for Game::run
    Queue<Event> event_queue;
    Queue<std::optional<Message>> outgoing_messages;   // Messages to be sent to opponent
    ChoiceMade user_choice_made, opponent_choice_made;
    ChoiceReveal user_choice_reveal, opponent_choice_reveal;

    // Current score
    unsigned int wins = 0, losses = 0;

    // Checks if all bits from condition are on
    inline bool check(State condition);

    // Turn on bits from conditions
    void state_on(State conditions);

    // Turn off bits from conditions
    void state_off(State conditions);

    // Receive messages and store them in incoming_messages
    void run_server();

    // Send messages from outgoing_messages
    void run_client();

    // Read user input and store in choices
    void run_ui();

    // Reveal user's choice
    void reveal();
public:
    // Initialize a game
    Game(const char *server_port, const char *client_host, const char *client_port);

    // Play the game
    void run();
};