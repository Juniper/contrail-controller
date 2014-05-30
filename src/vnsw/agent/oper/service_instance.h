/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_OPER_SERVICE_INSTANCE_H__
#define __AGENT_OPER_SERVICE_INSTANCE_H__

#include <boost/uuid/uuid.hpp>
#include "cmn/agent_db.h"

using boost::uuids::uuid;

class ServiceInstanceKey : public AgentKey {
  public:
    ServiceInstanceKey(uuid uuid) {
        uuid_ = uuid;
    }
    const uuid &si_uuid() const {
        return uuid_;
    }

  private:
    uuid uuid_;
};

class ServiceInstance : public AgentRefCount<ServiceInstance>,
    public AgentDBEntry {
public:
    enum ServiceType {
        Other   = 1,
        SourceNAT,
        LoadBalancer,
        Firewall,
        Analyzer
    };
    enum VirtualizationType {
        VirtualMachine  = 1,
        NetworkNamespace,
    };

    ServiceInstance(uuid si_uuid);
    ServiceInstance(uuid si_uuid, uuid instance_id,
                    int service_type, int virtualization_type,
                    uuid vmi_inside, uuid vmi_outside);
    virtual bool IsLess(const DBEntry &rhs) const;
    virtual std::string ToString() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual KeyPtr GetDBRequestKey() const;
    virtual bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;

    virtual uint32_t GetRefCount() const {
        return AgentRefCount<ServiceInstance>::GetRefCount();
    }

    void StartNetworkNamespace(bool restart);
    void StopNetworkNamespace();

    uuid GetInstanceId() const {return instance_id_;};
    int GetServiceType() const {return service_type_;};
    int GetVirtualizationType() const {return virtualization_type_;};
    uuid GetUuidInside() const {return vmi_inside_;};
    uuid GetUuidOutside() const {return vmi_outside_;};

    void SetInstanceId(uuid instance_id) {instance_id_ = instance_id;};
    void SetServiceType(int type) {service_type_ = type;};
    void SetVirtualizationType(int type) {virtualization_type_ = type;};
    void SetUuidInside(uuid vmi) {vmi_inside_ = vmi;};
    void SetUuidOutside(uuid vmi) {vmi_outside_ = vmi;};

private:
    uuid uuid_;

    /* virtual machine */
    uuid instance_id_;

    /* template parameters */
    int service_type_;
    int virtualization_type_;

    /* interfaces */
    uuid vmi_inside_;
    uuid vmi_outside_;

    DISALLOW_COPY_AND_ASSIGN(ServiceInstance);
};

class ServiceInstanceTable : public AgentDBTable {
 public:
    ServiceInstanceTable(DB *db, const std::string &name);

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;
    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual void Delete(DBEntry *entry, const DBRequest *req);

    /*
     * IFNodeToReq
     *
     * Convert the ifmap node to a (key,data) pair stored in the database.
     */
    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req);

    static DBTableBase *CreateTable(DB *db, const std::string &name);

 private:
   void GetVnsUuid(Agent *agent, IFMapNode *node, std::string left, std::string right,
                   uuid &left_uuid, uuid &right_uuid);
   bool ChangeHandler(DBEntry *entry, const DBRequest *req);
    DISALLOW_COPY_AND_ASSIGN(ServiceInstanceTable);
};

#endif
