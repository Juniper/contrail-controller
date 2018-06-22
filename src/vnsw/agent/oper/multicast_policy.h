/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_multcast_policy_hpp
#define vnsw_agent_multcast_policy_hpp

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <agent_types.h>
#include <oper/oper_db.h>

using namespace boost::uuids;
using namespace std;

struct SourceGroupInfo {
public:
    enum Action {
        ACTION_PASS,
        ACTION_DENY,
    };
    IpAddress source;
    IpAddress group;
    Action action;
};

struct MulticastPolicyKey : public AgentOperDBKey {
    MulticastPolicyKey(uuid mp_uuid) : AgentOperDBKey(), mp_uuid_(mp_uuid) {} ;
    virtual ~MulticastPolicyKey() { };

    uuid mp_uuid_;
};

struct MulticastPolicyData : public AgentOperDBData {
    MulticastPolicyData(Agent *agent, IFMapNode *node,
                    const std::string &name,
                    const std::vector<SourceGroupInfo> &lst) :
    AgentOperDBData(agent, node), name_(name), mcast_sg_(lst) {}
    virtual ~MulticastPolicyData() {}

    std::string name_;
    std::vector<SourceGroupInfo> mcast_sg_;
};

class MulticastPolicyEntry :
                AgentRefCount<MulticastPolicyEntry>, public AgentOperDBEntry {
public:
    MulticastPolicyEntry(uuid mp_uuid) : mp_uuid_(mp_uuid) {};
    virtual ~MulticastPolicyEntry() { };

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual string ToString() const;

    const uuid &GetMpUuid() const {return mp_uuid_;};
    const std::string& name() const {return name_;}
    void set_name(std::string& name) {
        name_ = name;
    }
    const std::vector<SourceGroupInfo> & mcast_sg() const {
        return mcast_sg_;
    }
    void set_mcast_sg(std::vector<SourceGroupInfo> &mcast_sg) {
        mcast_sg_ = mcast_sg;
    }

    uint32_t GetRefCount() const {
        return AgentRefCount<MulticastPolicyEntry>::GetRefCount();
    }

    SourceGroupInfo::Action GetAction(IpAddress source, IpAddress group) const;

    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;
    void SendObjectLog(SandeshTraceBufferPtr ptr,
                       AgentLogEvent::type event) const;

private:
    friend class MulticastPolicyTable;
    uuid mp_uuid_;
    std::string name_;
    std::vector<SourceGroupInfo> mcast_sg_;
    DISALLOW_COPY_AND_ASSIGN(MulticastPolicyEntry);
};

class MulticastPolicyTable : public AgentOperDBTable {
public:
    static const uint32_t kInvalidMpId = 0;
    MulticastPolicyTable(DB *db, const std::string &name) :
                            AgentOperDBTable(db, name) { }
    virtual ~MulticastPolicyTable() { }

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t  Hash(const DBRequestKey *key) const {return 0;};

    virtual DBEntry *OperDBAdd(const DBRequest *req);
    virtual bool OperDBOnChange(DBEntry *entry, const DBRequest *req);
    virtual bool OperDBDelete(DBEntry *entry, const DBRequest *req);

    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u);
    virtual bool IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u);
    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context);
    bool ProcessConfig(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u);

    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static MulticastPolicyTable *GetInstance() {return mp_table_;};

private:
    static MulticastPolicyTable* mp_table_;
    bool ChangeHandler(DBEntry *entry, const DBRequest *req);
    bool IsEqual(const std::vector<SourceGroupInfo> &lhs,
                 const std::vector<SourceGroupInfo> &rhs) const;
    DISALLOW_COPY_AND_ASSIGN(MulticastPolicyTable);
};

#endif // vnsw_agent_multicast_policy_hpp
