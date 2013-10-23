/////////////////////////////////////////////////////////////////////////////
//  vxlan.h
//  vnsw/agent
/////////////////////////////////////////////////////////////////////////////

#ifndef vnsw_agent_vxlan_hpp
#define vnsw_agent_vxlan_hpp

#include <cmn/agent_cmn.h>

using namespace std;

class VxLanId : AgentRefCount<VxLanId>, public AgentDBEntry {
public:
    VxLanId(uint32_t label) : label_(label){ }
    virtual ~VxLanId();

    bool IsLess(const DBEntry &rhs) const {
        const VxLanId &vxlan_id = static_cast<const VxLanId &>(rhs);
        return label_ < vxlan_id.label_;
    };
    virtual string ToString() const { return "vxlan_id"; };
    virtual void SetKey(const DBRequestKey *key);
    virtual KeyPtr GetDBRequestKey() const;
    
    uint32_t GetLabel() const {return label_;};
    const NextHop *GetNextHop() const {return nh_.get();};

    static void CreateReq(uint32_t label, const std::string &vrf_name);
    // Delete vxlan_id Label entry
    static void DeleteReq(uint32_t label);

    AgentDBTable *DBToTable() const;
    uint32_t GetRefCount() const {
        return AgentRefCount<VxLanId>::GetRefCount();
    }

    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;
    void SendObjectLog(AgentLogEvent::type event) const;

private:
    uint32_t label_;
    NextHopRef nh_;
    friend class VxLanTable;
    DISALLOW_COPY_AND_ASSIGN(VxLanId);
};

class VxLanIdKey : public AgentKey {
public:
    VxLanIdKey(uint32_t label) :
        AgentKey(), label_(label) { };
    virtual ~VxLanIdKey() { };

    uint32_t label_;
};

class VxLanIdData : public AgentData {
public:
    VxLanIdData(const string &vrf_name) {vrf_name_ = vrf_name;};
    virtual ~VxLanIdData() { };

    string vrf_name_;
};

/////////////////////////////////////////////////////////////////////////////
// Implementation of Vxlan(vxlan_id) Table
/////////////////////////////////////////////////////////////////////////////
class VxLanTable : public AgentDBTable {
public:
    static const uint32_t kInvalidLabel = 0;
    VxLanTable(DB *db, const std::string &name) : AgentDBTable(db, name) { };
    virtual ~VxLanTable() { };

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t Hash(const DBRequestKey *key) const {return 0;};

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual void Delete(DBEntry *entry, const DBRequest *req);

    // Allocate and Free label from the label_table
    static DBTableBase *CreateTable(DB *db, const std::string &name);

private:
    bool ChangeHandler(VxLanId *vxlan_id, const DBRequest *req);
    DBTableBase::ListenerId vn_table_listener_id_;
    DISALLOW_COPY_AND_ASSIGN(VxLanTable);
};

#endif // vnsw_agent_vxlan_hpp
