/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_OPER_QOS_QUEUE_H
#define __AGENT_OPER_QOS_QUEUE_H

#include <cmn/agent.h>
#include <oper_db.h>
#include <cmn/index_vector.h>

class Agent;
class DB;

struct QosQueueKey : public AgentOperDBKey {
    QosQueueKey(const boost::uuids::uuid &uuid):
        uuid_(uuid) {}

    QosQueueKey(const QosQueueKey &rhs):
        uuid_(rhs.uuid_) {}

    bool IsLess(const QosQueueKey &rhs) const {
        return uuid_ < rhs.uuid_;
    }

    boost::uuids::uuid uuid_;
};

struct QosQueueData : public AgentOperDBData {

    QosQueueData(const Agent *agent, IFMapNode *node, const std::string &name):
    AgentOperDBData(agent, node), name_(name) {}
    std::string name_;
};

class QosQueue :
    AgentRefCount<QosQueue>, public AgentOperDBEntry {
public:
    QosQueue(const boost::uuids::uuid &uuid);
    virtual ~QosQueue();

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
        return AgentRefCount<QosQueue>::GetRefCount();
    }

    const boost::uuids::uuid& uuid() const {return uuid_;}
    uint32_t  id() const {
        return id_;
    }
    void set_id(uint32_t id) {
        id_ = id;
    }

    const std::string& name() const {
        return name_;
    }
private:
    boost::uuids::uuid uuid_;
    uint32_t id_;
    std::string name_;
    DISALLOW_COPY_AND_ASSIGN(QosQueue);
};

class QosQueueTable : public AgentOperDBTable {
public:
    static const uint32_t kInvalidIndex=0xFF;
    QosQueueTable(Agent *agent, DB *db, const std::string &name);
    virtual ~QosQueueTable();

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
    void ReleaseIndex(QosQueue *qos_queue);
    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context);
private:
    IndexVector<QosQueue> index_table_;
    DISALLOW_COPY_AND_ASSIGN(QosQueueTable);
};
#endif
