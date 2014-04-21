/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp_peer_key.h"

#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/string_generator.hpp>

#include "bgp/bgp_config.h"

using namespace boost::asio::ip;
using boost::system::error_code;

BgpPeerKey::BgpPeerKey() {
    boost::uuids::nil_generator nil;
    uuid = nil();
}

BgpPeerKey::BgpPeerKey(const BgpNeighborConfig *config) {
    const autogen::BgpRouterParams &peer = config->peer_config();
    error_code ec;
    endpoint.address(address::from_string(peer.address, ec));
    endpoint.port(peer.port);
#if defined(__EXCEPTIONS)
    try {
#endif
        boost::uuids::string_generator gen;
        uuid = gen(config->uuid());
#if defined(__EXCEPTIONS)
    } catch (std::exception &ex) {
        boost::uuids::nil_generator nil;
        uuid = nil();
    }
#endif
}

bool BgpPeerKey::operator<(const BgpPeerKey &rhs) const {
    if (endpoint != rhs.endpoint) {
        return (endpoint < rhs.endpoint);
    }
    return uuid < rhs.uuid;
}

bool BgpPeerKey::operator>(const BgpPeerKey &rhs) const {
    if (endpoint != rhs.endpoint) {
        return (endpoint > rhs.endpoint);
    }
    return uuid > rhs.uuid;
}

bool BgpPeerKey::operator==(const BgpPeerKey &rhs) const {
    return (endpoint == rhs.endpoint && uuid == rhs.uuid);
}

bool BgpPeerKey::operator!=(const BgpPeerKey &rhs) const {
    return (endpoint != rhs.endpoint || uuid != rhs.uuid);
}
