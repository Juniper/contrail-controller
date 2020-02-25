/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vrf_hpp
#define vnsw_agent_vrf_hpp

#include <cmn/agent_cmn.h>
#include <cmn/index_vector.h>
#include <cmn/agent.h>
#include <oper/agent_types.h>
#include <oper/oper_db.h>
#include <oper/agent_route_walker.h>
#include <oper/interface.h>

class LifetimeActor;
class LifetimeManager;
class ComponentNHData;
class AgentRouteWalker;
class AgentRouteResync;

struct VrfKey : public AgentOperDBKey {
    VrfKey(const string &name) : AgentOperDBKey(), name_(name) { }
    virtual ~VrfKey() { }

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

struct VrfData : public AgentOperDBData {
    enum VrfEntryFlags {
        ConfigVrf = 1 << 0,     // vrf is received from config
        GwVrf     = 1 << 1,     // GW configured for this VRF
        MirrorVrf = 1 << 2,     // internally Created VRF
        PbbVrf    = 1 << 3,     // Per ISID VRF
        //Note addition of new flag may need update in
        //ConfigFlags() API, if flag being added is a property
        //flag(Ex PbbVrf) and not flag indicating Config origination(ex: GwVrf)
    };

    enum VrfId {
        FABRIC_VRF_ID,
        LINKLOCAL_VRF_ID
    };

    VrfData(Agent *agent, IFMapNode *node, uint32_t flags,
            const boost::uuids::uuid &vn_uuid, uint32_t isid,
            const std::string bmac_vrf_name,
            uint32_t mac_aging_time, bool learning_enabled,
            uint32_t hbf_rintf = Interface::kInvalidIndex,
            uint32_t hbf_lintf = Interface::kInvalidIndex) :
        AgentOperDBData(agent, node), flags_(flags), vn_uuid_(vn_uuid),
        isid_(isid), bmac_vrf_name_(bmac_vrf_name),
        mac_aging_time_(mac_aging_time), learning_enabled_(learning_enabled),
        forwarding_vrf_name_(""), hbf_rintf_(hbf_rintf),
        hbf_lintf_(hbf_lintf) {}
    virtual ~VrfData() {}

    uint32_t ConfigFlags() {
        return ~(PbbVrf);
    }

    uint32_t flags_;
    boost::uuids::uuid vn_uuid_;
    boost::uuids::uuid si_vn_ref_uuid_;
    uint32_t isid_;
    std::string bmac_vrf_name_;
    uint32_t mac_aging_time_;
    bool learning_enabled_;
    std::string forwarding_vrf_name_;
    uint32_t hbf_rintf_;
    uint32_t hbf_lintf_;
};

class VrfEntry : AgentRefCount<VrfEntry>, public AgentOperDBEntry {
public:
    static const uint32_t kInvalidIndex = 0xFFFFFFFF;
    static const uint32_t kDeleteTimeout = 900 * 1000;

    VrfEntry(const string &name, uint32_t flags, Agent *agent);
    virtual ~VrfEntry();

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual string ToString() const;

    const uint32_t vrf_id() const {return id_;};
    const string &GetName() const {return name_;};
    VnEntry *vn() const { return vn_.get(); }
    VnEntry *si_vn_ref() const { return si_vn_ref_.get(); }

    uint32_t GetRefCount() const {
        return AgentRefCount<VrfEntry>::GetRefCount();
    }

    uint32_t flags() const { return flags_; }
    void set_flags(uint32_t flags) { flags_ |= flags; }
    bool are_flags_set(const VrfData::VrfEntryFlags &flags) const {
        return (flags_ & flags);
    }

    void set_table_label(uint32_t label) {
        table_label_ = label;
    }
    uint32_t table_label() const {
        return table_label_;
    }

    const std::string& GetExportName() {
        if (are_flags_set(VrfData::PbbVrf)) {
            return bmac_vrf_name_;
        }
        return name_;
    }

    bool learning_enabled() const {
        return learning_enabled_;
    }

    bool ShouldExportRoute() const {
        if (are_flags_set(VrfData::PbbVrf)) {
            return false;
        }
        return true;
    }

    bool layer2_control_word() const {
        return layer2_control_word_;
    }

    const uint32_t hbf_rintf() const { return hbf_rintf_; }
    const uint32_t hbf_lintf() const { return hbf_lintf_; }

    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;
    InetUnicastRouteEntry *GetUcRoute(const IpAddress &addr) const;
    InetUnicastRouteEntry *GetUcRoute(const InetUnicastRouteEntry &rt_key)const;
    bool UpdateVxlanId(Agent *agent, uint32_t new_vxlan_id);

