/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "oper/nexthop.h"
#include "oper/tunnel_nh.h"
#include "oper/mirror_table.h"
#include "oper/route_common.h"
#include "pkt/pkt_init.h"
#include "services/arp_proto.h"
#include "services/services_sandesh.h"
#include "services_init.h"

void ArpProto::Shutdown() {
    del_gratuitous_arp_entry();
    // we may have arp entries in arp cache without ArpNH, empty them
    for (ArpIterator it = arp_cache_.begin(); it != arp_cache_.end(); ) {
        it = DeleteArpEntry(it);
    }
}

ArpProto::ArpProto(Agent *agent, boost::asio::io_service &io,
                   bool run_with_vrouter) :
    Proto(agent, "Agent::Services", PktHandler::ARP, io),
    run_with_vrouter_(run_with_vrouter), ip_fabric_interface_index_(-1),
    ip_fabric_interface_(NULL), gratuitous_arp_entry_(NULL),
    max_retries_(kMaxRetries), retry_timeout_(kRetryTimeout),
    aging_timeout_(kAgingTimeout) {

    vrf_table_listener_id_ = agent->vrf_table()->Register(
                             boost::bind(&ArpProto::VrfNotify, this, _1, _2));
    interface_table_listener_id_ = agent->interface_table()->Register(
                                   boost::bind(&ArpProto::InterfaceNotify,
                                               this, _2));
    nexthop_table_listener_id_ = agent->nexthop_table()->Register(
                                 boost::bind(&ArpProto::NextHopNotify, this, _2));
}

ArpProto::~ArpProto() {
    agent_->vrf_table()->Unregister(vrf_table_listener_id_);
    agent_->interface_table()->Unregister(interface_table_listener_id_);
    agent_->nexthop_table()->Unregister(nexthop_table_listener_id_);
}

ProtoHandler *ArpProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                          boost::asio::io_service &io) {
    return new ArpHandler(agent(), info, io);
}

void ArpProto::VrfNotify(DBTablePartBase *part, DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    ArpVrfState *state;

    state = static_cast<ArpVrfState *>(entry->GetState(part->parent(),
                                                   vrf_table_listener_id_));
    if (entry->IsDeleted()) {
        if (state) {
            for (ArpProto::ArpIterator it = arp_cache_.begin();
                 it != arp_cache_.end();) {
                ArpEntry *arp_entry = it->second;
                if (arp_entry->key().vrf == vrf && arp_entry->DeleteArpRoute()) {
                    it = DeleteArpEntry(it);
                } else
                    it++;
            }
            state->Delete();
        }
        return;
    }

    if (!state){
        state = new ArpVrfState(agent_, this, vrf,
                                vrf->GetInet4UnicastRouteTable());
        state->route_table_listener_id = vrf->
            GetInet4UnicastRouteTable()->
            Register(boost::bind(&ArpVrfState::RouteUpdate, state,  _1, _2));
        entry->SetState(part->parent(), vrf_table_listener_id_, state);
    }
}

//Send ARP request on interface in Active-BackUp mode
//So that preference of route can be incremented if the VM replies to ARP
void ArpVrfState::SendArpRequestForVm(Inet4UnicastRouteEntry *route) {
    for (Route::PathList::const_iterator it = route->GetPathList().begin();
            it != route->GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path->peer() &&
            path->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER) {
            if (path->subnet_gw_ip() == Ip4Address(0)) {
                return;
            }
            const NextHop *nh = path->nexthop(agent);
            if (nh->GetType() != NextHop::INTERFACE) {
                continue;
            }

            const InterfaceNH *intf_nh =
                static_cast<const  InterfaceNH *>(nh);
            const Interface *intf =
                static_cast<const Interface *>(intf_nh->GetInterface());
            if (intf->type() != Interface::VM_INTERFACE) {
                //Ignore non vm interface nexthop
                continue;
            }

            uint32_t intf_id = intf->id();
            bool wait_for_traffic = path->path_preference().wait_for_traffic();
            if (wait_for_traffic == false) {
                continue;
            }
            boost::shared_ptr<PktInfo> pkt
                (new PktInfo(agent, MIN_ETH_PKT_LEN, PktHandler::ARP, 0));
            ArpHandler arp_handler(agent, pkt,
                                   *(agent->event_manager()->io_service()));

            if (path->subnet_gw_ip().is_v4()) {
                arp_handler.SendArp(ARPOP_REQUEST, agent->vrrp_mac(),
                        path->subnet_gw_ip().to_v4().to_ulong(),
                        MacAddress(), route->addr().to_ulong(),
                        intf_id, route->vrf_id());
            }
            arp_proto->IncrementStatsVmArpReq();
        }
    }
}

