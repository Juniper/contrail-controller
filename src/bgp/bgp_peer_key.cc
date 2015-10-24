/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_peer_key.h"


#include "base/string_util.h"
#include "bgp/bgp_config.h"

using boost::asio::ip::address;
using boost::system::error_code;
using boost::uuids::nil_generator;
using std::exception;

BgpPeerKey::BgpPeerKey() {
    nil_generator nil;
    uuid = nil();
}

BgpPeerKey::BgpPeerKey(const BgpNeighborConfig *config) {
    error_code ec;
    endpoint.address(config->peer_address());
    endpoint.port(config->port());

    if (!config->uuid().empty()) {
#if defined(__EXCEPTIONS)
    try {
#endif
        uuid = StringToUuid(config->uuid());
#if defined(__EXCEPTIONS)
    } catch (exception &ex) {
        nil_generator nil;
        uuid = nil();
    }
#endif
    } else {
        boost::uuids::nil_generator nil;
        uuid = nil();
    }
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
