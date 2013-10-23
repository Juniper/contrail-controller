/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>

#include <cmn/agent_cmn.h>
#include <controller/controller_export.h>
#include <controller/controller_vrf_export.h>
#include <oper/vrf.h>
#include <oper/mirror_table.h>
#include <controller/controller_peer.h>
#include "controller/controller_init.h"
#include "controller/controller_types.h"

VrfExport::State::State() : DBState(), exported_(false), 
    force_chg_(false), rt_export_() {
        for (uint32_t rt_table_type = 0; 
             rt_table_type < AgentRouteTableAPIS::MAX; rt_table_type++) {
            ucwalkid_[rt_table_type] = DBTableWalker::kInvalidWalkerId;
            mcwalkid_[rt_table_type] = DBTableWalker::kInvalidWalkerId;
        }
};

VrfExport::State::~State() {
    DBTableWalker *walker = Agent::GetInstance()->GetDB()->GetWalker();

    for (uint32_t rt_table_type = 0; 
         rt_table_type < AgentRouteTableAPIS::MAX; rt_table_type++)
    {
        if (ucwalkid_[rt_table_type] != DBTableWalker::kInvalidWalkerId)
            walker->WalkCancel(ucwalkid_[rt_table_type]);

        if (mcwalkid_[rt_table_type] != DBTableWalker::kInvalidWalkerId)
            walker->WalkCancel(mcwalkid_[rt_table_type]);
    }
};

void VrfExport::Notify(AgentXmppChannel *bgp_xmpp_peer, 
                       DBTablePartBase *partition, DBEntryBase *e) {

    BgpPeer *bgp_peer = static_cast<BgpPeer *>(bgp_xmpp_peer->GetBgpPeer());
    DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();
    VrfEntry *vrf = static_cast<VrfEntry *>(e);
    State *state = static_cast<State *>(vrf->GetState(partition->parent(), id));
    uint8_t table_type;

    if (vrf->IsDeleted()) {
        if (state == NULL) {
            return;
        }

        if (vrf->GetName().compare(Agent::GetInstance()->GetDefaultVrf()) != 0) {
            for (table_type = 0; table_type < AgentRouteTableAPIS::MAX; 
                 table_type++) {
                state->rt_export_[table_type]->Unregister();
            }
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
            for (table_type = 0; table_type < AgentRouteTableAPIS::MAX; 
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
                if (vrf->GetName().compare(Agent::GetInstance()->GetDefaultVrf()) != 0) {
                    bool associate = true;
                    AgentRouteTable *table;
                    uint8_t table_type;
                    for (table_type = 0; table_type < AgentRouteTableAPIS::MAX; 
                         table_type++) {
                        table = static_cast<AgentRouteTable *>
                            (vrf->GetRouteTable(table_type));
                        table->RouteTableWalkerNotify(vrf, bgp_xmpp_peer, state,
                                                      associate, true, true);
                    }
                }
            }
            return;
        }
    } else {
        CONTROLLER_TRACE(Trace, bgp_peer->GetName(), vrf->GetName(),
                         "Already subscribed");
    }
}
