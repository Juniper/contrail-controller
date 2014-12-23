/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef BASE_STRING_UTIL_H__
#define BASE_STRING_UTIL_H__

#include <string>
#include <sstream>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

// Writes a number into a string
template <typename NumberType>
static inline const std::string integerToString(const NumberType &num) {
    std::stringstream ss;
    ss << num;
    return ss.str();
}

// int8_t must be handled specially because std::stringstream sees
// int8_t as a text type instead of an integer type
template <>
inline const std::string integerToString<>(const int8_t &num) {
    std::stringstream ss;
    ss << (int16_t)num;
    return ss.str();
}

// uint8_t must be handled specially because std::stringstream sees
// uint8_t as a text type instead of an integer type
template <>
inline const std::string integerToString<>(const uint8_t &num) {
    std::stringstream ss;
    ss << (uint16_t)num;
    return ss.str();
}

// Writes a number into a string as hex
template <typename NumberType>
static inline const std::string integerToHexString(const NumberType &num) {
    std::stringstream ss;
    ss << std::hex << num;
    return ss.str();
}

// signed int8 must be handled specially because std::stringstream sees
// int8_t as a text type instead of an integer type
template <>
inline const std::string integerToHexString<>(const int8_t &num) {
    std::stringstream ss;
    ss << std::hex << (int16_t)num;
    return ss.str();
}

// unsigned int8 must be handled specially because std::stringstream sees
// u_int8_t as a text type instead of an integer type
template <>
inline const std::string integerToHexString<>(const uint8_t &num) {
    std::stringstream ss;
    ss << std::hex << (uint16_t)num;
    return ss.str();
}

// Converts string into a number
template <typename NumberType>
inline bool stringToInteger(const std::string& str, NumberType &num) {
    char *endptr;
    num = strtoul(str.c_str(), &endptr, 10);
    return endptr[0] == '\0';
}

template <typename NumberType>
inline bool stringToLongLong(const std::string& str, NumberType &num) {
    char *endptr;
    num = strtoull(str.c_str(), &endptr, 10);
    return endptr[0] == '\0';
}

template <>
inline bool stringToInteger<>(const std::string& str, int64_t &num) {
    return stringToLongLong(str, num);
}

template <>
inline bool stringToInteger<>(const std::string& str, uint64_t &num) {
    return stringToLongLong(str, num);
}

template <>
inline bool stringToInteger<>(const std::string& str, double &num) {
    char *endptr;
    num = strtod(str.c_str(), &endptr);
    return endptr[0] == '\0';
}

//
// Split a the initial part of the string based on 'seperator' characters
// and return the resultant list of tokens after converting each token into
// NumberType elements
//
template<typename NumberType>
static inline bool stringToIntegerList(std::string input,
                                       std::string seperator,
                                       std::vector<NumberType> &entries) {
    std::vector<std::string> tokens;

    boost::split(tokens, input, boost::is_any_of(seperator),
                 boost::token_compress_on);

    if (!tokens.size()) {
        return false;
    }

    std::vector<std::string>::iterator iter;

    for (iter = tokens.begin(); iter != tokens.end(); iter++) {
        std::stringstream ss(*iter);
        NumberType value;
        ss >> value;

        //
        // Bail if there is an error during the conversion.
        //
        if (ss.fail()) {
            return false;
        }
        entries.push_back(value);
    }

    return true;
}

static inline std::string UuidToString(const boost::uuids::uuid &id)
{
    std::stringstream uuidstring;
    uuidstring << id;
    return uuidstring.str();
}

static inline boost::uuids::uuid StringToUuid(const std::string &str)
{
    boost::uuids::uuid u = boost::uuids::nil_uuid();
    std::stringstream uuidstring(str);
    uuidstring >> u;
    return u;
}

#endif  // BASE_STRING_UTIL_H__
