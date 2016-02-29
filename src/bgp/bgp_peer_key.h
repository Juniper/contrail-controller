/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_PEER_KEY_H_
#define SRC_BGP_BGP_PEER_KEY_H_

#include <boost/asio/ip/tcp.hpp>
#include <boost/uuid/uuid.hpp>

class BgpNeighborConfig;

struct BgpPeerKey {
    BgpPeerKey();
    explicit BgpPeerKey(const BgpNeighborConfig *config);

    bool operator<(const BgpPeerKey &rhs) const;
    bool operator>(const BgpPeerKey &rhs) const;
    bool operator==(const BgpPeerKey &rhs) const;
    bool operator!=(const BgpPeerKey &rhs) const;

    uint32_t address() const { return endpoint.address().to_v4().to_ulong(); }

    boost::asio::ip::tcp::endpoint endpoint;
    boost::uuids::uuid uuid;
};


#endif  // SRC_BGP_BGP_PEER_KEY_H_
