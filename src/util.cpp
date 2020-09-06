#include "util.hpp"
#include <string.h>

std::string strerror(const std::string& s) {
    const size_t buflen = 256; // Up to 256 characters, should be enough
    char buf[buflen];
    return s + ": " + strerror_r(errno, buf, buflen);
}