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

void Game::run_server() {
    Event event;
    server = std::make_unique<Server>(server_port);
    server->listen();
    while (true) {
        auto connection = server->accept();
        event.type = server_connected;
        event_queue.put(event);

        while (true) {
            Message message;
            if (connection->recv(&message)) {
                Event event;
                event.type = message_received;
                event.data.message = message;
                event_queue.put(event);
            } else {
                break;
            }
        }
        event.type = server_disconnected;
        event_queue.put(event);
    }
}

void Game::state_on(State conditions) {
    state |= conditions;
}

void Game::state_off(State conditions) {
    state &= ~conditions;
}

void Game::run_client() {
    Event event;
    while (true) {
        try {
            client = std::make_unique<Connection>(client_host, client_port);
        } catch (const ConnectionError& e) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        event.type = client_connected;
        event_queue.put(event);

        // Detect when socket closes by reading all the time
        auto client_recv_thread = std::thread([this]() {
            struct {
                char data[256];
            } buffer;
            while (true) {
                if (!client->recv(&buffer)) {
                    break;
                }
            }
            outgoing_messages.put(std::nullopt);
        });

        for (std::optional<Message> message; message = outgoing_messages.get();) {
            try {
                client->send(&message.value());
            } catch (const BrokenPipe& e) {
                break;
            }
        }
        client_recv_thread.join();
        outgoing_messages.clear();
        event.type = client_disconnected;
        event_queue.put(event);
    }
}

Choice to_choice(const std::string& s) {
    if (s == "rock") {
        return Choice::rock;
    }
    if (s == "paper") {
        return Choice::paper;
    }
    if (s == "scissors") {
        return Choice::scissors;
    }
    return Choice::invalid;
}

// Reads user's input
void Game::run_ui() {
    std::string s;
    while (true) {
        std::cin >> s;
        if (auto choice = to_choice(s); choice != Choice::invalid) {
            Event event;
            event.type = user_choice;
            event.data.choice = choice;
            event_queue.put(event);
        }
    }
}

void Game::reveal() {
    Message message;
    message.message_type = choice_reveal;
    message.data.choice_reveal = user_choice_reveal;
    outgoing_messages.put(message);
}

Game::Game(const char *server_port, const char *client_host, const char *client_port):
    server_port(server_port),
    client_host(client_host),
    client_port(client_port) {}


void Game::run() {
    state = 0;
    ui_thread = std::thread(&Game::run_ui, this);
    server_thread = std::thread(&Game::run_server, this);
    client_thread = std::thread(&Game::run_client, this);
    std::cout << "Connecting..." << std::endl;
    while (true) {
        auto event = event_queue.get();
        if (event.type == client_connected || event.type == server_connected) {
            if (event.type == client_connected) {
                state_on(condition_client_connected);
            }
            else {
                state_on(condition_server_connected);
            }
            if (check(condition_client_connected | condition_server_connected)) {

                std::cout << "Connected.\nMake a choice: " << std::flush;
            }
        }
        else if (event.type == client_disconnected || event.type == server_disconnected) {
            if (check(condition_client_connected | condition_server_connected)) {
                std::cout << "\nDisconnected, reconnecting..." << std::endl;
            }
            state &= condition_client_connected | condition_server_connected;
            if (event.type == client_disconnected) {
                state_off(condition_client_connected);
            }
            if (event.type == client_disconnected) {
                state_off(condition_client_connected);
            }
            // Reset 
            wins = losses = 0;
        }
        else if (event.type == user_choice) {
            if (check(condition_client_connected | condition_server_connected) && !check(condition_user_choice_made)) {
                user_choice_reveal = ChoiceReveal(event.data.choice);
                user_choice_made = ChoiceMade(user_choice_reveal);
                state_on(condition_user_choice_made);

                Message message;
                message.message_type = choice_made;
                message.data.choice_made = user_choice_made;
                outgoing_messages.put(message);

                // Reveal user's choice if opponent already announced his
                if (check(condition_opponent_announced)) {
                    reveal();
                }
            }
        }
        else if (event.type == message_received) {
            if (event.data.message.message_type == MessageType::choice_made && !check(condition_opponent_announced)) {
                opponent_choice_made = event.data.message.data.choice_made;
                state_on(condition_opponent_announced);

                // Reveal user's choice
                if (check(condition_user_choice_made)) {
                    reveal();
                }
            }
            else if (event.data.message.message_type == MessageType::choice_reveal && check(condition_opponent_announced)) {
                opponent_choice_reveal = event.data.message.data.choice_reveal;
                auto opponent_choice_made2 = ChoiceMade(opponent_choice_reveal);

                // Check HMAC validity
                if (!std::equal(opponent_choice_made.hash, opponent_choice_made.hash + DIGEST_SIZE, opponent_choice_made2.hash)) {
                    // Hash invalid
                    std::cout << "Opponent's hash doesn't match.\nYOU WIN!" << std::endl;
                    ++wins;
                }
                else {
                    // Hash valid
                    std::cout << "Opponent's choice: " << opponent_choice_reveal.choice << std::endl;
                    int d = (3 + static_cast<int>(user_choice_reveal.choice) - static_cast<int>(opponent_choice_reveal.choice)) % 3;
                    if (d == 1) {
                        std::cout << "YOU WIN!" << std::endl;
                        ++wins;
                    }
                    else if (d == 2) {
                        std::cout << "YOU LOSE!" << std::endl;
                        ++losses;
                    }
                    else {
                        std::cout << "TIE!" << std::endl;
                    }
                }
                std::cout << "Score: " << wins << " - " << losses << std::endl;

                // Next round
                state = condition_client_connected | condition_server_connected;
                std::cout << "Make a choice: " << std::flush;
            }
        }
    }
}