    LifetimeActor *deleter();
    virtual void RetryDelete();
    bool AllRouteTablesEmpty() const;
    void SendObjectLog(AgentLogEvent::type event) const;
    void StartDeleteTimer();
    bool DeleteTimeout();
    void CancelDeleteTimer();
    void PostAdd();
    void AddNH(Ip4Address ip, uint8_t plen, ComponentNHData *nh_data) ;
    void DeleteNH(Ip4Address ip, uint8_t plen, ComponentNHData *nh_data) ;
    uint32_t GetNHCount(Ip4Address ip, uint8_t plen) ;
    void UpdateLabel(Ip4Address ip, uint8_t plen, uint32_t label);
    uint32_t GetLabel(Ip4Address ip, uint8_t plen);
    uint32_t vxlan_id() const {return vxlan_id_;}
    std::vector<ComponentNHData>* GetNHList(Ip4Address ip, uint8_t plen);
    bool FindNH(const Ip4Address &ip, uint8_t plen,
                const ComponentNHData &nh_data);

    InetUnicastAgentRouteTable *GetInet4UnicastRouteTable() const;
    InetUnicastAgentRouteTable *GetInet4MplsUnicastRouteTable() const;
    AgentRouteTable *GetInet4MulticastRouteTable() const;
    AgentRouteTable *GetEvpnRouteTable() const;
    AgentRouteTable *GetBridgeRouteTable() const;
    InetUnicastAgentRouteTable *GetInet6UnicastRouteTable() const;
    AgentRouteTable *GetRouteTable(uint8_t table_type) const;
    void CreateTableLabel(bool learning_enabled, bool l2,
                          bool flod_unknown_unicast,
                          bool layer2_control_word);
    const std::string GetTableTypeString(uint8_t table_type) const;
    bool AllRouteTableDeleted() const;
    bool RouteTableDeleted(uint8_t table_type) const;
    void SetRouteTableDeleted(uint8_t table_type);
    void DeleteRouteTables();
    void ResyncRoutes();
    bool allow_route_add_on_deleted_vrf() const {
        return allow_route_add_on_deleted_vrf_;
    }

    //To be set in test cases only
    void set_allow_route_add_on_deleted_vrf(bool val) {
        allow_route_add_on_deleted_vrf_ = val;
    }
    InetUnicastAgentRouteTable *GetInetUnicastRouteTable(const IpAddress &addr) const;
    void ReleaseWalker();

    uint32_t isid() const {
        return isid_;
    }

    const std::string bmac_vrf_name() const {
        return bmac_vrf_name_;
    }

    bool IsPbbVrf() const {
        return are_flags_set(VrfData::PbbVrf);
    }

    uint32_t mac_aging_time() const {
        return mac_aging_time_;
    }

    void set_mac_aging_time(uint32_t aging_time) {
        mac_aging_time_ = aging_time;
    }

    VrfEntry* forwarding_vrf() const {
        return forwarding_vrf_.get();
    }
    int rd() const {return rd_;}
    void set_rd(int rd) {rd_ = rd;}
    void set_routing_vrf(bool val) {routing_vrf_ = val;}
    bool routing_vrf() {return routing_vrf_;}
    void set_hbf_rintf(uint32_t idx) {hbf_rintf_ = idx;}
    void set_hbf_lintf(uint32_t idx) {hbf_lintf_ = idx;}

private:
    friend class VrfTable;
    void CreateRouteTables();
    void SetNotify();
    int RDInstanceId(bool tor_agent_enabled) const;

    class DeleteActor;
    string name_;
    uint32_t id_;
    uint32_t flags_;
    VnEntryRef vn_;
    // service-instance primary VN reference
    VnEntryRef si_vn_ref_;
    NextHopRef nh_;
    DBTableWalker::WalkId walkid_;
    boost::scoped_ptr<DeleteActor> deleter_;
    AgentRouteTable *rt_table_db_[Agent::ROUTE_TABLE_MAX];
    Timer *delete_timeout_timer_;
    uint32_t table_label_;
    uint32_t vxlan_id_;
    uint32_t rt_table_delete_bmap_;
    IFMapDependencyManager::IFMapNodePtr vrf_node_ptr_;
    AgentRouteWalkerPtr route_resync_walker_;
    bool allow_route_add_on_deleted_vrf_;
    string bmac_vrf_name_;
    uint32_t isid_;
    tbb::atomic<uint32_t> mac_aging_time_;
    bool learning_enabled_;
    bool layer2_control_word_;
    bool l2_;
    VrfEntryRef forwarding_vrf_;
    int rd_;
    bool routing_vrf_;
    uint32_t retries_;
    uint32_t hbf_rintf_;
    uint32_t hbf_lintf_;
    DISALLOW_COPY_AND_ASSIGN(VrfEntry);
};

class VrfTable : public AgentOperDBTable {
public:
    const static uint32_t kDefaultMacAgingTime = 300;
    // Map from VRF Name to VRF Entry
    typedef map<string, VrfEntry *> VrfNameTree;
    typedef pair<string, VrfEntry *> VrfNamePair;

