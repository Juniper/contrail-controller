/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <oper/vrf.h>
#include <oper/peer.h>

Peer::PeerMap Peer::peer_map_;
tbb::mutex Peer::mutex_;

void Peer::DelPeerRoutes(DelPeerDone cb) {
    VrfTable *vrf_table = Agent::GetInstance()->GetVrfTable();
    vrf_table->DelPeerRoutes(this, cb);
};

void Peer::PeerNotifyRoutes() {
    VrfTable *vrf_table = Agent::GetInstance()->GetVrfTable();
    vrf_table->VrfTableWalkerNotify(this);
};

void Peer::PeerNotifyMcastBcastRoutes(bool associate) {
    VrfTable *vrf_table = Agent::GetInstance()->GetVrfTable();
    vrf_table->VrfTableWalkerMcastBcastNotify(this, associate);
}
