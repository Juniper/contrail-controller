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
#include <controller/controller_export.h>
#include <controller/controller_peer.h>
#include <oper/agent_sandesh.h>

using namespace std;
using namespace boost::asio;

static void Layer2TableEnqueue(Agent *agent, const string &vrf_name,
                               DBRequest *req) {
    AgentRouteTable *table = 
        agent->vrf_table()->GetLayer2RouteTable(vrf_name);
    if (table) {
        table->Enqueue(req);
    }
}

static void Layer2TableProcess(Agent *agent, const string &vrf_name,
                               DBRequest &req) {
    AgentRouteTable *table = 
        agent->vrf_table()->GetLayer2RouteTable(vrf_name);
    if (table) {
        table->Process(req);
    }
}

DBTableBase *Layer2AgentRouteTable::CreateTable(DB *db, 
                                                const std::string &name) {
    AgentRouteTable *table = new Layer2AgentRouteTable(db, name);
    table->Init();
    //table->InitRouteTable(db, table, name, Agent::LAYER2);
    return table;
}

AgentRoute *
Layer2RouteKey::AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const 
{
    Layer2RouteEntry * entry = new Layer2RouteEntry(vrf, dmac_, vm_ip_, plen_, 
                                                    peer()->GetType(), is_multicast); 
    return static_cast<AgentRoute *>(entry);
}

void Layer2AgentRouteTable::AddLocalVmRouteReq(const Peer *peer,
                                               const uuid &intf_uuid,
                                               const string &vn_name, 
                                               const string &vrf_name,
                                               uint32_t mpls_label,
                                               uint32_t vxlan_id,
                                               struct ether_addr &mac,
                                               const Ip4Address &vm_ip,
                                               uint32_t plen) { 
    assert(peer);
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Layer2RouteKey *key = new Layer2RouteKey(peer, vrf_name, mac, vm_ip, 32);
    req.key.reset(key);

    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    SecurityGroupList sg_list;
    LocalVmRoute *data = new LocalVmRoute(intf_key, mpls_label, vxlan_id,
                                          false, vn_name,
                                          InterfaceNHFlags::LAYER2,
                                          sg_list);
    data->set_tunnel_bmap(TunnelType::AllType());
    req.data.reset(data);
    Layer2TableEnqueue(Agent::GetInstance(), vrf_name, &req);
}

void Layer2AgentRouteTable::AddLocalVmRoute(const Peer *peer,
                                            const uuid &intf_uuid,
                                            const string &vn_name, 
                                            const string &vrf_name,
                                            uint32_t mpls_label,
                                            uint32_t vxlan_id,
                                            struct ether_addr &mac,
                                            const Ip4Address &vm_ip,
                                            uint32_t plen) { 
    assert(peer);
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Layer2RouteKey *key = new Layer2RouteKey(peer, vrf_name, mac, vm_ip, 32);
    req.key.reset(key);

    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    SecurityGroupList sg_list;
    LocalVmRoute *data = new LocalVmRoute(intf_key, mpls_label, vxlan_id,
                                          false, vn_name,
                                          InterfaceNHFlags::LAYER2,
                                          sg_list);
    data->set_tunnel_bmap(TunnelType::AllType());
    req.data.reset(data);
    Layer2TableProcess(Agent::GetInstance(), vrf_name, req);
}

void Layer2AgentRouteTable::AddLayer2BroadcastRoute(const string &vrf_name,
                                                    const string &vn_name,
                                                    const Ip4Address &dip,
                                                    const Ip4Address &sip,
                                                    int vxlan_id) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Layer2RouteKey *key = 
        new Layer2RouteKey(Agent::GetInstance()->local_vm_peer(), vrf_name);
    req.key.reset(key);

    MulticastRoute *data = new MulticastRoute(sip, dip, vn_name, vrf_name, vxlan_id,
                                              Composite::L2COMP); 
    req.data.reset(data);
    Layer2TableEnqueue(Agent::GetInstance(), vrf_name, &req);
}


