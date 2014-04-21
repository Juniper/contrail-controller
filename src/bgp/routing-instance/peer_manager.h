/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef routing_instance_peer_manager_h
#define routing_instance_peer_manager_h

#include <map>

#include <boost/asio/ip/tcp.hpp>

#include "bgp/bgp_peer_key.h"
#include "bgp/ipeer.h"
#include "bgp/routing-instance/routing_instance.h"

class BgpPeer;
class BgpServer;
class RoutingInstance;
class BgpNeighborResp;

class PeerManager {
public:
    typedef std::multimap<BgpPeerKey, BgpPeer *> BgpPeerKeyMap;
    typedef std::map<std::string, BgpPeer *> BgpPeerNameMap;

    PeerManager(RoutingInstance *instance) : instance_(instance) { }
    virtual ~PeerManager() { }

    virtual BgpPeer *PeerFind(std::string address);
    virtual BgpPeer *PeerLookup(std::string name);
    virtual BgpPeer *PeerLookup(boost::asio::ip::tcp::endpoint remote_endpoint);
    virtual BgpPeer *PeerLocate(BgpServer *server,
                                const BgpNeighborConfig *config);
    void PeerResurrect(std::string name);
    void TriggerPeerDeletion(const BgpNeighborConfig *config);
    virtual void DestroyIPeer(IPeer *ipeer);

    virtual BgpPeer *NextPeer(BgpPeerKey &key);

    void FillBgpNeighborInfo(std::vector<BgpNeighborResp> &nbr_list,
                             std::string peer);


    size_t GetNeighborCount(std::string up_or_down);

    size_t size() { return peers_by_key_.size(); }
    const std::string &name() const { return instance_->name(); }
    const RoutingInstance *instance() const { return instance_; }
    RoutingInstance *instance() { return instance_; }
    BgpServer *server() { return instance_->server(); }

    const BgpPeerKeyMap &peer_map() const { return peers_by_key_; }
    BgpPeerKeyMap *peer_map_mutable() { return &peers_by_key_; }

private:
    friend class PeerManagerTest;

    void InsertPeerByKey(BgpPeerKey key, BgpPeer *peer);
    void RemovePeerByKey(BgpPeerKey key, BgpPeer *peer);
    void InsertPeerByName(const std::string name, BgpPeer *peer);
    void RemovePeerByName(const std::string name, BgpPeer *peer);

    BgpPeerKeyMap peers_by_key_;
    BgpPeerNameMap peers_by_name_;
    RoutingInstance *instance_;
};

#endif
