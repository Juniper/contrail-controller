/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/peer_manager.h"

#include "base/task_annotations.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/routing-instance/routing_instance_log.h"

using namespace std;
using namespace boost::asio;

BgpPeer *PeerManager::PeerLocate(BgpServer *server,
    const BgpNeighborConfig *config) {
    BgpPeerKey key(config);

    BgpPeerNameMap::iterator loc = peers_by_name_.find(config->name());
    if (loc != peers_by_name_.end()) {
        if (loc->second->IsDeleted())
            return loc->second;
        RemovePeerByKey(loc->second->peer_key(), loc->second);
        InsertPeerByKey(key, loc->second);
        return loc->second;
    }

    BgpPeer *peer =
        BgpObjectFactory::Create<BgpPeer>(server, instance(), config);
    peer->Initialize();
    InsertPeerByKey(key, peer);
    InsertPeerByName(config->name(), peer);
    RoutingInstanceInfo info = instance_->GetDataCollection("Add");
    info.set_peer(peer->ToString());
    ROUTING_INSTANCE_COLLECTOR_INFO(info);

    BGP_LOG_PEER(Config, peer, SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,
                 BGP_PEER_DIR_NA, "Created peer");
    RTINSTANCE_LOG_PEER(Create, instance_, peer,
        SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL);
    return peer;
}

//
// Resurrect the BgpPeer with given name if we have configuration for it.
//
void PeerManager::PeerResurrect(string name) {
    CHECK_CONCURRENCY("bgp::Config");

    if (instance_->deleted())
        return;

    const BgpNeighborConfig *config = instance_->config()->FindNeighbor(name);
    if (!config)
        return;

    PeerLocate(server(), config);
}

//
// Delete the BgpPeer corresponding to the given BgpNeighborConfig.
//
void PeerManager::TriggerPeerDeletion(const BgpNeighborConfig *config) {
    CHECK_CONCURRENCY("bgp::Config");

    BgpPeerNameMap::iterator loc = peers_by_name_.find(config->name());
    if (loc == peers_by_name_.end())
        return;

    BgpPeer *peer = loc->second;
    peer->ManagedDelete();

    RoutingInstanceInfo info = instance_->GetDataCollection("Delete");
    info.set_peer(peer->ToString());
    ROUTING_INSTANCE_COLLECTOR_INFO(info);

    RTINSTANCE_LOG_PEER(Delete, instance_, peer,
        SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL);

    // Configuration is deleted by the config manager (parser)
    // Do not hold reference to it any more
    peer->ClearConfig();
}

//
// Concurrency: Called from bgp config task
//
// Complete the deletion process of a peer.  Remove it from BgpPeerKeyMap and
// BgpPeerNameMap.
//
void PeerManager::DestroyIPeer(IPeer *ipeer) {
    CHECK_CONCURRENCY("bgp::Config");

    BgpPeer *peer = static_cast<BgpPeer *>(ipeer);
    string peer_name = peer->peer_name();
    BGP_LOG_PEER(Config, peer, SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,
                 BGP_PEER_DIR_NA, "Destroyed peer");
    RTINSTANCE_LOG_PEER(Destroy, instance_, peer,
        SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL);
    RemovePeerByKey(peer->peer_key(), peer);
    RemovePeerByName(peer->peer_name(), peer);
    delete peer;

    PeerResurrect(peer_name);
}

//
// Insert the BgpPeer info the BgpPeerKeyMap.
//
void PeerManager::InsertPeerByKey(const BgpPeerKey key, BgpPeer *peer) {
    peers_by_key_.insert(make_pair(key, peer));
}

//
// Remove the BgpPeer from the BgpPeerKeyMap.  There may be more than
// one BgpPeer with the same key, so we need to find the right one.
//
void PeerManager::RemovePeerByKey(const BgpPeerKey key, BgpPeer *peer) {
    for (BgpPeerKeyMap::iterator loc = peers_by_key_.find(key);
         loc != peers_by_key_.end() && loc->first == key; ++loc) {
        if (loc->second == peer) {
            peers_by_key_.erase(loc);
            return;
        }
    }
    assert(false);
}

