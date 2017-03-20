/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vn_hpp
#define vnsw_agent_vn_hpp

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <oper/agent_types.h>
#include <oper/oper_db.h>
#include <oper/oper_dhcp_options.h>

using namespace boost::uuids;
using namespace std;

class AgentRouteResync;

namespace autogen {
    class NetworkIpam;
    class VirtualDns;
    struct IpamType;
    struct VirtualDnsType;
}

bool IsVRFServiceChainingInstance(const std::string &vn_name,
                                  const std::string &vrf_name);
class VmInterface;

struct VnIpam {
    IpAddress ip_prefix;
    uint32_t   plen;
    IpAddress default_gw;
    // In case of TSN, we could have different addresses for default_gw & dns
    IpAddress dns_server;
    bool       installed;    // is the route to send pkts to host installed
    bool       dhcp_enable;
    std::string ipam_name;
    OperDhcpOptions oper_dhcp_options;
    uint32_t alloc_unit;

    VnIpam(const std::string& ip, uint32_t len, const std::string& gw,
           const std::string& dns, bool dhcp, const std::string &name,
           const std::vector<autogen::DhcpOptionType> &dhcp_options,
           const std::vector<autogen::RouteType> &host_routes,
           uint32_t alloc);

    bool IsV4() const {
        return ip_prefix.is_v4();
    }
    bool IsV6() const {
        return ip_prefix.is_v6();
    }
    bool operator<(const VnIpam &rhs) const {
        if (ip_prefix != rhs.ip_prefix)
            return ip_prefix < rhs.ip_prefix;

        return (plen < rhs.plen);
    }
    Ip4Address GetBroadcastAddress() const;
    Ip4Address GetSubnetAddress() const;
    Ip6Address GetV6SubnetAddress() const;

    bool IsSubnetMember(const IpAddress &ip) const;
};

// Per IPAM data of the VN
struct VnIpamLinkData {
    OperDhcpOptions oper_dhcp_options_;

    bool operator==(const VnIpamLinkData &rhs) const {
        if (oper_dhcp_options_.host_routes() ==
            rhs.oper_dhcp_options_.host_routes())
            return true;
        return false;
    }
};

struct VnKey : public AgentOperDBKey {
    VnKey(const boost::uuids::uuid &id) : AgentOperDBKey(), uuid_(id) { }
    virtual ~VnKey() { }

    boost::uuids::uuid uuid_;
};

struct VnData : public AgentOperDBData {
    typedef std::map<std::string, VnIpamLinkData> VnIpamDataMap;
    typedef std::pair<std::string, VnIpamLinkData> VnIpamDataPair;

    VnData(const Agent *agent, IFMapNode *node, const string &name,
           const uuid &acl_id, const string &vrf_name,
           const uuid &mirror_acl_id, const uuid &mc_acl_id,
           const std::vector<VnIpam> &ipam, const VnIpamDataMap &vn_ipam_data,
           int vxlan_id, int vnid, bool bridging,
           bool layer3_forwarding, bool admin_state, bool enable_rpf,
           bool flood_unknown_unicast, Agent::ForwardingMode forwarding_mode,
           const boost::uuids::uuid &qos_config_uuid, bool mirror_destination,
           bool pbb_etree_enable, bool pbb_evpn_enable,
           bool layer2_control_word) :
        AgentOperDBData(agent, node), name_(name), vrf_name_(vrf_name),
        acl_id_(acl_id), mirror_acl_id_(mirror_acl_id),
        mirror_cfg_acl_id_(mc_acl_id), ipam_(ipam), vn_ipam_data_(vn_ipam_data),
        vxlan_id_(vxlan_id), vnid_(vnid), bridging_(bridging),
        layer3_forwarding_(layer3_forwarding), admin_state_(admin_state),
        enable_rpf_(enable_rpf),
        flood_unknown_unicast_(flood_unknown_unicast),
        forwarding_mode_(forwarding_mode), qos_config_uuid_(qos_config_uuid),
        mirror_destination_(mirror_destination),
        pbb_etree_enable_(pbb_etree_enable), pbb_evpn_enable_(pbb_evpn_enable),
        layer2_control_word_(layer2_control_word) {
    };
    virtual ~VnData() { }

    string name_;
    string vrf_name_;
    uuid acl_id_;
    uuid mirror_acl_id_;
    uuid mirror_cfg_acl_id_;
    std::vector<VnIpam> ipam_;
    VnIpamDataMap vn_ipam_data_;
    int vxlan_id_;
    int vnid_;
    bool bridging_;
    bool layer3_forwarding_;
    bool admin_state_;
    bool enable_rpf_;
    bool flood_unknown_unicast_;
    Agent::ForwardingMode forwarding_mode_;
    boost::uuids::uuid qos_config_uuid_;
    bool mirror_destination_;
    bool pbb_etree_enable_;
    bool pbb_evpn_enable_;
    bool layer2_control_word_;
};

