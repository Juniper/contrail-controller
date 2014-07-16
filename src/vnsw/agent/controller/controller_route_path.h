/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef controller_route_path_hpp
#define controller_route_path_hpp

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <oper/route_common.h>

using namespace std;

//Forward declaration
class AgentXmppChannel;
class AgentRouteData;
class Peer;
class BgpPeer;
class TunnelNHKey;
class TunnelNHData;
class TunnelType;
class ControllerVmRoute;

class ControllerPeerPath : public AgentRouteData {
public:
    ControllerPeerPath(const Peer *peer);
    ~ControllerPeerPath() { }

    bool CheckPeerValidity() const;
    // Only to be used for tests
    void set_sequence_number(uint64_t sequence_number) {
        sequence_number_ = sequence_number;
    }

private:
    const Peer *peer_;
    const AgentXmppChannel *channel_;
    uint64_t sequence_number_;
};

class ControllerVmRoute : public ControllerPeerPath {
public:
    ControllerVmRoute(const Peer *peer, const string &vrf_name,
                  const Ip4Address &addr, uint32_t label,
                  const string &dest_vn_name, int bmap,
                  const SecurityGroupList &sg_list,
                  const PathPreference &path_preference,
                  DBRequest &req):
        ControllerPeerPath(peer), server_vrf_(vrf_name), server_ip_(addr),
        tunnel_bmap_(bmap), label_(label), dest_vn_name_(dest_vn_name),
        sg_list_(sg_list),path_preference_(path_preference)
        {nh_req_.Swap(&req);}
    // Data passed in case of delete from BGP peer, to validate 
    // the request at time of processing.
    ControllerVmRoute(const Peer *peer) : ControllerPeerPath(peer) { }
    virtual ~ControllerVmRoute() { }

    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    virtual string ToString() const {return "remote VM";}
    virtual bool IsPeerValid() const;
    const SecurityGroupList &sg_list() const {return sg_list_;}
    static ControllerVmRoute *MakeControllerVmRoute(const Peer *peer,
                                            const string &default_vrf,
                                            const Ip4Address &router_id,
                                            const string &vrf_name,
                                            const Ip4Address &server_ip, 
                                            TunnelType::TypeBmap bmap,
                                            uint32_t label,
                                            const string &dest_vn_name,
                                            const SecurityGroupList &sg_list,
                                            const PathPreference
                                            &path_preference);

private:
    string server_vrf_;
    Ip4Address server_ip_;
    TunnelType::TypeBmap tunnel_bmap_;
    uint32_t label_;
    string dest_vn_name_;
    SecurityGroupList sg_list_;
    PathPreference path_preference_;
    DBRequest nh_req_;
    DISALLOW_COPY_AND_ASSIGN(ControllerVmRoute);
};

class ControllerEcmpRoute : public ControllerPeerPath {
public:
    ControllerEcmpRoute(const Peer *peer, const Ip4Address &dest_addr,
                        uint8_t plen, const string &vn_name, uint32_t label,
                        bool local_ecmp_nh, const string &vrf_name,
                        SecurityGroupList sg_list,
                        const PathPreference &path_preference,
                        DBRequest &nh_req) :
        ControllerPeerPath(peer), dest_addr_(dest_addr), plen_(plen),
        vn_name_(vn_name), label_(label), local_ecmp_nh_(local_ecmp_nh),
        vrf_name_(vrf_name), sg_list_(sg_list), path_preference_(path_preference)
        {nh_req_.Swap(&nh_req);}

    virtual ~ControllerEcmpRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    virtual string ToString() const {return "inet4 ecmp";}
    virtual bool IsPeerValid() const;

private:
    Ip4Address dest_addr_;
    uint8_t plen_;
    string vn_name_;
    uint32_t label_;
    bool local_ecmp_nh_;
    string vrf_name_;
    SecurityGroupList sg_list_;
    PathPreference path_preference_;
    DBRequest nh_req_;
    DISALLOW_COPY_AND_ASSIGN(ControllerEcmpRoute);
};

#endif //controller_route_path_hpp
