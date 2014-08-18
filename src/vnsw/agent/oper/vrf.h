/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vrf_hpp
#define vnsw_agent_vrf_hpp

#include <cmn/agent_cmn.h>
#include <cmn/index_vector.h>
#include <cmn/agent.h>
#include <oper/agent_types.h>

using namespace std;
class LifetimeActor;
class LifetimeManager;
class ComponentNHData;
class AgentRouteWalker;

struct VrfKey : public AgentKey {
    VrfKey(const string &name) : AgentKey(), name_(name) { };
    virtual ~VrfKey() { };

    void Init(const string &vrf_name) {name_ = vrf_name;};
    bool IsLess(const VrfKey &rhs) const {
        return name_ < rhs.name_;
    }

    bool IsEqual(const VrfKey &rhs) const {
        if ((IsLess(rhs) == false) && (rhs.IsLess(*this) == false)) {
            return true;
        }
        return false;
    }

    string name_;
};

struct VrfData : public AgentData {
    enum VrfEntryFlags {
        ConfigVrf = 1 << 0,     // vrf is received from config
        GwVrf     = 1 << 1,     // GW configured for this VRF
    };

    VrfData(uint32_t flags) : AgentData(), flags_(flags) {}
    virtual ~VrfData() {}

    uint32_t flags_;
};

class VrfEntry : AgentRefCount<VrfEntry>, public AgentDBEntry {
public:
    static const uint32_t kInvalidIndex = 0xFFFFFFFF;
    static const uint32_t kDeleteTimeout = 900 * 1000;

    VrfEntry(const string &name, uint32_t flags);
    virtual ~VrfEntry();

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual string ToString() const;

    const uint32_t vrf_id() const {return id_;};
    const string &GetName() const {return name_;};

    uint32_t GetRefCount() const {
        return AgentRefCount<VrfEntry>::GetRefCount();
    }

    uint32_t flags() const { return flags_; }
    void set_flags(uint32_t flags) { flags_ |= flags; }
    bool are_flags_set(const VrfData::VrfEntryFlags &flags) const {
        return (flags_ & flags);
    }

    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;
    Inet4UnicastRouteEntry *GetUcRoute(const Ip4Address &addr) const;
    Inet4UnicastRouteEntry *GetUcRoute(const Inet4UnicastRouteEntry &rt_key) const;

    LifetimeActor *deleter();
    void SendObjectLog(AgentLogEvent::type event) const;
    void StartDeleteTimer();
    bool DeleteTimeout();
    void CancelDeleteTimer();
    void PostAdd();
    bool CanDelete(DBRequest *req);
    void AddNH(Ip4Address ip, uint8_t plen, ComponentNHData *nh_data) ;
    void DeleteNH(Ip4Address ip, uint8_t plen, ComponentNHData *nh_data) ;
    uint32_t GetNHCount(Ip4Address ip, uint8_t plen) ;
    void UpdateLabel(Ip4Address ip, uint8_t plen, uint32_t label);
    uint32_t GetLabel(Ip4Address ip, uint8_t plen);
    std::vector<ComponentNHData>* GetNHList(Ip4Address ip, uint8_t plen);
    bool FindNH(const Ip4Address &ip, uint8_t plen,
                const ComponentNHData &nh_data);

    AgentRouteTable *GetInet4UnicastRouteTable() const;
    AgentRouteTable *GetInet4MulticastRouteTable() const;
    AgentRouteTable *GetLayer2RouteTable() const;
    AgentRouteTable *GetRouteTable(uint8_t table_type) const;
private:
    friend class VrfTable;
    class DeleteActor;
    string name_;
    uint32_t id_;
    uint32_t flags_;
    DBTableWalker::WalkId walkid_;
    boost::scoped_ptr<DeleteActor> deleter_;
    AgentRouteTable *rt_table_db_[Agent::ROUTE_TABLE_MAX];
    Timer *delete_timeout_timer_;
    DISALLOW_COPY_AND_ASSIGN(VrfEntry);
};

class VrfTable : public AgentDBTable {
public:
    // Map from VRF Name to VRF Entry
    typedef map<string, VrfEntry *> VrfNameTree;
    typedef pair<string, VrfEntry *> VrfNamePair;

    // Map from VRF Name to Route Table
    typedef map<string, RouteTable *> VrfDbTree;
    typedef pair<string, RouteTable *> VrfDbPair;

    VrfTable(DB *db, const std::string &name) :
        AgentDBTable(db, name), db_(db),
        walkid_(DBTableWalker::kInvalidWalkerId), shutdown_walk_(NULL) { };
    virtual ~VrfTable() { };

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t Hash(const DBRequestKey *key) const {return 0;};
    virtual void Input(DBTablePartition *root, DBClient *client,
                       DBRequest *req);
    void VrfReuse(std::string name);
    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual void Delete(DBEntry *entry, const DBRequest *req);
    virtual void OnZeroRefcount(AgentDBEntry *e);

    // Create a VRF entry with given name
    void CreateVrf(const string &name, uint32_t flags = VrfData::ConfigVrf);
    void DeleteVrf(const string &name, uint32_t flags = VrfData::ConfigVrf);
    void CreateVrfReq(const string &name, uint32_t flags = VrfData::ConfigVrf);
    void DeleteVrfReq(const string &name, uint32_t flags = VrfData::ConfigVrf);
    //Add and delete routine for VRF not deleted on VRF config delete
    void CreateStaticVrf(const string &name);
    void DeleteStaticVrf(const string &name);
 
    // Create VRF Table with given name
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static VrfTable *GetInstance() {return vrf_table_;};

    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req);

    VrfEntry *FindVrfFromName(const string &name);
    VrfEntry *FindVrfFromId(size_t index);
    void FreeVrfId(size_t index) {index_table_.Remove(index);};

    virtual bool CanNotify(IFMapNode *dbe);
    
    AgentRouteTable *GetInet4UnicastRouteTable(const std::string &vrf_name);
    AgentRouteTable *GetInet4MulticastRouteTable(const std::string &vrf_name);
    AgentRouteTable *GetLayer2RouteTable(const std::string &vrf_name);
    AgentRouteTable *GetRouteTable(const string &vrf_name, uint8_t table_type);
    bool IsStaticVrf(const std::string &vrf_name) const {
        if (static_vrf_set_.find(vrf_name) != static_vrf_set_.end()) {
            return true;
        }
        return false;
    }

    void DeleteRoutes();
    void Shutdown();
    void reset_shutdown_walk() { shutdown_walk_ = NULL; }
    AgentRouteWalker *shutdown_walk() const { return shutdown_walk_; }
private:
    friend class VrfEntry;

    DB *db_;
    static VrfTable *vrf_table_;
    IndexVector<VrfEntry> index_table_;
    VrfNameTree name_tree_;
    VrfDbTree dbtree_[Agent::ROUTE_TABLE_MAX];
    DBTableWalker::WalkId walkid_;
    std::set<std::string> static_vrf_set_;
    AgentRouteWalker *shutdown_walk_;
    DISALLOW_COPY_AND_ASSIGN(VrfTable);
};

#endif // vnsw_agent_vrf_hpp
