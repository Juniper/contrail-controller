/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "oper/nexthop.h"
#include "oper/tunnel_nh.h"
#include "oper/mirror_table.h"
#include "oper/route_common.h"
#include "ksync/ksync_index.h"
#include "ksync/interface_ksync.h"
#include "pkt/pkt_init.h"
#include "services/arp_proto.h"
#include "services/services_sandesh.h"
#include "services_init.h"

void ArpProto::Init() {
}

void ArpProto::Shutdown() {
}

ArpProto::ArpProto(Agent *agent, boost::asio::io_service &io,
                   bool run_with_vrouter) :
    Proto(agent, "Agent::Services", PktHandler::ARP, io),
    run_with_vrouter_(run_with_vrouter), ip_fabric_interface_index_(-1),
    ip_fabric_interface_(NULL), gracious_arp_entry_(NULL),
    max_retries_(kMaxRetries), retry_timeout_(kRetryTimeout),
    aging_timeout_(kAgingTimeout) {

    memset(ip_fabric_interface_mac_, 0, ETH_ALEN);
    vid_ = agent->GetVrfTable()->Register(
                  boost::bind(&ArpProto::VrfNotify, this, _1, _2));
    iid_ = agent->GetInterfaceTable()->Register(
                  boost::bind(&ArpProto::InterfaceNotify, this, _2));
    nhid_ = agent->GetNextHopTable()->Register(
                  boost::bind(&ArpProto::NextHopNotify, this, _2));
}

ArpProto::~ArpProto() {
    agent_->GetVrfTable()->Unregister(vid_);
    agent_->GetInterfaceTable()->Unregister(iid_);
    agent_->GetNextHopTable()->Unregister(nhid_);
    del_gracious_arp_entry();
    assert(arp_cache_.size() == 0);
}

ProtoHandler *ArpProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                          boost::asio::io_service &io) {
    return new ArpHandler(agent(), info, io);
}

void ArpProto::VrfNotify(DBTablePartBase *part, DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    ArpVrfState *state;

    state = static_cast<ArpVrfState *>(entry->GetState(part->parent(), vid_)); 
    if (entry->IsDeleted()) {
        SendArpIpc(ArpHandler::VRF_DELETE, 0, vrf);
        if (state) {
            vrf->GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->
                Unregister(fabric_route_table_listener_);
            entry->ClearState(part->parent(), vid_);
            delete state;
        }
        return;
    }

    if (!state && vrf->GetName() == agent_->GetDefaultVrf()) {
        state = new ArpVrfState;
        //Set state to seen
        state->seen_ = true;
        fabric_route_table_listener_ = vrf->
            GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->
            Register(boost::bind(&ArpProto::RouteUpdate, this,  _1, _2));
        entry->SetState(part->parent(), vid_, state);
    }
}

void ArpProto::RouteUpdate(DBTablePartBase *part, DBEntryBase *entry) {
    Inet4UnicastRouteEntry *route = static_cast<Inet4UnicastRouteEntry *>(entry);
    ArpRouteState *state;

    state = static_cast<ArpRouteState *>
        (entry->GetState(part->parent(), fabric_route_table_listener_));

    if (entry->IsDeleted()) {
        if (state) {
            if (gracious_arp_entry() && gracious_arp_entry()->key().ip ==
                                        route->GetIpAddress().to_ulong()) {
                del_gracious_arp_entry();
            }
            entry->ClearState(part->parent(), fabric_route_table_listener_);
            delete state;
        }
        return;
    }

    if (!state && route->GetActiveNextHop()->GetType() == NextHop::RECEIVE) {
        state = new ArpRouteState;
        state->seen_ = true;
        entry->SetState(part->parent(), fabric_route_table_listener_, state);
        del_gracious_arp_entry();
        //Send Grat ARP
        SendArpIpc(ArpHandler::ARP_SEND_GRACIOUS, 
                   route->GetIpAddress().to_ulong(), route->GetVrfEntry());
    }
}

void ArpProto::InterfaceNotify(DBEntryBase *entry) {
    Interface *itf = static_cast<Interface *>(entry);
    if (entry->IsDeleted()) {
        if (itf->type() == Interface::PHYSICAL && 
            itf->name() == agent_->GetIpFabricItfName()) {
            SendArpIpc(ArpHandler::ITF_DELETE, 0, itf->vrf());
            //assert(0);
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
            SendArpIpc(ArpHandler::ARP_DELETE, arp_nh->GetIp()->to_ulong(),
                       arp_nh->GetVrf());
        } else if (arp_nh->IsValid() == false) { 
            SendArpIpc(ArpHandler::ARP_RESOLVE, arp_nh->GetIp()->to_ulong(),
                       arp_nh->GetVrf());
        }
        break;
    }

    default:
        break;
    }
}

bool ArpProto::TimerExpiry(ArpKey &key, ArpHandler::ArpMsgType timer_type) {
    SendArpIpc(timer_type, key);
    return false;
}

void ArpProto::del_gracious_arp_entry() {
    if (gracious_arp_entry_) {
        delete gracious_arp_entry_;
        gracious_arp_entry_ = NULL;
    }
}

void ArpProto::UpdateArp(Ip4Address &ip, struct ether_addr &mac,
                         const string &vrf_name, const Interface &intf,
                         DBRequest::DBOperation op, bool resolved) {
    ArpNH      *arp_nh;
    ArpNHKey   nh_key(vrf_name, ip);
    arp_nh = static_cast<ArpNH *>(agent_->GetNextHopTable()->
                                  FindActiveEntry(&nh_key));

    std::string mac_str;
    ServicesSandesh::MacToString(mac.ether_addr_octet, mac_str);

    switch (op) {
    case DBRequest::DB_ENTRY_ADD_CHANGE: {
        if (arp_nh) {
            if (arp_nh->GetResolveState() && 
                memcmp(&mac, arp_nh->GetMac(), sizeof(mac)) == 0) {
                // MAC address unchanged, ignore
                return;
            }
        }

        ARP_TRACE(Trace, "Add", ip.to_string(), vrf_name, mac_str);
        break;
    }
    case DBRequest::DB_ENTRY_DELETE:
        if (!arp_nh)
            return;
        ARP_TRACE(Trace, "Delete", ip.to_string(), vrf_name, mac_str);
        break;

    default:
        assert(0);
    }

	agent_->GetDefaultInet4UnicastRouteTable()->ArpRoute(
                              op, ip, mac, vrf_name, intf, resolved, 32);
}

void ArpProto::SendArpIpc(ArpHandler::ArpMsgType type, 
                          in_addr_t ip, const VrfEntry *vrf) {
    ArpIpc *ipc = new ArpIpc(type, ip, vrf);
    agent_->pkt()->pkt_handler()->SendMessage(PktHandler::ARP, ipc);
}

void ArpProto::SendArpIpc(ArpHandler::ArpMsgType type, ArpKey &key) {
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
