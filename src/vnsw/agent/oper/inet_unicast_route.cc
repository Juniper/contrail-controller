/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>

#include "base/task_annotations.h"
#include <boost/foreach.hpp>
#include <cmn/agent_cmn.h>
#include "net/address_util.h"
#include <route/route.h>
#include <oper/ecmp_load_balance.h>
#include <oper/route_common.h>
#include <oper/vrf.h>
#include <oper/tunnel_nh.h>
#include <oper/mpls.h>
#include <oper/vxlan.h>
#include <oper/mirror_table.h>
#include <oper/multicast.h>
#include <controller/controller_export.h>
#include <controller/controller_peer.h>
#include <controller/controller_route_path.h>
#include <oper/agent_sandesh.h>

using namespace std;
using namespace boost::asio;

AgentRoute *
InetUnicastRouteKey::AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const {
    InetUnicastRouteEntry * entry =
        new InetUnicastRouteEntry(vrf, dip_, plen_, is_multicast);
    return static_cast<AgentRoute *>(entry);
}

AgentRouteKey *InetUnicastRouteKey::Clone() const {
    return (new InetUnicastRouteKey(peer(), vrf_name_, dip_, plen_));
}

/////////////////////////////////////////////////////////////////////////////
// InetUnicastAgentRouteTable functions
/////////////////////////////////////////////////////////////////////////////
InetUnicastAgentRouteTable::InetUnicastAgentRouteTable(DB *db,
                                                       const std::string &name) :
    AgentRouteTable(db, name), walkid_(DBTableWalker::kInvalidWalkerId) {

    if (name.find("uc.route.0") != std::string::npos) {
        type_ = Agent::INET4_UNICAST;
    } else if (name.find("uc.route6.0") != std::string::npos) {
        type_ = Agent::INET6_UNICAST;
    } else if (name.find("evpn.route.0") != std::string::npos) {
        type_ = Agent::EVPN;
    } else if (name.find("l2.route.0") != std::string::npos) {
        type_ = Agent::BRIDGE;
    } else if (name.find("mc.route.0") != std::string::npos) {
        type_ = Agent::INET4_MULTICAST;
    } else {
        type_ = Agent::INVALID;
    }
}

DBTableBase *
InetUnicastAgentRouteTable::CreateTable(DB *db, const std::string &name) {
    AgentRouteTable *table = new InetUnicastAgentRouteTable(db, name);
    table->Init();
    return table;
}

InetUnicastRouteEntry *
InetUnicastAgentRouteTable::FindLPM(const IpAddress &ip) {
    uint32_t plen = 128;
    if (ip.is_v4()) {
        plen = 32;
    }
    InetUnicastRouteEntry key(NULL, ip, plen, false);
    return tree_.LPMFind(&key);
}

InetUnicastRouteEntry *
InetUnicastAgentRouteTable::FindLPM(const InetUnicastRouteEntry &rt_key) {
    return tree_.LPMFind(&rt_key);
}

InetUnicastRouteEntry *
InetUnicastAgentRouteTable::FindResolveRoute(const Ip4Address &ip) {
    uint8_t plen = 32;
    InetUnicastRouteEntry *rt = NULL;
    do {
        InetUnicastRouteEntry key(NULL, ip, plen, false);
        rt = tree_.LPMFind(&key);
        if (rt) {
            const NextHop *nh = rt->GetActiveNextHop();
            if (nh && nh->GetType() == NextHop::RESOLVE)
                return rt;
        }
    } while (rt && --plen);

    return NULL;
}

InetUnicastRouteEntry *
InetUnicastAgentRouteTable::FindResolveRoute(const string &vrf_name,
                                             const Ip4Address &ip) {
    VrfEntry *vrf =
        Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    InetUnicastAgentRouteTable *rt_table =
              static_cast<InetUnicastAgentRouteTable *>
              (vrf->GetInet4UnicastRouteTable());
    return rt_table->FindResolveRoute(ip);
}

static void Inet4UnicastTableEnqueue(Agent *agent, DBRequest *req) {
    AgentRouteTable *table = agent->fabric_inet4_unicast_table();
    if (table) {
        table->Enqueue(req);
    }
}

static void Inet6UnicastTableEnqueue(Agent *agent, const string &vrf_name,
                                     DBRequest *req) {
    AgentRouteTable *table = agent->fabric_inet4_unicast_table();
    if (table) {
        table->Enqueue(req);
    }
}

static void InetUnicastTableEnqueue(Agent *agent, const string &vrf,
                                    DBRequest *req) {
    InetUnicastRouteKey *key = static_cast<InetUnicastRouteKey *>(req->key.get());
    if (key->addr().is_v4()) {
        Inet4UnicastTableEnqueue(agent, req);
    } else if (key->addr().is_v6()) {
        Inet6UnicastTableEnqueue(agent, vrf, req);
    }
}

static void Inet4UnicastTableProcess(Agent *agent, const string &vrf_name,
                                     DBRequest &req) {
    AgentRouteTable *table =
        agent->vrf_table()->GetInet4UnicastRouteTable(vrf_name);
    if (table) {
        table->Process(req);
    }
}

static void Inet6UnicastTableProcess(Agent *agent, const string &vrf_name,
                                     DBRequest &req) {
    AgentRouteTable *table =
        agent->vrf_table()->GetInet6UnicastRouteTable(vrf_name);
    if (table) {
        table->Process(req);
    }
}

static void InetUnicastTableProcess(Agent *agent, const string &vrf_name,
                                    DBRequest &req) {
    InetUnicastRouteKey *key = static_cast<InetUnicastRouteKey *>(req.key.get());
    if (key->addr().is_v4()) {
        Inet4UnicastTableProcess(agent, vrf_name, req);
    } else if (key->addr().is_v6()) {
        Inet6UnicastTableProcess(agent, vrf_name, req);
    }
}

/*
 * Traverse all smaller subnets w.r.t. route sent and mark the arp flood flag
 * accordingly.
 */
bool InetUnicastAgentRouteTable::ResyncSubnetRoutes(const InetUnicastRouteEntry *rt,
                                                    bool add_change)
{
    const IpAddress addr = rt->addr();
    uint16_t plen = rt->plen();
    InetUnicastRouteEntry *lpm_rt = GetNextNonConst(rt);

    Ip4Address v4_parent_mask;
    Ip6Address v6_parent_mask;

    if (GetTableType() == Agent::INET4_UNICAST) {
        v4_parent_mask = Address::GetIp4SubnetAddress(addr.to_v4(),
                                                      plen);
    } else {
        v6_parent_mask = Address::GetIp6SubnetAddress(addr.to_v6(),
                                                      plen);
    }

    while ((lpm_rt != NULL) && (plen < lpm_rt->plen())) {
        if (GetTableType() == Agent::INET4_UNICAST) {
            Ip4Address node_mask =
                Address::GetIp4SubnetAddress(lpm_rt->addr().to_v4(),
                                             plen);
            if (v4_parent_mask != node_mask)
                break;

        } else {
            Ip6Address node_mask =
                Address::GetIp6SubnetAddress(lpm_rt->addr().to_v6(),
                                             plen);
            if (v6_parent_mask != node_mask)
                break;
        }

        //Ignored all non subnet routes.
        if (lpm_rt->IsHostRoute() == false) {
            bool notify = false;
            if (lpm_rt->ipam_subnet_route() != add_change) {
                lpm_rt->set_ipam_subnet_route(add_change);
                notify = true;
            }

            if (lpm_rt->proxy_arp() == true) {
                if (add_change == true) {
                    lpm_rt->set_proxy_arp(false);
                    notify = true;
                } 
            }

            if (notify) {
                //Send notify 
                NotifyEntry(lpm_rt);
            }
        }

        lpm_rt = GetNextNonConst(lpm_rt);
    }
    return false;
}

