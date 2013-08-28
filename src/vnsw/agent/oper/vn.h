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

using namespace boost::uuids;
using namespace std;

namespace autogen {
    class NetworkIpam;
    class VirtualDns;
    struct IpamType;
    struct VirtualDnsType;
}

class VmPortInterface;
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
        if (ipam_name != rhs.ipam_name)
            return ipam_name < rhs.ipam_name;

        if (ip_prefix != rhs.ip_prefix)
            return ip_prefix < rhs.ip_prefix;

        return (default_gw < rhs.default_gw);
    }
    Ip4Address GetBroadcastAddress() const {
        Ip4Address broadcast(ip_prefix.to_ulong() | 
                             ~(0xFFFFFFFF << (32 - plen)));
        return broadcast;
    }
    bool IsSubnetMember(Ip4Address &ip) const {
        return ((ip_prefix.to_ulong() | ~(0xFFFFFFFF << (32 - plen))) == 
                 (ip.to_ulong() | ~(0xFFFFFFFF << (32 - plen)))); 
    }
};

struct VnKey : public AgentKey {
    VnKey(uuid id) : AgentKey(), uuid_(id) {} ;
    virtual ~VnKey() { };

    uuid uuid_;
};

struct VnData : public AgentData {
    VnData(const string &name, const uuid &acl_id, const string &vrf_name,
           const uuid &mirror_acl_id, const uuid &mc_acl_id, 
           const std::vector<VnIpam> &ipam) :
                AgentData(), name_(name), vrf_name_(vrf_name), acl_id_(acl_id),
                mirror_acl_id_(mirror_acl_id), mirror_cfg_acl_id_(mc_acl_id),
                ipam_(ipam) {
    };
    virtual ~VnData() { };

    string name_;
    string vrf_name_;
    uuid acl_id_;
    uuid mirror_acl_id_;
    uuid mirror_cfg_acl_id_;
    std::vector<VnIpam> ipam_;
};

class VnEntry : AgentRefCount<VnEntry>, public AgentDBEntry {
public:
    VnEntry(uuid id) : uuid_(id) { };
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
    bool GetIpamData(const VmPortInterface *vmitf, 
                     autogen::IpamType &ipam_type) const;

    AgentDBTable *DBToTable() const;
    uint32_t GetRefCount() const {
        return AgentRefCount<VnEntry>::GetRefCount();
    }

    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;
    void SendObjectLog(AgentLogEvent::type event) const;
private:
    friend class VnTable;
    uuid uuid_;
    string name_;
    AclDBEntryRef acl_;
    AclDBEntryRef mirror_acl_;
    AclDBEntryRef mirror_cfg_acl_;
    VrfEntryRef vrf_;
    std::vector<VnIpam> ipam_;
    DISALLOW_COPY_AND_ASSIGN(VnEntry);
};

class VnTable : public AgentDBTable {
public:
    VnTable(DB *db, const std::string &name) : AgentDBTable(db, name) { }
    virtual ~VnTable() { }

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t  Hash(const DBRequestKey *key) const {return 0;};

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual void Delete(DBEntry *entry, const DBRequest *req);

    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req);

    static DBTableBase *CreateTable(DB *db, const std::string &name);

    void AddVn(const uuid &vn_uuid, const string &name, const uuid &acl_id,
               const string &vrf_name, const std::vector<VnIpam> &ipam);
    void DelVn(const uuid &vn_uuid);

    static void IpamVnSync(IFMapNode *node);

private:
    bool IpamChangeNotify(std::vector<VnIpam> &old_ipam, 
                          std::vector<VnIpam> &new_ipam, VnEntry *vn);
    void DeleteIpamHostRoutes(VnEntry *vn);
    void AddHostRouteForGw(VnEntry *vn, VnIpam &ipam);
    void DelHostRouteForGw(VnEntry *vn, VnIpam &ipam);
    bool ChangeHandler(DBEntry *entry, const DBRequest *req);
    IFMapNode *FindTarget(IFMapAgentTable *table, IFMapNode *node, 
                          std::string node_type);

    DISALLOW_COPY_AND_ASSIGN(VnTable);
};

class DomainConfig {
public:
    typedef std::map<std::string, IFMapNode  *> DomainConfigMap;
    typedef std::pair<std::string, IFMapNode *> DomainConfigPair;
    
    static void IpamSync(IFMapNode *node);
    static void VDnsSync(IFMapNode *node);

    static bool GetIpam(const std::string &name, autogen::IpamType &ipam);
    static bool GetVDns(const std::string &vdns, 
                        autogen::VirtualDnsType &vdns_type);

private:
    static DomainConfigMap ipam_config_;
    static DomainConfigMap vdns_config_;

    DISALLOW_COPY_AND_ASSIGN(DomainConfig);
};

#endif // vnsw_agent_vn_hpp