//
// Insert the BgpPeer info the BgpPeerNameMap.
//
void PeerManager::InsertPeerByName(const string name, BgpPeer *peer) {
    peers_by_name_.insert(make_pair(name, peer));
}

//
// Remove the BgpPeer from the BgpPeerNameMap.
//
void PeerManager::RemovePeerByName(const string name, BgpPeer *peer) {
    BgpPeerNameMap::iterator loc = peers_by_name_.find(name);
    assert(loc != peers_by_name_.end() && loc->second == peer);
    peers_by_name_.erase(loc);
}

BgpPeer *PeerManager::PeerFind(string ip_address) {
    if (ip_address.empty())
        return NULL;

    boost::system::error_code ec;
    boost::asio::ip::tcp::endpoint endpoint;
    endpoint.address(boost::asio::ip::address::from_string(ip_address, ec));
    if (ec)
        return NULL;

    return PeerLookup(endpoint);
}

BgpPeer *PeerManager::PeerLookup(string name) {
    BgpPeerNameMap::iterator loc = peers_by_name_.find(name);
    return (loc != peers_by_name_.end() ? loc->second : NULL);
}

size_t PeerManager::GetNeighborCount(string up_or_down) {
    size_t count = 0;
    BgpPeerNameMap::iterator iter;

    for (iter = peers_by_name_.begin(); iter != peers_by_name_.end(); iter++) {
        BgpPeer *peer = iter->second;
        if (boost::iequals(up_or_down, "up") && !peer->IsReady())
            continue;
        if (boost::iequals(up_or_down, "down") && peer->IsReady())
            continue;
        count += 1;
    }

    return count;
}

//
// Concurrency: Called from state machine thread
//
BgpPeer *PeerManager::PeerLookup(ip::tcp::endpoint remote_endpoint) {
    BgpPeer    *peer = NULL;
    BgpPeerKey  peer_key;

    // Bail if the instance is undergoing deletion.
    if (instance_->deleted())
        return NULL;

    peer_key.endpoint.address(remote_endpoint.address());

    // Do a partial match, as we do not know the peer's port yet.
    BgpPeerKeyMap::iterator loc = peers_by_key_.lower_bound(peer_key);
    while (loc != peers_by_key_.end()) {

        // Check if the address does indeed match as we are doing a partial
        // match here
        if (loc->second->peer_key().endpoint.address() !=
            peer_key.endpoint.address()) {
            break;
        }

        // This peer certainly matches with the IP address. If we do not find
        // an exact match with the peer-id, then just use this.
        peer = loc->second;
        break;
    }

    return peer;
}

BgpPeer *PeerManager::NextPeer(BgpPeerKey &peer_key) {
    // Do a partial match
    BgpPeerKeyMap::iterator loc = peers_by_key_.upper_bound(peer_key);
    if (loc != peers_by_key_.end()) {
        peer_key = loc->second->peer_key();
        return loc->second;
    }

    return NULL;
}

void PeerManager::FillBgpNeighborInfo(vector<BgpNeighborResp> &nbr_list,
    string ip_address) {
    if (!ip_address.empty()) {
        boost::system::error_code ec;
        boost::asio::ip::tcp::endpoint endpoint;
        endpoint.address(
            boost::asio::ip::address::from_string(ip_address, ec));
        if (ec)
            return;
        BgpPeer *peer = PeerLookup(endpoint);
        if (peer)
            peer->FillNeighborInfo(nbr_list);
    } else {
        BgpPeerKey key = BgpPeerKey();
        while (BgpPeer *peer = NextPeer(key)) {
            peer->FillNeighborInfo(nbr_list);
        }
    }
}