//Null peer means sync is done on routes.
//If peer is specified then path belonging to peer will be resync'd
//Functions in TraverseHostRoutesInSubnet:
//1) If subnet rt added does not have any supernet then visit all unresolved
//   routes and resync them.
//2) If supernet route is present then traverse lpm routes under this newly
//   added subnet and resync all host routes pointing to supernet route of this
//   subnet.
//3) Skip all the host routes present under this subnet belonging to a better
//   subnet(better subnet is the one which comes under subnet being added).
//   e.g. 1.1.1.10/24 is a better subnet in 1.1.1.10/16 while supernet can be
//   1.1.1.0/8   
void
InetUnicastAgentRouteTable::TraverseHostRoutesInSubnet(InetUnicastRouteEntry *rt,
                                                       const Peer *peer)
{
    const IpAddress addr = rt->addr();
    uint16_t plen = rt->plen();
    InetUnicastRouteEntry *supernet_rt = GetSuperNetRoute(rt->addr());

    //If supernet route is NULL, then this is the default route and visit to
    //unresolved route for resync should suffice.
    if (supernet_rt == NULL) {
        //Resync all unresolved routes.
        EvaluateUnresolvedRoutes();
        return;
    }
    //So suernet route is present and this subnet route is a better route.
    //Look for all routes which were pointing to supernet route and move them
    //to this subnet route, if they fall in same subnet.
    IpAddress parent_mask = GetSubnetAddress(addr, plen);
    for (InetUnicastRouteEntry *lpm_rt = GetNextNonConst(rt);
         (lpm_rt != NULL) && (plen < lpm_rt->plen());
         lpm_rt= GetNextNonConst(lpm_rt)) {
        IpAddress node_mask = GetSubnetAddress(lpm_rt->addr(), plen);
        if (parent_mask != node_mask)
            break;

        //Non host route, lets continue as this route will have its own NH and it
        //need not be modified.
        if (lpm_rt->IsHostRoute() == false) {
            continue;
        }

        //If supernet route of this subnet route is not parent of lpm_rt added,
        //then skip as its not lpm_rt dependant as well.
        //There may be some other subnet which is handling it.
        //e.g. 1.1.0.0/8, 1.1.0.0/16, 1.1.1.0/24, 1.1.1.1, 1.1.0.1
        //Here parent of 1.1.1.1 will be 1.1.1.0/24 and not 1.1.0.0/16.
        //So if subnet route getting added is 1.1.0.0/16 in presence of 1.1.1.0/24
        //then 1.1.1.1 should not be modified.
        if (lpm_rt->GetActivePath()->dependant_rt() != supernet_rt)
            continue;


        //Proceed only for host route
        //Resync will ensure that subnet route is added to lpm host route
        //dependant rt(gw_ip).
        lpm_rt->EnqueueRouteResync(); 
    }
}

IpAddress
InetUnicastAgentRouteTable::GetSubnetAddress(const IpAddress &addr,
                                             uint16_t plen) const {
    if (type_ == Agent::INET4_UNICAST) {
        return (Address::GetIp4SubnetAddress(addr.to_v4(), plen));
    }
    return (Address::GetIp6SubnetAddress(addr.to_v6(), plen));
}

//Tries searching for subnet route to which this route belongs.
//Route itself can be a subnet/host. To search for supernet plen is reduced by
//one and findlpm is issued.
InetUnicastRouteEntry *
InetUnicastAgentRouteTable::GetSuperNetRoute(const IpAddress &addr) {
    InetUnicastRouteEntry key(NULL, addr, (GetHostPlen(addr) - 1),
                              false);
    InetUnicastRouteEntry *parent_key = FindLPM(key);
    return parent_key;
}

/////////////////////////////////////////////////////////////////////////////
// Inet4UnicastAgentRouteEntry functions
/////////////////////////////////////////////////////////////////////////////
InetUnicastRouteEntry::InetUnicastRouteEntry(VrfEntry *vrf,
                                             const IpAddress &addr,
                                             uint8_t plen,
                                             bool is_multicast) :
    AgentRoute(vrf, is_multicast), plen_(plen), ipam_subnet_route_(false),
    proxy_arp_(false) {
        if (addr.is_v4()) {
        addr_ = Address::GetIp4SubnetAddress(addr.to_v4(), plen);
    } else {
        addr_ = Address::GetIp6SubnetAddress(addr.to_v6(), plen);
    }
}

string InetUnicastRouteKey::ToString() const {
    ostringstream str;
    str << dip_.to_string();
    str << "/";
    str << (int)plen_;
    return str.str();
}

string InetUnicastRouteEntry::ToString() const {
    ostringstream str;
    str << addr_.to_string();
    str << "/";
    str << (int)plen_;
    return str.str();
}

int InetUnicastRouteEntry::CompareTo(const Route &rhs) const {
    const InetUnicastRouteEntry &a =
        static_cast<const InetUnicastRouteEntry &>(rhs);

    if (addr_ < a.addr_) {
        return -1;
    }

    if (addr_ > a.addr_) {
        return 1;
    }

    if (plen_ < a.plen_) {
        return -1;
    }

    if (plen_ > a.plen_) {
        return 1;
    }

    return 0;
}

DBEntryBase::KeyPtr InetUnicastRouteEntry::GetDBRequestKey() const {
    Agent *agent =
        (static_cast<InetUnicastAgentRouteTable *>(get_table()))->agent();
    InetUnicastRouteKey *key =
        new InetUnicastRouteKey(agent->local_peer(),
                                 vrf()->GetName(), addr_, plen_);
    return DBEntryBase::KeyPtr(key);
}

void InetUnicastRouteEntry::SetKey(const DBRequestKey *key) {
    Agent *agent = 
        (static_cast<InetUnicastAgentRouteTable *>(get_table()))->agent();
    const InetUnicastRouteKey *k = 
        static_cast<const InetUnicastRouteKey*>(key);
    SetVrf(agent->vrf_table()->FindVrfFromName(k->vrf_name()));
    IpAddress tmp(k->addr());
    set_addr(tmp);
    set_plen(k->plen());
}

bool InetUnicastRouteEntry::IsHostRoute() const {
    InetUnicastAgentRouteTable *table =
        static_cast<InetUnicastAgentRouteTable *>(get_table());
    if (table->GetTableType() == Agent::INET4_UNICAST) {
        if (plen_ != Address::kMaxV4PrefixLen)
            return false;
    } else if (table->GetTableType() == Agent::INET6_UNICAST) {
        if (plen_ != Address::kMaxV6PrefixLen)
            return false;
    }
    return true;
}

bool InetUnicastRouteEntry::ModifyEcmpPath(const IpAddress &dest_addr,
                                            uint8_t plen, const VnListType &vn_list,
                                            uint32_t label, bool local_ecmp_nh,
                                            const string &vrf_name,
                                            SecurityGroupList sg_list,
                                            const CommunityList &communities,
                                            const PathPreference &path_preference,
                                            TunnelType::TypeBmap tunnel_bmap,
                                            const EcmpLoadBalance &ecmp_load_balance,
                                            DBRequest &nh_req,
                                            Agent* agent, AgentPath *path,
                                            const string &route_str,
                                            bool alloc_label) {
    bool ret = false;
    NextHop *nh = NULL;

    agent->nexthop_table()->Process(nh_req);
    if (alloc_label) {
        NextHopKey *key = static_cast<NextHopKey *>(nh_req.key.get());
        // Create MPLS label and point it to Composite NH
        label = agent->mpls_table()->CreateRouteLabel(
                    MplsTable::kInvalidLabel, key, vrf_name,
                    route_str);
    }
    nh = static_cast<NextHop *>(agent->nexthop_table()->
                                FindActiveEntry(nh_req.key.get()));
    if (nh == NULL) {
        VrfEntry *vrf = agent->vrf_table()->FindVrfFromName(vrf_name);
        if (vrf->IsDeleted())
            return ret;
        assert(0);
    }
    if (path->label() != label) {
        path->set_label(label);
        ret = true;
    }

    ret = SyncEcmpPath(path, sg_list, communities, path_preference,
                       tunnel_bmap, ecmp_load_balance);

    path->set_dest_vn_list(vn_list);
    ret = true;
    path->set_unresolved(false);

    if (path->ChangeNH(agent, nh) == true)
        ret = true;

    return ret;
}

