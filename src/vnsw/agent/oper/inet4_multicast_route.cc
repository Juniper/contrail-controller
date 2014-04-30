/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>

#include <cmn/agent_cmn.h>
#include <route/route.h>

#include <oper/route_common.h>
#include <oper/vrf.h>
#include <oper/tunnel_nh.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <oper/multicast.h>
#include <controller/controller_export.h>
#include <controller/controller_peer.h>
#include <controller/controller_init.h>
#include <oper/agent_sandesh.h>

using namespace std;
using namespace boost::asio;

static void MulticastTableEnqueue(Agent *agent, const string &vrf_name,
                                  DBRequest *req) {
    AgentRouteTable *table = 
        agent->vrf_table()->GetInet4MulticastRouteTable(vrf_name);
    if (table) {
        table->Enqueue(req);
    }
}

static void MulticastTableProcess(Agent *agent, const string &vrf_name,
                                  DBRequest &req) {
    AgentRouteTable *table = 
        agent->vrf_table()->GetInet4MulticastRouteTable(vrf_name);
    if (table) {
        table->Process(req);
    }
}

DBTableBase *Inet4MulticastAgentRouteTable::CreateTable(DB *db, 
                                                      const std::string &name) {
    AgentRouteTable *table = new Inet4MulticastAgentRouteTable(db, name);
    table->Init();
    //table->InitRouteTable(db, table, name, Agent::INET4_MULTICAST);
    return table;
}

void 
Inet4MulticastAgentRouteTable::AddMulticastRoute(const string &vrf_name, 
                                                 const string &vn_name,
                                                 const Ip4Address &src_addr,
                                                 const Ip4Address &grp_addr) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    Inet4MulticastRouteKey *rt_key = new Inet4MulticastRouteKey(vrf_name, 
                                                                grp_addr, 
                                                                src_addr);
    MulticastRoute *data = new MulticastRoute(src_addr, grp_addr, 
                                              vn_name, vrf_name, 0,
                                              Composite::L3COMP);
    req.key.reset(rt_key);
    req.data.reset(data);
    MulticastTableEnqueue(Agent::GetInstance(), vrf_name, &req);
} 

void 
Inet4MulticastAgentRouteTable::AddVHostRecvRoute(const string &vm_vrf,
                                                 const string &interface_name,
                                                 const Ip4Address &addr,
                                                 bool policy) 
{
    DBRequest req;

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    Inet4MulticastRouteKey *rt_key = new Inet4MulticastRouteKey(vm_vrf, addr);
    req.key.reset(rt_key);
    InetInterfaceKey intf_key(Agent::GetInstance()->
                                     vhost_interface_name());
    ReceiveRoute *data = 
        new ReceiveRoute(intf_key, MplsTable::kInvalidLabel,
                         TunnelType::AllType(), policy,
                         Agent::GetInstance()->GetFabricVnName());
    //data->SetMulticast(true);
    req.data.reset(data);
    MulticastTableEnqueue(Agent::GetInstance(), vm_vrf, &req);
}

void 
Inet4MulticastAgentRouteTable::DeleteMulticastRoute(const string &vrf_name, 
                                                    const Ip4Address &src_addr,
                                                    const Ip4Address &grp_addr) 
{
    DBRequest req;

    req.oper = DBRequest::DB_ENTRY_DELETE;
    Inet4MulticastRouteKey *rt_key = new Inet4MulticastRouteKey(vrf_name, 
                                                                grp_addr, 
                                                                src_addr);

    req.key.reset(rt_key);
    req.data.reset(NULL);
    MulticastTableEnqueue(Agent::GetInstance(), vrf_name, &req);
}

void Inet4MulticastAgentRouteTable::Delete(const string &vrf_name,
                                           const Ip4Address &src_addr,
                                           const Ip4Address &grp_addr) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new Inet4MulticastRouteKey(vrf_name, grp_addr, src_addr));
    req.data.reset(NULL);
    MulticastTableProcess(Agent::GetInstance(), vrf_name, req);
}

AgentRoute *
Inet4MulticastRouteKey::AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const 
{
    Inet4MulticastRouteEntry * entry = new Inet4MulticastRouteEntry(vrf, dip_,
                                                                    sip_);
    return static_cast<AgentRoute *>(entry);
}

string Inet4MulticastRouteEntry::ToString() const {
    ostringstream str;
    str << "Group:";
    str << dst_addr_.to_string();
    str << " Src:";
    str << src_addr_.to_string();
    return str.str();
}

int Inet4MulticastRouteEntry::CompareTo(const Route &rhs) const {
    const Inet4MulticastRouteEntry &a = 
        static_cast<const Inet4MulticastRouteEntry &>(rhs);

    if (src_addr_ < a.src_addr_)
        return -1;

    if (src_addr_ > a.src_addr_)
        return 1;
 
    if (dst_addr_ < a.dst_addr_)
        return -1;

    if (dst_addr_ > a.dst_addr_)
        return 1;

    return 0;
}

DBEntryBase::KeyPtr Inet4MulticastRouteEntry::GetDBRequestKey() const {
    Inet4MulticastRouteKey *key = 
        new Inet4MulticastRouteKey(vrf()->GetName(), dst_addr_, 
                                   src_addr_);
    return DBEntryBase::KeyPtr(key);
}

void Inet4MulticastRouteEntry::SetKey(const DBRequestKey *key) {
    const Inet4MulticastRouteKey *k = 
        static_cast<const Inet4MulticastRouteKey *>(key);
    SetVrf(Agent::GetInstance()->vrf_table()->FindVrfFromName(k->vrf_name()));
    Ip4Address grp(k->dest_ip_addr());
    Ip4Address src(k->src_ip_addr());
    set_dest_ip_addr(grp);
    set_src_ip_addr(src);
}

bool Inet4MulticastRouteEntry::DBEntrySandesh(Sandesh *sresp, bool stale) const {
    Inet4McRouteResp *resp = static_cast<Inet4McRouteResp *>(sresp);

    RouteMcSandeshData data;
    data.set_src(src_ip_addr().to_string());
    data.set_grp(dest_ip_addr().to_string());
    MulticastGroupObject *mc_obj = MulticastHandler::GetInstance()->
        FindGroupObject(vrf()->GetName(), dest_ip_addr());
    Agent *agent = (static_cast<AgentRouteTable *>(get_table()))->agent();
    if (!stale || (mc_obj->peer_identifier() != agent->controller()->
                   multicast_sequence_number())) {
        GetActiveNextHop()->SetNHSandeshData(data.nh);
    }

    std::vector<RouteMcSandeshData> &list = 
        const_cast<std::vector<RouteMcSandeshData>&>(resp->get_route_list());
    list.push_back(data);
    return true;
}

void Inet4McRouteReq::HandleRequest() const {
    VrfEntry *vrf = 
        Agent::GetInstance()->vrf_table()->FindVrfFromId(get_vrf_index());
    if (!vrf) {
        ErrorResp *resp = new ErrorResp();
        resp->set_context(context());
        resp->Response();
        return;
    }

    AgentInet4McRtSandesh *sand = new AgentInet4McRtSandesh(vrf, 
                                                            context(), "", get_stale());
    sand->DoSandesh();
}