void Layer2AgentRouteTable::AddRemoteVmRouteReq(const Peer *peer,
                                                const string &vrf_name,
                                                TunnelType::TypeBmap bmap,
                                                const Ip4Address &server_ip,
                                                uint32_t label,
                                                struct ether_addr &mac,
                                                const Ip4Address &vm_ip,
                                                uint32_t plen) { 
    assert(peer);
    DBRequest nh_req;
    nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    if (bmap != (1 << TunnelType::VXLAN) || 
        (TunnelType::ComputeType(TunnelType::AllType()) != 
                                 (1 << TunnelType::VXLAN))) {
    }

    NextHopKey *nh_key = 
        new TunnelNHKey(Agent::GetInstance()->GetDefaultVrf(),
                        Agent::GetInstance()->GetRouterId(), server_ip,
                        false, TunnelType::ComputeType(bmap));
    nh_req.key.reset(nh_key);

    TunnelNHData *nh_data = new TunnelNHData();
    nh_req.data.reset(nh_data);

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Layer2RouteKey *key = new Layer2RouteKey(peer, vrf_name, mac, vm_ip, plen);
    req.key.reset(key);

    SecurityGroupList sg_list;
    RemoteVmRoute *data = 
        new RemoteVmRoute(Agent::GetInstance()->GetDefaultVrf(),
                          server_ip, label, "", bmap, 
                          sg_list, nh_req);
    req.data.reset(data);
    ostringstream str;
    str << (ether_ntoa ((struct ether_addr *)&mac));

    Layer2TableEnqueue(Agent::GetInstance(), vrf_name, &req);
}

void Layer2AgentRouteTable::DeleteReq(const Peer *peer, const string &vrf_name,
                                      const struct ether_addr &mac) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    Layer2RouteKey *key = new Layer2RouteKey(peer, vrf_name, mac);
    req.key.reset(key);
    req.data.reset(NULL);
    Layer2TableEnqueue(Agent::GetInstance(), vrf_name, &req);
}

void Layer2AgentRouteTable::Delete(const Peer *peer, const string &vrf_name,
                                   const struct ether_addr &mac) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    Layer2RouteKey *key = new Layer2RouteKey(peer, vrf_name, mac);
    req.key.reset(key);
    req.data.reset(NULL);
    Layer2TableProcess(Agent::GetInstance(), vrf_name, req);
}

void Layer2AgentRouteTable::DeleteBroadcastReq(const string &vrf_name) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    Layer2RouteKey *key = 
        new Layer2RouteKey(Agent::GetInstance()->local_vm_peer(), vrf_name);
    req.key.reset(key);
    req.data.reset(NULL);
    Layer2TableEnqueue(Agent::GetInstance(), vrf_name, &req);
}

string Layer2RouteEntry::ToString() const {
    ostringstream str;
    str << (ether_ntoa ((struct ether_addr *)&mac_));
    return str.str();
}

int Layer2RouteEntry::CompareTo(const Route &rhs) const {
    const Layer2RouteEntry &a = static_cast<const Layer2RouteEntry &>(rhs);

    return (memcmp(&mac_, &a.mac_, sizeof(struct ether_addr)));
};

DBEntryBase::KeyPtr Layer2RouteEntry::GetDBRequestKey() const {
    Layer2RouteKey *key =
        new Layer2RouteKey(Agent::GetInstance()->local_vm_peer(), 
                           vrf()->GetName(), mac_);
    return DBEntryBase::KeyPtr(key);
}

void Layer2RouteEntry::SetKey(const DBRequestKey *key) {
    const Layer2RouteKey *k = static_cast<const Layer2RouteKey *>(key);
    SetVrf(Agent::GetInstance()->vrf_table()->FindVrfFromName(k->vrf_name()));
    memcpy(&mac_, &(k->GetMac()), sizeof(struct ether_addr));
}

bool Layer2RouteEntry::DBEntrySandesh(Sandesh *sresp, bool stale) const {
    Layer2RouteResp *resp = static_cast<Layer2RouteResp *>(sresp);
    RouteL2SandeshData data;
    data.set_mac(ToString());

    for (Route::PathList::const_iterator it = GetPathList().begin();
         it != GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path) {
            if (stale && !path->is_stale())
                continue;
            PathSandeshData pdata;
            path->SetSandeshData(pdata);
            if (is_multicast()) {
                pdata.set_vxlan_id(path->vxlan_id());
            }
            data.path_list.push_back(pdata);
        }
    }
    std::vector<RouteL2SandeshData> &list = 
        const_cast<std::vector<RouteL2SandeshData>&>(resp->get_route_list());
    list.push_back(data);
    return true;
}

void Layer2RouteReq::HandleRequest() const {
    VrfEntry *vrf = 
        Agent::GetInstance()->GetVrfTable()->FindVrfFromId(get_vrf_index());
    if (!vrf) {
        ErrorResp *resp = new ErrorResp();
        resp->set_context(context());
        resp->Response();
        return;
    }

    AgentLayer2RtSandesh *sand = new AgentLayer2RtSandesh(vrf, 
                                                          context(), "", get_stale());
    sand->DoSandesh();
}
