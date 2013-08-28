/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_route_vmpath_hpp
#define vnsw_agent_route_vmpath_hpp

#include <base/dependency.h>
#include <cmn/agent_cmn.h>
#include <route/path.h>
#include <oper/mpls.h>
#include <oper/peer.h>

using namespace std;
using namespace boost::uuids;

struct Inet4RouteData;
struct Inet4UcRouteKey;
class Inet4UcRoute;

class AgentPath : public Path {
public:
    AgentPath(const Peer *peer, Inet4UcRoute *rt) : 
        Path(), peer_(peer), nh_(NULL), label_(MplsTable::kInvalidLabel),
        dest_vn_name_(""), unresolved_(true), vrf_name_(""), gw_rt_(rt), 
        proxy_arp_(false), force_policy_(false), tunnel_bmap_(0) { };
    virtual ~AgentPath() { };

    virtual std::string ToString() const { return "AgentPath"; };

    const Peer *GetPeer() const {return peer_;};
    const NextHop *GetNextHop() const;
    uint32_t GetLabel() const {return label_;};
    const string &GetDestVnName() const {return dest_vn_name_;};
    const string &GetVrfName() const {return vrf_name_;};
    const bool IsUnresolved() const {return unresolved_;};
    const SecurityGroupList &GetSecurityGroupList() const {return sg_list_;};  
    void ClearSecurityGroupList() { sg_list_.clear(); }

    // Invoked on route data change
    bool RouteChange(AgentDBTable *nh_table, Inet4UcRouteKey *rt_key, 
                     Inet4RouteData *d, bool &sync);
    const Ip4Address& GetGatewayIp() const {return gw_ip_;};
    bool Sync(Inet4UcRoute *sync_route);
    bool GetProxyArp() const {return proxy_arp_;};
private:
    // Peer distributing the route. Will be NULL for self generated routes
    const Peer *peer_;
    NextHopRef nh_;
    uint32_t label_;
    string dest_vn_name_;
    // Points to gateway route, if this path is part of
    // indirect route
    bool unresolved_;
    Ip4Address gw_ip_;
    string vrf_name_;
    DependencyRef<Inet4UcRoute, Inet4UcRoute> gw_rt_;
    bool proxy_arp_;
    bool force_policy_;
    TunnelType::TypeBmap tunnel_bmap_;
    SecurityGroupList sg_list_;
    DISALLOW_COPY_AND_ASSIGN(AgentPath);
};

#endif // vnsw_agent_route_vmpath_hpp
