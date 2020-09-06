#pragma once

#include <mutex>
#include <condition_variable>
#include <thread>
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

// Rock paper scissors game
class Game {
    // Game states
    using State = unsigned; 
    const static State condition_running            = 1 << 0;
    const static State condition_server_connected   = 1 << 1;
    const static State condition_client_connected   = 1 << 2;

    const char *server_port, *client_host, *client_port;
    std::unique_ptr<Server> server;     // Server for incoming messages
    std::unique_ptr<Connection> client; // Connection for outgoing messages
    std::thread server_thread, client_thread, ui_thread;
    State state = 0;                    // Current state
    std::mutex state_m;                 // Mutex for state variable
    std::condition_variable state_cv;   // Notifications for state changes
    Queue<Message> incoming_messages;   // Messages received from opponent
    Queue<Message> outgoing_messages;   // Messages to be sent to opponent
    Queue<Choice> choices;              // Player's game choices

    // Current score
    unsigned int wins, losses;

    // Checks if all bits from condition are on
    inline bool check(State condition);

    // Wait for next_condition while maintaining previous condition
    // Returns
    //  - true if next_condition was achieved without breaking previous_condition
    //  - false if previous_condition was broken before achieving next_conditon
    bool wait_for(const State previous_condition, const State next_condition);

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
public:
    // Initialize a game
    Game(const char *server_port, const char *client_host, const char *client_port);

    // Play the game
    void run();
};