// Function to create a ECMP path from path1 and path2
// Creates Composite-NH with 2 Component-NH (one for each of path1 and path2)
// Creates a new MPLS Label for the ECMP path
AgentPath *InetUnicastRouteEntry::AllocateEcmpPath(Agent *agent,
                                                   const AgentPath *path1,
                                                   const AgentPath *path2) {
    // Allocate and insert a path
    AgentPath *path = new AgentPath(agent->ecmp_peer(), NULL);
    InsertPath(path);

    const NextHop* path1_nh = path1->ComputeNextHop(agent);
    bool composite_nh_policy = path1_nh->NexthopToInterfacePolicy();

    // Create Component NH to be added to ECMP path
    DBEntryBase::KeyPtr key1 = path1_nh->GetDBRequestKey();
    NextHopKey *nh_key1 = static_cast<NextHopKey *>(key1.release());
    std::auto_ptr<const NextHopKey> nh_akey1(nh_key1);
    nh_key1->SetPolicy(false);
    ComponentNHKeyPtr component_nh_data1(new ComponentNHKey(path1->label(),
                                                            nh_akey1));

    const NextHop* path2_nh = path2->ComputeNextHop(agent);
    if (!composite_nh_policy) {
        composite_nh_policy = path2_nh->NexthopToInterfacePolicy();
    }
    DBEntryBase::KeyPtr key2 = path2_nh->GetDBRequestKey();
    NextHopKey *nh_key2 = static_cast<NextHopKey *>(key2.release());
    std::auto_ptr<const NextHopKey> nh_akey2(nh_key2);
    nh_key2->SetPolicy(false);
    ComponentNHKeyPtr component_nh_data2(new ComponentNHKey(path2->label(),
                                                            nh_akey2));

    ComponentNHKeyList component_nh_list;
    component_nh_list.push_back(component_nh_data1);
    component_nh_list.push_back(component_nh_data2);

    // Directly call AddChangePath to update NH in the ECMP path
    // It will also create CompositeNH if necessary
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(Composite::LOCAL_ECMP,
                                        composite_nh_policy, component_nh_list,
                                        vrf()->GetName()));
    nh_req.data.reset(new CompositeNHData());

    InetUnicastRouteEntry::ModifyEcmpPath(addr_, plen_, path2->dest_vn_list(),
                                          MplsTable::kInvalidLabel, true,
                                          vrf()->GetName(),
                                          path2->sg_list(),
                                          path2->communities(),
                                          path2->path_preference(),
                                          path2->tunnel_bmap(),
                                          path2->ecmp_load_balance(),
                                          nh_req, agent, path, ToString(),
                                          true);

    RouteInfo rt_info;
    FillTrace(rt_info, AgentRoute::CHANGE_PATH, path);
    AgentRouteTable *table = static_cast<AgentRouteTable *>(get_table());
    OPER_TRACE_ROUTE_ENTRY(Route, table, rt_info);
    AGENT_ROUTE_LOG(table, "Path change", ToString(), vrf()->GetName(),
                    GETPEERNAME(agent->ecmp_peer()));

    return path;
}

// Handle deletion of a path in route. If the path being deleted is part of
// ECMP, then deletes the Component-NH for the path.
// Delete ECMP path if there is single Component-NH in Composite-NH
bool InetUnicastRouteEntry::EcmpDeletePath(AgentPath *path) {
    if (path->peer() == NULL) {
        return false;
    }

    if (path->peer()->GetType() != Peer::LOCAL_VM_PORT_PEER) {
        return false;
    }

    Agent *agent = 
        (static_cast<InetUnicastAgentRouteTable *> (get_table()))->agent();

    // Composite-NH is made from LOCAL_VM_PORT_PEER, count number of paths
    // with LOCAL_VM_PORT_PEER
    int count = 0;
    for(Route::PathList::const_iterator it = GetPathList().begin(); 
        it != GetPathList().end(); it++) {
        const AgentPath *it_path =
            static_cast<const AgentPath *>(it.operator->());

        if (it_path->peer() &&
            it_path->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER &&
            it_path->path_preference().is_ecmp() == true &&
            it_path != path)
            count++;
    }

    AgentPath *ecmp = NULL;
    // Sanity check. When more than one LOCAL_VM_PORT_PEER, ECMP must be present
    if (count >= 1) {
        ecmp = FindPath(agent->ecmp_peer());
        if (ecmp == NULL) {
            return false;
        }
    }

    if (count == 1 && ecmp) {
        // There is single path of type LOCAL_VM_PORT_PEER. Delete the ECMP path
        remove(ecmp);
        //Enqueue MPLS label delete request
        agent->mpls_table()->FreeLabel(ecmp->label());
        delete ecmp;
    } else if (count > 1) {
        // Remove Component-NH for the path being deleted
        DeleteComponentNH(agent, path);
    }

    return true;
}

/*
 * This routine finds out if there is supernet for this subnet route and that
 * is used to inherit flood flag. In case supernet is pointing to resolve i.e.
 * gateway without having Ipam path, then search continues further
 */
bool InetUnicastRouteEntry::IpamSubnetRouteAvailable() const {
    return (GetIpamSuperNetRoute() != NULL);
}

InetUnicastRouteEntry *
InetUnicastRouteEntry::GetIpamSuperNetRoute() const {
    if (plen_ == 0)
        return NULL;

    //Local path present means that this route itself was programmed
    //because of IPAM add as well and hence its eligible for flood in
    //all paths where NH is tunnel.
    InetUnicastAgentRouteTable *table =
        static_cast<InetUnicastAgentRouteTable *>(get_table());

    //Search for supernet, if none then dont flood else again search for
    //local path. If found then mark for flood otherwise check for active path
    //and retain flood flag from that path.
    uint16_t plen = plen_ - 1;
    while (plen != 0) {
        assert(plen < plen_);
        InetUnicastRouteEntry key(vrf(), addr_, plen, false);
        // Find next highest matching route
        InetUnicastRouteEntry *supernet_rt = table->FindRouteUsingKey(key);
        
        if (supernet_rt == NULL)
            return NULL;

        if (supernet_rt->ipam_subnet_route())
            return supernet_rt;

        plen--;
    }

    return NULL;
}

bool InetUnicastRouteEntry::ReComputePathAdd(AgentPath *path) {
    bool ret = false;
    InetUnicastAgentRouteTable *uc_rt_table =
        static_cast<InetUnicastAgentRouteTable *>(get_table());

    if (IsHostRoute() == false)
        uc_rt_table->TraverseHostRoutesInSubnet(this,
                                                uc_rt_table->agent()->
                                                inet_evpn_peer());

    // ECMP path are managed by route module. Update ECMP path with
    // addition of new path
    ret |= EcmpAddPath(path);
    return ret;
}

bool InetUnicastRouteEntry::ReComputePathDeletion(AgentPath *path) {
    if (IsHostRoute() == false) {
        //TODO merge both evpn inet and subnet routes handling
        UpdateDependantRoutes();
    }

    //Subnet discard = Ipam subnet route.
    //Ipam path is getting deleted so all the smaller subnets should be
    //resynced to remove the arp flood marking.
    if (path->is_subnet_discard()) {
        //Reset flag on route as ipam is going off.
        ipam_subnet_route_ = false;
        proxy_arp_ = false;
        InetUnicastAgentRouteTable *uc_rt_table =
            static_cast<InetUnicastAgentRouteTable *>(get_table());
        uc_rt_table->ResyncSubnetRoutes(this, false);
        return true;
    }
    // ECMP path are managed by route module. Update ECMP path with
    // deletion of new path
    return EcmpDeletePath(path);
}

bool InetUnicastRouteEntry::SyncEcmpPath(AgentPath *path,
                                         const SecurityGroupList sg_list,
                                         const CommunityList &communities,
                                         const PathPreference &path_preference,
                                         TunnelType::TypeBmap tunnel_bmap,
                                         const EcmpLoadBalance
                                         &ecmp_load_balance) {
    if (!path) {
        return false;
    }

    bool ret = false;
    path->set_tunnel_bmap(tunnel_bmap);
    TunnelType::Type new_tunnel_type =
        TunnelType::ComputeType(path->tunnel_bmap());
    if (path->tunnel_type() != new_tunnel_type) {
        path->set_tunnel_type(new_tunnel_type);
        ret = true;
    }

    SecurityGroupList path_sg_list;
    path_sg_list = path->sg_list();
    if (path_sg_list != sg_list) {
        path->set_sg_list(sg_list);
        ret = true;
    }

    CommunityList path_communities;
    path_communities = path->communities();
    if (path_communities != communities) {
        path->set_communities(communities);
        ret = true;
    }

    if (path_preference != path->path_preference()) {
        path->set_path_preference(path_preference);
        ret = true;
    }

    if (path->ecmp_load_balance() != ecmp_load_balance) {
        path->set_ecmp_load_balance(ecmp_load_balance);
        ret = true;
    }

    return ret;
}

