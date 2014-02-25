/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <oper/peer.h>
#include <oper/vrf.h>

void Peer::DelPeerRoutes(DelPeerDone cb) {
    VrfTable *vrf_table = Agent::GetInstance()->GetVrfTable();
    vrf_table->DelPeerRoutes(this, cb);
}

void Peer::PeerNotifyRoutes() {
    VrfTable *vrf_table = Agent::GetInstance()->GetVrfTable();
    vrf_table->VrfTableWalkerNotify(this);
}

void Peer::PeerNotifyMulticastRoutes(bool associate) {
    VrfTable *vrf_table = Agent::GetInstance()->GetVrfTable();
    vrf_table->VrfTableWalkerMulticastNotify(this, associate);
}
