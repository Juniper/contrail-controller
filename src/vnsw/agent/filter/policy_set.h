/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_FILTER_POLICY_SET_H_
#define SRC_VNSW_AGENT_FILTER_POLICY_SET_H_

#include <boost/scoped_ptr.hpp>
#include <db/db.h>
#include <cmn/agent.h>
#include <cmn/agent_db.h>
#include <oper/oper_db.h>

class PolicySetTable;

struct PolicySetKey : public AgentOperDBKey {
    PolicySetKey(const boost::uuids::uuid &uuid) :
        AgentOperDBKey(), uuid_(uuid) {};
    boost::uuids::uuid uuid_;
};

typedef std::map<std::string, boost::uuids::uuid> FirewallPolicyUuidList;
typedef std::pair<std::string, boost::uuids::uuid> FirewallPolicyPair;

struct PolicySetData : public AgentOperDBData {
    PolicySetData(Agent *agent, IFMapNode *node, const std::string &name,
                  bool global, FirewallPolicyUuidList &list):
        AgentOperDBData(agent, node), name_(name),
        global_(global), fw_policy_uuid_list_(list) {}
    ~PolicySetData() {}

    const std::string name_;
    bool global_;
    FirewallPolicyUuidList fw_policy_uuid_list_;
};

class PolicySet : AgentRefCount<PolicySet>, public AgentOperDBEntry {
public:
    typedef std::vector<AclDBEntryConstRef> FirewallPolicyList;
    PolicySet(const boost::uuids::uuid &uuid);
    ~PolicySet();

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual std::string ToString() const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    bool DBEntrySandesh(Sandesh *resp, std::string &name) const;
    bool Change(PolicySetTable *table, const PolicySetData *data);
    void Delete();

    const boost::uuids::uuid &uuid() const { return uuid_; }
    const std::string &name() const { return name_; }

    virtual uint32_t GetRefCount() const {
        return AgentRefCount<PolicySet>::GetRefCount();
    }

    const AclDBEntry* GetAcl(uint32_t index) {
        return fw_policy_list_[index].get();
    }

    FirewallPolicyList& fw_policy_list() {
        return fw_policy_list_;
    }

    bool global() const {
        return global_;
    }

private:
    friend class PolicySetTable;
    boost::uuids::uuid uuid_;
    std::string name_;
    FirewallPolicyUuidList fw_policy_uuid_list_;
    FirewallPolicyList fw_policy_list_;
    bool global_;
    DISALLOW_COPY_AND_ASSIGN(PolicySet);
};

class PolicySetTable : public AgentOperDBTable {
public:
    PolicySetTable(DB *db, const std::string &name) :
        AgentOperDBTable(db, name), global_policy_set_(NULL) {};
    ~PolicySetTable() {};

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;

    virtual size_t Hash(const DBEntry *entry) const {return 0;}
    virtual size_t Hash(const DBRequestKey *key) const {return 0;}

    virtual DBEntry *OperDBAdd(const DBRequest *req);
    virtual bool OperDBOnChange(DBEntry *entry, const DBRequest *req);
    virtual bool OperDBResync(DBEntry *entry, const DBRequest *req);
    virtual bool OperDBDelete(DBEntry *entry, const DBRequest *req);

    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req,
                             const boost::uuids::uuid &u);
    bool ProcessConfig(IFMapNode *node, DBRequest &req,
                       const boost::uuids::uuid &u);
    virtual bool IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u);

    virtual AgentSandeshPtr
        GetAgentSandesh(const AgentSandeshArguments *args,
                        const std::string &context);
    PolicySet* Find(const boost::uuids::uuid &u);
    static DBTableBase *CreateTable(DB *db, const std::string &name);

    PolicySet* global_policy_set() const {
        return global_policy_set_;
    }

    void set_global_policy_set(PolicySet *ps) {
        global_policy_set_ = ps;
    }

private:
    PolicySet* global_policy_set_;
    DISALLOW_COPY_AND_ASSIGN(PolicySetTable);
};
#endif