    // Config tree of VRF to VMI entries. Tree used to track the VMIs that are
    // already processed after VRF has been added
    typedef std::set<std::string, boost::uuids::uuid> CfgVmiTree;

    // Map from VRF Name to Route Table
    typedef map<string, RouteTable *> VrfDbTree;
    typedef pair<string, RouteTable *> VrfDbPair;

    VrfTable(DB *db, const std::string &name) :
        AgentOperDBTable(db, name), db_(db),
        walkid_(DBTableWalker::kInvalidWalkerId),
        route_delete_walker_(NULL),
        vrf_delete_walker_(NULL) { };
    virtual ~VrfTable();

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t Hash(const DBRequestKey *key) const {return 0;};
    virtual void Input(DBTablePartition *root, DBClient *client,
                       DBRequest *req);
    void VrfReuse(std::string name);
    virtual DBEntry *OperDBAdd(const DBRequest *req);
    virtual bool OperDBOnChange(DBEntry *entry, const DBRequest *req);
    virtual bool OperDBDelete(DBEntry *entry, const DBRequest *req);
    virtual void OnZeroRefcount(AgentDBEntry *e);
    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context);
    virtual void Clear();

    // Create a VRF entry with given name
    void CreateVrf(const string &name,
                   const boost::uuids::uuid &vn_uuid, uint32_t flags);
    void CreateVrf(const string &name,
                   const boost::uuids::uuid &vn_uuid, uint32_t flags,
                   uint32_t isid, const std::string& bmac_vrf_name,
                   uint32_t mac_aging_time, bool learning_enabled);
    void DeleteVrf(const string &name, uint32_t flags = VrfData::ConfigVrf);
    void CreateVrfReq(const string &name, uint32_t flags = VrfData::ConfigVrf);
    void CreateVrfReq(const string &name, const boost::uuids::uuid &vn_uuid,
                      uint32_t flags = VrfData::ConfigVrf);
    void DeleteVrfReq(const string &name, uint32_t flags = VrfData::ConfigVrf);
    //Add and delete routine for VRF not deleted on VRF config delete
    void CreateStaticVrf(const string &name);
    void DeleteStaticVrf(const string &name);
    void CreateFabricPolicyVrf(const string &name);

    // Create VRF Table with given name
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static VrfTable *GetInstance() {return vrf_table_;};

    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u);
    bool ProcessConfig(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u);

    VrfEntry *FindVrfFromName(const string &name);
    VrfEntry *FindVrfFromId(size_t index);
    VrfEntry *FindVrfFromIdIncludingDeletedVrf(size_t index);
    void FreeVrfId(size_t index);

    InetUnicastAgentRouteTable *GetInet4UnicastRouteTable
        (const std::string &vrf_name);
    InetUnicastAgentRouteTable *GetInet4MplsUnicastRouteTable
        (const std::string &vrf_name);
    AgentRouteTable *GetInet4MulticastRouteTable(const std::string &vrf_name);
    AgentRouteTable *GetEvpnRouteTable(const std::string &vrf_name);
    AgentRouteTable *GetBridgeRouteTable(const std::string &vrf_name);
    AgentRouteTable *GetRouteTable(const string &vrf_name, uint8_t table_type);
    InetUnicastAgentRouteTable *GetInet6UnicastRouteTable
        (const std::string &vrf_name);
    bool IsStaticVrf(const std::string &vrf_name) const {
        if (static_vrf_set_.find(vrf_name) != static_vrf_set_.end()) {
            return true;
        }
        return false;
    }

    void DeleteRoutes();
    void Shutdown();
    void DeleteFromDbTree(int table_type, const std::string &vrf_name);
    void reset_route_delete_walker() {route_delete_walker_.reset();}
    void reset_vrf_delete_walker() {vrf_delete_walker_.reset();}

private:
    friend class VrfEntry;

    DB *db_;
    static VrfTable *vrf_table_;
    IndexVector<VrfEntry *> index_table_;
    VrfNameTree name_tree_;
    VrfDbTree dbtree_[Agent::ROUTE_TABLE_MAX];
    DBTableWalker::WalkId walkid_;
    std::set<std::string> static_vrf_set_;
    AgentRouteWalkerPtr route_delete_walker_;
    AgentRouteWalkerPtr vrf_delete_walker_;
    DISALLOW_COPY_AND_ASSIGN(VrfTable);
};

#endif // vnsw_agent_vrf_hpp
