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

const int BGPaaSUtils::kMaxSessions = 4;

void BGPaaSUtils::GetDerivedBgpaasServicePortRange(
                        const uint16_t port_range_start,
                        const uint16_t port_range_end,
                        const uint32_t max_session,
                        uint16_t  &bgpaas_der_port_start,
                        uint16_t  &bgpaas_der_port_end) {
    uint32_t total_der_ports = (port_range_end - port_range_start) *
                                 max_session;
    if ( (total_der_ports + port_range_end + 1) > USHRT_MAX) {
        bgpaas_der_port_start = bgpaas_der_port_start - total_der_ports;
        bgpaas_der_port_end = port_range_start - 1;
    } else {
        bgpaas_der_port_start = port_range_end + 1;
        bgpaas_der_port_end = bgpaas_der_port_start + total_der_ports;
    }
}

BGPaaSUtils::BgpAsServicePortIndexPair BGPaaSUtils::DecodeBgpaasServicePort(
                                            const uint32_t sport,
                                            const uint16_t port_range_start,
                                            const uint16_t port_range_end,
                                            const uint32_t max_session) {
    size_t   index = 0;
    uint32_t original_sport;

    if (!port_range_start || !port_range_end) {
        return std::make_pair(sport, index);
    }

    if ((sport >= port_range_start) &&
        (sport <= port_range_end)) {
        return std::make_pair(sport, index);
    }

    uint16_t bgpaas_der_port_start = 0;
    uint16_t bgpaas_der_port_end = 0;
    GetDerivedBgpaasServicePortRange(port_range_start,
                                     port_range_end,
                                     max_session,
                                     bgpaas_der_port_start,
                                     bgpaas_der_port_end);

    original_sport = ((sport - bgpaas_der_port_start) / (max_session - 1))
                      + port_range_start;
    index = ((sport - bgpaas_der_port_start) % (max_session - 1)) + 1;

    return std::make_pair(original_sport, index);
}

uint32_t BGPaaSUtils::EncodeBgpaasServicePort(const uint32_t sport,
                                            const size_t   index,
                                            const uint16_t port_range_start,
                                            const uint16_t port_range_end,
                                            const uint32_t max_session) {
    if (!index || !port_range_start || !port_range_end) {
        return sport;
    }

    uint16_t bgpaas_der_port_start = 0;
    uint16_t bgpaas_der_port_end = 0;
    GetDerivedBgpaasServicePortRange(port_range_start,
                                     port_range_end,
                                     max_session,
                                     bgpaas_der_port_start,
                                     bgpaas_der_port_end);

    return (bgpaas_der_port_start + ((sport - port_range_start)
                                     * (max_session - 1))
                                  + (index - 1));
}
