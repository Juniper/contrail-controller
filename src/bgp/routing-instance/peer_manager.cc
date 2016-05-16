/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/peer_manager.h"

#include "base/task_annotations.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_server.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/routing_instance_log.h"

struct BgpSandeshContext;

using std::make_pair;
using std::string;
using std::vector;

//
// Find or create a BgpPeer for the given BgpNeighborConfig.
// Return NULL if the peer already exists and is being deleted. The BgpPeer
// will eventually get created in this case via PeerResurrect.
//
BgpPeer *PeerManager::PeerLocate(BgpServer *server,
    const BgpNeighborConfig *config) {
    BgpPeerKey key(config);

    BgpPeerNameMap::iterator loc = peers_by_name_.find(config->name());
    if (loc != peers_by_name_.end()) {
        if (loc->second->IsDeleted())
            return NULL;
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
// Also insert it into the BgpServer's EndpointToBgpPeerList - in case it's
// a BGPaaS peer. This needs to happen here since we would not have been
// able to do it from BgpServer::ConfigUpdater::ProcessNeighborConfig since
// old incarnation of the BgpPeer still existed at that point.
//
void PeerManager::PeerResurrect(string name) {
    CHECK_CONCURRENCY("bgp::Config");

    if (instance_->deleted())
        return;

    const BgpConfigManager *config_manager =
            instance_->manager()->server()->config_manager();
    const BgpNeighborConfig *config =
            config_manager->FindNeighbor(instance_->name(), name);
    if (!config)
        return;

    BgpPeer *peer = PeerLocate(server(), config);
    assert(peer);
    server()->InsertPeer(peer->endpoint(), peer);
}

//
// Delete the BgpPeer corresponding to the given BgpNeighborConfig.
//
BgpPeer *PeerManager::TriggerPeerDeletion(const BgpNeighborConfig *config) {
    CHECK_CONCURRENCY("bgp::Config");

    BgpPeerNameMap::iterator loc = peers_by_name_.find(config->name());
    if (loc == peers_by_name_.end())
        return NULL;

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
    return peer;
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

BgpPeer *PeerManager::PeerFind(string ip_address) const {
    if (ip_address.empty())
        return NULL;

    boost::system::error_code ec;
    boost::asio::ip::tcp::endpoint endpoint;
    endpoint.address(boost::asio::ip::address::from_string(ip_address, ec));
    if (ec)
        return NULL;

    return PeerLookup(endpoint);
}

BgpPeer *PeerManager::PeerLookup(string name) const {
    BgpPeerNameMap::const_iterator loc = peers_by_name_.find(name);
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

void PeerManager::ClearAllPeers() {
    BgpPeerNameMap::iterator iter;

    for (iter = peers_by_name_.begin(); iter != peers_by_name_.end(); iter++) {
        BgpPeer *peer = iter->second;
        peer->Clear(BgpProto::Notification::OtherConfigChange);
    }
}

//
// Concurrency: Called from state machine thread
//
BgpPeer *PeerManager::PeerLookup(TcpSession::Endpoint remote_endpoint) const {
    BgpPeer    *peer = NULL;
    BgpPeerKey  peer_key;

    // Bail if the instance is undergoing deletion.
    if (instance_->deleted())
        return NULL;

    peer_key.endpoint.address(remote_endpoint.address());

    // Do a partial match, as we do not know the peer's port yet.
    BgpPeerKeyMap::const_iterator loc = peers_by_key_.lower_bound(peer_key);
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

const BgpPeer *PeerManager::NextPeer(BgpPeerKey &peer_key) const {
    // Do a partial match
    BgpPeerKeyMap::const_iterator loc = peers_by_key_.upper_bound(peer_key);
    if (loc != peers_by_key_.end()) {
        peer_key = loc->second->peer_key();
        return loc->second;
    }

    return NULL;
}

const string &PeerManager::name() const {
    return instance_->name();
}

BgpServer *PeerManager::server() const {
    return instance_->server();
}

void PeerManager::FillBgpNeighborInfo(const BgpSandeshContext *bsc,
        vector<BgpNeighborResp> *bnr_list, const string &search_string,
        bool summary) const {
    BgpPeerKey key = BgpPeerKey();
    while (const BgpPeer *peer = NextPeer(key)) {
        if (search_string.empty() ||
            (peer->peer_basename().find(search_string) != string::npos) ||
            (peer->peer_address_string().find(search_string) != string::npos) ||
            (search_string == "deleted" && peer->IsDeleted())) {
            BgpNeighborResp bnr;
            peer->FillNeighborInfo(bsc, &bnr, summary);
            bnr_list->push_back(bnr);
        }
    }
}
