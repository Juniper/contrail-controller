/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_sandesh_h_
#define vnsw_agent_sandesh_h_

#include <cmn/agent_cmn.h>

/////////////////////////////////////////////////////////////////////////////
// Header file defining infra to implement introspect for DB-Tables
/////////////////////////////////////////////////////////////////////////////

class VrfEntry;
class DBEntryBase;
class PageReqData;

/////////////////////////////////////////////////////////////////////////////
// Class to encode/decode the arguments used in pagination links
// Stores arguments in form of <key, value> pair in a map
/////////////////////////////////////////////////////////////////////////////
class AgentSandeshArguments {
public:
    typedef std::map<std::string, std::string> ArgumentMap;

    AgentSandeshArguments() { }
    ~AgentSandeshArguments() { }

    bool Add(const std::string &key, const std::string &val);
    bool Add(const std::string &key, int val);
    bool Del(const std::string &key);
    bool Get(const std::string &key, std::string *val) const;
    std::string GetString(const std::string &key) const;
    bool Get(const std::string &key, int *val) const;
    int GetInt(const std::string &key) const;

    int Encode(std::string *str);
    int Decode(const std::string &str);
private:
    ArgumentMap arguments_;
    DISALLOW_COPY_AND_ASSIGN(AgentSandeshArguments);
};

/////////////////////////////////////////////////////////////////////////////
// Manager class for AgentSandesh.
// Agent gets Paging requests in Sandesh task context. Agent needs to iterate
// thru the DBTable. So, we need to move the request to a task in DBTable
// context. A work-queue is defined for this reason
/////////////////////////////////////////////////////////////////////////////
class AgentSandeshManager {
public:
    struct PageRequest {
        PageRequest() : key_(""), context_("") { }
        PageRequest(const std::string &key, const std::string &context) :
            key_(key), context_(context) {
        }
        PageRequest(const PageRequest &req) :
            key_(req.key_), context_(req.context_) { 
        }
        ~PageRequest() { }

        std::string key_;
        std::string context_;
    };

    AgentSandeshManager(Agent *agent);
    ~AgentSandeshManager();

    void Init();
    void Shutdown();
    void AddPageRequest(const std::string &key, const std::string &context);
    bool Run(PageRequest req);
private:
    Agent *agent_;
    WorkQueue<PageRequest> page_request_queue_;
    DISALLOW_COPY_AND_ASSIGN(AgentSandeshManager);
};

/////////////////////////////////////////////////////////////////////////////
// A common base class to handle introspect requests for DBTables. Provides
// support for common features such as,
//
// - Support for pagination. Including links for prev, next and first pages
// - Supports query of complete table
/////////////////////////////////////////////////////////////////////////////
class AgentSandesh;
typedef class boost::shared_ptr<AgentSandesh> AgentSandeshPtr;
class AgentSandesh {
public:
    static const uint8_t entries_per_sandesh = 100;
    static const uint16_t kEntriesPerPage = 100;
    AgentSandesh(const std::string &context, const std::string &name) :
        name_(name), resp_(NULL), count_(0), total_entries_(0),
        context_(context), walkid_(DBTableWalker::kInvalidWalkerId) {
    }
    AgentSandesh(const std::string &context) :
        name_(""), resp_(NULL), count_(0), context_(context),
        walkid_(DBTableWalker::kInvalidWalkerId) {
    }

    virtual ~AgentSandesh() {}

    // Check if a DBEntry passes filter
    virtual bool Filter(const DBEntryBase *entry) { return true; }
    // Convert from filter to arguments. Arguments will be converted to string
    // and passed in next/prev pagination links
    virtual bool FilterToArgs(AgentSandeshArguments *args) {
        args->Add("name", name_);
        return true;
    }
    std::string context() const { return context_; }

    void DoSandeshInternal(AgentSandeshPtr sandesh, int start, int count);
    static void DoSandesh(AgentSandeshPtr sandesh, int start, int count);
    static void DoSandesh(AgentSandeshPtr sandesh);
    void MakeSandeshPageReq(PageReqData *req, DBTable *table, int first,
                            int last, int end, int count, int page_size);

protected:
    std::string name_; // name coming in the sandesh request
    SandeshResponse *resp_;
private:
    bool EntrySandesh(DBEntryBase *entry, int first, int last);
    void SandeshDone(AgentSandeshPtr ptr, int first, int page_size);
    void SetResp();
    virtual DBTable *AgentGetTable() = 0;
    virtual void Alloc() = 0;
    virtual bool UpdateResp(DBEntryBase *entry);

    int count_;
    int total_entries_;
    std::string context_;
    DBTableWalker::WalkId walkid_;
    DISALLOW_COPY_AND_ASSIGN(AgentSandesh);
};

