/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <base/bgp_as_service_utils.h>

using namespace std;
void BgpaasUtils::GetDerivedBgpaasServicePort(
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

BgpaasUtils::BgpAsServicePortIndexPair BgpaasUtils::DecodeBgpaasServicePort(
                                            const uint32_t sport,
                                            const uint32_t max_session,
                                            const uint16_t port_range_start,
                                            const uint16_t port_range_end) {
    size_t   index = 0;
    uint32_t original_sport;

    if ((sport >= port_range_start) &&
        (sport <= port_range_end)) {
        return std::make_pair(sport, index);
    }

    uint16_t bgpaas_der_port_start = 0;
    uint16_t bgpaas_der_port_end = 0;
    GetDerivedBgpaasServicePort(port_range_start,
                                                   port_range_end,
                                                   max_session,
                                                   bgpaas_der_port_start,
                                                   bgpaas_der_port_end);

    original_sport = ((sport - bgpaas_der_port_start) / (max_session - 1))
                      + port_range_start;
    index = ((sport - bgpaas_der_port_start) % (max_session -1)) + 1;

    return std::make_pair(original_sport, index);
}

uint32_t BgpaasUtils::EncodeBgpaasServicePort(const uint32_t sport,
                                            const size_t   index,
                                            const uint32_t max_session,
                                            const uint16_t port_range_start,
                                            const uint16_t port_range_end) {
    if (!index) {
        return sport;
    }

    uint16_t bgpaas_der_port_start = 0;
    uint16_t bgpaas_der_port_end = 0;
    GetDerivedBgpaasServicePort(port_range_start,
                                               port_range_end,
                                               max_session,
                                               bgpaas_der_port_start,
                                               bgpaas_der_port_end);

    return (bgpaas_der_port_start + ((sport - port_range_start)
                                     * (max_session - 1))
                                  + (index - 1));
}