// Handle add/update of a path in route. 
// If there are more than one path of type LOCAL_VM_PORT_PEER, creates/updates
// Composite-NH for them
bool InetUnicastRouteEntry::EcmpAddPath(AgentPath *path) {

    if (path->peer() == NULL) {
        return false;
    }

    // We are interested only in path from LOCAL_VM_PORT_PEER
    if (path->peer()->GetType() != Peer::LOCAL_VM_PORT_PEER ||
        path->path_preference().is_ecmp() == false) {
        return false;
    }

    path->set_tunnel_bmap(TunnelType::MplsType());
    Agent *agent = 
        (static_cast<InetUnicastAgentRouteTable *> (get_table()))->agent();

    // Count number of paths from LOCAL_VM_PORT_PEER already present
    const AgentPath *ecmp = NULL;
    const AgentPath *vm_port_path = NULL;
    int count = 0;
    for(Route::PathList::const_iterator it = GetPathList().begin(); 
        it != GetPathList().end(); it++) {
        const AgentPath *it_path = 
            static_cast<const AgentPath *>(it.operator->());

        if (it_path->peer() == agent->ecmp_peer())
            ecmp = it_path;

        if (it_path->peer() &&
            it_path->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER &&
            it_path->path_preference().is_ecmp() == true) {
            count++;
            if (it_path != path)
                vm_port_path = it_path;
        }
    }

    if (count == 0) {
        return false;
    }

    // Sanity check. When more than one LOCAL_VM_PORT_PEER, ECMP must be present
    if (count > 2) {
        assert(ecmp != NULL);
    }

    if (count == 1) {
        assert(ecmp == NULL);
        return false;
    }

    bool ret = false;
    if (count == 2 && ecmp == NULL) {
        // This is second path being added, make ECMP 
        AllocateEcmpPath(agent, vm_port_path, path);
        ret = true;
    } else if (count > 2) {
        // ECMP already present, add/update Component-NH for the path
        AppendEcmpPath(agent, path);
        ret = true;
    } else if (ecmp) {
        AgentPath *ecmp_path = FindPath(agent->ecmp_peer());
        bool updated = UpdateComponentNH(agent, ecmp_path, path);
        ret = SyncEcmpPath(ecmp_path, path->sg_list(),
                           path->communities(), path->path_preference(),
                           path->tunnel_bmap(),
                           path->ecmp_load_balance());
        if (updated) {
            ret = true;
        }
    }

    return ret;
}

void InetUnicastRouteEntry::AppendEcmpPath(Agent *agent,
                                           AgentPath *path) {
    AgentPath *ecmp_path = FindPath(agent->ecmp_peer());
    assert(ecmp_path);

    const NextHop* path_nh = path->ComputeNextHop(agent);
    DBEntryBase::KeyPtr key = path_nh->GetDBRequestKey();
    NextHopKey *nh_key = static_cast<NextHopKey *>(key.release());
    std::auto_ptr<const NextHopKey> nh_akey(nh_key);
    nh_key->SetPolicy(false);
    ComponentNHKeyPtr comp_nh_key_ptr(new ComponentNHKey(path->label(), nh_akey));

    ComponentNHKeyList component_nh_key_list;
    const CompositeNH *comp_nh =
        static_cast<const CompositeNH *>(ecmp_path->ComputeNextHop(agent));
    bool composite_nh_policy = false;
    component_nh_key_list = comp_nh->AddComponentNHKey(comp_nh_key_ptr,
                                                       composite_nh_policy);
    // Form the request for Inet4UnicastEcmpRoute and invoke AddChangePath
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(Composite::LOCAL_ECMP,
                                        composite_nh_policy,
                                        component_nh_key_list,
                                        vrf()->GetName()));
    nh_req.data.reset(new CompositeNHData());

    InetUnicastRouteEntry::ModifyEcmpPath(addr_, plen_, path->dest_vn_list(),
                               ecmp_path->label(), true, vrf()->GetName(),
                               path->sg_list(), path->communities(),
                               path->path_preference(),
                               path->tunnel_bmap(),
                               path->ecmp_load_balance(),
                               nh_req, agent, ecmp_path, "", false);

    NextHopKey *nh_key1 = static_cast<NextHopKey *>(nh_req.key.get());
    //Make MPLS label point to composite NH
    agent->mpls_table()->CreateRouteLabel(ecmp_path->label(), nh_key1,
                                          vrf()->GetName(), ToString());

    RouteInfo rt_info;
    FillTrace(rt_info, AgentRoute::CHANGE_PATH, path);
    AgentRouteTable *table = static_cast<AgentRouteTable *>(get_table());
    OPER_TRACE_ROUTE_ENTRY(Route, table, rt_info);
    AGENT_ROUTE_LOG(table, "Path change", ToString(), vrf()->GetName(),
                    GETPEERNAME(agent->ecmp_peer()));
}

/* When label of VMI changes and if that VMI (ie VMI's InterfaceNH) is part of
 * ECMP, then update the CompositeNH for ECMP route to point to right label for
 * that VMI. Label of VMI can change when policy-status of VMI changes */
bool InetUnicastRouteEntry::UpdateComponentNH(Agent *agent,
                                              AgentPath *ecmp_path,
                                              AgentPath *path) {
    if (!ecmp_path) {
        return false;
    }
    //Build ComponentNHKey for new path
    const NextHop* path_nh = path->ComputeNextHop(agent);
    DBEntryBase::KeyPtr key = path_nh->GetDBRequestKey();
    NextHopKey *nh_key = static_cast<NextHopKey *>(key.get());
    nh_key->SetPolicy(false);

    ComponentNHKeyList component_nh_key_list;
    const CompositeNH *comp_nh =
        static_cast<const CompositeNH *>(ecmp_path->ComputeNextHop(agent));
    bool composite_nh_policy = false;
    bool updated = comp_nh->UpdateComponentNHKey(path->label(), nh_key,
                                                 component_nh_key_list,
                                                 composite_nh_policy);

    if (!updated) {
        return false;
    }
    // Form the request for Inet4UnicastEcmpRoute and invoke AddChangePath
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(Composite::LOCAL_ECMP,
                                        composite_nh_policy,
                                        component_nh_key_list,
                                        vrf()->GetName()));
    nh_req.data.reset(new CompositeNHData());

    InetUnicastRouteEntry::ModifyEcmpPath(addr_, plen_,
                               ecmp_path->dest_vn_list(),
                               ecmp_path->label(), true, vrf()->GetName(),
                               ecmp_path->sg_list(), ecmp_path->communities(),
                               ecmp_path->path_preference(),
                               ecmp_path->tunnel_bmap(),
                               ecmp_path->ecmp_load_balance(),
                               nh_req, agent, ecmp_path, "", false);

    NextHopKey *nh_key1 = static_cast<NextHopKey *>(nh_req.key.get());
    //Make MPLS label point to updated composite NH
    agent->mpls_table()->CreateRouteLabel(ecmp_path->label(), nh_key1,
                                          vrf()->GetName(), ToString());

    RouteInfo rt_info;
    FillTrace(rt_info, AgentRoute::CHANGE_PATH, path);
    AgentRouteTable *table = static_cast<AgentRouteTable *>(get_table());
    OPER_TRACE_ROUTE_ENTRY(Route, table, rt_info);
    AGENT_ROUTE_LOG(table, "Path Update", ToString(), vrf()->GetName(),
                    GETPEERNAME(agent->ecmp_peer()));
    return true;
}

void InetUnicastRouteEntry::DeleteComponentNH(Agent *agent, AgentPath *path) {
    AgentPath *ecmp_path = FindPath(agent->ecmp_peer());

    assert(ecmp_path);
    DBEntryBase::KeyPtr key = path->ComputeNextHop(agent)->GetDBRequestKey();
    NextHopKey *nh_key = static_cast<NextHopKey *>(key.release());
    std::auto_ptr<const NextHopKey> nh_akey(nh_key);
    nh_key->SetPolicy(false);
    ComponentNHKeyPtr comp_nh_key_ptr(new ComponentNHKey(path->label(), nh_akey));

    ComponentNHKeyList component_nh_key_list;
    bool comp_nh_policy = false;
    const CompositeNH *comp_nh =
        static_cast<const CompositeNH *>(ecmp_path->ComputeNextHop(agent));
    component_nh_key_list = comp_nh->DeleteComponentNHKey(comp_nh_key_ptr,
                                                          comp_nh_policy);

    // Form the request for Inet4UnicastEcmpRoute and invoke AddChangePath
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(Composite::LOCAL_ECMP,
                                        comp_nh_policy, component_nh_key_list,
                                        vrf()->GetName()));
    nh_req.data.reset(new CompositeNHData());

    if (!InetUnicastRouteEntry::ModifyEcmpPath(addr_, plen_,
                               ecmp_path->dest_vn_list(),
                               ecmp_path->label(), true, vrf()->GetName(),
                               ecmp_path->sg_list(), ecmp_path->communities(),
                               ecmp_path->path_preference(),
                               ecmp_path->tunnel_bmap(),
                               ecmp_path->ecmp_load_balance(),
                               nh_req, agent, ecmp_path, "", false)) {
        return;
    }

    NextHopKey *nh_key1 = static_cast<NextHopKey *>(nh_req.key.get());
    //Make MPLS label point to composite NH
    agent->mpls_table()->CreateRouteLabel(ecmp_path->label(), nh_key1,
                                          vrf()->GetName(), ToString());

    RouteInfo rt_info;
    FillTrace(rt_info, AgentRoute::CHANGE_PATH, path);
    AgentRouteTable *table = static_cast<AgentRouteTable *>(get_table());
    OPER_TRACE_ROUTE_ENTRY(Route, table, rt_info);
    AGENT_ROUTE_LOG(table, "Path change", ToString(), vrf()->GetName(),
                    GETPEERNAME(agent->ecmp_peer()));
}

