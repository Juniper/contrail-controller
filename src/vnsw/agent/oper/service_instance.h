/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef VNSW_AGENT_OPER_SERVICE_INSTANCE_H__
#define VNSW_AGENT_OPER_SERVICE_INSTANCE_H__

#include <map>
#include <boost/uuid/uuid.hpp>
#include "cmn/agent_db.h"

class DBGraph;
class IFMapDependencyManager;
class NamespaceManager;

class ServiceInstanceKey : public AgentKey {
  public:
    explicit ServiceInstanceKey(boost::uuids::uuid uuid) {
        uuid_ = uuid;
    }
    const boost::uuids::uuid &instance_id() const {
        return uuid_;
    }

  private:
    boost::uuids::uuid uuid_;
};

class ServiceInstance : public AgentRefCount<ServiceInstance>,
    public AgentDBEntry {
public:
    enum ServiceType {
        Other   = 1,
        SourceNAT,
        LoadBalancer,
    };
    enum VirtualizationType {
        VirtualMachine  = 1,
        NetworkNamespace,
    };

    /*
     * Properties computed from walking the ifmap graph.
     * POD type.
     */
    struct Properties {
        void Clear();
        int CompareTo(const Properties &rhs) const;
        const std::string &ServiceTypeString() const;

        bool Usable() const;
        std::string DiffString(const Properties &rhs) const;

        /* template parameters */
        int service_type;
        int virtualization_type;

        /* virtual machine */
        boost::uuids::uuid instance_id;

        /* interfaces */
        boost::uuids::uuid vmi_inside;
        boost::uuids::uuid vmi_outside;

        std::string mac_addr_inside;
        std::string mac_addr_outside;

        std::string ip_addr_inside;
        std::string ip_addr_outside;

        int ip_prefix_len_inside;
        int ip_prefix_len_outside;
    };

    ServiceInstance();
    virtual bool IsLess(const DBEntry &rhs) const;
    virtual std::string ToString() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual KeyPtr GetDBRequestKey() const;
    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;

    virtual uint32_t GetRefCount() const {
        return AgentRefCount<ServiceInstance>::GetRefCount();
    }

    /*
     * Walk the IFMap graph and calculate the properties for this node.
     */
    void CalculateProperties(DBGraph *graph, Properties *properties);

    void set_node(IFMapNode *node) { node_ = node; }

    IFMapNode *node() { return node_; }

    void set_properties(const Properties &properties) {
        properties_ = properties;
    }

    const Properties &properties() const { return properties_; }

    const boost::uuids::uuid &uuid() const { return uuid_; }

    bool IsUsable() const;

private:
    boost::uuids::uuid uuid_;
    IFMapNode *node_;
    Properties properties_;

    DISALLOW_COPY_AND_ASSIGN(ServiceInstance);
};

class ServiceInstanceTable : public AgentDBTable {
 public:
    ServiceInstanceTable(DB *db, const std::string &name);

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;

    /*
     * Register with the dependency manager.
     */
    void Initialize(DBGraph *graph, IFMapDependencyManager *dependency_manager);

    /*
     * Add/Delete methods establish the mapping between the IFMapNode
     * and the ServiceInstance DBEntry with the dependency manager.
     */
    virtual DBEntry *Add(const DBRequest *request);
    virtual void Delete(DBEntry *entry, const DBRequest *request);
    virtual bool OnChange(DBEntry *entry, const DBRequest *request);

    /*
     * IFNodeToReq
     *
     * Convert the ifmap node to a (key,data) pair stored in the database.
     */
    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req);

    static DBTableBase *CreateTable(DB *db, const std::string &name);

 private:
    /*
     * Invoked by dependency manager whenever a node in the graph
     * changes in a way that it affects the service instance
     * configuration. The dependency tracking policy is configured in
     * the dependency manager directly.
     */
    void ChangeEventHandler(DBEntry *entry);

    DBGraph *graph_;
    IFMapDependencyManager *dependency_manager_;

    DISALLOW_COPY_AND_ASSIGN(ServiceInstanceTable);
};

#endif  // VNSW_AGENT_OPER_SERVICE_INSTANCE_H__
