/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef controller_route_path_hpp
#define controller_route_path_hpp

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <oper/route_common.h>
#include <xmpp_enet_types.h>
#include <xmpp_unicast_types.h>

//Forward declaration
class AgentXmppChannel;
struct AgentRouteData;
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
class ClonedLocalPath;
class EcmpData;

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
    ControllerPeerPath(const BgpPeer *peer);
    ~ControllerPeerPath() { }

    virtual bool UpdateRoute(AgentRoute *route) {return false;}

private:
    const BgpPeer *peer_;
};

/*
 * Implementation for adding remote route data.
 */
class ControllerVmRoute : public ControllerPeerPath {
public:
    ControllerVmRoute(const BgpPeer *peer, const string &vrf_name,
                  const Ip4Address &addr, uint32_t label,
                  const VnListType &dest_vn_list, int bmap,
                  const SecurityGroupList &sg_list,
                  const TagList &tag_list,
                  const PathPreference &path_preference,
                  DBRequest &req, bool ecmp_suppressed,
                  const EcmpLoadBalance &ecmp_load_balance,
                  bool etree_leaf,
                  const MacAddress &rewrite_dmac = MacAddress()) :
        ControllerPeerPath(peer), server_vrf_(vrf_name), tunnel_dest_(addr),
        tunnel_bmap_(bmap), label_(label), dest_vn_list_(dest_vn_list),
        sg_list_(sg_list),tag_list_(tag_list), path_preference_(path_preference),
        ecmp_suppressed_(ecmp_suppressed), ecmp_load_balance_(ecmp_load_balance),
        etree_leaf_(etree_leaf), rewrite_dmac_(rewrite_dmac)
        {
            nh_req_.Swap(&req);
            tunnel_dest_list_.push_back(addr);
            label_list_.push_back(label);
        }
    // Data passed in case of delete from BGP peer, to validate
    // the request at time of processing.
    ControllerVmRoute(const BgpPeer *peer) : ControllerPeerPath(peer) { }
    virtual ~ControllerVmRoute() { }

    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
                                       const AgentRoute *rt);
    virtual bool UpdateRoute(AgentRoute *route);
    virtual string ToString() const {return "remote VM";}
    const SecurityGroupList &sg_list() const {return sg_list_;}
    const TagList &tag_list() const {return tag_list_;}
    static ControllerVmRoute *MakeControllerVmRoute(const BgpPeer *peer,
                                            const string &default_vrf,
                                            const Ip4Address &router_id,
                                            const string &vrf_name,
                                            const Ip4Address &tunnel_dest,
                                            TunnelType::TypeBmap bmap,
                                            uint32_t label,
                                            MacAddress rewrite_dmac,
                                            const VnListType &dest_vn_list,
                                            const SecurityGroupList &sg_list,
                                            const TagList &tag_list,
                                            const PathPreference &path_preference,
                                            bool ecmp_suppressed,
                                            const EcmpLoadBalance &ecmp_load_balance,
                                            bool etree_leaf);

private:
    string server_vrf_;
    Ip4Address tunnel_dest_;
    TunnelType::TypeBmap tunnel_bmap_;
    uint32_t label_;
    VnListType dest_vn_list_;
    SecurityGroupList sg_list_;
    TagList tag_list_;
    PathPreference path_preference_;
    DBRequest nh_req_;
    bool ecmp_suppressed_;
    EcmpLoadBalance ecmp_load_balance_;
    bool etree_leaf_;
    MacAddress rewrite_dmac_;
    std::vector<IpAddress> tunnel_dest_list_;
    std::vector<uint32_t> label_list_;
    DISALLOW_COPY_AND_ASSIGN(ControllerVmRoute);
};

class ControllerMplsRoute : public ControllerPeerPath {
public:
    ControllerMplsRoute(const BgpPeer *peer, const string &vrf_name,
                  const Ip4Address &addr, uint32_t label,
                  const VnListType &dest_vn_list, int bmap,
                  const SecurityGroupList &sg_list,
                  const TagList &tag_list,
                  const PathPreference &path_preference,
                  DBRequest &req, bool ecmp_suppressed,
                  const EcmpLoadBalance &ecmp_load_balance,
                  bool etree_leaf,
                  const MacAddress &rewrite_dmac = MacAddress()) :
        ControllerPeerPath(peer), server_vrf_(vrf_name), tunnel_dest_(addr),
        tunnel_bmap_(bmap), label_(label), dest_vn_list_(dest_vn_list),
        sg_list_(sg_list),tag_list_(tag_list), path_preference_(path_preference),
        ecmp_suppressed_(ecmp_suppressed), ecmp_load_balance_(ecmp_load_balance),
        etree_leaf_(etree_leaf), rewrite_dmac_(rewrite_dmac)
        {nh_req_.Swap(&req);}
    // Data passed in case of delete from BGP peer, to validate
    // the request at time of processing.
    ControllerMplsRoute(const BgpPeer *peer) : ControllerPeerPath(peer) { }
    virtual ~ControllerMplsRoute() { }

    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
                                       const AgentRoute *rt);
    //virtual bool UpdateRoute(AgentRoute *route);
    virtual string ToString() const {return "remote PE";}
    const SecurityGroupList &sg_list() const {return sg_list_;}
    const TagList &tag_list() const {return tag_list_;}
    static ControllerMplsRoute *MakeControllerMplsRoute(const BgpPeer *peer,
                                            const string &default_vrf,
                                            const Ip4Address &router_id,
                                            const string &vrf_name,
                                            const Ip4Address &tunnel_dest,
                                            TunnelType::TypeBmap bmap,
                                            uint32_t label,
                                            MacAddress rewrite_dmac,
                                            const VnListType &dest_vn_list,
                                            const SecurityGroupList &sg_list,
                                            const TagList &tag_list,
                                            const PathPreference &path_preference,
                                            bool ecmp_suppressed,
                                            const EcmpLoadBalance &ecmp_load_balance,
                                            bool etree_leaf);