/////////////////////////////////////////////////////////////////////////////
//  AgentSandesh class implementation for different OperDB Tables
/////////////////////////////////////////////////////////////////////////////
class AgentVnSandesh : public AgentSandesh {
public:
    AgentVnSandesh(const std::string &context, const std::string &name,
                   const std::string &u, const std::string &vxlan_id,
                   const std::string &ipam_name);
    ~AgentVnSandesh() {}
    virtual bool Filter(const DBEntryBase *entry);
    virtual bool FilterToArgs(AgentSandeshArguments *args);

private:
    friend class VnListReq;
    DBTable *AgentGetTable();
    void Alloc();
    std::string name_;
    std::string uuid_str_;
    std::string vxlan_id_;
    std::string ipam_name_;

    boost::uuids::uuid uuid_;

};

class AgentSgSandesh : public AgentSandesh {
public:
    AgentSgSandesh(std::string context, std::string name)
        : AgentSandesh(context, name) {}

private:
    DBTable *AgentGetTable();
    void Alloc();
};

class AgentVmSandesh : public AgentSandesh {
public:
    AgentVmSandesh(std::string context, std::string uuid)
        : AgentSandesh(context, uuid) {}

private:
    DBTable *AgentGetTable();
    void Alloc();
};

class AgentIntfSandesh : public AgentSandesh {
public:
    AgentIntfSandesh(const std::string &context, const std::string &type,
                     const std::string &name, const std::string &u,
                     const std::string &vn, const std::string &mac,
                     const std::string &v4, const std::string &v6,
                     const std::string &parent, const std::string &ip_active,
                     const std::string &ip6_active,
                     const std::string &l2_active);
    ~AgentIntfSandesh() { }
    virtual bool Filter(const DBEntryBase *entry);
    virtual bool FilterToArgs(AgentSandeshArguments *args);

private:
    friend class ItfReq;
    DBTable *AgentGetTable();
    void Alloc();

    // Filters
    std::string type_;
    std::string name_;
    std::string uuid_str_;
    std::string vn_;
    std::string mac_str_;
    std::string v4_str_;
    std::string v6_str_;
    std::string parent_uuid_str_;
    std::string ip_active_str_;
    std::string ip6_active_str_;
    std::string l2_active_str_;

    boost::uuids::uuid uuid_;
    MacAddress mac_;
    Ip4Address v4_;
    Ip6Address v6_;
    boost::uuids::uuid parent_uuid_;
};

class AgentNhSandesh : public AgentSandesh {
public:
    AgentNhSandesh(const std::string &context, const std::string &type,
                   const std::string &nh_index, const std::string &policy_enabled);
    ~AgentNhSandesh() { }
    virtual bool Filter(const DBEntryBase *entry);
    virtual bool FilterToArgs(AgentSandeshArguments *args);

private:
    friend class NhListReq;
    DBTable *AgentGetTable();
    void Alloc();

    // Filters
    std::string type_;
    std::string nh_index_;
    std::string policy_enabled_;
};

class AgentMplsSandesh : public AgentSandesh {
public:
    AgentMplsSandesh(const std::string &context, const std::string &type,
                     const std::string &label);
    ~AgentMplsSandesh() { }
    virtual bool Filter(const DBEntryBase *entry);
    virtual bool FilterToArgs(AgentSandeshArguments *args);

private:
    friend class MplsReq;
    DBTable *AgentGetTable();
    void Alloc();

    // Filters
    std::string type_;
    std::string label_;

};

class AgentVrfSandesh : public AgentSandesh {
public:
    AgentVrfSandesh(std::string context, std::string name)
        : AgentSandesh(context, name) {}

private:
    DBTable *AgentGetTable();
    void Alloc();
};

class AgentInet4UcRtSandesh : public AgentSandesh {
public:
    AgentInet4UcRtSandesh(VrfEntry *vrf, std::string context, bool stale)
        : AgentSandesh(context, ""), vrf_(vrf), stale_(stale) {
        dump_table_ = true;
    }
    AgentInet4UcRtSandesh(VrfEntry *vrf, std::string context,
                          Ip4Address addr, uint8_t plen, bool stale) 
        : AgentSandesh(context, ""), vrf_(vrf), addr_(addr), plen_(plen), 
        stale_(stale) {
        dump_table_ = false;
    }

private:
    DBTable *AgentGetTable();
    void Alloc();
    bool UpdateResp(DBEntryBase *entry);

    VrfEntry *vrf_;
    Ip4Address addr_;
    uint8_t plen_;
    bool stale_;
    bool dump_table_;
};

class AgentInet4McRtSandesh : public AgentSandesh {
public:
    AgentInet4McRtSandesh(VrfEntry *vrf, std::string context, std::string name, 
                          bool stale) 
        : AgentSandesh(context, name), vrf_(vrf), stale_(stale) {}

private:
    DBTable *AgentGetTable();
    void Alloc();
    bool UpdateResp(DBEntryBase *entry);

    VrfEntry *vrf_;
    bool stale_;
};