const NextHop* InetUnicastRouteEntry::GetLocalNextHop() const {
    Agent *agent =
        (static_cast<InetUnicastAgentRouteTable *> (get_table()))->agent();

    if (FindPath(agent->ecmp_peer())) {
        return FindPath(agent->ecmp_peer())->ComputeNextHop(agent);
    }
   
    //If a route is leaked, and it points to local composite nexthop
    //then choose that
    if (GetActivePath()->local_ecmp_mpls_label()) {
        return GetActivePath()->local_ecmp_mpls_label()->nexthop();
    }

    //Choose the first local vm peer path
    for (Route::PathList::const_iterator it = GetPathList().begin();
            it != GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path) {
            if (path->peer() && 
                path->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER) {
                return path->ComputeNextHop(agent);
            }
        }
    }

    const NextHop *nh = GetActiveNextHop();
    if (nh->GetType() == NextHop::COMPOSITE) {
        const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
        //Get the local composite NH
        return comp_nh->GetLocalNextHop();
    }
    return NULL;
}

/////////////////////////////////////////////////////////////////////////////
// AgentRouteData virtual functions
/////////////////////////////////////////////////////////////////////////////

bool Inet4UnicastArpRoute::AddChangePathExtended(Agent *agent, AgentPath *path,
                                                 const AgentRoute *rt) {
    bool ret = false;

    ArpNHKey key(vrf_name_, addr_, policy_);
    NextHop *nh = 
        static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key));
    path->set_unresolved(false);
    
    if (path->dest_vn_list() != vn_list_) {
        path->set_dest_vn_list(vn_list_);
        ret = true;
    }

    if (path->sg_list() != sg_list_) {
        path->set_sg_list(sg_list_);
        ret = true;
    }

    if (path->ChangeNH(agent, nh) == true)
        ret = true;

    if (nh) {
        if (path->CopyArpData()) {
            ret = true;
        }
    }

    return ret;
}

bool Inet4UnicastGatewayRoute::AddChangePathExtended(Agent *agent, AgentPath *path,
                                                     const AgentRoute *agent_rt) {
    path->set_vrf_name(vrf_name_);

    InetUnicastAgentRouteTable *table = NULL;
    table = static_cast<InetUnicastAgentRouteTable *>
        (agent->vrf_table()->GetInet4UnicastRouteTable(vrf_name_));
    InetUnicastRouteEntry *rt = table->FindRoute(gw_ip_); 
    if (rt == NULL || rt->plen() == 0) {
        path->set_unresolved(true);
    } else if (rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE) {
        const ResolveNH *nh =
            static_cast<const ResolveNH *>(rt->GetActiveNextHop());
        path->set_unresolved(true);
        VnListType vn_list;
        vn_list.insert(vn_name_);
        InetUnicastAgentRouteTable::AddArpReq(vrf_name_, gw_ip_.to_v4(),
                                              nh->interface()->vrf()->GetName(),
                                              nh->interface(), nh->PolicyEnabled(),
                                              vn_list, sg_list_);
    } else {
        path->set_unresolved(false);
    }

    if (path->label() != mpls_label_) {
        path->set_label(mpls_label_);
    }

    SecurityGroupList path_sg_list;
    path_sg_list = path->sg_list();
    if (path_sg_list != sg_list_) {
        path->set_sg_list(sg_list_);
    }

    CommunityList path_communities;
    path_communities = path->communities();
    if (path_communities != communities_) {
        path->set_communities(communities_);
    }

    //Reset to new gateway route, no nexthop for indirect route
    path->set_gw_ip(gw_ip_);
    path->ResetDependantRoute(rt);
    VnListType dest_vn_list;
    dest_vn_list.insert(vn_name_);
    if (path->dest_vn_list() != dest_vn_list) {
        path->set_dest_vn_list(dest_vn_list);
    }

    return true;
}

InetEvpnRoutePath::InetEvpnRoutePath(const Peer *peer,
                                     AgentRoute *rt) :
                                     AgentPath(peer, rt) {
}

const AgentPath *InetEvpnRoutePath::UsablePath() const {
    //In InetEvpnRoutePath nexthop will always be NULL.
    //Valid NH is dependant on parent route(subnet).
    if (dependant_rt()) {
        const AgentPath *path = dependant_rt()->GetActivePath();
        if (path != NULL)
            return path;
    }
    return this;
}

bool InetEvpnRoutePath::SyncDependantRoute(const AgentRoute *sync_route) {
    const InetUnicastRouteEntry *dependant_route =
        dynamic_cast<const InetUnicastRouteEntry *>(dependant_rt());
    InetUnicastAgentRouteTable *table =
        static_cast<InetUnicastAgentRouteTable *>(sync_route->get_table());
    InetUnicastRouteEntry *parent_subnet_route =
        table->GetSuperNetRoute(dynamic_cast<const InetUnicastRouteEntry *>
                                (sync_route)->addr());

    if (parent_subnet_route != dependant_route) {
        set_gw_ip(parent_subnet_route ? parent_subnet_route->addr() :
                  IpAddress());
        if (parent_subnet_route) {
            ResetDependantRoute(parent_subnet_route);
            set_unresolved(false);
        } else {
            //Clear old dependant route
            ClearDependantRoute();
            set_unresolved(true);
        }
        return true;
    }
    return false;
}

bool InetEvpnRoutePath::Sync(AgentRoute *sync_route) {
    return SyncDependantRoute(sync_route);
}

const NextHop* InetEvpnRoutePath::ComputeNextHop(Agent *agent) const {
    //InetEvpnRoutePath is deleted when parent evpn route is going off.
    //Now it may happen that supernet route which was used as dependant_rt in
    //this path has been deleted.
    if ((dependant_rt() == NULL) ||
        (dependant_rt()->GetActiveNextHop() == NULL)) {
        DiscardNH key;
        return static_cast<NextHop *>
            (agent->nexthop_table()->FindActiveEntry(&key));
    }

    return AgentPath::ComputeNextHop(agent);
}

AgentPath *InetEvpnRouteData::CreateAgentPath(const Peer *peer,
                                              AgentRoute *rt) const {
    return (new InetEvpnRoutePath(peer, rt));
}

bool InetEvpnRouteData::AddChangePathExtended(Agent *agent,
                                              AgentPath *path,
                                              const AgentRoute *route) {
    return dynamic_cast<InetEvpnRoutePath *>(path)->SyncDependantRoute(route);
}

Inet4UnicastInterfaceRoute::Inet4UnicastInterfaceRoute
(const PhysicalInterface *interface, const std::string &vn_name) :
        AgentRouteData(AgentRouteData::ADD_DEL_CHANGE, false, 0),
        interface_key_(new PhysicalInterfaceKey(interface->name())),
        vn_name_(vn_name) {
}

bool Inet4UnicastInterfaceRoute::AddChangePathExtended(Agent *agent, AgentPath *path,
                                                       const AgentRoute *rt) {
    bool ret = false;

    path->set_unresolved(false);
    VnListType vn_list;
    vn_list.insert(agent->fabric_vn_name());
    if (path->dest_vn_list() != vn_list) {
        path->set_dest_vn_list(vn_list);
        ret = true;
    }

    Interface *intf = static_cast<Interface *>(
            agent->interface_table()->FindActiveEntry(interface_key_.get()));
    assert(intf);
    InterfaceNHKey key(interface_key_->Clone(), false,
                       InterfaceNHFlags::INET4, intf->mac());
    NextHop *nh = static_cast<NextHop *>
        (agent->nexthop_table()->FindActiveEntry(&key));
    assert(nh);
    if (path->ChangeNH(agent, nh) == true) {
        ret = true;
    }

    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh functions
/////////////////////////////////////////////////////////////////////////////

bool InetUnicastRouteEntry::DBEntrySandesh(Sandesh *sresp, bool stale) const {

    RouteUcSandeshData data;
    data.set_src_ip(addr_.to_string());
    data.set_src_plen(plen_);
    data.set_ipam_subnet_route(ipam_subnet_route_);
    data.set_proxy_arp(proxy_arp_);
    data.set_src_vrf(vrf()->GetName());
    data.set_multicast(AgentRoute::is_multicast());
    for (Route::PathList::const_iterator it = GetPathList().begin();
         it != GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path) {
            PathSandeshData pdata;
            path->SetSandeshData(pdata);
            data.path_list.push_back(pdata);
        }
    }

    if (addr_.is_v4()) {
        Inet4UcRouteResp *v4_resp = static_cast<Inet4UcRouteResp *>(sresp);
        std::vector<RouteUcSandeshData> &list =
        const_cast<std::vector<RouteUcSandeshData>&>(v4_resp->get_route_list());
        list.push_back(data);
    } else {
        Inet6UcRouteResp *v6_resp = static_cast<Inet6UcRouteResp *>(sresp);
        std::vector<RouteUcSandeshData> &list =
        const_cast<std::vector<RouteUcSandeshData>&>(v6_resp->get_route_list());
        list.push_back(data);
    }
    return true;
}

