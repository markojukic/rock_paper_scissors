#pragma once

#include <netdb.h>
#include <stdexcept>
#include <memory>
#include "util.hpp"


// When a connection to server fails
class ConnectionError: public std::runtime_error {
public:
    ConnectionError();
};


// Initialize a TCP server/connection
//  - if host == nullptr, initializes a TCP server that listens on localhost:port,
//  - if host != nullptr, initializes a TCP connection to host:port
void init(const char* host, const char* port, int& socket_fd, sockaddr_storage& addr);


// TCP connection
class Connection {
    int socket_fd;              // Socket for communication to server
    sockaddr_storage addr = {}; // Server address
public:
    // Construct a connection from socket_fd and addr
    Connection(const int socket_fd, const sockaddr_storage& addr);

    // Initialize a TCP connection to host:port
    Connection(const char* host, const char* port);

    // Close socket
    ~Connection();

    // Send data out over a socket
    void send(const char* buf, const size_t len);

    // Receive raw object - should only be used for types that have a consistent
    // memory representation across different platforms.
    // Returns:
    //  - true if the message was successfully read
    //  - false if the remote side has closed the connection
    template<typename T>
    bool recv(T* data) { // returns false 
        auto n = ::recv(socket_fd, data, sizeof(T), MSG_WAITALL);
        if (n == -1) {
            throw std::runtime_error(strerror("recv"));
        }
        return n != 0;
    }

    // Receive raw object - should only be used for types that have a consistent
    // memory representation across different platforms
    template<typename T>
    void send(const T* data) {
        send((const char*)data, sizeof(T));
    }
};


// TCP server
class Server {
    int socket_fd;              // Socket that accepts connections
    sockaddr_storage addr = {}; // Server address
    const int backlog;          // Limit the number of outstanding connections in the socket's listen queue
public:
    // Initialize a TCP server
    Server(const char* port, int backlog = 10);

    // Close socket
    ~Server();

    // Start listening for incoming connections
    void listen();

    // Accept one incoming connection
    std::unique_ptr<Connection> accept();
};
