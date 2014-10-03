/////////////////////////////////////////////////////////////////////////////
//  vxlan.h
//  vnsw/agent
/////////////////////////////////////////////////////////////////////////////

#ifndef vnsw_agent_vxlan_hpp
#define vnsw_agent_vxlan_hpp

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <agent_types.h>

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

    static void Create(uint32_t vxlan_id, const std::string &vrf_name);
    // Delete vxlan_id vxlan_id entry
    static void Delete(uint32_t vxlan_id);

    uint32_t GetRefCount() const {
        return AgentRefCount<VxLanId>::GetRefCount();
    }

    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;
    void SendObjectLog(AgentLogEvent::type event) const;

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
    VxLanIdData(const string &vrf_name, DBRequest &req) :
        vrf_name_(vrf_name) { 
            nh_req_.Swap(&req);
        }; 
    virtual ~VxLanIdData() { };
    string &vrf_name() {return vrf_name_;}
    DBRequest &nh_req() {return nh_req_;}

private:
    string vrf_name_;
    DBRequest nh_req_;
};

/////////////////////////////////////////////////////////////////////////////
// Implementation of Vxlan(vxlan_id) Table
/////////////////////////////////////////////////////////////////////////////
class VxLanTable : public AgentDBTable {
public:
    static const uint32_t kInvalidvxlan_id = 0;
    VxLanTable(DB *db, const std::string &name) : AgentDBTable(db, name) { };
    virtual ~VxLanTable() { };

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t Hash(const DBRequestKey *key) const {return 0;};

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual void Delete(DBEntry *entry, const DBRequest *req);
    virtual void OnZeroRefcount(AgentDBEntry *e);

    void Process(DBRequest &req);

    // Allocate and Free vxlan_id from the vxlan_id_table
    static DBTableBase *CreateTable(DB *db, const std::string &name);

private:
    bool ChangeHandler(VxLanId *vxlan_id, const DBRequest *req);
    DBTableBase::ListenerId vn_table_listener_id_;
    DISALLOW_COPY_AND_ASSIGN(VxLanTable);
};

#endif // vnsw_agent_vxlan_hpp