bool InetUnicastRouteEntry::DBEntrySandesh(Sandesh *sresp, IpAddress addr,
                                            uint8_t plen, bool stale) const {
    if (addr_ == addr && plen_ == plen) {
        return DBEntrySandesh(sresp, stale);
    }

    return false;
}

void UnresolvedRoute::HandleRequest() const {
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromId(0);
    if (!vrf) {
        ErrorResp *resp = new ErrorResp();
        resp->set_context(context());
        resp->Response();
        return;
    }

    int count = 0;
    Inet4UcRouteResp *resp = new Inet4UcRouteResp();

    //TODO - Convert inet4ucroutetable to agentroutetable
    AgentRouteTable *rt_table = static_cast<AgentRouteTable *>
        (vrf->GetInet4UnicastRouteTable());
    InetUnicastAgentRouteTable::UnresolvedRouteTree::const_iterator it;
    it = rt_table->unresolved_route_begin();
    for (;it != rt_table->unresolved_route_end(); it++) {
        count++;
        const AgentRoute *rt = *it;
        rt->DBEntrySandesh(resp, false);
        if (count == 1) {
            resp->set_context(context()+"$");
            resp->Response();
            count = 0;
            resp = new Inet4UcRouteResp();
        }
    }

    resp->set_context(context());
    resp->Response();
    return;
}

void Inet4UcRouteReq::HandleRequest() const {
    VrfEntry *vrf = 
        Agent::GetInstance()->vrf_table()->FindVrfFromId(get_vrf_index());
    if (!vrf) {
        ErrorResp *resp = new ErrorResp();
        resp->set_context(context());
        resp->Response();
        return;
    }

    AgentSandeshPtr sand;
    if (get_src_ip().empty()) {
        sand.reset(new AgentInet4UcRtSandesh(vrf, context(), get_stale()));
    } else {
        boost::system::error_code ec;
        Ip4Address src_ip = Ip4Address::from_string(get_src_ip(), ec);
        sand.reset(new AgentInet4UcRtSandesh(vrf, context(), src_ip,
                                             (uint8_t)get_prefix_len(),
                                             get_stale()));
    }
    sand->DoSandesh(sand);
}

AgentSandeshPtr InetUnicastAgentRouteTable::GetAgentSandesh
(const AgentSandeshArguments *args, const std::string &context) {
    if (type_ == Agent::INET4_UNICAST) {
        return AgentSandeshPtr(new AgentInet4UcRtSandesh(vrf_entry(), context,
                                                         false));
    } else {
        return AgentSandeshPtr(new AgentInet6UcRtSandesh(vrf_entry(), context,
                                                         false));
    }
}

void Inet6UcRouteReq::HandleRequest() const {
    VrfEntry *vrf =
        Agent::GetInstance()->vrf_table()->FindVrfFromId(get_vrf_index());
    if (!vrf) {
        ErrorResp *resp = new ErrorResp();
        resp->set_context(context());
        resp->Response();
        return;
    }

    AgentSandeshPtr sand;
    if (get_src_ip().empty()) {
        sand.reset(new AgentInet6UcRtSandesh(vrf, context(), get_stale()));
    } else {
        boost::system::error_code ec;
        Ip6Address src_ip = Ip6Address::from_string(get_src_ip(), ec);
        sand.reset(new AgentInet6UcRtSandesh(vrf, context(), src_ip,
                                             (uint8_t)get_prefix_len(),
                                             get_stale()));
    }
    sand->DoSandesh(sand);
}

/////////////////////////////////////////////////////////////////////////////
// Helper functions to enqueue request or process inline
/////////////////////////////////////////////////////////////////////////////

// Request to delete an entry
void 
InetUnicastAgentRouteTable::DeleteReq(const Peer *peer, const string &vrf_name,
                                      const IpAddress &addr, uint8_t plen,
                                      AgentRouteData *data) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new InetUnicastRouteKey(peer, vrf_name, addr, plen));
    req.data.reset(data);
    InetUnicastTableEnqueue(Agent::GetInstance(), vrf_name, &req);
}

// Inline delete request
void 
InetUnicastAgentRouteTable::Delete(const Peer *peer, const string &vrf_name,
                                   const IpAddress &addr, uint8_t plen) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new InetUnicastRouteKey(peer, vrf_name, addr, plen));
    req.data.reset(NULL);
    InetUnicastTableProcess(Agent::GetInstance(), vrf_name, req);
}

// Utility function to create a route to trap packets to agent.
// Assumes that Interface-NH for "HOST Interface" is already present
void 
InetUnicastAgentRouteTable::AddHostRoute(const string &vrf_name,
                                         const IpAddress &addr,
                                         uint8_t plen,
                                         const std::string &dest_vn_name,
                                         bool relaxed_policy) {
    Agent *agent = Agent::GetInstance();
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(agent->local_peer(), vrf_name,
                                           addr, plen));

    PacketInterfaceKey intf_key(nil_uuid(), agent->GetHostInterfaceName());
    HostRoute *data = new HostRoute(intf_key, dest_vn_name);
    data->set_relaxed_policy(relaxed_policy);
    req.data.reset(data);

    InetUnicastTableEnqueue(Agent::GetInstance(), vrf_name, &req);
}

// Create Route with VLAN NH
void 
InetUnicastAgentRouteTable::AddVlanNHRouteReq(const Peer *peer,
                                              const string &vm_vrf,
                                              const IpAddress &addr,
                                              uint8_t plen,
                                              VlanNhRoute *data) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(peer, vm_vrf, addr, plen));
    req.data.reset(data);
    InetUnicastTableEnqueue(Agent::GetInstance(), vm_vrf, &req);
}

void
InetUnicastAgentRouteTable::AddVlanNHRouteReq(const Peer *peer,
                                              const string &vm_vrf,
                                              const IpAddress &addr,
                                              uint8_t plen,
                                              const uuid &intf_uuid,
                                              uint16_t tag,
                                              uint32_t label,
                                              const VnListType &dest_vn_list,
                                              const SecurityGroupList &sg_list,
                                              const PathPreference
                                              &path_preference) {
    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    VlanNhRoute *data = new VlanNhRoute(intf_key, tag, label, dest_vn_list,
                                        sg_list, path_preference,
                                        peer->sequence_number());
    AddVlanNHRouteReq(peer, vm_vrf, addr, plen, data);
}

// Create Route with VLAN NH
void
InetUnicastAgentRouteTable::AddVlanNHRoute(const Peer *peer,
                                           const string &vm_vrf,
                                           const IpAddress &addr,
                                           uint8_t plen,
                                           const uuid &intf_uuid,
                                           uint16_t tag,
                                           uint32_t label,
                                           const VnListType &dest_vn_list,
                                           const SecurityGroupList &sg_list,
                                           const PathPreference
                                           &path_preference) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(peer, vm_vrf, addr, plen));

    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    req.data.reset(new VlanNhRoute(intf_key, tag, label, dest_vn_list,
                                   sg_list, path_preference,
                                   peer->sequence_number()));
    InetUnicastTableProcess(Agent::GetInstance(), vm_vrf, req);
}

void
InetUnicastAgentRouteTable::AddLocalVmRouteReq(const Peer *peer,
                                               const string &vm_vrf,
                                               const IpAddress &addr,
                                               uint8_t plen,
                                               LocalVmRoute *data) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(peer, vm_vrf, addr, plen));

    req.data.reset(data);

    InetUnicastTableEnqueue(Agent::GetInstance(), vm_vrf, &req);
}

void
InetUnicastAgentRouteTable::AddLocalVmRouteReq(const Peer *peer,
                                               const string &vm_vrf,
                                               const IpAddress &addr,
                                               uint8_t plen,
                                               const uuid &intf_uuid,
                                               const VnListType &vn_list,
                                               uint32_t label,
                                               const SecurityGroupList &sg_list,
                                               const CommunityList &communities,
                                               bool force_policy,
                                               const PathPreference
                                               &path_preference,
                                               const IpAddress &subnet_service_ip,
                                               const EcmpLoadBalance &ecmp_load_balance,
                                               bool is_local,
                                               bool is_health_check_service)
{
    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    LocalVmRoute *data = new LocalVmRoute(intf_key, label,
                                    VxLanTable::kInvalidvxlan_id, force_policy,
                                    vn_list, InterfaceNHFlags::INET4, sg_list,
                                    communities, path_preference,
                                    subnet_service_ip, ecmp_load_balance,
                                    is_local, is_health_check_service,
                                    peer->sequence_number(),
                                    false);

    AddLocalVmRouteReq(peer, vm_vrf, addr, plen, data);
}

