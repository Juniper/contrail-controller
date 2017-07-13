/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <base/bgp_as_service_utils.h>
/*
 *  This utils apis will be used by BGP As A Service feature to map 
 *  between the original service port allocated for the given BgpAsAService
 *  session and the derived service port for each VMI who is sharing the same
 *  session
 *  GetDerivedBgpaasServicePortRange - From the given config port range and max
 *  session, derive the internal port range 
 *  Port range and maximum number of VMIs who is sharing the given BgpAsAService
 *  session will be given by config 
 *  EncodeBgpaasServicePort - For the given original service port and
 *  index, derive the service port which will be used by the corresponding
 *  flow which is associated with the corresponding VMI.
 *  DecodeBgpaasServicePort - For the given service port, decode the 
 *  original service port and the index for the given port.
 */
using namespace std;
BgpaasUtils::BgpAsServicePortIndexPair BgpaasUtils::DecodeBgpaasServicePort(
                                            const uint32_t sport,
                                            const uint16_t port_range_start,
                                            const uint16_t port_range_end) {
    size_t   index = 0;
    uint32_t original_sport;

    if ((sport >= port_range_start) &&
        (sport <= port_range_end)) {
        return std::make_pair(sport, index);
    }

    uint16_t port_range = port_range_end - port_range_start + 1;
    original_sport = (((sport - port_range_end) % port_range) - 1)
                                        + port_range_start;
    index = ((sport - port_range_end) / port_range) + 1;

    return std::make_pair(original_sport, index);
}

uint32_t BgpaasUtils::EncodeBgpaasServicePort(const uint32_t sport,
                                            const size_t   index,
                                            const uint16_t port_range_start,
                                            const uint16_t port_range_end) {
    if (!index) {
        return sport;
    }

    uint16_t port_range = port_range_end - port_range_start + 1;
    return (port_range_start + (port_range * index)
                             + (sport - port_range_start));
}
