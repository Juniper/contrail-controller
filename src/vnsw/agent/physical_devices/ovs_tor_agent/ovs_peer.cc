/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include <physical_devices/ovs_tor_agent/ovs_peer.h>

OvsPeer::OvsPeer(const IpAddress &peer_ip, uint64_t gen_id) :
    Peer(Peer::OVS_PEER, "OVS-" + peer_ip.to_string()), peer_ip_(peer_ip),
    gen_id_(gen_id) {
}

OvsPeer::~OvsPeer() {
}

bool OvsPeer::Compare(const Peer *rhs) const {
    const OvsPeer *rhs_peer = static_cast<const OvsPeer *>(rhs);
    if (gen_id_ != rhs_peer->gen_id_)
        return gen_id_ < rhs_peer->gen_id_;

    return peer_ip_ < rhs_peer->peer_ip_;
}

OvsPeerManager::OvsPeerManager() : gen_id_(0) {
}

OvsPeerManager::~OvsPeerManager() {
    assert(table_.size() == 0);
}

OvsPeer *OvsPeerManager::Allocate(const IpAddress &peer_ip) {
    OvsPeer *peer = new OvsPeer(peer_ip, gen_id_++);
    table_.insert(peer);
}

void OvsPeerManager::Free(OvsPeer *peer) {
    table_.erase(peer);
    delete peer;
}