void
InetUnicastAgentRouteTable::AddClonedLocalPathReq(const Peer *peer,
                                                  const string &vm_vrf,
                                                  const IpAddress &addr,
                                                  uint8_t plen,
                                                  ClonedLocalPath *data) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(peer, vm_vrf, addr, plen));
    req.data.reset(data);
    InetUnicastTableEnqueue(Agent::GetInstance(), vm_vrf, &req);
}

// Create Route for a local VM
// Assumes that Interface-NH for "VM Port" is already present
void 
InetUnicastAgentRouteTable::AddLocalVmRoute(const Peer *peer,
                                            const string &vm_vrf,
                                            const IpAddress &addr,
                                            uint8_t plen,
                                            const uuid &intf_uuid,
                                            const VnListType &vn_list,
                                            uint32_t label,
                                            const SecurityGroupList &sg_list,
                                            const CommunityList &communities,
                                            bool force_policy,
                                            const PathPreference
                                            &path_preference,
                                            const IpAddress &subnet_service_ip,
                                            const EcmpLoadBalance &ecmp_load_balance,
                                            bool is_local,
                                            bool is_health_check_service)
{
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(peer, vm_vrf, addr, plen));

    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    req.data.reset(new LocalVmRoute(intf_key, label, VxLanTable::kInvalidvxlan_id,
                                    force_policy, vn_list,
                                    InterfaceNHFlags::INET4, sg_list, communities,
                                    path_preference, subnet_service_ip,
                                    ecmp_load_balance, is_local,
                                    is_health_check_service,
                                    peer->sequence_number(), false));
    InetUnicastTableProcess(Agent::GetInstance(), vm_vrf, req);
}

void 
InetUnicastAgentRouteTable::AddRemoteVmRouteReq(const Peer *peer, 
                                                const string &vm_vrf,
                                                const IpAddress &vm_addr,
                                                uint8_t plen,
                                                AgentRouteData *data) {
    if (Agent::GetInstance()->simulate_evpn_tor())
        return;
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(peer, vm_vrf, vm_addr, plen));
    req.data.reset(data);
    InetUnicastTableEnqueue(Agent::GetInstance(), vm_vrf, &req);
}
 
void
InetUnicastAgentRouteTable::AddArpReq(const string &route_vrf_name,
                                      const Ip4Address &ip,
                                      const string &nexthop_vrf_name,
                                      const Interface *intf, bool policy,
                                      const VnListType &vn_list,
                                      const SecurityGroupList &sg_list) {
    Agent *agent = Agent::GetInstance();
    DBRequest  nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new ArpNHKey(route_vrf_name, ip, policy));
    nh_req.data.reset(new ArpNHData(
                static_cast<InterfaceKey *>(intf->GetDBRequestKey().release())));
    agent->nexthop_table()->Enqueue(&nh_req);

    DBRequest  rt_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    rt_req.key.reset(new InetUnicastRouteKey(agent->local_peer(),
                                              route_vrf_name, ip, 32));
    rt_req.data.reset(new Inet4UnicastArpRoute(nexthop_vrf_name, ip, policy,
                                               vn_list, sg_list));
    Inet4UnicastTableEnqueue(agent, &rt_req);
}

void
InetUnicastAgentRouteTable::ArpRoute(DBRequest::DBOperation op,
                                     const string &route_vrf_name,
                                     const Ip4Address &ip,
                                     const MacAddress &mac,
                                     const string &nexthop_vrf_name,
                                     const Interface &intf,
                                     bool resolved,
                                     const uint8_t plen,
                                     bool policy,
                                     const VnListType &vn_list,
                                     const SecurityGroupList &sg) {
    Agent *agent = Agent::GetInstance();
    DBRequest  nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    ArpNHKey *nh_key = new ArpNHKey(nexthop_vrf_name, ip, policy);
    if (op == DBRequest::DB_ENTRY_DELETE) {
        //In case of delete we want to set the
        //nexthop as invalid, hence use resync operation
        //We dont want the nexthop to created again
        //in case of duplicate delete
        nh_key->sub_op_ = AgentKey::RESYNC;
    }
    nh_req.key.reset(nh_key);
    ArpNHData *arp_data = new ArpNHData(mac,
               static_cast<InterfaceKey *>(intf.GetDBRequestKey().release()),
               resolved);
    nh_req.data.reset(arp_data);

    DBRequest  rt_req(op);
    InetUnicastRouteKey *rt_key =
        new InetUnicastRouteKey(agent->local_peer(), route_vrf_name, ip, plen);
    Inet4UnicastArpRoute *data = NULL;

    switch(op) {
    case DBRequest::DB_ENTRY_ADD_CHANGE:
        agent->nexthop_table()->Enqueue(&nh_req);
        data = new Inet4UnicastArpRoute(nexthop_vrf_name, ip, policy, vn_list, sg);
        break;

    case DBRequest::DB_ENTRY_DELETE: {
        VrfEntry *vrf = agent->vrf_table()->FindVrfFromName(route_vrf_name);
        InetUnicastRouteEntry *rt = 
            static_cast<InetUnicastRouteEntry *>(vrf->
                          GetInet4UnicastRouteTable()->Find(rt_key));
        assert(resolved==false);
        agent->nexthop_table()->Enqueue(&nh_req);

        // If no other route is dependent on this, remove the route; else ignore
        if (rt && rt->IsDependantRouteEmpty() && rt->IsTunnelNHListEmpty()) {
            data = new Inet4UnicastArpRoute(nexthop_vrf_name, ip, policy,
                                            vn_list, sg);
        } else {
            rt_key->sub_op_ = AgentKey::RESYNC;
            rt_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        }
        break;
    }

    default:
        assert(0);
    }

    rt_req.key.reset(rt_key);
    rt_req.data.reset(data);
    Inet4UnicastTableEnqueue(agent, &rt_req);
}

void
InetUnicastAgentRouteTable::CheckAndAddArpReq(const string &vrf_name,
                                              const Ip4Address &ip,
                                              const Interface *intf,
                                              const VnListType &vn_list,
                                              const SecurityGroupList &sg) {

    if (ip == Agent::GetInstance()->router_id() ||
        !IsIp4SubnetMember(ip, Agent::GetInstance()->router_id(),
                           Agent::GetInstance()->vhost_prefix_len())) {
        // TODO: add Arp request for GW
        // Currently, default GW Arp is added during init
        return;
    }
    AddArpReq(vrf_name, ip, intf->vrf()->GetName(), intf, false, vn_list, sg);
}

void InetUnicastAgentRouteTable::AddResolveRoute(const Peer *peer,
                                                 const string &vrf_name,
                                                 const Ip4Address &ip, 
                                                 const uint8_t plen,
                                                 const InterfaceKey &intf,
                                                 const uint32_t label,
                                                 bool policy,
                                                 const std::string &vn_name,
                                                 const SecurityGroupList
                                                 &sg_list) {
    Agent *agent = Agent::GetInstance();
    ResolveNH::CreateReq(&intf, policy);
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(peer, vrf_name, ip,
                                          plen));
    req.data.reset(new ResolveRoute(&intf, policy, label, vn_name, sg_list));
    Inet4UnicastTableEnqueue(agent, &req);
}

// Create Route for a interface NH.
// Used to create interface-nh pointing routes to vhost interfaces
void InetUnicastAgentRouteTable::AddInetInterfaceRouteReq(const Peer *peer,
                                                          const string &vm_vrf,
                                                          const Ip4Address &addr,
                                                          uint8_t plen,
                                                          const string &interface,
                                                          uint32_t label,
                                                          const VnListType &vn_list) {
    InetInterfaceKey intf_key(interface);
    InetInterfaceRoute *data = new InetInterfaceRoute
        (intf_key, label, TunnelType::GREType(), vn_list,
         peer->sequence_number());

    AddInetInterfaceRouteReq(peer, vm_vrf, addr, plen, data);
}

void InetUnicastAgentRouteTable::AddInetInterfaceRouteReq(const Peer *peer,
                                                          const string &vm_vrf,
                                                          const Ip4Address &addr,
                                                          uint8_t plen,
                                                          InetInterfaceRoute *data) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(peer, vm_vrf, addr, plen));
    req.data.reset(data);

    Inet4UnicastTableEnqueue(Agent::GetInstance(), &req);
}

