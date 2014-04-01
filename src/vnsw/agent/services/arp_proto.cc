/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "oper/nexthop.h"
#include "oper/tunnel_nh.h"
#include "oper/mirror_table.h"
#include "ksync/ksync_index.h"
#include "ksync/interface_ksync.h"
#include "pkt/pkt_init.h"
#include "services/arp_proto.h"
#include "services/services_sandesh.h"
#include "services_init.h"

void ArpProto::Shutdown() {
}

ArpProto::ArpProto(Agent *agent, boost::asio::io_service &io,
                   bool run_with_vrouter) :
    Proto(agent, "Agent::Services", PktHandler::ARP, io),
    run_with_vrouter_(run_with_vrouter), ip_fabric_interface_index_(-1),
    ip_fabric_interface_(NULL), gratuitous_arp_entry_(NULL),
    max_retries_(kMaxRetries), retry_timeout_(kRetryTimeout),
    aging_timeout_(kAgingTimeout) {

    memset(ip_fabric_interface_mac_, 0, ETH_ALEN);
    vrf_table_listener_id_ = agent->GetVrfTable()->Register(
                             boost::bind(&ArpProto::VrfNotify, this, _1, _2));
    interface_table_listener_id_ = agent->GetInterfaceTable()->Register(
                                   boost::bind(&ArpProto::InterfaceNotify,
                                               this, _2));
    nexthop_table_listener_id_ = agent->GetNextHopTable()->Register(
                                 boost::bind(&ArpProto::NextHopNotify, this, _2));
}

ArpProto::~ArpProto() {
    agent_->GetVrfTable()->Unregister(vrf_table_listener_id_);
    agent_->GetInterfaceTable()->Unregister(interface_table_listener_id_);
    agent_->GetNextHopTable()->Unregister(nexthop_table_listener_id_);
    del_gratuitous_arp_entry();
    // we may have arp entries in arp cache without ArpNH, empty them
    for (ArpCache::iterator it = arp_cache_.begin(); it != arp_cache_.end();
         ++it) {
        delete it->second;
    }
    arp_cache_.clear();
}

ProtoHandler *ArpProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                          boost::asio::io_service &io) {
    return new ArpHandler(agent(), info, io);
}

void ArpProto::VrfNotify(DBTablePartBase *part, DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    DBState *state;

    state = static_cast<DBState *>(entry->GetState(part->parent(),
                                                   vrf_table_listener_id_));
    if (entry->IsDeleted()) {
        if (state) {
            for (ArpProto::ArpCache::iterator it = arp_cache_.begin();
                 it != arp_cache_.end();) {
                ArpEntry *arp_entry = it->second;
                if (arp_entry->key().vrf == vrf && arp_entry->DeleteArpRoute()) {
                    arp_cache_.erase(it++);
                    delete arp_entry;
                } else
                    it++;
            }
            vrf->GetInet4UnicastRouteTable()->
                Unregister(fabric_route_table_listener_);
            // TODO: should clear state when arp_cache_ is emptied
            entry->ClearState(part->parent(), vrf_table_listener_id_);
            delete state;
        }
        return;
    }

    if (!state && vrf->GetName() == agent_->GetDefaultVrf()) {
        state = new DBState;
        fabric_route_table_listener_ = vrf->
            GetInet4UnicastRouteTable()->
            Register(boost::bind(&ArpProto::RouteUpdate, this,  _1, _2));
        entry->SetState(part->parent(), vrf_table_listener_id_, state);
    }
}

void ArpProto::RouteUpdate(DBTablePartBase *part, DBEntryBase *entry) {
    Inet4UnicastRouteEntry *route = static_cast<Inet4UnicastRouteEntry *>(entry);
    DBState *state =
        static_cast<DBState *>
        (entry->GetState(part->parent(), fabric_route_table_listener_));

    if (entry->IsDeleted()) {
        if (state) {
            if (gratuitous_arp_entry() && gratuitous_arp_entry()->key().ip ==
                                        route->addr().to_ulong()) {
                del_gratuitous_arp_entry();
            }
            entry->ClearState(part->parent(), fabric_route_table_listener_);
            delete state;
        }
        return;
    }

    if (!state && route->GetActiveNextHop()->GetType() == NextHop::RECEIVE) {
        state = new DBState;
        entry->SetState(part->parent(), fabric_route_table_listener_, state);
        del_gratuitous_arp_entry();
        //Send Grat ARP
        SendArpIpc(ArpProto::ARP_SEND_GRATUITOUS, 
                   route->addr().to_ulong(), route->vrf());
    }
}

void ArpProto::InterfaceNotify(DBEntryBase *entry) {
    Interface *itf = static_cast<Interface *>(entry);
    if (entry->IsDeleted()) {
        if (itf->type() == Interface::PHYSICAL && 
            itf->name() == agent_->GetIpFabricItfName()) {
            for (ArpProto::ArpCache::iterator it = arp_cache_.begin();
                 it != arp_cache_.end();) {
                ArpEntry *arp_entry = it->second;
                if (arp_entry->DeleteArpRoute()) {
                    arp_cache_.erase(it++);
                    delete arp_entry;
                } else
                    it++;
            }
        }
    } else {
        if (itf->type() == Interface::PHYSICAL && 
            itf->name() == agent_->GetIpFabricItfName()) {
            set_ip_fabric_interface(itf);
            set_ip_fabric_interface_index(itf->id());
            if (run_with_vrouter_) {
                set_ip_fabric_interface_mac((char *)itf->mac().ether_addr_octet);
            } else {
                char mac[ETH_ALEN];
                memset(mac, 0, ETH_ALEN);
                set_ip_fabric_interface_mac(mac);
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

bool ArpProto::AddArpEntry(const ArpKey &key, ArpEntry *entry) {
    return arp_cache_.insert(ArpCachePair(key, entry)).second;
}

bool ArpProto::DeleteArpEntry(const ArpKey &key) {
    return (bool) arp_cache_.erase(key);
}

ArpEntry *ArpProto::FindArpEntry(const ArpKey &key) {
    ArpCache::iterator it = arp_cache_.find(key);
    if (it == arp_cache_.end())
        return NULL;
    return it->second;
}
