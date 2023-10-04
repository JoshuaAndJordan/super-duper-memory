// Copyright (C) 2023 Joshua and Jordan Ogunyinka
#pragma once

#include <string>

namespace keep_my_journal::utils {

std::string base64Encode(std::basic_string<unsigned char> const &binary_data);
std::string base64Encode(std::string const &binary_data);
std::string base64Decode(std::string const &asc_data);
std::basic_string<unsigned char> hmac256Encode(std::string const &data,
                                               std::string const &key);
} // namespace keep_my_journal::utils
