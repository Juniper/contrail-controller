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
 *  From the given range, the dervied port will be defined as follows, 
 *   Ex : User Configured Range : 50000 - 50512 
 *        Session:1 : config allocates : 50000 for VMI1 
 *        Sharing the session-1 for VMI2: allocates 50513
 *        Sharing the session-1 for VMI3: allocates 51026 
 *
 *        Session:2 : config allocates : 50001 for VMI4 
 *        Sharing the session-2 for VMI5: allocates 50514 
 *        Sharing the session-2 for VMI6: allocates 51027
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

    /*
     *  From the given derived port, the original port will be
     *  calculated based on posisiton in the the given port range in
     *  addition to the port range start.
     *  Ex : given port is 51026 and the range is 50000-50512
     *       original port will be 50000 and index will be 2 
     */
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
    /*
     *  From the given original port and index, the derived port will be
     *  calculated based on selection of derived range which is the multiple
     *  of given port range by the given index in addition to the position of 
     *  the original port with respect to port range start.
     *  Ex : given port, index : 50000, 2 and the range is 50000-50512
     *       derived port port will be 51026
     */
    return (port_range_start + (port_range * index)
                             + (sport - port_range_start));
}
