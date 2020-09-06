#include "game.hpp"
#include <iostream>
#include <sys/random.h>
#include <openssl/hmac.h>
#include "util.hpp"
#include <bitset>


// Convert choice to string
std::ostream& operator<<(std::ostream &os, const Choice &choice) { 
    if (choice == Choice::rock) {
        return os << "rock";
    }
    if (choice == Choice::paper) {
        return os << "paper";
    }
    return os << "scissors";
}

ChoiceReveal::ChoiceReveal(Choice choice): choice(choice) {
    // If the urandom source has been initialized, reads of up to 256 bytes
    // will always return as many bytes as requested and will not be
    // interrupted by signals
    auto n = getrandom(secret, SECRET_LENGTH, 0);
    if (n == -1) {
        throw std::runtime_error(strerror("getrandom"));
    }
    if (n < SECRET_LENGTH) {
        throw std::runtime_error("getrandom: fewer bytes than requested were returned");
    }
}

ChoiceMade::ChoiceMade(const ChoiceReveal& choice_reveal) {
    auto ctx = HMAC_CTX_new(); // Must create ctx with HMAC_CTX_new() for use in HMAC_Init_ex()
    if (ctx == nullptr) {
        throw std::runtime_error("HMAC_CTX_new");
    }
    const void* key;
    HMAC_Init_ex(ctx, choice_reveal.secret, SECRET_LENGTH, EVP_DIGEST(), nullptr);
    HMAC_Update(ctx, reinterpret_cast<const unsigned char*>(&choice_reveal.choice), sizeof(choice_reveal.choice));
    unsigned int hash_size = DIGEST_SIZE;
    HMAC_Final(ctx, hash, &hash_size);
    HMAC_CTX_free(ctx);
}

inline bool Game::check(State condition) {
    return (state & condition) == condition;
}

bool Game::wait_for(const State previous_condition, const State next_condition) { // Wait for next_condition while maintaining previous_condition
    std::unique_lock lock(state_m);
    state_cv.wait(lock, [&] {
        return !check(previous_condition) || check(next_condition);
    });
    return check(previous_condition);
}

void Game::run_server() {
    server = std::make_unique<Server>(server_port);
    server->listen();
    while (true) {
        state_off(condition_server_connected);
        auto connection = server->accept();
        state_on(condition_server_connected);
        while (true) {
            Message message;
            if (connection->recv(&message)) {
                incoming_messages.put(message);
            } else {
                break; // client disconnected - reset score?
            }
            // std::cout << "Server received message" << std::endl;
            // pass the message to incoming messages queue
        }
    }
}

void Game::state_on(State conditions) {
    {
        std::lock_guard lock(state_m);
        state |= conditions;
    }
    state_cv.notify_all(); // notify_one?
}

void Game::state_off(State conditions) {
    {
        std::lock_guard lock(state_m);
        state &= ~conditions;
    }
    state_cv.notify_all(); // notify_one?
}

void Game::run_client() {
    while (true) {
        state_off(condition_client_connected);
        try {
            client = std::make_unique<Connection>(client_host, client_port);
        } catch (const ConnectionError& e) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        state_on(condition_client_connected);

        while (true) {
            auto message = outgoing_messages.get();
            client->send(&message);
        }
    }
}

void Game::run_ui() {
    std::string s;
    while (true) {
        std::cin >> s;
        if (s == "rock") {
            choices.put(Choice::rock);
        }
        else if (s == "paper") {
            choices.put(Choice::paper);
        }
        else if (s == "scissors") {
            choices.put(Choice::scissors);
        }
    }
}

Game::Game(const char *server_port, const char *client_host, const char *client_port): server_port(server_port), client_host(client_host), client_port(client_port) {
}

void Game::run() { // Should be run only once?
    state = condition_running;
    ui_thread = std::thread(&Game::run_ui, this);
    server_thread = std::thread(&Game::run_server, this);
    client_thread = std::thread(&Game::run_client, this);
    while (true) {
        State previous_condition = condition_running;
        State next_condition = previous_condition | condition_server_connected | condition_client_connected;
        // std::cout << "Waiting for " << next_condition << '\n';
        if (!wait_for(previous_condition, next_condition)) { // wait for client and server to connect
            continue;
        }
        // Get user input
        std::cout << "Make a choice: ";
        std::cout.flush();
        choices.clear();
        auto choice = choices.get();
        auto my_choice_reveal = ChoiceReveal(choice);
        auto my_choice_made = ChoiceMade(my_choice_reveal);
        Message message;
        message.message_type = choice_made;
        message.data.choice_made = my_choice_made;
        outgoing_messages.put(message);

        // Get opponent's choice
        std::cout << "Waiting for opponent's choice" << std::endl;
        ChoiceMade opponent_choice_made;
        while (true) {
            auto msg = incoming_messages.get();
            if (msg.message_type == choice_made) {
                opponent_choice_made = msg.data.choice_made;
                break;
            }
        }

        // Reveal my choice
        message.message_type = choice_reveal;
        message.data.choice_reveal = my_choice_reveal;
        outgoing_messages.put(message);

        std::cout << "Waiting for opponent's reveal" << std::endl;
        // Wait for opponent's reveal
        ChoiceReveal opponent_choice_reveal;
        while (true) {
            auto msg = incoming_messages.get();
            if (msg.message_type == choice_reveal) {
                opponent_choice_reveal = msg.data.choice_reveal;
                break;
            }
        }

        // Check opponent's reveal for validity
        // Print opponent's choice
        auto opponent_choice_made2 = ChoiceMade(opponent_choice_reveal);
        if (!std::equal(opponent_choice_made.hash, opponent_choice_made.hash + DIGEST_SIZE, opponent_choice_made2.hash)) { // Hash invalid
            std::cout << "Opponent's hash doesn't match.\nYOU WIN!\n";
            ++wins; // wins.fetch_add(1);
        }
        else { // Hash valid
            std::cout << "Opponent's choice: " << opponent_choice_reveal.choice << '\n';
            int d = (3 + static_cast<int>(choice) - static_cast<int>(opponent_choice_reveal.choice)) % 3;
            if (d == 1) {
                std::cout << "YOU WIN!\n";
                ++wins; // wins.fetch_add(1);
            }
            else if (d == 2) {
                std::cout << "YOU LOSE!\n";
                ++losses; // losses.fetch_add(1);
            }
            else {
                std::cout << "TIE!\n";
            }
        }
        std::cout << "Score: " << wins << " - " << losses << '\n';
        // std::cout << "Score: " << wins.load() << " - " << losses.load() << '\n';
    }
}