void ArpVrfState::RouteUpdate(DBTablePartBase *part, DBEntryBase *entry) {
    Inet4UnicastRouteEntry *route = static_cast<Inet4UnicastRouteEntry *>(entry);

    DBState *state =
        static_cast<DBState *>
        (entry->GetState(part->parent(), route_table_listener_id));

    if (entry->IsDeleted() || deleted) {
        if (state) {
            if (arp_proto->gratuitous_arp_entry() &&
                arp_proto->gratuitous_arp_entry()->key().ip ==
                    route->addr().to_ulong()) {
                arp_proto->del_gratuitous_arp_entry();
            }
            entry->ClearState(part->parent(), route_table_listener_id);
            delete state;
        }
        return;
    }

    if (!state && route->vrf()->GetName() == agent->fabric_vrf_name() &&
        route->GetActiveNextHop()->GetType() == NextHop::RECEIVE) {
        state = new DBState;
        entry->SetState(part->parent(), route_table_listener_id, state);
        arp_proto->del_gratuitous_arp_entry();
        //Send Grat ARP
        arp_proto->SendArpIpc(ArpProto::ARP_SEND_GRATUITOUS,
                          route->addr().to_ulong(), route->vrf());
    }

    //Check if there is a local VM path, if yes send a
    //ARP request, to trigger route preference state machine
    SendArpRequestForVm(route);
}

bool ArpVrfState::DeleteRouteState(DBTablePartBase *part, DBEntryBase *entry) {
    RouteUpdate(part, entry);
    return true;
}

void ArpVrfState::Delete() {
    deleted = true;
    DBTableWalker *walker = agent->db()->GetWalker();
    walker->WalkTable(rt_table, NULL,
            boost::bind(&ArpVrfState::DeleteRouteState, this, _1, _2),
            boost::bind(&ArpVrfState::WalkDone, this, _1, this));
}

void ArpVrfState::WalkDone(DBTableBase *partition, ArpVrfState *state) {
    arp_proto->ValidateAndClearVrfState(vrf);
    state->rt_table->Unregister(route_table_listener_id);
    state->table_delete_ref.Reset(NULL);
    delete state;
}

ArpVrfState::ArpVrfState(Agent *agent_ptr, ArpProto *proto, VrfEntry *vrf_entry,
                         AgentRouteTable *table):
    agent(agent_ptr), arp_proto(proto), vrf(vrf_entry), rt_table(table),
    route_table_listener_id(DBTableBase::kInvalidId),
    table_delete_ref(this, table->deleter()), deleted(false) {
}

void ArpProto::InterfaceNotify(DBEntryBase *entry) {
    Interface *itf = static_cast<Interface *>(entry);
    if (entry->IsDeleted()) {
        if (itf->type() == Interface::PHYSICAL &&
            itf->name() == agent_->fabric_interface_name()) {
            for (ArpProto::ArpIterator it = arp_cache_.begin();
                 it != arp_cache_.end();) {
                ArpEntry *arp_entry = it->second;
                if (arp_entry->DeleteArpRoute()) {
                    it = DeleteArpEntry(it);
                } else
                    it++;
            }
            set_ip_fabric_interface(NULL);
            set_ip_fabric_interface_index(-1);
        }
    } else {
        if (itf->type() == Interface::PHYSICAL &&
            itf->name() == agent_->fabric_interface_name()) {
            set_ip_fabric_interface(itf);
            set_ip_fabric_interface_index(itf->id());
            if (run_with_vrouter_) {
                set_ip_fabric_interface_mac(itf->mac());
            } else {
                set_ip_fabric_interface_mac(MacAddress());
            }
        }
    }
}

