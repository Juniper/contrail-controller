/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_ecmp_hpp
#define vnsw_ecmp_hpp

#include <cmn/agent_cmn.h>
#include <oper/route_common.h>

class EcmpLoadBalance;
class PathPreference;
class TunnelType;

class EcmpData {
public:
    EcmpData(Agent *agent,
             const string &vrf_name,
             const string &route_str,
             AgentPath *path,
             bool del);
    virtual ~EcmpData() { }

    bool Update(AgentRoute *rt);
    bool UpdateWithParams(const SecurityGroupList &sg_list,
                          const TagList &tag_list,
                          const CommunityList &community_list,
                          const PathPreference &path_preference,
                          const TunnelType::TypeBmap bmap,
                          const EcmpLoadBalance &ecmp_load_balance,
                          const VnListType &vn_list,
                          DBRequest &nh_req);
    static const NextHop* GetLocalNextHop(const AgentRoute *rt);

private:
    //Ecmp formed with local_vm_peer
    bool LocalVmPortPeerEcmp(AgentRoute *rt);
    //Ecmp formed by bgp peer(CN)
    bool BgpPeerEcmp();
    //Helper routines
    bool EcmpAddPath(AgentRoute *rt);
    bool EcmpDeletePath(AgentRoute *rt);
    bool ModifyEcmpPath();
    bool UpdateNh();
    bool SyncParams();
    //Allocate or append
    void AllocateEcmpPath(AgentRoute *rt, const AgentPath *path2);
    bool UpdateComponentNH(AgentRoute *rt, AgentPath *path);
    void AppendEcmpPath(AgentRoute *rt, AgentPath *path);
    //Delete
    void DeleteComponentNH(AgentRoute *rt, AgentPath *path);

    //Variables
    AgentPath *path_;
    AgentPath *ecmp_path_;
    bool delete_;
    bool alloc_label_;
    uint32_t label_;
    std::string vrf_name_;
    std::string route_str_;
    VnListType vn_list_;
    SecurityGroupList sg_list_;
    TagList tag_list_;
    CommunityList community_list_;
    PathPreference path_preference_;
    TunnelType::TypeBmap tunnel_bmap_;
    EcmpLoadBalance ecmp_load_balance_;
    DBRequest nh_req_;
    Agent *agent_;
    DISALLOW_COPY_AND_ASSIGN(EcmpData);
};

#endif // vnsw_ecmp_hpp
