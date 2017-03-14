/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#include "mac_learning_db_client.h"
#include "mac_learning_init.h"
#include "mac_learning_proto.h"
#include "mac_learning_proto_handler.h"
#include "mac_learning.h"
#include "mac_learning_mgmt.h"
#include "mac_aging.h"

MacLearningDBClient::MacLearningDBClient(Agent *agent) :
    agent_(agent), interface_listener_id_(), vrf_listener_id_() {
}

void MacLearningDBClient::Init() {
    interface_listener_id_ = agent_->interface_table()->Register
        (boost::bind(&MacLearningDBClient::InterfaceNotify, this, _1, _2));
    vrf_listener_id_ = agent_->vrf_table()->Register
        (boost::bind(&MacLearningDBClient::VrfNotify, this, _1, _2));
}

void MacLearningDBClient::Shutdown() {
    agent_->interface_table()->Unregister(interface_listener_id_);
    agent_->vrf_table()->Unregister(vrf_listener_id_);
}

MacLearningDBClient::~MacLearningDBClient() {
}

void MacLearningDBClient::InterfaceNotify(DBTablePartBase *part, DBEntryBase *e) {
    Interface *intf = static_cast<Interface *>(e);
    if (intf->type() != Interface::VM_INTERFACE) {
        return;
    }

    MacLearningIntfState *state = static_cast<MacLearningIntfState *>
        (e->GetState(part->parent(), interface_listener_id_));

    VmInterface *vm_port = static_cast<VmInterface *>(intf);
    if (intf->IsDeleted()) {
        if (state) {
            DeleteEvent(vm_port, state);
        }
        return;
    }

    bool changed = false;
    bool delete_mac = false;
    if (state == NULL) {
        state = new MacLearningIntfState();
        e->SetState(part->parent(), interface_listener_id_, state);
        state->l2_label_ = vm_port->l2_label();
        state->sg_l_ = vm_port->sg_list();
        state->learning_enabled_ = vm_port->learning_enabled();
        state->policy_enabled_ = vm_port->policy_enabled();
        state->l2_active_ = vm_port->IsL2Active();
        changed = true;
        e->SetState(part->parent(), interface_listener_id_, state);
    } else {
        if (state->deleted_ == true) {
            state->deleted_ = false;
            changed = true;
        }

        if (state->l2_label_ != vm_port->l2_label()) {
            state->l2_label_ = vm_port->l2_label();
            changed = true;
        }

        const VmInterface::SecurityGroupEntryList &new_sg_l = vm_port->sg_list();
        if (state->sg_l_.list_ != new_sg_l.list_) {
            state->sg_l_ = new_sg_l;
            changed = true;
        }

        if (state->learning_enabled_ != vm_port->learning_enabled()) {
            state->learning_enabled_ = vm_port->learning_enabled();
            if (state->learning_enabled_ == false) {
                delete_mac = true;
            }
        }

        if (state->policy_enabled_ != vm_port->policy_enabled()) {
            state->policy_enabled_ = vm_port->policy_enabled();
            changed = true;
        }

        if (state->l2_active_ != vm_port->IsL2Active()) {
            state->l2_active_ = vm_port->IsL2Active();
            if (state->l2_active_ == false) {
                delete_mac = true;
            }
        }
    }

    if (delete_mac) {
        DeleteAllMac(vm_port, state);
        return;
    }

    if (changed) {
        AddEvent(vm_port, state);
    }
}

void MacLearningDBClient::RouteNotify(MacLearningVrfState *vrf_state,
                                      Agent::RouteTableType type,
                                      DBTablePartBase *partition,
                                      DBEntryBase *e) {
    DBTableBase::ListenerId id = vrf_state->bridge_listener_id_;
    MacLearningRouteState *rt_state =
        static_cast<MacLearningRouteState *>(e->GetState(partition->parent(),
                    vrf_state->bridge_listener_id_));
    AgentRoute *route = static_cast<AgentRoute *>(e);

    ReleaseToken(route);

    if (route->IsDeleted() && route->is_multicast() == false) {
        if (vrf_state && rt_state) {
            rt_state->gen_id_++;
            DeleteEvent(route, rt_state);
        }
        return;
    }

    if (vrf_state->deleted_) {
        // ignore route add/change for delete notified VRF.
        return;
    }

    if (route->is_multicast()) {
        return;
    }

    if (rt_state == NULL) {
        rt_state  = new MacLearningRouteState();
        route->SetState(partition->parent(), id, rt_state);
        AddEvent(route, rt_state);
    } else {
        ChangeEvent(route, rt_state);
    }
}

void MacLearningDBClient::MacLearningVrfState::Register(MacLearningDBClient *client,
                                                        VrfEntry *vrf) {
    // Register to the Bridge Unicast Table
    BridgeAgentRouteTable *bridge_table = static_cast<BridgeAgentRouteTable *>
                                              (vrf->GetBridgeRouteTable());
    bridge_listener_id_ =
        bridge_table->Register(boost::bind(&MacLearningDBClient::RouteNotify,
                    client, this, Agent::BRIDGE, _1,
                    _2));
}

void MacLearningDBClient::MacLearningVrfState::Unregister(VrfEntry *vrf) {
    BridgeAgentRouteTable *bridge_table = static_cast<BridgeAgentRouteTable *>
        (vrf->GetBridgeRouteTable());
    bridge_table->Unregister(bridge_listener_id_);
}

