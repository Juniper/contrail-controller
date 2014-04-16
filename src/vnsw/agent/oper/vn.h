/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vn_hpp
#define vnsw_agent_vn_hpp

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <cmn/agent_cmn.h>
#include <oper/agent_types.h>
#include <filter/acl.h>
#include <oper/vrf.h>
#include <oper/vxlan.h>

using namespace boost::uuids;
using namespace std;

namespace autogen {
    class NetworkIpam;
    class VirtualDns;
    struct IpamType;
    struct VirtualDnsType;
}

class VmInterface;
struct VnIpam {
    Ip4Address ip_prefix;
    uint32_t   plen;
    Ip4Address default_gw;
    bool       installed;    // is the route to send pkts to host installed
    std::string ipam_name;

    VnIpam(const std::string& ip, uint32_t len, const std::string& gw,
           std::string &name)
        : plen(len), installed(false), ipam_name(name) {
        boost::system::error_code ec;
        ip_prefix = Ip4Address::from_string(ip, ec);
        default_gw = Ip4Address::from_string(gw, ec);
    }
    bool operator<(const VnIpam &rhs) const {
        if (ip_prefix != rhs.ip_prefix)
            return ip_prefix < rhs.ip_prefix;

        return (plen < rhs.plen);
    }
    Ip4Address GetBroadcastAddress() const {
        Ip4Address broadcast(ip_prefix.to_ulong() | 
                             ~(0xFFFFFFFF << (32 - plen)));
        return broadcast;
    }
    Ip4Address GetSubnetAddress() const {
        return GetIp4SubnetAddress(ip_prefix, plen);
    }
    bool IsSubnetMember(const Ip4Address &ip) const {
        return ((ip_prefix.to_ulong() | ~(0xFFFFFFFF << (32 - plen))) == 
                 (ip.to_ulong() | ~(0xFFFFFFFF << (32 - plen)))); 
    }
};

struct VnSubnet {
    Ip4Address prefix;
    uint32_t plen;

    VnSubnet() : prefix(), plen(0) {}
    bool operator<(const VnSubnet &rhs) const {
        if (prefix != rhs.prefix)
            return prefix < rhs.prefix;
        return (plen < rhs.plen);
    }
    bool operator==(const VnSubnet &rhs) const {
        if (prefix == rhs.prefix && plen == rhs.plen)
            return true;
        return false;
    }
    std::string ToString() const { 
        char len[32];
        snprintf(len, sizeof(len), "%u", plen);
        return prefix.to_string() + "/" + std::string(len);
    }
};

// Per IPAM data of the VN
// TODO: move the subnet, gw data here
struct VnIpamLinkData {
    std::set<VnSubnet> host_routes;

    void AddRoute(const VnSubnet &route) { host_routes.insert(route); }
    bool operator==(const VnIpamLinkData &rhs) const {
        if (host_routes == rhs.host_routes)
            return true;
        return false;
    }
};

struct VnKey : public AgentKey {
    VnKey(uuid id) : AgentKey(), uuid_(id) {} ;
    virtual ~VnKey() { };

    uuid uuid_;
};

struct VnData : public AgentData {
    typedef std::map<std::string, VnIpamLinkData> VnIpamDataMap;
    typedef std::pair<std::string, VnIpamLinkData> VnIpamDataPair;

    VnData(const string &name, const uuid &acl_id, const string &vrf_name,
           const uuid &mirror_acl_id, const uuid &mc_acl_id, 
           const std::vector<VnIpam> &ipam, const VnIpamDataMap &vn_ipam_data,
           int vxlan_id, int vnid, bool layer2_forwarding,
           bool ipv4_forwarding) :
                AgentData(), name_(name), vrf_name_(vrf_name), acl_id_(acl_id),
                mirror_acl_id_(mirror_acl_id), mirror_cfg_acl_id_(mc_acl_id),
                ipam_(ipam), vn_ipam_data_(vn_ipam_data), vxlan_id_(vxlan_id),
                vnid_(vnid), layer2_forwarding_(layer2_forwarding), 
                ipv4_forwarding_(ipv4_forwarding) {  
    };
    virtual ~VnData() { };

    string name_;
    string vrf_name_;
    uuid acl_id_;
    uuid mirror_acl_id_;
    uuid mirror_cfg_acl_id_;
    std::vector<VnIpam> ipam_;
    VnIpamDataMap vn_ipam_data_;
    int vxlan_id_;
    int vnid_;
    bool layer2_forwarding_;
    bool ipv4_forwarding_;
};

class VnEntry : AgentRefCount<VnEntry>, public AgentDBEntry {
public:
    VnEntry(uuid id) : uuid_(id), vxlan_id_(0), vnid_(0), layer2_forwarding_(true), 
    ipv4_forwarding_(true) { };
    virtual ~VnEntry() { };

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual string ToString() const;