class AgentLayer2RtSandesh : public AgentSandesh {
public:
    AgentLayer2RtSandesh(VrfEntry *vrf, std::string context, std::string name,
                         bool stale)
        : AgentSandesh(context, name), vrf_(vrf), stale_(stale) {}

private:
    DBTable *AgentGetTable();
    void Alloc();
    bool UpdateResp(DBEntryBase *entry);

    VrfEntry *vrf_;
    bool stale_;
};

class AgentBridgeRtSandesh : public AgentSandesh {
public:
    AgentBridgeRtSandesh(VrfEntry *vrf, std::string context, std::string name, 
                         bool stale) 
        : AgentSandesh(context, name), vrf_(vrf), stale_(stale) {}

private:
    DBTable *AgentGetTable();
    void Alloc();
    bool UpdateResp(DBEntryBase *entry);

    VrfEntry *vrf_;
    bool stale_;
};

class AgentEvpnRtSandesh : public AgentSandesh {
public:
    AgentEvpnRtSandesh(VrfEntry *vrf, std::string context, std::string name,
                       bool stale)
        : AgentSandesh(context, name), vrf_(vrf), stale_(stale) {}

private:
    DBTable *AgentGetTable();
    void Alloc();
    bool UpdateResp(DBEntryBase *entry);

    VrfEntry *vrf_;
    bool stale_;
};

class AgentInet6UcRtSandesh : public AgentSandesh {
public:
    AgentInet6UcRtSandesh(VrfEntry *vrf, std::string context, bool stale) : 
        AgentSandesh(context, ""), vrf_(vrf), stale_(stale) {
        dump_table_ = true;
    }
    AgentInet6UcRtSandesh(VrfEntry *vrf, std::string context,
                          Ip6Address addr, uint8_t plen, bool stale) : 
        AgentSandesh(context, ""), vrf_(vrf), addr_(addr), plen_(plen), 
            stale_(stale) {
        dump_table_ = false;
    }

private:
    DBTable *AgentGetTable();
    void Alloc();
    bool UpdateResp(DBEntryBase *entry);

    VrfEntry *vrf_;
    Ip6Address addr_;
    uint8_t plen_;
    bool stale_;
    bool dump_table_;
};

class AgentAclSandesh : public AgentSandesh {
public:
    AgentAclSandesh(std::string context, std::string name)
        : AgentSandesh(context, name) {}

private:
    DBTable *AgentGetTable();
    void Alloc();
    bool UpdateResp(DBEntryBase *entry);
};

class AgentMirrorSandesh : public AgentSandesh {
public:
    AgentMirrorSandesh(const std::string &context, const std::string &analyzer_name);
    ~AgentMirrorSandesh() { }
    virtual bool Filter(const DBEntryBase *entry);
    virtual bool FilterToArgs(AgentSandeshArguments *args);

private:
    friend class MirrorEntryReq;
    DBTable *AgentGetTable();
    void Alloc();

    //Filters
    std::string analyzer_name_;
};

class AgentVrfAssignSandesh : public AgentSandesh {
public:
    AgentVrfAssignSandesh(std::string context, std::string uuid)
        : AgentSandesh(context, uuid) {}

private:
    DBTable *AgentGetTable();
    void Alloc();
};

class AgentVxLanSandesh : public AgentSandesh {
public:
    AgentVxLanSandesh(const std::string &context, const std::string &vxlan_id);
    ~AgentVxLanSandesh() { }
    virtual bool Filter(const DBEntryBase *entry);
    virtual bool FilterToArgs(AgentSandeshArguments *args);

private:
    friend class VxLanReq;
    DBTable *AgentGetTable();
    void Alloc();

    //Filters
    std::string vxlan_id_;
};

class AgentServiceInstanceSandesh : public AgentSandesh {
public:
    AgentServiceInstanceSandesh(std::string context, std::string uuid)
        : AgentSandesh(context, uuid) {}

private:
    DBTable *AgentGetTable();
    void Alloc();
};

class AgentLoadBalancerSandesh : public AgentSandesh {
public:
    AgentLoadBalancerSandesh(std::string context, std::string uuid)
        : AgentSandesh(context, uuid) {}

private:
    DBTable *AgentGetTable();
    void Alloc();
};

class AgentLoadBalancerV2Sandesh : public AgentSandesh {
public:
    AgentLoadBalancerV2Sandesh(std::string context, std::string uuid)
        : AgentSandesh(context, uuid) {}

private:
    DBTable *AgentGetTable();
    void Alloc();
};

class AgentHealthCheckSandesh : public AgentSandesh {
public:
    AgentHealthCheckSandesh(const std::string &context, const std::string &u);
    ~AgentHealthCheckSandesh() {}
    virtual bool Filter(const DBEntryBase *entry);
    virtual bool FilterToArgs(AgentSandeshArguments *args);

private:
    friend class VnListReq;
    DBTable *AgentGetTable();
    void Alloc();
    std::string uuid_str_;

    boost::uuids::uuid uuid_;

};

#endif // vnsw_agent_sandesh_h_
