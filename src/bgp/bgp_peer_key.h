/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __ctrlplane__bgp_peer_key__
#define __ctrlplane__bgp_peer_key__

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

    boost::asio::ip::tcp::endpoint endpoint;
    boost::uuids::uuid uuid;
};


#endif /* defined(__ctrlplane__bgp_peer_key__) */
