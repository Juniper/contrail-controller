/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef VNSW_AGENT_OPER_LOADBALANCER_H__
#define VNSW_AGENT_OPER_LOADBALANCER_H__

#include <boost/uuid/uuid.hpp>
#include <vnc_cfg_types.h>
#include "oper/oper_db.h"

class LoadbalancerKey : public AgentOperDBKey {
  public:
    explicit LoadbalancerKey(boost::uuids::uuid uuid) {
        uuid_ = uuid;
    }
    const boost::uuids::uuid &instance_id() const {
        return uuid_;
    }

  private:
    boost::uuids::uuid uuid_;
};

class Loadbalancer : public AgentRefCount<Loadbalancer>,
    public AgentOperDBEntry {
public:
    typedef std::set<boost::uuids::uuid> PoolSet;
    struct ListenerInfo {
        autogen::LoadbalancerListenerType properties;
        PoolSet pools;
        ListenerInfo(const autogen::LoadbalancerListenerType &p,
                     const PoolSet &pls) : properties(p), pools(pls) {
        }
    };
    typedef std::map<boost::uuids::uuid, ListenerInfo> ListenerMap;

    Loadbalancer();
    ~Loadbalancer();
    virtual bool IsLess(const DBEntry &rhs) const;
    virtual std::string ToString() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual KeyPtr GetDBRequestKey() const;
    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;

    virtual uint32_t GetRefCount() const {
        return AgentRefCount<Loadbalancer>::GetRefCount();
    }

    const autogen::LoadbalancerType &lb_info() const { return lb_info_; }
    void set_lb_info(const autogen::LoadbalancerType &info) {
        lb_info_ = info;
    }
    bool IsLBInfoEqual(const autogen::LoadbalancerType &rhs);
    bool IsListenerMapEqual(const ListenerMap &rhs);
    void set_listeners(const ListenerMap &list) { listeners_ = list; }
    const ListenerMap &listeners() const { return listeners_; }

    const boost::uuids::uuid &uuid() const { return uuid_; }

private:
    boost::uuids::uuid uuid_;
    autogen::LoadbalancerType lb_info_;
    ListenerMap listeners_;
    DISALLOW_COPY_AND_ASSIGN(Loadbalancer);
};

class LoadbalancerTable : public AgentOperDBTable {
public:
    LoadbalancerTable(DB *db, const std::string &name);
    ~LoadbalancerTable();

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;

    virtual DBEntry *OperDBAdd(const DBRequest *req);
    virtual bool OperDBOnChange(DBEntry *entry, const DBRequest *req);
    virtual bool OperDBDelete(DBEntry *entry, const DBRequest *req);
    /*
     * IFNodeToReq
     *
     * Convert the ifmap node to a (key,data) pair stored in the database.
     */
    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u);
    virtual bool IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u);

    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context);
    void Initialize(DBGraph *graph);
    static DBTableBase *CreateTable(DB *db, const std::string &name);

private:
    DBGraph *graph_;
    DISALLOW_COPY_AND_ASSIGN(LoadbalancerTable);
};
#endif