class VnEntry : AgentRefCount<VnEntry>, public AgentOperDBEntry {
public:
    VnEntry(Agent *agent, uuid id);
    virtual ~VnEntry();

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
    const VnIpam *GetIpam(const IpAddress &ip) const;
    IpAddress GetGatewayFromIpam(const IpAddress &ip) const;
    IpAddress GetDnsFromIpam(const IpAddress &ip) const;
    uint32_t GetAllocUnitFromIpam(const IpAddress &ip) const;
    bool GetVnHostRoutes(const std::string &ipam,
                         std::vector<OperDhcpOptions::HostRoute> *routes) const;
    bool GetIpamName(const IpAddress &vm_addr, std::string *ipam_name) const;
    bool GetIpamData(const IpAddress &vm_addr, std::string *ipam_name,
                     autogen::IpamType *ipam_type) const;
    bool GetIpamVdnsData(const IpAddress &vm_addr,
                         autogen::IpamType *ipam_type,
                         autogen::VirtualDnsType *vdns_type) const;
    bool GetPrefix(const Ip6Address &ip, Ip6Address *prefix,
                   uint8_t *plen) const;
    std::string GetProject() const;
    int GetVxLanId() const;
    int ComputeEthernetTag() const;
    bool Resync(); 
    void UpdateMacVmBindingFloodFlag();

    const VxLanId *vxlan_id_ref() const {return vxlan_id_ref_.get();}
    const VxLanId *vxlan_id() const {return vxlan_id_ref_.get();}
    void set_bridging(bool bridging) {bridging_ = bridging;}
    bool bridging() const {return bridging_;};
    bool layer3_forwarding() const {return layer3_forwarding_;};
    void set_layer3_forwarding(bool layer3_forwarding)
    {layer3_forwarding_ = layer3_forwarding;}
    Agent::ForwardingMode forwarding_mode() const {return forwarding_mode_;}

    bool admin_state() const {return admin_state_;}
    bool enable_rpf() const {return enable_rpf_;}
    bool flood_unknown_unicast() const {return flood_unknown_unicast_;}

    AgentDBTable *DBToTable() const;
    uint32_t GetRefCount() const {
        return AgentRefCount<VnEntry>::GetRefCount();
    }

    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;
    void SendObjectLog(AgentLogEvent::type event) const;
    void ResyncRoutes();
    bool IdentifyBgpRoutersServiceIp(const IpAddress &ip_address,
                                     bool *is_dns, bool *is_gateway) const;
    const AgentQosConfig* qos_config() const {
        return qos_config_.get();
    }

    const bool mirror_destination() const {
        return mirror_destination_;
    }
    int vnid() const {return vnid_;}

    bool pbb_etree_enable() const {
        return pbb_etree_enable_;
    }

    bool pbb_evpn_enable() const {
        return pbb_evpn_enable_;
    }

    bool layer2_control_word() const {
        return layer2_control_word_;
    }

private:
    friend class VnTable;

    Agent *agent_;
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
    bool bridging_;
    bool layer3_forwarding_;
    bool admin_state_;
    VxLanIdRef vxlan_id_ref_;
    uint32_t table_label_;
    bool enable_rpf_;
    bool flood_unknown_unicast_;
    uint32_t old_vxlan_id_;
    Agent::ForwardingMode forwarding_mode_;
    boost::scoped_ptr<AgentRouteResync> route_resync_walker_;
    AgentQosConfigConstRef qos_config_;
    bool mirror_destination_;
    bool pbb_etree_enable_;
    bool pbb_evpn_enable_;
    bool layer2_control_word_;
    DISALLOW_COPY_AND_ASSIGN(VnEntry);
};

class VnTable : public AgentOperDBTable {
public:
    VnTable(DB *db, const std::string &name) : AgentOperDBTable(db, name),
        walkid_(DBTableWalker::kInvalidWalkerId) { }
    virtual ~VnTable() { }

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t  Hash(const DBRequestKey *key) const {return 0;};
    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context);

    virtual DBEntry *OperDBAdd(const DBRequest *req);
    virtual bool OperDBOnChange(DBEntry *entry, const DBRequest *req);
    virtual bool OperDBDelete(DBEntry *entry, const DBRequest *req);
    virtual bool OperDBResync(DBEntry *entry, const DBRequest *req); 

    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u);
    virtual bool IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u);
    virtual bool ProcessConfig(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u);

    int ComputeCfgVxlanId(IFMapNode *node);
    void CfgForwardingFlags(IFMapNode *node, bool *l2, bool *l3, bool *rpf,
                            bool *flood_unknown_unicast,
                            Agent::ForwardingMode *forwarding_mode,
                            bool *mirror_destination);


    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static VnTable *GetInstance() {return vn_table_;};

    void AddVn(const uuid &vn_uuid, const string &name, const uuid &acl_id,
               const string &vrf_name, const std::vector<VnIpam> &ipam,
               const VnData::VnIpamDataMap &vn_ipam_data, int vn_id,
               int vxlan_id, bool admin_state, bool enable_rpf,
               bool flood_unknown_unicast, bool pbb_etree_enable,
               bool pbb_evpn_enable, bool layer2_control_word);
    void DelVn(const uuid &vn_uuid);
    void ResyncVxlan(const boost::uuids::uuid &vn);
    VnEntry *Find(const uuid &vn_uuid);
    void GlobalVrouterConfigChanged();
    bool VnEntryWalk(DBTablePartBase *partition, DBEntryBase *entry);
    void VnEntryWalkDone(DBTableBase *partition);
    bool RebakeVxlan(VnEntry *vn, bool op_del);
    bool EvaluateForwardingMode(VnEntry *vn);
    bool GetLayer3ForwardingConfig(Agent::ForwardingMode forwarding_mode) const;
    bool GetBridgingConfig(Agent::ForwardingMode forwarding_mode) const;
    bool ForwardingModeChangeHandler(bool old_layer3_forwarding,
                                     bool old_bridging,
                                     bool *resync_routes,
                                     VnData *data,
                                     VnEntry *vn);
    int GetCfgVnId(autogen::VirtualNetwork *cfg_vn);

