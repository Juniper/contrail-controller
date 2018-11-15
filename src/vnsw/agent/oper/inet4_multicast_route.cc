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
#include <oper/vxlan.h>
#include <oper/mirror_table.h>
#include <oper/multicast.h>
#include <controller/controller_export.h>
#include <controller/controller_peer.h>
#include <controller/controller_init.h>
#include <oper/agent_sandesh.h>

using namespace std;
using namespace boost::asio;

static void MulticastTableEnqueue(Agent *agent, DBRequest *req) {
    AgentRouteTable *table = agent->fabric_inet4_multicast_table();
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
                                                 const Ip4Address &grp_addr,
                                                 ComponentNHKeyList
                                                 &component_nh_key_list) {
    DBRequest nh_req;
    NextHopKey *nh_key;
    CompositeNHData *nh_data;

    nh_key = new CompositeNHKey(Composite::L3COMP, true, component_nh_key_list,
                                vrf_name);
    nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    nh_req.key.reset(nh_key);
    nh_data = new CompositeNHData();
    nh_req.data.reset(nh_data);

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    Inet4MulticastRouteKey *rt_key = new Inet4MulticastRouteKey(vrf_name,
                                                                grp_addr,
                                                                src_addr);
    MulticastRoute *data = new MulticastRoute(vn_name,
                                              MplsTable::kInvalidLabel,
                                              VxLanTable::kInvalidvxlan_id,
                                              TunnelType::AllType(),
                                              nh_req, Composite::L3COMP, 0);
    req.key.reset(rt_key);
    req.data.reset(data);
    MulticastTableEnqueue(Agent::GetInstance(), &req);
}

void
Inet4MulticastAgentRouteTable::AddMulticastRoute(const Peer *peer,
                                    const string &vrf_name,
                                    const Ip4Address &src_addr,
                                    const Ip4Address &grp_addr,
                                    uint32_t ethernet_tag,
                                    AgentRouteData *data) {

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    Inet4MulticastRouteKey *rt_key = new Inet4MulticastRouteKey(peer, vrf_name,
                                            grp_addr, src_addr, ethernet_tag);
    req.key.reset(rt_key);
    req.data.reset(data);
    MulticastTableEnqueue(Agent::GetInstance(), &req);
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
    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE,
                            boost::uuids::nil_uuid(), interface_name);
    ReceiveRoute *data =
        new ReceiveRoute(intf_key, MplsTable::kInvalidLabel,
                         TunnelType::AllType(), policy,
                         Agent::GetInstance()->fabric_vn_name());
    //data->SetMulticast(true);
    req.data.reset(data);
    MulticastTableEnqueue(Agent::GetInstance(), &req);
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
    MulticastTableEnqueue(Agent::GetInstance(), &req);
}

void
Inet4MulticastAgentRouteTable::DeleteMulticastRoute(const Peer *peer,
                                    const string &vrf_name,
                                    const Ip4Address &src_addr,
                                    const Ip4Address &grp_addr,
                                    uint32_t ethernet_tag,
                                    COMPOSITETYPE type) {

    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    Inet4MulticastRouteKey *rt_key = new Inet4MulticastRouteKey(peer, vrf_name,
                                            grp_addr, src_addr, ethernet_tag);
    req.key.reset(rt_key);

    DBRequest nh_req;
    const BgpPeer *bgp_peer = dynamic_cast<const BgpPeer *>(peer);
    if (bgp_peer) {
        req.data.reset(new MulticastRoute("", 0, ethernet_tag,
                                    TunnelType::AllType(), nh_req, type,
                                    bgp_peer->sequence_number()));
    } else {
        req.data.reset(new MulticastRoute("", 0, ethernet_tag,
                                    TunnelType::AllType(), nh_req, type, 0));
    }

    MulticastTableEnqueue(Agent::GetInstance(), &req);
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

AgentRouteKey *Inet4MulticastRouteKey::Clone() const {
    return (new Inet4MulticastRouteKey(vrf_name_, dip_, sip_));
}

Inet4MulticastRouteEntry *
Inet4MulticastAgentRouteTable::FindRoute(const Ip4Address &grp_addr,
                            const Ip4Address &src_addr) {

    Inet4MulticastRouteEntry entry(vrf_entry(), grp_addr, src_addr);
    return static_cast<Inet4MulticastRouteEntry *>(FindActiveEntry(&entry));
}

string Inet4MulticastRouteKey::ToString() const {
    ostringstream str;
    str << "Group:";
    str << dip_.to_string();
    str << " Src:";
    str << sip_.to_string();
    return str.str();
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

bool Inet4MulticastRouteEntry::ReComputePathAdd(AgentPath *path) {
    return ReComputeMulticastPaths(path, false);
}

bool Inet4MulticastRouteEntry::ReComputePathDeletion(AgentPath *path) {
    return ReComputeMulticastPaths(path, true);
}

bool Inet4MulticastRouteEntry::DBEntrySandesh(Sandesh *sresp, bool stale) const {
    Inet4McRouteResp *resp = static_cast<Inet4McRouteResp *>(sresp);

    RouteMcSandeshData data;
    data.set_src(src_ip_addr().to_string());
    data.set_grp(dest_ip_addr().to_string());
    MulticastGroupObject *mc_obj = MulticastHandler::GetInstance()->
        FindGroupObject(vrf()->GetName(), Ip4Address(), dest_ip_addr());
    Agent *agent = (static_cast<AgentRouteTable *>(get_table()))->agent();
    if (!stale || (mc_obj && (mc_obj->peer_identifier() != agent->controller()->
                   multicast_sequence_number()))) {
        GetActiveNextHop()->SetNHSandeshData(data.nh);
    }

    std::vector<RouteMcSandeshData> &list =
        const_cast<std::vector<RouteMcSandeshData>&>(resp->get_route_list());
    list.push_back(data);
    return true;
}

bool Inet4MulticastRouteEntry::DBEntrySandesh(Sandesh *sresp,
                                    const Ip4Address &src_addr,
                                    const Ip4Address &dst_addr,
                                    bool stale) const {

    if (src_addr_ == src_addr && dst_addr_ == dst_addr) {
        return DBEntrySandesh(sresp, stale);
    }

    return false;
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

    AgentSandeshPtr sand(new AgentInet4McRtSandesh(vrf, context(), "",
                                                   get_stale()));
    boost::system::error_code ec;
    Ip4Address zero_addr = IpAddress::from_string("0.0.0.0", ec).to_v4();
    Ip4Address src_ip, grp_ip;
    if (get_src_ip().empty()) {
        src_ip = zero_addr;
    } else {
        src_ip = Ip4Address::from_string(get_src_ip(), ec);
    }
    if (get_grp_ip().empty()) {
        sand.reset(new AgentInet4McRtSandesh(vrf, context(), "", get_stale()));
    } else {
        grp_ip = Ip4Address::from_string(get_grp_ip(), ec);
        sand.reset(new AgentInet4McRtSandesh(vrf, context(), "", src_ip,
                                grp_ip, get_stale()));
    }
    sand->DoSandesh(sand);
}

AgentSandeshPtr Inet4MulticastAgentRouteTable::GetAgentSandesh
(const AgentSandeshArguments *args, const std::string &context) {
    return AgentSandeshPtr(new AgentInet4McRtSandesh(vrf_entry(), context, "",
                                                     false));
}
