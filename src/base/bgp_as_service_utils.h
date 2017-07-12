/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_bgp_as_service_utils_h
#define ctrlplane_bgp_as_service_utils_h
#include <stdlib.h>
#include <limits.h>
#include <inttypes.h>
#include <utility>

class BGPaaSUtils {
public:
    typedef std::pair<uint32_t, size_t> BgpAsServicePortIndexPair;
    static uint32_t EncodeBgpaasServicePort(const uint32_t sport,
                                            const size_t   index,
                                            const uint16_t port_range_start,
                                            const uint16_t port_range_end);
    static BgpAsServicePortIndexPair DecodeBgpaasServicePort(
                                            const uint32_t sport,
                                            const uint16_t port_range_start,
                                            const uint16_t port_range_end);
};

#endif // ctrlplane_bgp_service_utils_h
