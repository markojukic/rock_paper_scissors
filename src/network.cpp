#include "network.hpp"
#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include <cstring>
#include "util.hpp"


ConnectionError::ConnectionError(): std::runtime_error("failed to connect") {
};


BrokenPipe::BrokenPipe(): std::runtime_error("broken pipe") {
};


void init(const char* host, const char* port, int& socket_fd, sockaddr_storage& addr) {
    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;        // Use IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;    // TCP

    if (host == nullptr) { // For server only
        // If the AI_PASSIVE flag is specified in hints.ai_flags, and node
        // is NULL, then the returned socket addresses will be suitable for
        // binding a socket that will accept  connections.
        hints.ai_flags = AI_PASSIVE;    // Use my IP
    }

    addrinfo* result;
    if (int rv = getaddrinfo(host, port, &hints, &result); rv != 0) {
        throw std::runtime_error(std::string("getaddrinfo: ") + gai_strerror(rv));
    }

    addrinfo *p;
    for (p = result; p != nullptr; p = p->ai_next) {
        if (socket_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol); socket_fd == -1) {
            std::cerr << strerror("client: socket") << '\n';
            continue;
        }
        if (host == nullptr) { // For server only
            // Allows other sockets to bind() to this port, unless there is
            // an active listening socket bound to the port already. This
            // gets around those “Address already in use” error messages
            // when you try to restart your server after a crash.
            int yes = 1;
            if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
                throw std::runtime_error(strerror("setsockopt"));
            }
            if (bind(socket_fd, p->ai_addr, p->ai_addrlen) == -1) {
                if (close(socket_fd) == -1) {
                    std::cerr << strerror("close") << '\n';
                }
                std::cerr << strerror("server: bind") << '\n';
                continue;
            }
        }
        else { // For client only
            if (connect(socket_fd, p->ai_addr, p->ai_addrlen) == -1) {
                if (close(socket_fd) == -1) {
                    std::cerr << strerror("close") << '\n';
                }
                // std::cerr << strerror("client: connect") << '\n';
                continue;
            }
        }
        break;
    }

    if (p == nullptr) {
        throw ConnectionError();
    }

    addr = {};
    std::memcpy(&addr, p->ai_addr, p->ai_addrlen);

    freeaddrinfo(result);
}

Connection::Connection(const char* host, const char* port) {
    init(host, port, socket_fd, addr);
}

Connection::Connection(const int socket_fd, const sockaddr_storage& addr): socket_fd(socket_fd), addr(addr) {
}

Connection::~Connection() {
    if (close(socket_fd) == -1) {
        std::cerr << strerror("close") << '\n';
    }
}

void Connection::send(const char* buf, const size_t len) {
    size_t sent = 0; // Bytes sent counter
    while(sent < len) {
        if (int n = ::send(socket_fd, buf+sent, len-sent, MSG_NOSIGNAL); n == -1) {
            if (errno == EPIPE) {
                throw BrokenPipe();
            }
            throw std::runtime_error(strerror("send"));
        } else {
            sent += n;
        }
    }
}

Server::Server(const char* port, int backlog): backlog(backlog) {
    init(nullptr, port, socket_fd, addr);
}

Server::~Server() {
    if (close(socket_fd) == -1) {
        std::cerr << strerror("close") << '\n';
    }
}

void Server::listen() {
    if (::listen(socket_fd, backlog) == -1) {
        throw std::runtime_error(strerror("listen"));
    }
}

std::unique_ptr<Connection> Server::accept() {
    sockaddr_storage peer_addr;
    socklen_t peer_addr_len = sizeof(sockaddr_storage); // Length of peer address
    int peer_socket_fd = ::accept(socket_fd, (sockaddr*)&peer_addr, &peer_addr_len); // New socket's descriptor
    if (peer_socket_fd == -1) {
        throw std::runtime_error(strerror("accept"));
    }
    return std::make_unique<Connection>(peer_socket_fd, peer_addr);
}
