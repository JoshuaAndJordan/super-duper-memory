#pragma once

#include <ctime>
#include <string>

namespace jordan::utils {
char getRandomChar();
std::string getRandomString(std::size_t);
std::size_t getRandomInteger();
std::time_t &proxyFetchInterval();
} // namespace jordan::utils
