/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_MIRROR_CFG_H__
#define __AGENT_MIRROR_CFG_H__

#include <map>
#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <agent_types.h>

using namespace boost::uuids;

class MirrorCfgDisplayResp;
struct MirrorCfgKey {
    std::string handle;
};

struct MirrorCfgData {
    MirrorCfgData() : 
        apply_vn(), src_vn(), src_ip_prefix(), src_ip_prefix_len(0),
        dst_vn(), dst_ip_prefix(), dst_ip_prefix_len(0), start_src_port(0),
        end_src_port(0), start_dst_port(0), end_dst_port(0), protocol(0),
        ip(), udp_port(0), time_period(60) {};
    ~MirrorCfgData() {};

    std::string apply_vn;
    std::string src_vn;
    std::string src_ip_prefix;
    int src_ip_prefix_len;

    // Destination
    std::string dst_vn;
    std::string dst_ip_prefix;
    int dst_ip_prefix_len;

    // -1 on both start and end means any
    // if there is no end_src_port, end_src_port will be same as start_src_port
    int start_src_port;
    int end_src_port;

    // Dest port, -1 means any
    // if there is no end_dst_port, end_dst_port will be same as start_dst_port
    int start_dst_port;
    int end_dst_port;

    // Protocol, -1 means any
    int protocol;

    // Mirror destination
    std::string ip;   
    int udp_port;
    // Time period for mirroring in seconds
    int time_period;
    std::string mirror_vrf;
};

struct AceInfo {
    AceInfo() : id (0) {acl_id = nil_uuid();};
    boost::uuids::uuid acl_id;
    int id;
};

struct MirrorCfgEntry {
    MirrorCfgKey key;
    MirrorCfgData data;
    AceInfo ace_info;
};

struct MirrorCfgKeyCmp {
    bool operator()(const MirrorCfgKey &lhs, const MirrorCfgKey &rhs) {
      return lhs.handle < rhs.handle;
    }
};

struct AclIdInfo {
    AclIdInfo() : num_of_entries(0), ace_id_latest(0) {id = nil_uuid();};
    boost::uuids::uuid id;
    int num_of_entries;
    int ace_id_latest;
};

class MirrorCfgTable {
public:
    typedef std::string VnIdStr;
    typedef boost::uuids::uuid AclUuid;
    // Config Tree
    typedef std::map<MirrorCfgKey, MirrorCfgEntry *, MirrorCfgKeyCmp> MirrorCfgTree;
    typedef std::map<VnIdStr, AclIdInfo> VnAclMap;

    MirrorCfgTable(AgentConfig *cfg) :
        agent_cfg_(cfg), mc_tree_(), vn_acl_map_() {};
    ~MirrorCfgTable() {};

    void Shutdown();
    void Init();
    void SetMirrorCfgSandeshData(std::string &handle, 
                                 MirrorCfgDisplayResp &resp);
    void SetMirrorCfgVnSandeshData(std::string &vn_name,
                                   MirrorCfgVnInfoResp &resp);
    const boost::uuids::uuid GetMirrorUuid(const std::string &vn_name) const;

    const char *Add(const MirrorCreateReq &cfg);
    void Delete(MirrorCfgKey &key);
private:
    AgentConfig *agent_cfg_;
    MirrorCfgTree mc_tree_;
    VnAclMap vn_acl_map_;

    const char *UpdateAclEntry(AclUuid &id, bool create, MirrorCfgEntry *e,
                               int ace_id);

    DISALLOW_COPY_AND_ASSIGN(MirrorCfgTable);
};

struct MirrorDestination {
    std::string handle;
    IpAddress sip;
    uint16_t sport;
    // Mirror destination
    IpAddress dip;   
    uint16_t dport;
    // Time period for mirroring in seconds
    int time_period;
    std::string mirror_vrf;
};

struct IntfMirrorCfgData {
    IntfMirrorCfgData() {intf_id = nil_uuid();};
    boost::uuids::uuid intf_id;
    std::string intf_name;
    MirrorDestination mirror_dest;
};

struct IntfMirrorCfgEntry {
    MirrorCfgKey key;
    IntfMirrorCfgData data;
};

class IntfMirrorCfgTable {
public:
    IntfMirrorCfgTable(AgentConfig *cfg) : agent_cfg_(cfg) {};
    ~IntfMirrorCfgTable() {};
    typedef std::map<MirrorCfgKey, IntfMirrorCfgEntry *, MirrorCfgKeyCmp> IntfMirrorCfgTree;
    const char *Add(const IntfMirrorCreateReq &cfg);
    void Delete(MirrorCfgKey &key);
    IntfMirrorCfgTable *CreateIntfMirrorCfgTable();
    void Init();
    void Shutdown();
    void SetIntfMirrorCfgSandeshData(std::string &handle, 
                                     IntfMirrorCfgDisplayResp &resp);
private:
    AgentConfig *agent_cfg_;
    IntfMirrorCfgTree intf_mc_tree_;
    DISALLOW_COPY_AND_ASSIGN(IntfMirrorCfgTable);
};

#endif // __AGENT_MIRROR_CFG_H__