void MacLearningDBClient::VrfNotify(DBTablePartBase *part, DBEntryBase *e) {
    VrfEntry *vrf = static_cast<VrfEntry *>(e);

    MacLearningVrfState *state = static_cast<MacLearningVrfState *>
        (e->GetState(part->parent(), vrf_listener_id_));

    if (vrf->IsDeleted()) {
        if (state) {
            DeleteEvent(vrf, state);
        }
        return;
    }

    if (state == NULL) {
        state = new MacLearningVrfState();
        e->SetState(part->parent(), vrf_listener_id_, state);
        state->Register(this, vrf);
        state->isid_ = vrf->isid();
        AddEvent(vrf, state);
    }

    if (state->learning_enabled_ != vrf->learning_enabled()) {
        state->learning_enabled_ = vrf->learning_enabled();
        if (state->learning_enabled_ == false) {
            DeleteAllMac(vrf, state);
        }
    }

    if (state->isid_ != vrf->isid()) {
        state->isid_ = vrf->isid();
        DeleteAllMac(vrf, state);
    }
}

void MacLearningDBClient::FreeRouteState(const DBEntry *ce, uint32_t gen_id) {
    DBEntry *e = const_cast<DBEntry *>(ce);
    AgentRoute *rt = static_cast<AgentRoute *>(e);
    if (rt->IsDeleted() == false) {
        return;
    }

    VrfEntry *vrf = rt->vrf();
    MacLearningVrfState *vrf_state = static_cast<MacLearningVrfState *>
        (vrf->GetState(vrf->get_table(), vrf_listener_id_));
    if (vrf_state == NULL) {
        return;
    }

    MacLearningRouteState *state =
        static_cast<MacLearningRouteState *>(rt->GetState(rt->get_table(),
            vrf_state->bridge_listener_id_));
    if (state->gen_id_ != gen_id) {
        return;
    }

    rt->ClearState(rt->get_table(), vrf_state->bridge_listener_id_);
    delete state;
}

void MacLearningDBClient::EnqueueAgingTableDelete(const VrfEntry *vrf) {
    MacLearningProto *ml_proto = agent_->mac_learning_proto();
    for (uint32_t i = 0; i < ml_proto->size(); i++) {
        MacLearningEntryRequestPtr ptr(new MacLearningEntryRequest(
                    MacLearningEntryRequest::DELETE_VRF, vrf->vrf_id()));
        ml_proto->Find(i)->aging_partition()->Enqueue(ptr);
    }
}

void MacLearningDBClient::FreeDBState(const DBEntry *entry, uint32_t gen_id) {
    if (dynamic_cast<const Interface *>(entry)) {
        DBTable *table = agent_->interface_table();
        Interface *intf = static_cast<Interface *>(table->Find(entry));
        DBState *state = intf->GetState(intf->get_table(),
                                        interface_listener_id_);
        intf->ClearState(intf->get_table(), interface_listener_id_);
        delete state;
        return;
    }

    if (dynamic_cast<const VrfEntry *>(entry)) {
        //Enqueue request to delete aging table pointers
        DBTable *table = agent_->vrf_table();
        VrfEntry *vrf = static_cast<VrfEntry *>(table->Find(entry));

        EnqueueAgingTableDelete(vrf);

        DBState *state = vrf->GetState(vrf->get_table(),
                                       vrf_listener_id_);
        MacLearningVrfState *vrf_state =
            static_cast<MacLearningVrfState *>(state);
        vrf->ClearState(vrf->get_table(), vrf_listener_id_);
        vrf_state->Unregister(vrf);
        delete state;
        return;
    }

    if (dynamic_cast<const AgentRoute *>(entry)) {
        FreeRouteState(entry, gen_id);
        return;
    }
}

void MacLearningDBClient::AddEvent(const DBEntry *entry,
                                   MacLearningDBState *state) {
    MacLearningMgmtRequestPtr ptr(new MacLearningMgmtRequest(
                MacLearningMgmtRequest::ADD_DBENTRY, entry, state->gen_id_));
    agent_->mac_learning_module()->mac_learning_mgmt()->Enqueue(ptr);
}

void MacLearningDBClient::ChangeEvent(const DBEntry *entry,
                                      MacLearningDBState *state) {
    MacLearningMgmtRequestPtr ptr(new MacLearningMgmtRequest(
                MacLearningMgmtRequest::CHANGE_DBENTRY, entry, state->gen_id_));
    agent_->mac_learning_module()->mac_learning_mgmt()->Enqueue(ptr);
}

void MacLearningDBClient::DeleteEvent(const DBEntry *entry,
                                      MacLearningDBState *state) {
    MacLearningMgmtRequestPtr ptr(new MacLearningMgmtRequest(
                MacLearningMgmtRequest::DELETE_DBENTRY, entry,
                state->gen_id_));
    agent_->mac_learning_module()->mac_learning_mgmt()->Enqueue(ptr);
}

void MacLearningDBClient::DeleteAllMac(const DBEntry *entry, MacLearningDBState *state) {
    MacLearningMgmtRequestPtr ptr(new MacLearningMgmtRequest(
                MacLearningMgmtRequest::DELETE_ALL_MAC, entry,
                state->gen_id_));
    agent_->mac_learning_module()->mac_learning_mgmt()->Enqueue(ptr);
}

void MacLearningDBClient::ReleaseToken(const DBEntry *entry) {
    const BridgeRouteEntry *brt =
        dynamic_cast<const BridgeRouteEntry *>(entry);

    //Get the partition id for mac of interest
    uint32_t id = agent_->mac_learning_proto()->Hash(brt->vrf()->vrf_id(),
                                                     brt->mac());
    MacLearningPartition *partition =
        agent_->mac_learning_proto()->Find(id);
    assert(partition);

    //Release the token
    MacLearningKey key(brt->vrf()->vrf_id(), brt->mac());
    partition->ReleaseToken(key);
}
