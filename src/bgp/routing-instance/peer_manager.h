/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_ROUTING_INSTANCE_PEER_MANAGER_H_
#define SRC_BGP_ROUTING_INSTANCE_PEER_MANAGER_H_

#include <boost/asio/ip/tcp.hpp>

#include <map>
#include <string>
#include <vector>

#include "bgp/bgp_peer_key.h"
#include "bgp/ipeer.h"
#include "io/tcp_session.h"

class BgpPeer;
class BgpServer;
class RoutingInstance;
class BgpNeighborResp;
class BgpSandeshContext;

class PeerManager {
public:
    typedef std::multimap<BgpPeerKey, BgpPeer *> BgpPeerKeyMap;
    typedef std::map<std::string, BgpPeer *> BgpPeerNameMap;

    explicit PeerManager(RoutingInstance *instance) : instance_(instance) { }
    virtual ~PeerManager() { }

    virtual BgpPeer *PeerFind(std::string address) const;
    virtual BgpPeer *PeerLookup(std::string name) const;
    virtual BgpPeer *PeerLookup(TcpSession::Endpoint remote_endpoint) const;
    virtual BgpPeer *PeerLocate(BgpServer *server,
                                const BgpNeighborConfig *config);
    void PeerResurrect(std::string name);
    BgpPeer *TriggerPeerDeletion(const BgpNeighborConfig *config);
    virtual void DestroyIPeer(IPeer *ipeer);
    void ClearAllPeers();

    const BgpPeer *NextPeer(const BgpPeerKey &key) const;
    size_t GetNeighborCount(std::string up_or_down);

    size_t size() { return peers_by_key_.size(); }
    const std::string &name() const;
    const RoutingInstance *instance() const { return instance_; }
    RoutingInstance *instance() { return instance_; }
    BgpServer *server() const;

    const BgpPeerKeyMap &peer_map() const { return peers_by_key_; }
    BgpPeerKeyMap *peer_map_mutable() { return &peers_by_key_; }

private:
    friend class PeerManagerTest;
    friend class BgpServerTest;

    void InsertPeerByKey(BgpPeerKey key, BgpPeer *peer);
    void RemovePeerByKey(BgpPeerKey key, BgpPeer *peer);
    void InsertPeerByName(const std::string name, BgpPeer *peer);
    void RemovePeerByName(const std::string name, BgpPeer *peer);

    BgpPeerKeyMap peers_by_key_;
    BgpPeerNameMap peers_by_name_;
    RoutingInstance *instance_;
};

#endif  // SRC_BGP_ROUTING_INSTANCE_PEER_MANAGER_H_
