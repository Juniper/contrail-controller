/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef VNSW_AGENT_OPER_LOADBALANCER_H__
#define VNSW_AGENT_OPER_LOADBALANCER_H__

#include <boost/uuid/uuid.hpp>
#include "cmn/agent_db.h"
#include "oper/ifmap_dependency_manager.h"

class IFMapDependencyManager;
class LoadbalancerProperties;

class LoadbalancerKey : public AgentKey {
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
    public AgentDBEntry {
public:
    typedef LoadbalancerProperties Properties;

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

    void set_properties(const Properties &properties);
    const Properties *properties() const;

    const boost::uuids::uuid &uuid() const { return uuid_; }

    void SetIFMapNodeState(IFMapDependencyManager::IFMapNodePtr ref) {
        ifmap_node_state_ref_ = ref;
    }

    IFMapNode *ifmap_node() {
        if (!ifmap_node_state_ref_)
            return NULL;
        IFMapNodeState *state = ifmap_node_state_ref_.get();
        return state->node();
    }


private:
    boost::uuids::uuid uuid_;
    std::auto_ptr<Properties> properties_;
    IFMapDependencyManager::IFMapNodePtr ifmap_node_state_ref_;
    DISALLOW_COPY_AND_ASSIGN(Loadbalancer);
};

class LoadbalancerTable : public AgentDBTable {
public:
    LoadbalancerTable(DB *db, const std::string &name);
    ~LoadbalancerTable();

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;

    /*
     * Register with the dependency manager.
     */
    void Initialize(DBGraph *graph, IFMapDependencyManager *dependency_manager);

    /*
     * Walk the IFMap graph and calculate the properties for this node.
     */
    void CalculateProperties(DBGraph *graph, IFMapNode *node,
            LoadbalancerProperties *properties);

    /*
     * Add/Delete methods establish the mapping between the IFMapNode
     * and the Loadbalancer DBEntry with the dependency manager.
     */
    virtual DBEntry *Add(const DBRequest *request);
    virtual bool Delete(DBEntry *entry, const DBRequest *request);
    virtual bool OnChange(DBEntry *entry, const DBRequest *request);

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
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    IFMapDependencyManager *dependency_manager() {
        return dependency_manager_;
    }

private:
    /*
     * Invoked by dependency manager whenever a node in the graph
     * changes in a way that it affects the service instance
     * configuration. The dependency tracking policy is configured in
     * the dependency manager directly.
     */
    void ChangeEventHandler(IFMapNode *node, DBEntry *entry);

    DBGraph *graph_;
    IFMapDependencyManager *dependency_manager_;

    DISALLOW_COPY_AND_ASSIGN(LoadbalancerTable);
};

#endif
