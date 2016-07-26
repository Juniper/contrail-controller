/////////////////////////////////////////////////////////////////////////////
//  vxlan.h
//  vnsw/agent
/////////////////////////////////////////////////////////////////////////////

#ifndef vnsw_agent_vxlan_hpp
#define vnsw_agent_vxlan_hpp

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <agent_types.h>
#include <oper/nexthop.h>
using namespace std;

class VxLanId : AgentRefCount<VxLanId>, public AgentDBEntry {
public:
    VxLanId(uint32_t vxlan_id) : AgentDBEntry(), vxlan_id_(vxlan_id){ }
    virtual ~VxLanId();

    bool IsLess(const DBEntry &rhs) const {
        const VxLanId &vxlan_id = static_cast<const VxLanId &>(rhs);
        return vxlan_id_ < vxlan_id.vxlan_id_;
    };
    virtual string ToString() const { return "vxlan_id"; };
    virtual void SetKey(const DBRequestKey *key);
    virtual KeyPtr GetDBRequestKey() const;
    
    uint32_t vxlan_id() const {return vxlan_id_;};
    const NextHop *nexthop() const {return nh_.get();};

    uint32_t GetRefCount() const {
        return AgentRefCount<VxLanId>::GetRefCount();
    }

    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;
    void SendObjectLog(const AgentDBTable *table,
                       AgentLogEvent::type event) const;

private:
    uint32_t vxlan_id_;
    NextHopRef nh_;
    friend class VxLanTable;
    DISALLOW_COPY_AND_ASSIGN(VxLanId);
};

class VxLanIdKey : public AgentKey {
public:
    VxLanIdKey(uint32_t vxlan_id) :
        AgentKey(), vxlan_id_(vxlan_id) { };
    virtual ~VxLanIdKey() { };
    uint32_t vxlan_id() const {return vxlan_id_;}

private:
    uint32_t vxlan_id_;
};

class VxLanIdData : public AgentData {
public:
    VxLanIdData(const string &vrf_name, DBRequest &req, bool mirror_destination) :
        vrf_name_(vrf_name) { 
            nh_req_.Swap(&req);
            mirror_destination_ = mirror_destination;
        }; 
    virtual ~VxLanIdData() { };
    string &vrf_name() {return vrf_name_;}
    DBRequest &nh_req() {return nh_req_;}
    bool mirror_destination() const {return mirror_destination_;}
private:
    string vrf_name_;
    DBRequest nh_req_;
    bool mirror_destination_;
};

/////////////////////////////////////////////////////////////////////////////
// Implementation of Vxlan(vxlan_id) Table
/////////////////////////////////////////////////////////////////////////////
class VxLanTable : public AgentDBTable {
public:
    //////////////////////////////////////////////////////////////////////////
    // VxLan entries are created from VN.
    // Ideally, each VN should have a different VxLan configured. However,
    // there can be transition where more than one VN can have same VxLan.
    // A config tree is maintained to handle the transition cases.
    //
    // The config tree is keyed with <vxlan-id, vn-name> and data is
    // <vrf-name, flood_unknown_unicast, active>
    //
    // For a given vxlan, there can be more than one ConfigEntries. The active
    // flag speicified, which config-entry is master for the vxlan.
    //
    // When first config entry is added for a vxlan, it becomes "active" and
    // also creates a vxlan entry.
    //
    // When subsequent config entry are added, they become "inactive" and are
    // added to config-tree. There is no change done to vxlan entry
    //
    // If an "active" config-entry is deleted, we will pick the first entry
    // for given vxlan and make it "active"
    //////////////////////////////////////////////////////////////////////////
    struct ConfigKey {
        ConfigKey(uint32_t vxlan_id, const boost::uuids::uuid &vn) :
            vxlan_id_(vxlan_id), vn_(vn) {
        }
        ConfigKey() : vxlan_id_(0), vn_() { }

        bool operator()(const ConfigKey &lhs, const ConfigKey &rhs) {
            if (lhs.vxlan_id_ != rhs.vxlan_id_)
                return lhs.vxlan_id_ < rhs.vxlan_id_;

            return lhs.vn_ < rhs.vn_;
        }

        uint32_t vxlan_id_;
        boost::uuids::uuid vn_;
    };

    struct ConfigEntry {
        ConfigEntry(const std::string &vrf, bool flood_unknown_unicast,
                    bool active, bool mirror_destination) :
            vrf_(vrf), flood_unknown_unicast_(flood_unknown_unicast),
            active_(active), mirror_destination_(mirror_destination) {
        }

        std::string vrf_;
        bool flood_unknown_unicast_;
        bool active_;
        bool mirror_destination_;
    };
    typedef std::map<ConfigKey, ConfigEntry, ConfigKey> ConfigTree;
    typedef std::map<uint32_t, ComponentNHKeyList>VxlanCompositeNHList;
    typedef std::pair<uint32_t , ComponentNHKeyList> VxlanCompositeNHEntry;
    static const uint32_t kInvalidvxlan_id = 0;
    VxLanTable(DB *db, const std::string &name) : AgentDBTable(db, name) { };
    virtual ~VxLanTable() { };

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t Hash(const DBRequestKey *key) const {return 0;};

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual bool Delete(DBEntry *entry, const DBRequest *req);
    virtual void OnZeroRefcount(AgentDBEntry *e);
    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context);

    void Process(DBRequest &req);

    void Create(uint32_t vxlan_id, const std::string &vrf_name,
                bool flood_unknown_unicast, bool mirror_destination);
    void Delete(uint32_t vxlan_id);

    VxLanId *Find(uint32_t vxlan_id);
    VxLanId *FindNoLock(uint32_t vxlan_id);
    VxLanId *Locate(uint32_t vxlan_id, const boost::uuids::uuid &vn,
                    const std::string &vrf, bool flood_unknown_unicast,
                    bool mirror_destination);
    VxLanId *Delete(uint32_t vxlan_id, const boost::uuids::uuid &vn);
    const ConfigTree &config_tree() const { return config_tree_; }
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    void Initialize();
    void Register();
    void Shutdown();
    void VmInterfaceNotify(DBTablePartBase *partition, DBEntryBase *e);
    bool AddCompositeNH(uint32_t vxlan_id, ComponentNHKeyPtr nh_key);
    bool DeleteCompositeNH(uint32_t vxlan_id, ComponentNHKeyPtr nh_key);
private:
    bool ChangeHandler(VxLanId *vxlan_id, const DBRequest *req);
    ConfigTree config_tree_;
    DBTableBase::ListenerId vn_table_listener_id_;
    DBTableBase::ListenerId interface_listener_id_;
    VxlanCompositeNHList vxlan_composite_nh_map_;
    DISALLOW_COPY_AND_ASSIGN(VxLanTable);
};

#endif // vnsw_agent_vxlan_hpp
