/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "net/community_type.h"

#include <map>

#include <boost/assign/list_of.hpp>

#include "base/string_util.h"

using std::map;
using std::string;

CommunityType::CommunityType() {
}

static const map<string, CommunityType::WellKnownCommunity> fromString = {
    {"no-advertise", CommunityType::NoAdvertise},
    {"no-export", CommunityType::NoExport},
    {"no-export-subconfed", CommunityType::NoExportSubconfed},
    {"LlgrStale", CommunityType::LlgrStale},
    {"NoLlgr", CommunityType::NoLlgr},
    {"no-reoriginate", CommunityType::NoReOriginate},
    {"accept-own", CommunityType::AcceptOwn},
    {"accept-own-nexthop", CommunityType::AcceptOwnNexthop}};

static const map<CommunityType::WellKnownCommunity, string> toString = {
    {CommunityType::NoAdvertise, "no-advertise"},
    {CommunityType::NoReOriginate, "no-reoriginate"},
    {CommunityType::NoExportSubconfed, "no-export-subconfed"},
    {CommunityType::LlgrStale, "llgr-stale"},
    {CommunityType::NoLlgr, "no-llgr"},
    {CommunityType::AcceptOwn, "accept-own"},
    {CommunityType::AcceptOwnNexthop, "accept-own-nexthop"},
    {CommunityType::NoExport, "no-export"}};

uint32_t CommunityType::CommunityFromString(
    const string &comm, boost::system::error_code *perr) {
    map<string, CommunityType::WellKnownCommunity>::const_iterator it =
        fromString.find(comm);
    if (it != fromString.end()) {
       return it->second;
    }
    size_t pos = comm.rfind(':');
    if (pos == string::npos) {
        if (perr != NULL) {
            *perr = make_error_code(boost::system::errc::invalid_argument);
        }
        return 0;
    }
    string first(comm.substr(0, pos));
    long asn = -1;
    char *endptr;
    asn = strtol(first.c_str(), &endptr, 10);
    if (asn >= 65535 || *endptr != '\0') {
        if (perr != NULL) {
            *perr = make_error_code(boost::system::errc::invalid_argument);
        }
        return 0;
    }
    string second(comm, pos + 1);
    long num = -1;
    num = strtol(second.c_str(), &endptr, 10);
    if (num >= 65535 || *endptr != '\0') {
        if (perr != NULL) {
            *perr = make_error_code(boost::system::errc::invalid_argument);
        }
        return 0;
    }
    return (asn*65536 + num);
}

const string CommunityType::CommunityToString(uint32_t comm) {
    map<CommunityType::WellKnownCommunity, string>::const_iterator it =
        toString.find((CommunityType::WellKnownCommunity)comm);
    if (it != toString.end()) {
        return it->second;
    }
    return integerToString(comm / 65536) + ":" +
        integerToString(comm % 65536);
}