void ArpProto::NextHopNotify(DBEntryBase *entry) {
    NextHop *nh = static_cast<NextHop *>(entry);

    switch(nh->GetType()) {
    case NextHop::ARP: {
        ArpNH *arp_nh = (static_cast<ArpNH *>(nh));
        if (arp_nh->IsDeleted()) {
            SendArpIpc(ArpProto::ARP_DELETE, arp_nh->GetIp()->to_ulong(),
                       arp_nh->GetVrf());
        } else if (arp_nh->IsValid() == false) {
            SendArpIpc(ArpProto::ARP_RESOLVE, arp_nh->GetIp()->to_ulong(),
                       arp_nh->GetVrf());
        }
        break;
    }

    default:
        break;
    }
}

bool ArpProto::TimerExpiry(ArpKey &key, uint32_t timer_type) {
    if (arp_cache_.find(key) != arp_cache_.end())
        SendArpIpc((ArpProto::ArpMsgType)timer_type, key);
    return false;
}

ArpEntry *ArpProto::gratuitous_arp_entry() const {
    return gratuitous_arp_entry_;
}

void ArpProto::set_gratuitous_arp_entry(ArpEntry *entry) {
    gratuitous_arp_entry_ = entry;
}

void ArpProto::del_gratuitous_arp_entry() {
    if (gratuitous_arp_entry_) {
        delete gratuitous_arp_entry_;
        gratuitous_arp_entry_ = NULL;
    }
}

void ArpProto::SendArpIpc(ArpProto::ArpMsgType type, 
                          in_addr_t ip, const VrfEntry *vrf) {
    ArpIpc *ipc = new ArpIpc(type, ip, vrf);
    agent_->pkt()->pkt_handler()->SendMessage(PktHandler::ARP, ipc);
}

void ArpProto::SendArpIpc(ArpProto::ArpMsgType type, ArpKey &key) {
    ArpIpc *ipc = new ArpIpc(type, key);
    agent_->pkt()->pkt_handler()->SendMessage(PktHandler::ARP, ipc);
}

bool ArpProto::AddArpEntry(ArpEntry *entry) {
    return arp_cache_.insert(ArpCachePair(entry->key(), entry)).second;
}

bool ArpProto::DeleteArpEntry(ArpEntry *entry) {
    if (!entry)
        return false;

    ArpProto::ArpIterator iter = arp_cache_.find(entry->key());
    if (iter == arp_cache_.end()) {
        return false;
    }

    DeleteArpEntry(iter);
    return true;
}

ArpProto::ArpIterator
ArpProto::DeleteArpEntry(ArpProto::ArpIterator iter) {
    ArpEntry *entry = iter->second;
    arp_cache_.erase(iter++);
    ValidateAndClearVrfState(const_cast<VrfEntry *>(entry->key().vrf));
    delete entry;
    return iter;
}

ArpEntry *ArpProto::FindArpEntry(const ArpKey &key) {
    ArpIterator it = arp_cache_.find(key);
    if (it == arp_cache_.end())
        return NULL;
    return it->second;
}

void ArpProto::ValidateAndClearVrfState(VrfEntry *vrf) {
    if (!vrf->IsDeleted())
        return;

    ArpKey key(0, vrf);
    ArpProto::ArpIterator it = arp_cache_.upper_bound(key);
    if (it != arp_cache_.end() && it->first.vrf == vrf) {
        return;
    }

    DBState *state = static_cast<DBState *>
        (vrf->GetState(vrf->get_table_partition()->parent(),
                       vrf_table_listener_id_));
    if (state) {
        vrf->ClearState(vrf->get_table_partition()->parent(),
                        vrf_table_listener_id_);
    }
}
