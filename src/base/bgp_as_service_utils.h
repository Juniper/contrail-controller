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
    static const int kMaxSessions;

    static void GetDerivedBgpaasServicePortRange(
        const uint16_t port_range_start, const uint16_t port_range_end,
        const uint32_t max_session, uint16_t  &bgpaas_der_port_start,
        uint16_t  &bgpaas_der_port_end);
    static uint32_t EncodeBgpaasServicePort(const uint32_t sport,
        const size_t index, const uint16_t port_range_start,
        const uint16_t port_range_end,
        const uint32_t max_session = kMaxSessions);
    static BgpAsServicePortIndexPair DecodeBgpaasServicePort(
        const uint32_t sport, const uint16_t port_range_start,
        const uint16_t port_range_end,
        const uint32_t max_session = kMaxSessions);
};

#endif // ctrlplane_bgp_service_utils_h
