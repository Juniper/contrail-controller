/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BGP__TEST_UTIL_H__
#define __BGP__TEST_UTIL_H__

#include <map>
#include <sstream>
#include <vector>

namespace bgp_util {
std::string NetworkConfigGenerate(
                const std::vector<std::string> &instance_names,
                const std::multimap<std::string, std::string> &connections);
}

#endif
