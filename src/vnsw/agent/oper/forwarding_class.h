/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_OPER_FORWARDING_CLASS_H
#define __AGENT_OPER_FORWARDING_CLASS_H

#include <cmn/agent.h>
#include <oper_db.h>
#include <cmn/index_vector.h>

class Agent;
class DB;
class ForwardingClassTable;

struct ForwardingClassKey : public AgentOperDBKey {
    ForwardingClassKey(const boost::uuids::uuid &uuid):
        uuid_(uuid) {}

    ForwardingClassKey(const ForwardingClassKey &rhs):
        uuid_(rhs.uuid_) {}

    bool IsLess(const ForwardingClassKey &rhs) const {
        return uuid_ < rhs.uuid_;
    }

    boost::uuids::uuid uuid_;
};

struct ForwardingClassData : public AgentOperDBData {

    ForwardingClassData(const Agent *agent, IFMapNode *node,
                        uint32_t dscp, uint32_t vlan_priority,
                        uint32_t mpls_exp, uint32_t id,
                        const boost::uuids::uuid &qos_queue_uuid,
                        const std::string &name):
    AgentOperDBData(agent, node), dscp_(dscp), vlan_priority_(vlan_priority),
    mpls_exp_(mpls_exp), id_(id), qos_queue_uuid_(qos_queue_uuid) {}

    uint32_t dscp_;
    uint32_t vlan_priority_;
    uint32_t mpls_exp_;
    uint32_t id_;
    boost::uuids::uuid qos_queue_uuid_;
    std::string name_;
};

class ForwardingClass :
    AgentRefCount<ForwardingClass>, public AgentOperDBEntry {
        public:
    ForwardingClass(const boost::uuids::uuid &uuid);
    virtual ~ForwardingClass();

    KeyPtr GetDBRequestKey() const;
    std::string ToString() const;
    virtual bool IsLess(const DBEntry &rhs) const;
    virtual bool DBEntrySandesh(Sandesh *resp, std::string &name) const;

    virtual bool Change(const DBRequest *req);
    virtual void Delete(const DBRequest *req);
    virtual void SetKey(const DBRequestKey *key);

    virtual bool DeleteOnZeroRefCount() const {
        return false;
    }
    virtual void OnZeroRefCount() {};
    uint32_t GetRefCount() const {
        return AgentRefCount<ForwardingClass>::GetRefCount();
    }

    uint32_t dscp() const {return dscp_;}
    uint32_t vlan_priority() const {return vlan_priority_;}
    uint32_t mpls_exp() const { return mpls_exp_;}
    uint32_t id() const {return id_;}
    const QosQueue* qos_queue_ref() const {
        return qos_queue_ref_.get();
    }
    const boost::uuids::uuid& uuid() const {
        return uuid_;
    }
    const std::string& name() const {
        return name_;
    }

private:
    boost::uuids::uuid uuid_;
    uint32_t id_;
    uint32_t dscp_;
    uint32_t vlan_priority_;
    uint32_t mpls_exp_;
    QosQueueConstRef qos_queue_ref_;
    std::string name_;
    DISALLOW_COPY_AND_ASSIGN(ForwardingClass);
};

class ForwardingClassTable : public AgentOperDBTable {
public:
    ForwardingClassTable(Agent *agent, DB *db, const std::string &name);
    virtual ~ForwardingClassTable();
    static const uint32_t kInvalidIndex=0xFF;

    static DBTableBase *CreateTable(Agent *agent, DB *db,
                                    const std::string &name);

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;

    virtual size_t Hash(const DBEntry *entry) const {return 0;}
    virtual size_t Hash(const DBRequestKey *key) const {return 0;}

    virtual DBEntry *OperDBAdd(const DBRequest *req);
    virtual bool OperDBOnChange(DBEntry *entry, const DBRequest *req);
    virtual bool OperDBDelete(DBEntry *entry, const DBRequest *req);
    virtual bool OperDBResync(DBEntry *entry, const DBRequest *req);

    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req,
                             const boost::uuids::uuid &u);
    virtual bool IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u);
    virtual bool ProcessConfig(IFMapNode *node, DBRequest &req,
                               const boost::uuids::uuid &u);
    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context);
    ForwardingClassData* BuildData(IFMapNode *node);
private:
    DISALLOW_COPY_AND_ASSIGN(ForwardingClassTable);
};
#endif
