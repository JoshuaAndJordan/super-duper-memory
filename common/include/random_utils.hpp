// Copyright (C) 2023 Joshua and Jordan Ogunyinka
#pragma once

#include <ctime>
#include <string>

namespace keep_my_journal::utils {
char getRandomChar();
std::string getRandomString(std::size_t);
std::size_t getRandomInteger();
std::time_t &proxyFetchInterval();
} // namespace keep_my_journal::utils
