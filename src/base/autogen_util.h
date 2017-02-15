/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#ifndef AUTOGEN_UTIL_H_
#define AUTOGEN_UTIL_H_

#include <stdint.h>
#include <time.h>

#include <sstream>
#include <string>

#include <boost/algorithm/string/trim.hpp>
#include <pugixml/pugixml.hpp>
#include "rapidjson/rapidjson.h"
#include "rapidjson/document.h"

using namespace contrail_rapidjson;
using namespace pugi;
using namespace std;

#include "base/compiler.h"
#if defined(__GNUC__) && (__GCC_HAS_PRAGMA > 0)
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

namespace autogen {

// Json Parse routines

static bool ParseString(const contrail_rapidjson::Value &node, std::string *s) {
    if (node.IsString()) {
        *s = node.GetString();
        return true;
    }

    std::stringstream ss;
    switch (node.GetType()) {
    case contrail_rapidjson::kNullType:
        *s = "null";
        break;
    case contrail_rapidjson::kTrueType:
        *s = "true";
        break;
    case contrail_rapidjson::kFalseType:
        *s = "false";
        break;
    case contrail_rapidjson::kStringType:
        *s = node.GetString();
        break;
    case contrail_rapidjson::kNumberType:
        if (node.IsUint())
            ss << node.GetUint();
        else if (node.IsInt())
            ss << node.GetInt();
        else if (node.IsUint64())
            ss << node.GetUint64();
        else if (node.IsInt64())
            ss << node.GetInt64();
        else if (node.IsDouble())
            ss << node.GetDouble();
        *s = ss.str();
        break;
    case contrail_rapidjson::kObjectType:
        return false;
    case contrail_rapidjson::kArrayType:
        return false;
    }
    return true;
}

static bool ParseInteger(const char *nptr, int *valuep) {
    char *endp;
    *valuep = strtoul(nptr, &endp, 10);
    while (isspace(*endp))
        endp++;
    return (endp[0] == '\0');
}

static bool ParseUnsignedLong(const char *nptr, uint64_t *valuep) {
    char *endp;
    *valuep = strtoull(nptr, &endp, 10);
    while (isspace(*endp))
        endp++;
    return (endp[0] == '\0');
}

static bool ParseBoolean(const char *bptr, bool *valuep) {
    if (strcmp(bptr, "true") ==0)
        *valuep = true;
    else
        *valuep = false;
    return true;
}

static bool ParseInteger(const pugi::xml_attribute &attr, int *valuep) {
    return ParseInteger(attr.value(), valuep);
}

static bool ParseUnsignedLong(const pugi::xml_attribute &attr,
                              uint64_t *valuep) {
    return ParseUnsignedLong(attr.value(), valuep);
}

static bool ParseBoolean(const pugi::xml_attribute &attr, bool *valuep) {
    return ParseBoolean(attr.value(), valuep);
}

static bool ParseInteger(const pugi::xml_node &node, int *valuep) {
    return ParseInteger(node.child_value(), valuep);
}

static bool ParseUnsignedLong(const pugi::xml_node &node, uint64_t *valuep) {
    return ParseUnsignedLong(node.child_value(), valuep);
}

static bool ParseBoolean(const pugi::xml_node &node, bool *valuep) {
    return ParseBoolean(node.child_value(), valuep);
}

static bool ParseDateTime(const pugi::xml_node &node, time_t *valuep) {
    string value(node.child_value());
    boost::trim(value);
    struct tm tm;
    char *endp;
    memset(&tm, 0, sizeof(tm));
    if (value.size() == 0) return true;
    endp = strptime(value.c_str(), "%FT%T", &tm);
    if (!endp) return false;
    *valuep = timegm(&tm);
    return true;
}
static bool ParseTime(const pugi::xml_node &node, time_t *valuep) {
    string value(node.child_value());
    boost::trim(value);
    struct tm tm;
    char *endp;
    endp = strptime(value.c_str(), "%T", &tm);
    if (!endp) return false;
    *valuep = timegm(&tm);
    return true;
}
static std::string FormatDateTime(const time_t *valuep) {
    struct tm tm;
    char result[100];
    gmtime_r(valuep, &tm);
    strftime(result, sizeof(result), "%FT%T", &tm);
    return std::string(result);
}
static std::string FormatTime(const time_t *valuep) {
    struct tm tm;
    char result[100];
    gmtime_r(valuep, &tm);
    strftime(result, sizeof(result), "%T", &tm);
    return std::string(result);
}

// Json Parse routines
static bool ParseInteger(const contrail_rapidjson::Value &node, int *valuep) {
    if (node.IsString())
        return ParseInteger(node.GetString(), valuep);
    if (!node.IsInt())
        return false;
    *valuep = node.GetInt();
    return true;
}

static bool ParseUnsignedLong(const contrail_rapidjson::Value &node, uint64_t *valuep) {
    if (node.IsString())
        return ParseUnsignedLong(node.GetString(), valuep);
    if (!node.IsUint64())
        return false;
    *valuep = node.GetUint64();
    return true;
}

static bool ParseBoolean(const contrail_rapidjson::Value &node, bool *valuep) {
    if (node.IsString())
        return ParseBoolean(node.GetString(), valuep);
    if (!node.IsBool())
        return false;
    *valuep = node.GetBool();
    return true;
}

static bool ParseDateTime(const contrail_rapidjson::Value &node, time_t *valuep) {
    if (!node.IsString())
        return false;
    string value(node.GetString());
    boost::trim(value);
    struct tm tm;
    char *endp;
    memset(&tm, 0, sizeof(tm));
    if (value.size() == 0) return true;
    endp = strptime(value.c_str(), "%FT%T", &tm);
    if (!endp) return false;
    *valuep = timegm(&tm);
    return true;
}

static bool ParseTime(const contrail_rapidjson::Value &node, time_t *valuep) {
    if (!node.IsString())
        return false;
    string value(node.GetString());
    boost::trim(value);
    struct tm tm;
    char *endp;
    endp = strptime(value.c_str(), "%T", &tm);
    if (!endp) return false;
    *valuep = timegm(&tm);
    return true;
}

}  // namespace autogen

#endif  // AUTOGEN_UTIL_H_
