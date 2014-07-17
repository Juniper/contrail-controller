/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>

#include <cmn/agent_cmn.h>
#include <oper/route_common.h>
#include <oper/agent_route_walker.h>
#include <oper/vrf.h>
#include <oper/mirror_table.h>

#include <controller/controller_export.h>
#include <controller/controller_vrf_export.h>
#include <controller/controller_route_walker.h>
#include <controller/controller_peer.h>
#include <controller/controller_init.h>
#include <controller/controller_types.h>

VrfExport::State::State() : DBState(), exported_(false), 
    force_chg_(false), rt_export_() {
};

VrfExport::State::~State() {
};

void VrfExport::Notify(AgentXmppChannel *bgp_xmpp_peer, 
                       DBTablePartBase *partition, DBEntryBase *e) {

    BgpPeer *bgp_peer = static_cast<BgpPeer *>(bgp_xmpp_peer->bgp_peer_id());
    VrfEntry *vrf = static_cast<VrfEntry *>(e);

    //Peer is decommissioned so ignore the notification as there is no active
    //listener. Deletion of state for decommisioned peer will happen via delpeer
    //walk.
    if (!AgentXmppChannel::IsBgpPeerActive(bgp_xmpp_peer) 
        && !(vrf->IsDeleted())) {
        return;
    }

    if (vrf->IsDeleted()) {
        if (bgp_peer) { 
            CONTROLLER_TRACE(Trace, bgp_peer->GetName(), vrf->GetName(), 
                             "VRF deleted, remove state");
            bgp_peer->DeleteVrfState(partition, e);
        }
        bgp_xmpp_peer->agent()->controller()->
            DeleteVrfStateOfDecommisionedPeers(partition, e);
        return;
    }

    if (!AgentXmppChannel::IsBgpPeerActive(bgp_xmpp_peer))
        return;

    DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();
    State *state = static_cast<State *>(vrf->GetState(partition->parent(), id));
    uint8_t table_type;


    if (state == NULL) {
        state = new State();
        state->exported_ = false;
        state->force_chg_ = true;
        vrf->SetState(partition->parent(), id, state);

        if (vrf->GetName().compare(bgp_xmpp_peer->agent()->fabric_vrf_name()) != 0) {
            // Dont export routes belonging to Fabric VRF table
            for (table_type = 0; table_type < Agent::ROUTE_TABLE_MAX;
                 table_type++)
            {
                state->rt_export_[table_type] = 
                    RouteExport::Init(
                     static_cast<AgentRouteTable *>                 
                     (vrf->GetRouteTable(table_type)), 
                     bgp_xmpp_peer);
            }
        }
    }

    if ((state->exported_ == false) || (state->force_chg_ == true)) {
        if (AgentXmppChannel::ControllerSendSubscribe(bgp_xmpp_peer, vrf, 
                                                      true)) {
            CONTROLLER_TRACE(Trace, bgp_peer->GetName(), vrf->GetName(),
                             "Subscribe");

            state->exported_ = true; 
            if (state->force_chg_ == true) {
                if (vrf->GetName().compare(bgp_xmpp_peer->agent()->
                                           fabric_vrf_name()) != 0) {
                    bgp_peer->route_walker()->StartRouteWalk(vrf);
                }
            }
            return;
        }
    } else {
        CONTROLLER_TRACE(Trace, bgp_peer->GetName(), vrf->GetName(),
                         "Already subscribed");
    }
}
