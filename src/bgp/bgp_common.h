/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_bgp_common_h
#define ctrlplane_bgp_common_h
#include <boost/intrusive_ptr.hpp>
typedef uint16_t as_t;
typedef uint32_t as4_t;

class RoutingPolicy;
typedef boost::intrusive_ptr<RoutingPolicy> RoutingPolicyPtr;

#endif