    const uuid &GetUuid() const {return uuid_;};
    const string &GetName() const {return name_;};
    bool IsAclSet() const {
        return ((acl_.get() != NULL) || (mirror_acl_.get() != NULL) ||
                (mirror_cfg_acl_.get() != NULL));
    };
    const AclDBEntry *GetAcl() const {return acl_.get();};
    const AclDBEntry *GetMirrorAcl() const {return mirror_acl_.get();};
    const AclDBEntry *GetMirrorCfgAcl() const {return mirror_cfg_acl_.get();};
    VrfEntry *GetVrf() const {return vrf_.get();};
    const std::vector<VnIpam> &GetVnIpam() const { return ipam_; };
    bool GetVnHostRoutes(const std::string &ipam, std::set<VnSubnet> *routes) const;
    bool GetIpamName(const Ip4Address &vm_addr, std::string *ipam_name) const;
    bool GetIpamData(const Ip4Address &vm_addr, std::string *ipam_name,
                     autogen::IpamType *ipam_type) const;
    bool GetIpamVdnsData(const Ip4Address &vm_addr, 
                         autogen::IpamType *ipam_type,
                         autogen::VirtualDnsType *vdns_type) const;
    int GetVxLanId() const;
    bool Resync(); 
    bool VxLanNetworkIdentifierChanged();
    bool ReEvaluateVxlan(VrfEntry *old_vrf, int new_vxlan_id, int new_vnid,
                         bool new_layer2_forwarding,
                         bool vxlan_network_identifier_mode_changed);

    const VxLanId *vxlan_id_ref() const {return vxlan_id_ref_.get();}
    bool layer2_forwarding() const {return layer2_forwarding_;};
    bool Ipv4Forwarding() const {return ipv4_forwarding_;};

    AgentDBTable *DBToTable() const;
    uint32_t GetRefCount() const {
        return AgentRefCount<VnEntry>::GetRefCount();
    }

    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;
    void SendObjectLog(AgentLogEvent::type event) const;

private:
    void RebakeVxLan(int vxlan_id);
    friend class VnTable;

    uuid uuid_;
    string name_;
    AclDBEntryRef acl_;
    AclDBEntryRef mirror_acl_;
    AclDBEntryRef mirror_cfg_acl_;
    VrfEntryRef vrf_;
    std::vector<VnIpam> ipam_;
    VnData::VnIpamDataMap vn_ipam_data_;
    int vxlan_id_;
    int vnid_;
    bool layer2_forwarding_;
    bool ipv4_forwarding_;
    VxLanIdRef vxlan_id_ref_;
    DISALLOW_COPY_AND_ASSIGN(VnEntry);
};

class VnTable : public AgentDBTable {
public:
    VnTable(DB *db, const std::string &name) : AgentDBTable(db, name),
        walkid_(DBTableWalker::kInvalidWalkerId) { }
    virtual ~VnTable() { }

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t  Hash(const DBRequestKey *key) const {return 0;};

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual void Delete(DBEntry *entry, const DBRequest *req);
    virtual bool Resync(DBEntry *entry, DBRequest *req); 

    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req);

    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static VnTable *GetInstance() {return vn_table_;};

    void AddVn(const uuid &vn_uuid, const string &name, const uuid &acl_id,
               const string &vrf_name, const std::vector<VnIpam> &ipam,
               const VnData::VnIpamDataMap &vn_ipam_data, int vxlan_id);
    void DelVn(const uuid &vn_uuid);
    void UpdateVxLanNetworkIdentifierMode();
    bool VnEntryWalk(DBTablePartBase *partition, DBEntryBase *entry);
    void VnEntryWalkDone(DBTableBase *partition);

    static void IpamVnSync(IFMapNode *node);

private:
    static VnTable *vn_table_;
    bool IpamChangeNotify(std::vector<VnIpam> &old_ipam, 
                          std::vector<VnIpam> &new_ipam, VnEntry *vn);
    void UpdateSubnetGateway(const VnIpam &old_ipam, const VnIpam &new_ipam, 
                             VnEntry *vn);
    void AddIPAMRoutes(VnEntry *vn, VnIpam &ipam);
    void DelIPAMRoutes(VnEntry *vn, VnIpam &ipam);
    void DeleteAllIpamRoutes(VnEntry *vn);
    void AddSubnetRoute(VnEntry *vn, VnIpam &ipam);
    void DelSubnetRoute(VnEntry *vn, VnIpam &ipam);
    void AddHostRouteForGw(VnEntry *vn, const VnIpam &ipam);
    void DelHostRouteForGw(VnEntry *vn, const VnIpam &ipam);
    bool ChangeHandler(DBEntry *entry, const DBRequest *req);
    IFMapNode *FindTarget(IFMapAgentTable *table, IFMapNode *node, 
                          std::string node_type);
    DBTableWalker::WalkId walkid_;

    DISALLOW_COPY_AND_ASSIGN(VnTable);
};

class DomainConfig {
public:
    typedef std::map<std::string, IFMapNode  *> DomainConfigMap;
    typedef std::pair<std::string, IFMapNode *> DomainConfigPair;
    typedef boost::function<void(IFMapNode *)> Callback;
    
    DomainConfig() {}
    virtual ~DomainConfig() {}
    void RegisterIpamCb(Callback cb);
    void RegisterVdnsCb(Callback cb);
    void IpamSync(IFMapNode *node);
    void VDnsSync(IFMapNode *node);

    bool GetIpam(const std::string &name, autogen::IpamType *ipam);
    bool GetVDns(const std::string &vdns, autogen::VirtualDnsType *vdns_type);

private:
    void CallVdnsCb(IFMapNode *node);
    void CallIpamCb(IFMapNode *node);

    DomainConfigMap ipam_config_;
    DomainConfigMap vdns_config_;
    std::vector<Callback> ipam_callback_;
    std::vector<Callback> vdns_callback_;

    DISALLOW_COPY_AND_ASSIGN(DomainConfig);
};

#endif // vnsw_agent_vn_hpp
