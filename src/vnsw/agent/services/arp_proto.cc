/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "oper/nexthop.h"
#include "oper/tunnel_nh.h"
#include "oper/mirror_table.h"
#include "oper/agent_route.h"
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
    run_with_vrouter_(run_with_vrouter), ip_fabric_intf_index_(-1),
    ip_fabric_intf_(NULL), gracious_arp_entry_(NULL), max_retries_(kMaxRetries),
    retry_timeout_(kRetryTimeout), aging_timeout_(kAgingTimeout) {

    arp_nh_client_ = new ArpNHClient(io);
    memset(ip_fabric_intf_mac_, 0, ETH_ALEN);
    vid_ = agent->GetVrfTable()->Register(
                  boost::bind(&ArpProto::VrfUpdate, this, _1, _2));
    iid_ = agent->GetInterfaceTable()->Register(
                  boost::bind(&ArpProto::ItfUpdate, this, _2));
}

ArpProto::~ArpProto() {
    delete arp_nh_client_;
    agent_->GetVrfTable()->Unregister(vid_);
    agent_->GetInterfaceTable()->Unregister(iid_);
    DelGraciousArpEntry();
}

ProtoHandler *ArpProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                          boost::asio::io_service &io) {
    return new ArpHandler(agent(), info, io);
}

void ArpProto::VrfUpdate(DBTablePartBase *part, DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    ArpVrfState *state;

    state = static_cast<ArpVrfState *>(entry->GetState(part->parent(), vid_)); 
    if (entry->IsDeleted()) {
        ArpHandler::SendArpIpc(ArpHandler::VRF_DELETE, 0, vrf);
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
            if (GraciousArpEntry() && GraciousArpEntry()->Key().ip ==
                                      route->GetIpAddress().to_ulong()) {
                DelGraciousArpEntry();
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
        DelGraciousArpEntry();
        //Send Grat ARP
        ArpHandler::SendArpIpc(ArpHandler::ARP_SEND_GRACIOUS, 
                               route->GetIpAddress().to_ulong(), 
                               route->GetVrfEntry());
    }
}

void ArpProto::ItfUpdate(DBEntryBase *entry) {
    Interface *itf = static_cast<Interface *>(entry);
    if (entry->IsDeleted()) {
        if (itf->type() == Interface::PHYSICAL && 
            itf->name() == agent_->GetIpFabricItfName()) {
            ArpHandler::SendArpIpc(ArpHandler::ITF_DELETE, 0, itf->vrf());
            //assert(0);
        }
    } else {
        if (itf->type() == Interface::PHYSICAL && 
            itf->name() == agent_->GetIpFabricItfName()) {
            IPFabricIntf(itf);
            IPFabricIntfIndex(itf->id());
            if (run_with_vrouter_) {
                IPFabricIntfMac((char *)itf->mac().ether_addr_octet);
            } else {
                char mac[ETH_ALEN];
                memset(mac, 0, ETH_ALEN);
                IPFabricIntfMac(mac);
            }
        }
    }
}

bool ArpProto::TimerExpiry(ArpKey &key, ArpHandler::ArpMsgType timer_type) {
    ArpHandler::SendArpIpc(timer_type, key);
    return false;
}

void ArpProto::DelGraciousArpEntry() {
    if (gracious_arp_entry_) {
        delete gracious_arp_entry_;
        gracious_arp_entry_ = NULL;
    }
}

///////////////////////////////////////////////////////////////////////////////

ArpNHClient::ArpNHClient(boost::asio::io_service &io) : io_(io) {
    listener_id_ = Agent::GetInstance()->GetNextHopTable()->Register
                   (boost::bind(&ArpNHClient::Notify, this, _1, _2));
}

ArpNHClient::~ArpNHClient() {
    Agent::GetInstance()->GetNextHopTable()->Unregister(listener_id_);
}

void ArpNHClient::UpdateArp(Ip4Address &ip, struct ether_addr &mac,
                            const string &vrf_name, const Interface &intf,
                            DBRequest::DBOperation op, bool resolved) {
    ArpNH      *arp_nh;
    ArpNHKey   nh_key(vrf_name, ip);
    arp_nh = static_cast<ArpNH *>(Agent::GetInstance()->GetNextHopTable()->FindActiveEntry(&nh_key));

    std::string mac_str;
    ServicesSandesh::MacToString(mac.ether_addr_octet, mac_str);

    switch (op) {
    case DBRequest::DB_ENTRY_ADD_CHANGE: {
        if (arp_nh) {
            if (arp_nh->GetResolveState() && 
                memcmp(&mac, arp_nh->GetMac(), sizeof(mac)) == 0) {
                // MAC address unchanges ignore
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

	Agent::GetInstance()->GetDefaultInet4UnicastRouteTable()->ArpRoute(
                              op, ip, mac, vrf_name, intf, resolved, 32);
}

void ArpNHClient::HandleArpNHmodify(ArpNH *nh) {
    if (nh->IsDeleted()) {
        ArpHandler::SendArpIpc(ArpHandler::ARP_DELETE,
                               nh->GetIp()->to_ulong(), nh->GetVrf());
    } else if (nh->IsValid() == false) { 
        ArpHandler::SendArpIpc(ArpHandler::ARP_RESOLVE,
                               nh->GetIp()->to_ulong(), nh->GetVrf());
    }
}

void ArpNHClient::Notify(DBTablePartBase *partition, DBEntryBase *e) {
    NextHop *nh = static_cast<NextHop *>(e);

    switch(nh->GetType()) {
    case NextHop::ARP:
        HandleArpNHmodify(static_cast<ArpNH *>(nh));
        break;

    default:
        break;
    }
}
