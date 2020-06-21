/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>

#include <base/address_util.h>
#include <base/task_annotations.h>
#include <boost/foreach.hpp>
#include <cmn/agent_cmn.h>
#include <route/route.h>
#include <oper/ecmp.h>
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

AgentRouteKey *InetMplsUnicastRouteKey::Clone() const {
    return (new InetMplsUnicastRouteKey(peer(), vrf_name_, dip_, plen_));
}
/////////////////////////////////////////////////////////////////////////////
// InetUnicastAgentRouteTable functions
/////////////////////////////////////////////////////////////////////////////
InetUnicastAgentRouteTable::InetUnicastAgentRouteTable(DB *db,
                                                       const std::string &name) :
    AgentRouteTable(db, name), walkid_(DBTableWalker::kInvalidWalkerId) {

    if (name.find("uc.route.0") != std::string::npos) {
        type_ = Agent::INET4_UNICAST;
    } else if (name.find("uc.route.3") != std::string::npos) {
        type_ = Agent::INET4_MPLS;
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

static void Inet4MplsUnicastTableEnqueue(Agent *agent, DBRequest *req) {
    AgentRouteTable *table = agent->fabric_inet4_mpls_table();
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
bool InetUnicastAgentRouteTable::ResyncSubnetRoutes
(const InetUnicastRouteEntry *rt, bool val) {
    const IpAddress addr = rt->addr();
    uint16_t plen = rt->plen();
    InetUnicastRouteEntry *lpm_rt = GetNextNonConst(rt);

    Ip4Address v4_parent_mask;
    Ip6Address v6_parent_mask;

    if (GetTableType() == Agent::INET4_UNICAST) {
        v4_parent_mask = Address::GetIp4SubnetAddress(addr.to_v4(),
                                                      plen);
    } else if (GetTableType() == Agent::INET6_UNICAST) {
        v6_parent_mask = Address::GetIp6SubnetAddress(addr.to_v6(),
                                                      plen);
    } else {
        // skip processing for inet4_mpls table
        // not expected
        return false;
    }


    // Iterate thru all the routes under this subnet and update route flags
    while ((lpm_rt != NULL) && (plen < lpm_rt->plen())) {
        if (GetTableType() == Agent::INET4_UNICAST) {
            Ip4Address node_mask =
                Address::GetIp4SubnetAddress(lpm_rt->addr().to_v4(),
                                             plen);
            if (v4_parent_mask != node_mask)
                break;

        } else if (GetTableType() == Agent::INET6_UNICAST) {
            Ip6Address node_mask =
                Address::GetIp6SubnetAddress(lpm_rt->addr().to_v6(),
                                             plen);
            if (v6_parent_mask != node_mask)
                break;
        }

        // Update ipam_host_route_ and proxy_arp_ flags for host-routes
        bool notify = false;
        if (lpm_rt->UpdateIpamHostFlags(val)) {
            notify = true;
        }

        if (notify) {
            NotifyEntry(lpm_rt);
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
    } else if (type_ == Agent::INET6_UNICAST) {
        return (Address::GetIp6SubnetAddress(addr.to_v6(), plen));
    }
    return IpAddress(Ip4Address());
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
    ipam_host_route_(false), proxy_arp_(false) {
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


Agent::RouteTableType InetUnicastRouteEntry::GetTableType() const {
    return ((InetUnicastAgentRouteTable *)get_table())->GetTableType();
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
    InetUnicastAgentRouteTable *table =
        (static_cast<InetUnicastAgentRouteTable *>(get_table()));
    Agent *agent = table->agent();
    InetUnicastRouteKey *key = NULL;
    if ((table->GetTableType() == Agent::INET4_MPLS)) {
        key =
        new InetMplsUnicastRouteKey(agent->local_peer(),
                                 vrf()->GetName(), addr_, plen_);
    } else {
        key =
        new InetUnicastRouteKey(agent->local_peer(),
                                 vrf()->GetName(), addr_, plen_);
    }

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

    if (path->nexthop() && path->nexthop()->IsValid() &&
        path->nexthop()->GetType() == NextHop::ARP) {
        //Add bridge route for IP fabric ARP routes, so that
        //MAC stitching can be done for VM routes based on corresponding
        //compute node
        const ArpNH *arp_nh = static_cast<const ArpNH *>(path->nexthop());
        BridgeAgentRouteTable *table =
            static_cast<BridgeAgentRouteTable *>(vrf()->GetBridgeRouteTable());
        table->AddMacVmBindingRoute(path->peer(), vrf()->GetName(), arp_nh->GetMac(),
                                    NULL, false);
    }

    // ECMP path are managed by route module. Update ECMP path with
    // addition of new path
    EcmpData ecmp_data(uc_rt_table->agent(), vrf()->GetName(), ToString(),
                       path, false);
    ret |= ecmp_data.Update(this);
    return ret;
}

bool InetUnicastRouteEntry::ReComputePathDeletion(AgentPath *path) {
    if (IsHostRoute() == false) {
        //TODO merge both evpn inet and subnet routes handling
        UpdateDependantRoutes();
    }


    if (path->nexthop() && path->nexthop()->GetType() == NextHop::ARP) {
        const ArpNH *arp_nh =
            static_cast<const ArpNH *>(path->nexthop());
        BridgeAgentRouteTable *table =
            static_cast<BridgeAgentRouteTable *>(vrf()->GetBridgeRouteTable());
        table->DeleteMacVmBindingRoute(path->peer(), vrf()->GetName(),
                                       arp_nh->GetMac(), NULL);
    }

    InetUnicastAgentRouteTable *uc_rt_table =
        static_cast<InetUnicastAgentRouteTable *>(get_table());
    //Subnet discard = Ipam subnet route.
    //Ipam path is getting deleted so all the smaller subnets should be
    //resynced to remove the arp flood marking.
    if (path->is_subnet_discard()) {
        // Reset flag on route as ipam is going off.
        UpdateRouteFlags(false, false, false);
        uc_rt_table->ResyncSubnetRoutes(this, false);
        return true;
    }
    // ECMP path are managed by route module. Update ECMP path with
    // deletion of new path
    EcmpData ecmp_data(uc_rt_table->agent(), vrf()->GetName(), ToString(),
                       path, true);
    return ecmp_data.Update(this);
}

// ipam_host_route_ flag:
// ipam_host_route_ is set if this is host-route and falls in ipam-subnet range
//
// proxy_arp_ flag:
// For hosts within subnet, we assume ARP must not be proxied and should be
// flooded instead. If we get EVPN route, in KSYNC the EVPN path processing
// will take preference and KSYNC will set Proxy flag
bool InetUnicastRouteEntry::UpdateIpamHostFlags(bool ipam_host_route) {
    // For non-host routes, set proxy always
    if (IsHostRoute() == false) {
        return UpdateRouteFlags(false, false, true);
    }

    bool proxy_arp = ipam_host_route ? false : true;
    return UpdateRouteFlags(false, ipam_host_route, proxy_arp);
}

bool InetUnicastRouteEntry::UpdateRouteFlags(bool ipam_subnet_route,
                                             bool ipam_host_route,
                                             bool proxy_arp) {
    bool ret = false;
    if (ipam_subnet_route_ != ipam_subnet_route) {
        ipam_subnet_route_ = ipam_subnet_route;
        ret = true;
    }

    if (ipam_host_route_ != ipam_host_route) {
        ipam_host_route_ = ipam_host_route;
        ret = true;
    }

    if (proxy_arp_ != proxy_arp) {
        proxy_arp_ = proxy_arp;
        ret = true;
    }

    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// AgentRouteData virtual functions
/////////////////////////////////////////////////////////////////////////////

bool Inet4UnicastArpRoute::AddChangePathExtended(Agent *agent, AgentPath *path,
                                                 const AgentRoute *rt) {
    bool ret = true;

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

    if (path->tag_list() != tag_list_) {
        path->set_tag_list(tag_list_);
        ret = true;
    }

    if (path->ChangeNH(agent, nh) == true)
        ret = true;

    if (nh) {
        if (path->CopyArpData()) {
            ret = true;
        }
    }

    path->set_tunnel_bmap(1 << TunnelType::NATIVE);

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
        std::string nexthop_vrf = nh->get_interface()->vrf()->GetName();
        if (nh->get_interface()->vrf()->forwarding_vrf()) {
            nexthop_vrf = nh->get_interface()->vrf()->forwarding_vrf()->GetName();
        }
        InetUnicastAgentRouteTable::AddArpReq(vrf_name_, gw_ip_.to_v4(),
                                              nexthop_vrf,
                                              nh->get_interface(), nh->PolicyEnabled(),
                                              vn_list_, sg_list_, tag_list_);
    } else {
        path->set_unresolved(false);
    }

    if (path->label() != mpls_label_) {
        path->set_label(mpls_label_);
    }

    path->set_nexthop(NULL);

    SecurityGroupList path_sg_list;
    path_sg_list = path->sg_list();
    if (path_sg_list != sg_list_) {
        path->set_sg_list(sg_list_);
    }

    TagList path_tag_list;
    path_tag_list = path->tag_list();
    if (path_tag_list != tag_list_) {
        path->set_tag_list(tag_list_);
    }

    CommunityList path_communities;
    path_communities = path->communities();
    if (path_communities != communities_) {
        path->set_communities(communities_);
    }

    //Reset to new gateway route, no nexthop for indirect route
    path->set_gw_ip(gw_ip_);
    path->ResetDependantRoute(rt);
    if (rt) {
        path->set_tunnel_bmap(rt->GetActivePath()->tunnel_bmap());
    }

    if (native_encap_) {
        path->set_tunnel_bmap(path->tunnel_bmap() | (1 << TunnelType::NATIVE));
    }

    if (path->dest_vn_list() != vn_list_) {
        path->set_dest_vn_list(vn_list_);
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
(const PhysicalInterface *intrface, const std::string &vn_name) :
        AgentRouteData(AgentRouteData::ADD_DEL_CHANGE, false, 0),
        interface_key_(new PhysicalInterfaceKey(intrface->name())),
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
    data.set_ipam_host_route(ipam_host_route_);
    data.set_proxy_arp(proxy_arp_);
    data.set_src_vrf(vrf()->GetName());
    data.set_multicast(AgentRoute::is_multicast());
    data.set_intf_route_type(AgentRoute::intf_route_type());
    for (Route::PathList::const_iterator it = GetPathList().begin();
         it != GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path) {
            PathSandeshData pdata;
            path->SetSandeshData(pdata);
            if (vrf()->GetName() == Agent::GetInstance()->fabric_vrf_name()
                && (path->tunnel_bmap() & TunnelType::NativeType())) {
                pdata.set_active_tunnel_type("Native");
            }
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

void Inet4MplsUcRouteReq::HandleRequest() const {
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
        sand.reset(new AgentInet4MplsUcRtSandesh(vrf, context(), get_stale()));
    } else {
        boost::system::error_code ec;
        Ip4Address src_ip = Ip4Address::from_string(get_src_ip(), ec);
        sand.reset(new AgentInet4MplsUcRtSandesh(vrf, context(), src_ip,
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
    } else if (type_ == Agent::INET4_MPLS) {
        return AgentSandeshPtr(new AgentInet4MplsUcRtSandesh(vrf_entry(), context,
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

void
InetUnicastAgentRouteTable::Delete(const Peer *peer, const string &vrf_name,
                                   const IpAddress &addr, uint8_t plen,
                                   AgentRouteData *data) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new InetUnicastRouteKey(peer, vrf_name, addr, plen));
    req.data.reset(data);
    InetUnicastTableProcess(Agent::GetInstance(), vrf_name, req);
}

void
InetUnicastAgentRouteTable::DeleteMplsRouteReq(const Peer *peer, const string &vrf_name,
                                      const IpAddress &addr, uint8_t plen,
                                      AgentRouteData *data) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new InetMplsUnicastRouteKey(peer, vrf_name, addr, plen));
    req.data.reset(data);
    Inet4MplsUnicastTableEnqueue(Agent::GetInstance(),  &req);
}

// Utility function to create a route to trap packets to agent.
// Assumes that Interface-NH for "HOST Interface" is already present
void
InetUnicastAgentRouteTable::AddHostRoute(const string &vrf_name,
                                         const IpAddress &addr,
                                         uint8_t plen,
                                         const std::string &dest_vn_name,
                                         bool policy) {
    Agent *agent = Agent::GetInstance();
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(agent->local_peer(), vrf_name,
                                           addr, plen));

    PacketInterfaceKey intf_key(boost::uuids::nil_uuid(),
                                agent->GetHostInterfaceName());
    HostRoute *data = new HostRoute(intf_key, dest_vn_name);
    data->set_policy(policy);
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
                                              const boost::uuids::uuid &intf_uuid,
                                              uint16_t tag,
                                              uint32_t label,
                                              const VnListType &dest_vn_list,
                                              const SecurityGroupList &sg_list,
                                              const TagList &tag_list,
                                              const PathPreference
                                              &path_preference) {
    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    VlanNhRoute *data = new VlanNhRoute(intf_key, tag, label, dest_vn_list,
                                        sg_list, tag_list, path_preference,
                                        peer->sequence_number());
    AddVlanNHRouteReq(peer, vm_vrf, addr, plen, data);
}

// Create Route with VLAN NH
void
InetUnicastAgentRouteTable::AddVlanNHRoute(const Peer *peer,
                                           const string &vm_vrf,
                                           const IpAddress &addr,
                                           uint8_t plen,
                                           const boost::uuids::uuid &intf_uuid,
                                           uint16_t tag,
                                           uint32_t label,
                                           const VnListType &dest_vn_list,
                                           const SecurityGroupList &sg_list,
                                           const TagList &tag_list,
                                           const PathPreference
                                           &path_preference) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(peer, vm_vrf, addr, plen));

    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    req.data.reset(new VlanNhRoute(intf_key, tag, label, dest_vn_list,
                                   sg_list, tag_list, path_preference,
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
                                               const boost::uuids::uuid &intf_uuid,
                                               const VnListType &vn_list,
                                               uint32_t label,
                                               const SecurityGroupList &sg_list,
                                               const TagList &tag_list,
                                               const CommunityList &communities,
                                               bool force_policy,
                                               const PathPreference
                                               &path_preference,
                                               const IpAddress &subnet_service_ip,
                                               const EcmpLoadBalance &ecmp_load_balance,
                                               bool is_local,
                                               bool is_health_check_service,
                                               bool native_encap,
                                               const std::string &intf_name)
{
    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, intf_name);
    LocalVmRoute *data = new LocalVmRoute(intf_key, label,
                                    VxLanTable::kInvalidvxlan_id, force_policy,
                                    vn_list, InterfaceNHFlags::INET4, sg_list,
                                    tag_list, communities, path_preference,
                                    subnet_service_ip, ecmp_load_balance,
                                    is_local, is_health_check_service,
                                    peer->sequence_number(),
                                    false, native_encap);

    AddLocalVmRouteReq(peer, vm_vrf, addr, plen, data);
}

void InetUnicastAgentRouteTable::ResyncRoute(const Peer *peer,
                                             const string &vrf,
                                             const IpAddress &addr,
                                             uint8_t plen) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);

    InetUnicastRouteKey *key = new InetUnicastRouteKey(peer, vrf, addr, plen);
    key->sub_op_ = AgentKey::RESYNC;
    req.key.reset(key);
    req.data.reset(NULL);
    InetUnicastTableEnqueue(Agent::GetInstance(), vrf, &req);
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
                                            const boost::uuids::uuid &intf_uuid,
                                            const VnListType &vn_list,
                                            uint32_t label,
                                            const SecurityGroupList &sg_list,
                                            const TagList &tag_list,
                                            const CommunityList &communities,
                                            bool force_policy,
                                            const PathPreference
                                            &path_preference,
                                            const IpAddress &subnet_service_ip,
                                            const EcmpLoadBalance &ecmp_load_balance,
                                            bool is_local,
                                            bool is_health_check_service,
                                            const std::string &intf_name,
                                            bool native_encap,
                                            const std::string &intf_route_type)
{
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(peer, vm_vrf, addr, plen));

    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, intf_name);
    req.data.reset(new LocalVmRoute(intf_key, label, VxLanTable::kInvalidvxlan_id,
                                    force_policy, vn_list,
                                    InterfaceNHFlags::INET4, sg_list, tag_list,
                                    communities, path_preference,
                                    subnet_service_ip,
                                    ecmp_load_balance, is_local,
                                    is_health_check_service,
                                    peer->sequence_number(), false, native_encap,
                                    intf_route_type));
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
                                      const SecurityGroupList &sg_list,
                                      const TagList &tag_list) {
    Agent *agent = Agent::GetInstance();
    ArpNHKey key(route_vrf_name, ip, policy);
    NextHop *nh =
        static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key));
    if (!nh) {
        DBRequest  nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
        nh_req.key.reset(new ArpNHKey(route_vrf_name, ip, policy));
        nh_req.data.reset(new ArpNHData(
                    static_cast<InterfaceKey *>(intf->GetDBRequestKey().release())));
        agent->nexthop_table()->Enqueue(&nh_req);
    }
    DBRequest  rt_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    rt_req.key.reset(new InetUnicastRouteKey(agent->local_peer(),
                                              route_vrf_name, ip, 32));
    rt_req.data.reset(new Inet4UnicastArpRoute(nexthop_vrf_name, ip, policy,
                                               vn_list, sg_list, tag_list));
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
                                     const SecurityGroupList &sg,
                                     const TagList &tag) {
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
        data = new Inet4UnicastArpRoute(nexthop_vrf_name, ip, policy,
                                        vn_list, sg, tag);
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
                                            vn_list, sg, tag);
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

bool InetUnicastAgentRouteTable::ShouldAddArp(const Ip4Address &ip) {
    if (ip == Agent::GetInstance()->router_id() ||
        !IsIp4SubnetMember(ip, Agent::GetInstance()->router_id(),
                           Agent::GetInstance()->vhost_prefix_len())) {
        // TODO: add Arp request for GW
        // Currently, default GW Arp is added during init
        return false;
    }

    return true;
}

void
InetUnicastAgentRouteTable::CheckAndAddArpRoute(const string &route_vrf_name,
                                                const Ip4Address &ip,
                                                const MacAddress &mac,
                                                const Interface *intf,
                                                bool resolved,
                                                const VnListType &vn_list,
                                                const SecurityGroupList &sg,
                                                const TagList &tag) {
    if (!ShouldAddArp(ip)) {
        return;
    }
    std::string nexthop_vrf = intf->vrf()->GetName();
    if (intf->vrf()->forwarding_vrf()) {
        nexthop_vrf = intf->vrf()->forwarding_vrf()->GetName();
    }
    ArpRoute(DBRequest::DB_ENTRY_ADD_CHANGE, route_vrf_name, ip, mac,
             nexthop_vrf, *intf, resolved, 32, false, vn_list, sg, tag);
}

void
InetUnicastAgentRouteTable::CheckAndAddArpReq(const string &vrf_name,
                                              const Ip4Address &ip,
                                              const Interface *intf,
                                              const VnListType &vn_list,
                                              const SecurityGroupList &sg,
                                              const TagList &tag) {

    if (!ShouldAddArp(ip)) {
        return;
    }
    std::string nexthop_vrf = intf->vrf()->GetName();
    if (intf->vrf()->forwarding_vrf()) {
        nexthop_vrf = intf->vrf()->forwarding_vrf()->GetName();
    }
    AddArpReq(vrf_name, ip, nexthop_vrf, intf, false, vn_list, sg, tag);
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
                                                 &sg_list,
                                                 const TagList
                                                 &tag_list) {
    Agent *agent = Agent::GetInstance();
    ResolveNH::CreateReq(&intf, policy);
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(peer, vrf_name, ip,
                                          plen));
    req.data.reset(new ResolveRoute(&intf, policy, label, vn_name, sg_list, tag_list));
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
        (intf_key, label, TunnelType::MplsType(), vn_list,
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
                                      const InterfaceKey &intf_key,
                                      const IpAddress &addr, uint8_t plen,
                                      const string &vn_name, bool policy,
                                      bool native_encap) {
    req->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req->key.reset(new InetUnicastRouteKey(peer, vrf, addr, plen));

    int tunnel_bmap = TunnelType::AllType();
    if (native_encap) {
        tunnel_bmap |= TunnelType::NativeType();
    }

    req->data.reset(new ReceiveRoute(intf_key, MplsTable::kInvalidExportLabel,
                                    tunnel_bmap, policy, vn_name));
}

static void AddVHostMplsRecvRouteInternal(DBRequest *req, const Peer *peer,
                                      const string &vrf,
                                      const InterfaceKey &intf_key,
                                      const IpAddress &addr, uint8_t plen,
                                      const string &vn_name, bool policy,
                                      bool native_encap) {
    req->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req->key.reset(new InetMplsUnicastRouteKey(peer, vrf, addr, plen));

    int tunnel_bmap = TunnelType::AllType();
    if (native_encap) {
        tunnel_bmap |= TunnelType::NativeType();
    }

    req->data.reset(new ReceiveRoute(intf_key, MplsTable::kImplicitNullLabel,
                                    tunnel_bmap, policy, vn_name));
}
void InetUnicastAgentRouteTable::AddVHostMplsRecvRouteReq
    (const Peer *peer, const string &vrf, const InterfaceKey &intf_key,
     const IpAddress &addr, uint8_t plen, const string &vn_name, bool policy,
     bool native_encap) {
    DBRequest req;
    AddVHostMplsRecvRouteInternal(&req, peer, vrf, intf_key, addr, plen,
                              vn_name, policy, native_encap);
    Inet4MplsUnicastTableEnqueue(Agent::GetInstance(), &req);
}
void InetUnicastAgentRouteTable::AddVHostRecvRoute(const Peer *peer,
                                                   const string &vrf,
                                                   const InterfaceKey &intf_key,
                                                   const IpAddress &addr,
                                                   uint8_t plen,
                                                   const string &vn_name,
                                                   bool policy, bool native_encap, bool ipam_host_route) {
    DBRequest req;
    AddVHostRecvRouteInternal(&req, peer, vrf, intf_key, addr, plen,
                              vn_name, policy, native_encap);
    static_cast<ReceiveRoute *>(req.data.get())->SetProxyArp(true);
    static_cast<ReceiveRoute *>(req.data.get())->SetIpamHostRoute(ipam_host_route);
    if (addr.is_v4()) {
        Inet4UnicastTableProcess(Agent::GetInstance(), vrf, req);
    } else if (addr.is_v6()) {
        Inet6UnicastTableProcess(Agent::GetInstance(), vrf, req);
    }
}

void InetUnicastAgentRouteTable::AddVHostRecvRouteReq
    (const Peer *peer, const string &vrf, const InterfaceKey &intf_key,
     const IpAddress &addr, uint8_t plen, const string &vn_name, bool policy,
     bool native_encap) {
    DBRequest req;
    AddVHostRecvRouteInternal(&req, peer, vrf, intf_key, addr, plen,
                              vn_name, policy, native_encap);
    static_cast<ReceiveRoute *>(req.data.get())->SetProxyArp(true);
    static_cast<ReceiveRoute *>(req.data.get())->SetIpamHostRoute(true);
    if (addr.is_v4()) {
        Inet4UnicastTableEnqueue(Agent::GetInstance(), &req);
    } else if (addr.is_v6()) {
        Inet6UnicastTableEnqueue(Agent::GetInstance(), vrf, &req);
    }
}

void
InetUnicastAgentRouteTable::AddVHostSubnetRecvRoute(const Peer *peer,
                                                    const string &vrf,
                                                    const InterfaceKey &intf_key,
                                                    const Ip4Address &addr,
                                                    uint8_t plen,
                                                    const string &vn_name,
                                                    bool policy) {
    DBRequest req;
    AddVHostRecvRouteInternal(&req, peer, vrf, intf_key, addr, plen,
                              vn_name, policy, true);
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
                                    const VnListType &vn_name, uint32_t label,
                                    const SecurityGroupList &sg_list,
                                    const TagList &tag_list,
                                    const CommunityList &communities,
                                    bool native_encap) {
    req->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req->key.reset(new InetUnicastRouteKey(peer,
                                           vrf_name, dst_addr, plen));
    req->data.reset(new Inet4UnicastGatewayRoute(gw_ip, vrf_name,
                                                 vn_name, label, sg_list,
                                                 tag_list, communities,
                                                 native_encap));
}

void InetUnicastAgentRouteTable::AddGatewayRoute(const Peer *peer,
                                                 const string &vrf_name,
                                                 const Ip4Address &dst_addr,
                                                 uint8_t plen,
                                                 const Ip4Address &gw_ip,
                                                 const VnListType &vn_name,
                                                 uint32_t label,
                                                 const SecurityGroupList
                                                 &sg_list,
                                                 const TagList
                                                 &tag_list,
                                                 const CommunityList
                                                 &communities,
                                                 bool native_encap) {
    DBRequest req;
    AddGatewayRouteInternal(peer, &req, vrf_name, dst_addr, plen, gw_ip, vn_name,
                            label, sg_list, tag_list, communities, native_encap);
    Inet4UnicastTableProcess(Agent::GetInstance(), vrf_name, req);
}

void
InetUnicastAgentRouteTable::AddGatewayRouteReq(const Peer *peer,
                                               const string &vrf_name,
                                               const Ip4Address &dst_addr,
                                               uint8_t plen,
                                               const Ip4Address &gw_ip,
                                               const VnListType &vn_list,
                                               uint32_t label,
                                               const SecurityGroupList
                                               &sg_list,
                                               const TagList
                                               &tag_list,
                                               const CommunityList
                                               &communities,
                                               bool native_encap) {
    DBRequest req;
    AddGatewayRouteInternal(peer, &req, vrf_name, dst_addr, plen, gw_ip,
                            vn_list, label, sg_list, tag_list, communities,
                            native_encap);
    Inet4UnicastTableEnqueue(Agent::GetInstance(), &req);
}

void
InetUnicastAgentRouteTable::AddMplsRouteReq(const Peer *peer,
                                               const string &vrf_name,
                                               const IpAddress &dst_addr,
                                               uint8_t plen,
                                               AgentRouteData *data ) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetMplsUnicastRouteKey(peer, vrf_name, dst_addr, plen));
    req.data.reset(data);
    Inet4MplsUnicastTableEnqueue(Agent::GetInstance(), &req);
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

    //Add local peer path with discard NH
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

void
InetUnicastAgentRouteTable::AddVrouterSubnetRoute(const IpAddress &dst_addr,
                                                  uint8_t plen) {
    /* Only IPv4 is supported */
    if (!dst_addr.is_v4()) {
        return;
    }
    const string &vrf_name = agent()->fabric_vrf_name();
    InetUnicastAgentRouteTable *table = NULL;
    table = agent()->vrf_table()->GetInet4UnicastRouteTable(vrf_name);
    const Peer *peer = agent()->fabric_rt_export_peer();

    VnListType vn_list;
    vn_list.insert(agent()->fabric_vn_name());
    const Ip4Address &gw = agent()->router_id();
    table->AddGatewayRoute(peer, vrf_name, dst_addr.to_v4(), plen, gw, vn_list,
                           MplsTable::kInvalidExportLabel, SecurityGroupList(),
                           TagList(), CommunityList(), true);
}

void
InetUnicastAgentRouteTable::AddVhostMplsRoute(const IpAddress &vhost_addr,
                                                        const Peer *peer) {
    /* Only IPv4 is supported */
    if (!vhost_addr.is_v4()) {
        return;
    }
    const string &vrf_name = agent()->fabric_vrf_name();
    InetUnicastAgentRouteTable *table = NULL;
    table = agent()->vrf_table()->GetInet4MplsUnicastRouteTable(vrf_name);

    VmInterfaceKey vmi_key(AgentKey::ADD_DEL_CHANGE, boost::uuids::nil_uuid(),
                               agent()->vhost_interface_name());
    table->AddVHostMplsRecvRouteReq(peer, vrf_name, vmi_key, vhost_addr,
                                32, agent()->fabric_vn_name(), false, false);
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
                                                 const Interface  *intrface,
                                                 const string &vn_name) {

    assert(intrface->type() == Interface::PHYSICAL);
    DBRequest  rt_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    rt_req.key.reset(new InetUnicastRouteKey(agent->local_peer(),
                                              vrf_name, ip, plen));
    const PhysicalInterface *phy_intf = static_cast<const PhysicalInterface *>
        (intrface);
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
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    const Peer *peer = agent()->inet_evpn_peer();
    if (evpn_route->IsType5()) {
        peer = agent()->evpn_routing_peer();
    }
    req.key.reset(new InetUnicastRouteKey(peer,
                                          evpn_route->vrf()->GetName(),
                                          ip_addr,
                                          GetHostPlen(ip_addr)));
    req.data.reset();
    Process(req);
}

void InetUnicastAgentRouteTable::AddEvpnRoutingRoute(const IpAddress &ip_addr,
                                    uint8_t plen,
                                    const VrfEntry *vrf,
                                    const Peer *peer,
                                    const SecurityGroupList &sg_list,
                                    const CommunityList &communities,
                                    const PathPreference &path_preference,
                                    const EcmpLoadBalance &ecmp_load_balance,
                                    const TagList &tag_list,
                                    DBRequest &nh_req,
                                    uint32_t vxlan_id,
                                    const VnListType& vn_list,
                                    const std::string& origin_vn) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    //Set key and data
    req.key.reset(new InetUnicastRouteKey(peer,
                                          vrf_entry()->GetName(),
                                          ip_addr,
                                          plen));
    req.data.reset(new EvpnRoutingData(nh_req,
                                       sg_list,
                                       communities,
                                       path_preference,
                                       ecmp_load_balance,
                                       tag_list,
                                       vrf,
                                       vxlan_id,
                                       vn_list,
                                       origin_vn));
    Process(req);
}
