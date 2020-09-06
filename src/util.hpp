#pragma once

#include <string>

// Convert errno code to std::string, thread safe
std::string strerror(const std::string& s);