static void AddVHostRecvRouteInternal(DBRequest *req, const Peer *peer,
                                      const string &vrf,
                                      const string &interface,
                                      const IpAddress &addr, uint8_t plen,
                                      const string &vn_name, bool policy) {
    req->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req->key.reset(new InetUnicastRouteKey(peer, vrf, addr, plen));

    InetInterfaceKey intf_key(interface);
    req->data.reset(new ReceiveRoute(intf_key, MplsTable::kInvalidLabel,
                                    TunnelType::AllType(), policy, vn_name));
}

void InetUnicastAgentRouteTable::AddVHostRecvRoute(const Peer *peer,
                                                   const string &vrf,
                                                   const string &interface,
                                                   const IpAddress &addr,
                                                   uint8_t plen,
                                                   const string &vn_name,
                                                   bool policy) {
    DBRequest req;
    AddVHostRecvRouteInternal(&req, peer, vrf, interface, addr, plen,
                              vn_name, policy);
    static_cast<ReceiveRoute *>(req.data.get())->set_proxy_arp();
    if (addr.is_v4()) {
        Inet4UnicastTableProcess(Agent::GetInstance(), vrf, req);
    } else if (addr.is_v6()) {
        Inet6UnicastTableProcess(Agent::GetInstance(), vrf, req);
    }
}

void InetUnicastAgentRouteTable::AddVHostRecvRouteReq
    (const Peer *peer, const string &vrf, const string &interface,
     const IpAddress &addr, uint8_t plen, const string &vn_name, bool policy) {
    DBRequest req;
    AddVHostRecvRouteInternal(&req, peer, vrf, interface, addr, plen,
                              vn_name, policy);
    static_cast<ReceiveRoute *>(req.data.get())->set_proxy_arp();
    if (addr.is_v4()) {
        Inet4UnicastTableEnqueue(Agent::GetInstance(), &req);
    } else if (addr.is_v6()) {
        Inet6UnicastTableEnqueue(Agent::GetInstance(), vrf, &req);
    }
}

void
InetUnicastAgentRouteTable::AddVHostSubnetRecvRoute(const Peer *peer,
                                                    const string &vrf,
                                                    const string &interface,
                                                    const Ip4Address &addr,
                                                    uint8_t plen,
                                                    const string &vn_name,
                                                    bool policy) {
    DBRequest req;
    AddVHostRecvRouteInternal(&req, peer, vrf, interface, addr, plen,
                              vn_name, policy);
    Inet4UnicastTableProcess(Agent::GetInstance(), vrf, req);
}

void InetUnicastAgentRouteTable::AddDropRoute(const string &vm_vrf,
                                              const Ip4Address &addr,
                                              uint8_t plen,
                                              const string &vn_name) {
    Agent *agent = Agent::GetInstance();
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(agent->local_peer(), vm_vrf,
                                      Address::GetIp4SubnetAddress(addr, plen),
                                      plen));
    req.data.reset(new DropRoute(vn_name));
    Inet4UnicastTableEnqueue(agent, &req);
}

void InetUnicastAgentRouteTable::DelVHostSubnetRecvRoute(const string &vm_vrf,
                                                         const Ip4Address &addr,
                                                         uint8_t plen) {
    DeleteReq(Agent::GetInstance()->local_peer(), vm_vrf,
              Address::GetIp4SubnetAddress(addr, plen), 32, NULL);
}

static void AddGatewayRouteInternal(const Peer *peer,
                                    DBRequest *req, const string &vrf_name,
                                    const Ip4Address &dst_addr, uint8_t plen,
                                    const Ip4Address &gw_ip,
                                    const string &vn_name, uint32_t label,
                                    const SecurityGroupList &sg_list,
                                    const CommunityList &communities) {
    req->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req->key.reset(new InetUnicastRouteKey(peer,
                                           vrf_name, dst_addr, plen));
    req->data.reset(new Inet4UnicastGatewayRoute(gw_ip, vrf_name,
                                                 vn_name, label, sg_list,
                                                 communities));
}

void InetUnicastAgentRouteTable::AddGatewayRoute(const Peer *peer,
                                                 const string &vrf_name,
                                                 const Ip4Address &dst_addr,
                                                 uint8_t plen,
                                                 const Ip4Address &gw_ip,
                                                 const string &vn_name,
                                                 uint32_t label,
                                                 const SecurityGroupList
                                                 &sg_list,
                                                 const CommunityList
                                                 &communities) {
    DBRequest req;
    AddGatewayRouteInternal(peer, &req, vrf_name, dst_addr, plen, gw_ip, vn_name,
                            label, sg_list, communities);
    Inet4UnicastTableProcess(Agent::GetInstance(), vrf_name, req);
}

void
InetUnicastAgentRouteTable::AddGatewayRouteReq(const Peer *peer,
                                               const string &vrf_name,
                                               const Ip4Address &dst_addr,
                                               uint8_t plen,
                                               const Ip4Address &gw_ip,
                                               const string &vn_name,
                                               uint32_t label,
                                               const SecurityGroupList
                                               &sg_list,
                                               const CommunityList
                                               &communities) {
    DBRequest req;
    AddGatewayRouteInternal(peer, &req, vrf_name, dst_addr, plen, gw_ip,
                            vn_name, label, sg_list, communities);
    Inet4UnicastTableEnqueue(Agent::GetInstance(), &req);
}

void
InetUnicastAgentRouteTable::AddIpamSubnetRoute(const string &vrf_name,
                                               const IpAddress &dst_addr,
                                               uint8_t plen,
                                               const string &vn_name) {
    Agent *agent_ptr = agent();
    AgentRouteTable *table = NULL;
    if (dst_addr.is_v4()) {
        table = agent_ptr->vrf_table()->GetInet4UnicastRouteTable(vrf_name);
    } else if (dst_addr.is_v6()) {
        table = agent_ptr->vrf_table()->GetInet6UnicastRouteTable(vrf_name);
    }

    //Add local perr path with discard NH
    DBRequest dscd_nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    dscd_nh_req.key.reset(new DiscardNHKey());
    dscd_nh_req.data.reset(NULL);

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new InetUnicastRouteKey(agent_ptr->local_peer(),
                                            vrf_name, dst_addr, plen));
    req.data.reset(new IpamSubnetRoute(dscd_nh_req, vn_name));
    if (table) {
        table->Process(req);
    }
}

uint8_t InetUnicastAgentRouteTable::GetHostPlen(const IpAddress &ip_addr) const {
    if (ip_addr.is_v4()) {
        return Address::kMaxV4PrefixLen;
    } else {
        return Address::kMaxV6PrefixLen;
    }
}

void
InetUnicastAgentRouteTable::AddInterfaceRouteReq(Agent *agent, const Peer *peer,
                                                 const string &vrf_name,
                                                 const Ip4Address &ip,
                                                 uint8_t plen,
                                                 const Interface *interface,
                                                 const string &vn_name) {

    assert(interface->type() == Interface::PHYSICAL);
    DBRequest  rt_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    rt_req.key.reset(new InetUnicastRouteKey(agent->local_peer(),
                                              vrf_name, ip, plen));
    const PhysicalInterface *phy_intf = static_cast<const PhysicalInterface *>
        (interface);
    rt_req.data.reset(new Inet4UnicastInterfaceRoute(phy_intf, vn_name));
    Inet4UnicastTableEnqueue(agent, &rt_req);
}

void InetUnicastAgentRouteTable::AddEvpnRoute(const AgentRoute *route) {
    const EvpnRouteEntry *evpn_route =
        dynamic_cast<const EvpnRouteEntry *>(route);
    const IpAddress &ip_addr = evpn_route->ip_addr();
    //No installation for evpn route with zero Ip prefix.
    if (ip_addr.is_unspecified())
        return;

    //label and parent-ip for NH need to be picket from parent route
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    //Set key and data
    req.key.reset(new InetUnicastRouteKey(agent()->inet_evpn_peer(),
                                          evpn_route->vrf()->GetName(),
                                          ip_addr,
                                          GetHostPlen(ip_addr)));
    req.data.reset(new InetEvpnRouteData());
    Process(req);
}

void InetUnicastAgentRouteTable::DeleteEvpnRoute(const AgentRoute *rt) {
    const EvpnRouteEntry *evpn_route =
        static_cast<const EvpnRouteEntry *>(rt);
    const IpAddress &ip_addr = evpn_route->ip_addr();
    if (ip_addr.is_unspecified())
        return;
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new InetUnicastRouteKey(agent()->inet_evpn_peer(),
                                          evpn_route->vrf()->GetName(),
                                          ip_addr,
                                          GetHostPlen(ip_addr)));
    req.data.reset();
    Process(req);
}
