/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef controller_route_path_hpp
#define controller_route_path_hpp

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <oper/route_common.h>

//Forward declaration
class AgentXmppChannel;
class AgentRouteData;
class EcmpLoadBalance;
class Peer;
class BgpPeer;
class TunnelNHKey;
class TunnelNHData;
class TunnelType;
class ControllerVmRoute;
class LocalVmRoute;
class InetInterfaceRoute;
class VlanNhRoute;
class VNController;

/*
 * Contains all Controller Route data definition.
 * Currently there are four kind of routes added from controller:
 * - Remote - Native to controller, derived from ControllerPeerPath
 * - Local - Derived from LocalVmRoute of agent_path
 * - Vlan - Derived from VlanNHRoute from agent_path
 * - Inet Interface - Derived from InetInterfaceRoute from agent_path.
 *
 */

class ControllerPeerPath : public AgentRouteData {
public:
    static const uint64_t kInvalidPeerIdentifier = 0xFFFFFFFFFFFFFFFFLL;
    ControllerPeerPath(const Peer *peer);
    ~ControllerPeerPath() { }

    virtual bool UpdateRoute(AgentRoute *route) {return false;}

private:
    const Peer *peer_;
};

/*
 * Implementation for adding remote route data.
 */
class ControllerVmRoute : public ControllerPeerPath {
public:
    ControllerVmRoute(const Peer *peer, const string &vrf_name,
                  const Ip4Address &addr, uint32_t label,
                  const VnListType &dest_vn_list, int bmap,
                  const SecurityGroupList &sg_list,
                  const PathPreference &path_preference,
                  DBRequest &req, bool ecmp_suppressed,
                  const EcmpLoadBalance &ecmp_load_balance):
        ControllerPeerPath(peer), server_vrf_(vrf_name), tunnel_dest_(addr),
        tunnel_bmap_(bmap), label_(label), dest_vn_list_(dest_vn_list),
        sg_list_(sg_list),path_preference_(path_preference),
        ecmp_suppressed_(ecmp_suppressed), ecmp_load_balance_(ecmp_load_balance)
        {nh_req_.Swap(&req);}
    // Data passed in case of delete from BGP peer, to validate 
    // the request at time of processing.
    ControllerVmRoute(const Peer *peer) : ControllerPeerPath(peer) { }
    virtual ~ControllerVmRoute() { }

    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt);
    virtual bool UpdateRoute(AgentRoute *route);
    virtual string ToString() const {return "remote VM";}
    const SecurityGroupList &sg_list() const {return sg_list_;}
    static ControllerVmRoute *MakeControllerVmRoute(const Peer *peer,
                                            const string &default_vrf,
                                            const Ip4Address &router_id,
                                            const string &vrf_name,
                                            const Ip4Address &tunnel_dest,
                                            TunnelType::TypeBmap bmap,
                                            uint32_t label,
                                            const VnListType &dest_vn_list,
                                            const SecurityGroupList &sg_list,
                                            const PathPreference &path_preference,
                                            bool ecmp_suppressed,
                                            const EcmpLoadBalance &ecmp_load_balance);

private:
    string server_vrf_;
    Ip4Address tunnel_dest_;
    TunnelType::TypeBmap tunnel_bmap_;
    uint32_t label_;
    VnListType dest_vn_list_;
    SecurityGroupList sg_list_;
    PathPreference path_preference_;
    DBRequest nh_req_;
    bool ecmp_suppressed_;
    EcmpLoadBalance ecmp_load_balance_;
    DISALLOW_COPY_AND_ASSIGN(ControllerVmRoute);
};

class ControllerEcmpRoute : public ControllerPeerPath {
public:
    ControllerEcmpRoute(const Peer *peer, const IpAddress &dest_addr,
                        uint8_t plen, const VnListType &vn_list,
                        uint32_t label, bool local_ecmp_nh,
                        const string &vrf_name, SecurityGroupList sg_list,
                        const PathPreference &path_preference,
                        TunnelType::TypeBmap tunnel_bmap,
                        const EcmpLoadBalance &ecmp_load_balance,
                        DBRequest &nh_req) :
        ControllerPeerPath(peer), dest_addr_(dest_addr), plen_(plen),
        vn_list_(vn_list), label_(label), local_ecmp_nh_(local_ecmp_nh),
        vrf_name_(vrf_name), sg_list_(sg_list),
        path_preference_(path_preference), tunnel_bmap_(tunnel_bmap),
        ecmp_load_balance_(ecmp_load_balance)
        {nh_req_.Swap(&nh_req);}

    virtual ~ControllerEcmpRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *);
    virtual string ToString() const {return "inet4 ecmp";}

private:
    IpAddress dest_addr_;
    uint8_t plen_;
    VnListType vn_list_;
    uint32_t label_;
    bool local_ecmp_nh_;
    string vrf_name_;
    SecurityGroupList sg_list_;
    PathPreference path_preference_;
    TunnelType::TypeBmap tunnel_bmap_;
    EcmpLoadBalance ecmp_load_balance_;
    DBRequest nh_req_;
    DISALLOW_COPY_AND_ASSIGN(ControllerEcmpRoute);
};

/*
 * ClonedLocalPath would be used to pick nexthop from the
 * local peer, instead of nexthop pointed by mpls label.
 * Currently it gets used in gateway interface. In case of
 * gateway interface, label exported by agent would point
 * to table nexthop, and the prefix route of gateway
 * interface would point resolve nexthop, so that ARP resolution
 * can be triggered when packet hits the subnet route.
 */
class ClonedLocalPath : public AgentRouteData {
public:
    ClonedLocalPath(uint32_t label, const VnListType &vn_list,
                    const SecurityGroupList &sg_list):
        AgentRouteData(false), mpls_label_(label),
        vn_list_(vn_list), sg_list_(sg_list) {}
    virtual ~ClonedLocalPath() {}
    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt);
    virtual std::string ToString() const {
        return "Nexthop cloned from local path";
    }
private:
    uint32_t mpls_label_;
    const VnListType vn_list_;
    const SecurityGroupList sg_list_;
    DISALLOW_COPY_AND_ASSIGN(ClonedLocalPath);
};

/*
 * In headless mode stale path is created when no CN server is present.
 * Last peer going down marks its path as stale and keep route alive, till
 * anothe CN takes over.
 * There can be only one stale path as multiple does not make any sense.
 * (Stale path is to keep traffic flowing).
 */
class StalePathData : public AgentRouteData {
public:
    StalePathData() : AgentRouteData(false) { }
    virtual ~StalePathData() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt);
    virtual std::string ToString() const {
        return "Stale path marking(healdess mode)";
    }

private:
    DISALLOW_COPY_AND_ASSIGN(StalePathData);
};
#endif //controller_route_path_hpp
