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
    force_chg_(false), rt_export_(), last_sequence_number_(0) {
};

VrfExport::State::~State() {
};

bool VrfExport::State::IsExportable(uint64_t sequence_number) {
    // Sequence number passed in argument is of channel to which this state
    // belongs. Once channel sends the vrf subscription after flap/fresh 
    // connection, state's sequence number will be updated to that of channel.
    // After this all routes in this VRF become eligible for export.
    // This is needed as control-node mandates that VRF is subscribed before any
    // route is seen in same else it will again flap the
    // connection(intentionally).
    // Note: In case of flaps etc CN removes VRF subscription.
    // This sequence number macthing helps in making sure that VRF sub is
    // sent before any route is published.
    return (last_sequence_number_ != sequence_number);
}

void VrfExport::Notify(const Agent *agent, AgentXmppChannel *bgp_xmpp_peer,
                       DBTablePartBase *partition, DBEntryBase *e) {

    BgpPeer *bgp_peer = static_cast<BgpPeer *>(bgp_xmpp_peer->bgp_peer_id());
    VrfEntry *vrf = static_cast<VrfEntry *>(e);

    //PBB VRF is implictly created, agent is not supposed to send RI
    //subscription message since control-node will not be aware of this RI
    //We still want to set a state and subscribe for bridge table for
    //building ingress replication tree
    bool send_subscribe  = vrf->ShouldExportRoute();

    uint32_t instance_id = vrf->RDInstanceId();
    //Instance ID being zero is possible because of VN unavailability and VRF
    //ending up with NULL VRF. Reason being config sequence.
    //So seeing 0 instance_id delete the state so that resubscribe can be done
    //with new id and then re-export all route.
    //Note: Assumption is that instance id will never change from non zero to
    //some other non zero value.
    //Also Instance ID check is for TSN and TA only.
    bool deleted = (vrf->IsDeleted()) || (instance_id == VrfEntry::kInvalidIndex);

    if (deleted) {
        bgp_peer->DeleteVrfState(partition, e);
        if (!AgentXmppChannel::IsXmppChannelActive(agent, bgp_xmpp_peer)) {
            return;
        }
        if (bgp_peer) { 
            CONTROLLER_TRACE(Trace, bgp_peer->GetName(), vrf->GetName(), 
                             "VRF deleted, remove state");
            bgp_peer->DeleteVrfState(partition, e);
        }
        return;
    }

    if (!AgentXmppChannel::IsBgpPeerActive(agent, bgp_xmpp_peer))
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
            for (table_type = (Agent::INVALID + 1);
                 table_type < Agent::ROUTE_TABLE_MAX;
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

    if (state->last_sequence_number_ == bgp_xmpp_peer->sequence_number()) {
        state->force_chg_ = false;
    } else {
        state->last_sequence_number_ = bgp_xmpp_peer->sequence_number();
    }

    if (send_subscribe == false) {
        state->force_chg_ = false;
    }

    if (send_subscribe && ((state->exported_ == false) ||
                          (state->force_chg_ == true))) {
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
                state->force_chg_ = false;
            }
            return;
        }
    } else {
        CONTROLLER_TRACE(Trace, bgp_peer->GetName(), vrf->GetName(),
                         "Already subscribed");
    }
}
