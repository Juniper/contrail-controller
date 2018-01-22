/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */
#ifndef BASE_REGEX_H_
#define BASE_REGEX_H_

#include <boost/regex.hpp>
#include <string>

namespace contrail {

class regex : public boost::regex {
public:
    regex() : boost::regex() {
    }

    explicit regex(const std::string &pattern,
                   boost::regex_constants::syntax_option_type flags =
                         boost::regex_constants::match_default |
                         boost::regex_constants::no_except) :
                     boost::regex(pattern, flags) {
    }
};

static inline bool regex_search(const std::string &input, const regex &regex) {
    return !regex.status() && boost::regex_search(input, regex);
}

static inline bool regex_search(const std::string &input,
                                boost::smatch &match, const regex &regex) {
    return !regex.status() && boost::regex_search(input, match, regex);
}

static inline bool regex_match(const std::string &input, const regex &regex) {
    return !regex.status() && boost::regex_match(input, regex);
}

static inline bool regex_match(const std::string &input, boost::smatch &match,
                               const regex &regex) {
    return !regex.status() && boost::regex_match(input, match, regex);
}

static inline bool regex_match(const char *input, boost::cmatch &match,
                               const regex &regex) {
    return !regex.status() && boost::regex_match(input, match, regex);
}

static inline bool regex_search(std::string::const_iterator &begin,
        std::string::const_iterator &end,
        boost::match_results<std::string::const_iterator> &results,
        const regex &regex, boost::regex_constants::match_flag_type flags) {
    return !regex.status() && boost::regex_search(begin, end, results, regex,
                                                  flags);
}

static inline const std::string regex_replace(const std::string &input,
        const regex &regex, const char* format,
        boost::regex_constants::match_flag_type flags) {
    return !regex.status() ? boost::regex_replace(input, regex, format, flags) :
                             input;
}

}  // namespace contrail

#endif  // BASE_REGEX_H_
