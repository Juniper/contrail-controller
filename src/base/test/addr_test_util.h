/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BASE__ADDR_TEST_UTIL_H__
#define __BASE__ADDR_TEST_UTIL_H__

#include "bgp/l3vpn/inetvpn_address.h"
#include "bgp/inet/inet_route.h"

namespace task_util {

Ip4Prefix Ip4PrefixIncrement(Ip4Prefix prefix, int incr = 1);
InetVpnPrefix InetVpnPrefixIncrement(InetVpnPrefix prefix, int incr = 1);

}

#endif // __ADDR_TEST_UTIL_H__

