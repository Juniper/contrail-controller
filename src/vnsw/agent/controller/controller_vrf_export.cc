/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <controller/controller_export.h>
#include <controller/controller_vrf_export.h>
#include <cmn/agent_cmn.h>
#include <oper/vrf.h>
#include <oper/mirror_table.h>
#include <controller/controller_peer.h>
#include "controller/controller_init.h"
#include "controller/controller_types.h"

void VrfExport::Notify(AgentXmppChannel *bgp_xmpp_peer, 
                       DBTablePartBase *partition, DBEntryBase *e) {

    BgpPeer *bgp_peer = static_cast<BgpPeer *>(bgp_xmpp_peer->GetBgpPeer());
    DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();
    VrfEntry *vrf = static_cast<VrfEntry *>(e);
    State *state = static_cast<State *>(vrf->GetState(partition->parent(), id));

    if (vrf->IsDeleted()) {
        if (state == NULL) {
            return;
        }

        if (vrf->GetName().compare(Agent::GetInstance()->GetDefaultVrf()) != 0) {
            state->inet4_multicast_export_->Unregister();
            state->inet4_unicast_export_->Unregister();
        }
 
        if (state->exported_ == false) {
            CONTROLLER_TRACE(Trace, bgp_peer->GetName(), vrf->GetName(),
                             "Not subscribed");
            vrf->ClearState(partition->parent(), id);
            delete state;
            return;
        }
  
        CONTROLLER_TRACE(Trace, bgp_peer->GetName(), vrf->GetName(),
                         "Unsubscribe");
        AgentXmppChannel::ControllerSendSubscribe(bgp_xmpp_peer, vrf, false); 

        vrf->ClearState(partition->parent(), id);
        delete state;
        return;
    }

    if (state == NULL) {
        state = new State();
        state->exported_ = false;
        state->force_chg_ = true;
        vrf->SetState(partition->parent(), id, state);

        if (vrf->GetName().compare(Agent::GetInstance()->GetDefaultVrf()) != 0) {
            // Dont export routes belonging to Fabric VRF table
            state->inet4_unicast_export_ =  
                Inet4RouteExport::UnicastInit(vrf->GetInet4UcRouteTable(), bgp_xmpp_peer);
            state->inet4_multicast_export_ =  
                Inet4RouteExport::MulticastInit(vrf->GetInet4McRouteTable(), bgp_xmpp_peer);
        }
    }

    if ((state->exported_ == false) || (state->force_chg_ == true)) {
        if (AgentXmppChannel::ControllerSendSubscribe(bgp_xmpp_peer, vrf, 
                                                      true)) {
            CONTROLLER_TRACE(Trace, bgp_peer->GetName(), vrf->GetName(),
                             "Subscribe");

            state->exported_ = true; 
            if (state->force_chg_ == true) {
                if (vrf->GetName().compare(Agent::GetInstance()->GetDefaultVrf()) != 0) {
                    bool associate = true;
                    bool subnet_only = false;
                    Inet4UcRouteTable *table = vrf->GetInet4UcRouteTable();
                    table->Inet4UcRouteTableWalkerNotify(vrf, bgp_xmpp_peer, state, 
                                                         subnet_only, associate);

                    Inet4McRouteTable *mc_table = vrf->GetInet4McRouteTable();
                    mc_table->Inet4McRouteTableWalkerNotify(vrf, bgp_xmpp_peer, state, 
                                                            associate);
                }
            }
            return;
        }
    } else {
        CONTROLLER_TRACE(Trace, bgp_peer->GetName(), vrf->GetName(),
                         "Already subscribed");
    }
}
