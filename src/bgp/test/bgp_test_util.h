/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BGP__TEST_UTIL_H__
#define __BGP__TEST_UTIL_H__

#include <map>
#include <sstream>
#include <vector>

class DB;

namespace bgp_util {
void NetworkConfigGenerate(DB *db,
    const std::vector<std::string> &instance_names,
    const std::multimap<std::string, std::string> &connections =
        std::multimap<std::string, std::string>(),
    const std::vector<std::string> &networks = std::vector<std::string>(),
    const std::vector<int> &network_ids = std::vector<int>());
}

#endif
