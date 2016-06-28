/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vm_hpp
#define vnsw_agent_vm_hpp

#include <cmn/agent_cmn.h>
#include <oper/oper_db.h>
#include <agent_types.h>

using namespace boost::uuids;
using namespace std;

struct VmKey : public AgentOperDBKey {
    VmKey(uuid id) : AgentOperDBKey(), uuid_(id) {} ;
    virtual ~VmKey() { }

    uuid uuid_;
};

struct VmData : public AgentOperDBData {
    typedef vector<uuid> SGUuidList;
    VmData(Agent *agent, IFMapNode *node, const std::string &name,
           const SGUuidList &sg_list) :
        AgentOperDBData(agent, node), name_(name), sg_list_(sg_list) { }
    virtual ~VmData() { }

    std::string name_;
    SGUuidList sg_list_;
};

class VmEntry : AgentRefCount<VmEntry>, public AgentOperDBEntry {
public:
    static const int kVectorIncreaseSize = 16;

    // kDropNewFlowsRecoveryThreshold is set to 90% of the Max flows for a
    // VM this value represents that recovery from the drop new flows will
    // happen only when total number of flow fall below this value
    static const int kDropNewFlowsRecoveryThreshold = 90;

    VmEntry(const uuid &id);
    virtual ~VmEntry();

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual string ToString() const;
    const string &GetCfgName() const { return name_; }
    void SetCfgName(std::string name) { name_ = name; }

    const uuid &GetUuid() const { return uuid_; }

    uint32_t GetRefCount() const {
        return AgentRefCount<VmEntry>::GetRefCount();
    }

    void SendObjectLog(AgentLogEvent::type event) const;
    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;

    uint32_t flow_count() const { return flow_count_; }
    void update_flow_count(int val) const;

    uint32_t linklocal_flow_count() const { return linklocal_flow_count_; }
    void update_linklocal_flow_count(int val) const {
        int tmp = linklocal_flow_count_.fetch_and_add(val);
        if (val < 0)
            assert(tmp >= val);
    }

    bool drop_new_flows() const { return drop_new_flows_; }

private:
    void SetInterfacesDropNewFlows(bool drop_new_flows) const;
    friend class VmTable;
    uuid uuid_;
    std::string name_;
    mutable tbb::atomic<int> flow_count_;
    mutable tbb::atomic<int> linklocal_flow_count_;
    mutable bool drop_new_flows_;
    DISALLOW_COPY_AND_ASSIGN(VmEntry);
};

class VmTable : public AgentOperDBTable {
public:
    VmTable(DB *db, const std::string &name) : AgentOperDBTable(db, name) { }
    virtual ~VmTable() { }

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;}
    virtual size_t Hash(const DBRequestKey *key) const {return 0;}

    virtual DBEntry *OperDBAdd(const DBRequest *req);
    virtual bool OperDBOnChange(DBEntry *entry, const DBRequest *req);
    virtual bool OperDBDelete(DBEntry *entry, const DBRequest *req);
    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u);
    virtual bool IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u);
    bool ProcessConfig(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u);
    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context);

    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static VmTable *GetInstance() {return vm_table_;}

private:
    static VmTable *vm_table_;
    DISALLOW_COPY_AND_ASSIGN(VmTable);
};

#endif // vnsw_agent_vm_hpp
