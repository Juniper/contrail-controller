/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */
#ifndef BASE_REGEX_H_
#define BASE_REGEX_H_

#include <boost/regex.hpp>
#include <string>

class Regex : public boost::regex {
public:
    Regex(const std::string &pattern,
          boost::regex_constants::syntax_option_type flags =
                boost::regex_constants::match_default |
                boost::regex_constants::no_except) :
            boost::regex(pattern, flags) {
    }

    bool regex_search(const std::string &input) const {
        return !status() ? boost::regex_search(input, *this) : false;
    }

    bool regex_search(const std::string &input, boost::smatch &match) const {
        return !status() ? boost::regex_search(input, match, *this) : false;
    }

    bool regex_match(const std::string &input) const {
        return !status() ? boost::regex_match(input, *this) : false;
    }

    bool regex_match(const std::string &input, boost::smatch &match) const {
        return !status() ? boost::regex_match(input, match, *this) : false;
    }

    bool regex_match(const char *input, boost::cmatch &match) const {
        return !status() ? boost::regex_match(input, match, *this) : false;
    }
};

#endif  // BASE_REGEX_H_