private:
    string server_vrf_;
    Ip4Address tunnel_dest_;
    TunnelType::TypeBmap tunnel_bmap_;
    uint32_t label_;
    VnListType dest_vn_list_;
    SecurityGroupList sg_list_;
    TagList tag_list_;
    PathPreference path_preference_;
    DBRequest nh_req_;
    bool ecmp_suppressed_;
    EcmpLoadBalance ecmp_load_balance_;
    bool etree_leaf_;
    MacAddress rewrite_dmac_;
    DISALLOW_COPY_AND_ASSIGN(ControllerMplsRoute);
};
class ControllerEcmpRoute : public ControllerPeerPath {
public:
    static const uint32_t maximum_ecmp_paths = 128;
    typedef std::list<ClonedLocalPath *> ClonedLocalPathList;
    typedef ClonedLocalPathList::iterator ClonedLocalPathListIter;
    //Uses Item to build attributes
    template <typename TYPE>
    ControllerEcmpRoute(const BgpPeer *peer,
                        const VnListType &vn_list,
                        const EcmpLoadBalance &ecmp_load_balance,
                        const TagList &tag_list,
                        const TYPE *item,
                        const AgentRouteTable *rt_table,
                        const std::string &prefix_str);
    //Non item based constructor (Used by UT)
    ControllerEcmpRoute(const BgpPeer *peer,
                        const VnListType &vn_list,
                        const EcmpLoadBalance &ecmp_load_balance,
                        const TagList &tag_list,
                        const SecurityGroupList &sg_list,
                        const PathPreference &path_pref,
                        TunnelType::TypeBmap tunnel_bmap,
                        DBRequest &nh_req,
                        const std::string &prefix_str);
    ControllerEcmpRoute(const BgpPeer *peer,
                        const VnListType &vn_list,
                        const EcmpLoadBalance &ecmp_load_balance,
                        const TagList &tag_list,
                        const SecurityGroupList &sg_list,
                        const PathPreference &path_pref,
                        TunnelType::TypeBmap tunnel_bmap,
                        std::vector<IpAddress> &tunnel_dest_list,
                        std::vector<uint32_t> &label_list,
                        const std::string &prefix_str,
                        const std::string &vrf_name);
    virtual ~ControllerEcmpRoute() { }
    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
                                       const AgentRoute *rt);
    virtual string ToString() const {return "inet4 ecmp";}
    bool CopyToPath(AgentPath *path);
    void BuildNhReq(const string &vrf_name, const autogen::ItemType *item_type,
                    const VnListType &vn_list);
    ClonedLocalPathList &cloned_local_path_list() {
        return cloned_data_list_;
    }

    void set_copy_local_path(bool copy_local_path) {
        copy_local_path_ = copy_local_path;
    }
private:
    VnListType vn_list_;
    SecurityGroupList sg_list_;
    EcmpLoadBalance ecmp_load_balance_;
    TagList tag_list_;
    PathPreference path_preference_;
    TunnelType::TypeBmap tunnel_bmap_;
    DBRequest nh_req_;
    ClonedLocalPathList cloned_data_list_;
    Agent *agent_;
    bool copy_local_path_;
    std::vector<IpAddress>tunnel_dest_list_;
    std::vector<uint32_t>label_list_;
    string vrf_name_;
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
                    const SecurityGroupList &sg_list,
                    const TagList &tag_list,
                    uint64_t sequence_number):
        AgentRouteData(AgentRouteData::ADD_DEL_CHANGE, false, sequence_number),
        mpls_label_(label), vn_list_(vn_list),
        sg_list_(sg_list), tag_list_(tag_list) {}
    virtual ~ClonedLocalPath() {}
    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
                                       const AgentRoute *rt);
    virtual std::string ToString() const {
        return "Nexthop cloned from local path";
    }
private:
    uint32_t mpls_label_;
    const VnListType vn_list_;
    const SecurityGroupList sg_list_;
    const TagList tag_list_;
    DISALLOW_COPY_AND_ASSIGN(ClonedLocalPath);
};

/*
 * stale path is created when no CN server is present.
 * Last peer going down marks its path as stale and keep route alive, till
 * anothe CN takes over.
 * There can be only one stale path as multiple does not make any sense.
 * (Stale path is to keep traffic flowing).
 */
class StalePathData : public AgentRouteData {
public:
    StalePathData(uint64_t sequence_number) :
        AgentRouteData(AgentRouteData::ADD_DEL_CHANGE, false,
                       sequence_number) { }
    virtual ~StalePathData() { }
    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
                                       const AgentRoute *rt);
    virtual bool CanDeletePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt) const;
    virtual std::string ToString() const {
        return "Stale path marking(healdess mode)";
    }

private:
    DISALLOW_COPY_AND_ASSIGN(StalePathData);
};
#endif //controller_route_path_hpp