private:
    static VnTable *vn_table_;
    bool IpamChangeNotify(std::vector<VnIpam> &old_ipam, 
                          std::vector<VnIpam> &new_ipam, VnEntry *vn);
    void UpdateHostRoute(const IpAddress &old_address,
                         const IpAddress &new_address, VnEntry *vn,
                         bool relaxed_policy);
    void AddIPAMRoutes(VnEntry *vn, VnIpam &ipam);
    void DelIPAMRoutes(VnEntry *vn, VnIpam &ipam);
    void AddAllIpamRoutes(VnEntry *vn);
    void DeleteAllIpamRoutes(VnEntry *vn);
    void AddSubnetRoute(VnEntry *vn, VnIpam &ipam);
    void DelSubnetRoute(VnEntry *vn, VnIpam &ipam);
    bool IsGwHostRouteRequired();
    void AddHostRoute(VnEntry *vn, const IpAddress &address,
                      bool relaxed_policy);
    void DelHostRoute(VnEntry *vn, const IpAddress &address);
    bool ChangeHandler(DBEntry *entry, const DBRequest *req);
    bool IsGatewayL2(const string &gateway) const;
    void BuildVnIpamData(const std::vector<autogen::IpamSubnetType> &subnets,
                         const std::string &ipam_name,
                         std::vector<VnIpam> *vn_ipam);
    VnData *BuildData(IFMapNode *node);
    IFMapNode *FindTarget(IFMapAgentTable *table, IFMapNode *node, 
                          std::string node_type);
    DBTableWalker::WalkId walkid_;

    DISALLOW_COPY_AND_ASSIGN(VnTable);
};

class OperNetworkIpam : public OperIFMapTable {
public:

    OperNetworkIpam(Agent *agent, DomainConfig *domain_config);
    virtual ~OperNetworkIpam();

    void ConfigDelete(IFMapNode *node);
    void ConfigAddChange(IFMapNode *node);
    void ConfigManagerEnqueue(IFMapNode *node);
private:
    DomainConfig *domain_config_;
    DISALLOW_COPY_AND_ASSIGN(OperNetworkIpam);
};

class OperVirtualDns : public OperIFMapTable {
public:

    OperVirtualDns(Agent *agent, DomainConfig *domain_config);
    virtual ~OperVirtualDns();

    void ConfigDelete(IFMapNode *node);
    void ConfigAddChange(IFMapNode *node);
    void ConfigManagerEnqueue(IFMapNode *node);
private:
    DomainConfig *domain_config_;
    DISALLOW_COPY_AND_ASSIGN(OperVirtualDns);
};

class DomainConfig {
public:
    typedef std::map<std::string, autogen::IpamType> IpamDomainConfigMap;
    typedef std::pair<std::string, autogen::IpamType> IpamDomainConfigPair;
    typedef std::map<std::string, autogen::VirtualDnsType> VdnsDomainConfigMap;
    typedef std::pair<std::string, autogen::VirtualDnsType> VdnsDomainConfigPair;
    typedef boost::function<void(IFMapNode *)> Callback;
    
    DomainConfig(Agent *);
    virtual ~DomainConfig();
    void Init();
    void Terminate();
    void RegisterIpamCb(Callback cb);
    void RegisterVdnsCb(Callback cb);
    void IpamDelete(IFMapNode *node);
    void IpamAddChange(IFMapNode *node);
    void VDnsDelete(IFMapNode *node);
    void VDnsAddChange(IFMapNode *node);
    bool GetIpam(const std::string &name, autogen::IpamType *ipam);
    bool GetVDns(const std::string &vdns, autogen::VirtualDnsType *vdns_type);

private:
    void CallVdnsCb(IFMapNode *node);
    void CallIpamCb(IFMapNode *node);
    bool IpamChanged(const autogen::IpamType &old,
                     const autogen::IpamType &cur) const;

    IpamDomainConfigMap ipam_config_;
    VdnsDomainConfigMap vdns_config_;
    std::vector<Callback> ipam_callback_;
    std::vector<Callback> vdns_callback_;

    DISALLOW_COPY_AND_ASSIGN(DomainConfig);
};

#endif // vnsw_agent_vn_hpp
