/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#ifndef SRC_VNSW_AGENT_PHYSICAL_DEVICES_TABLES_PHYSICAL_PORT_H_
#define SRC_VNSW_AGENT_PHYSICAL_DEVICES_TABLES_PHYSICAL_PORT_H_

#include <physical_devices/tables/device_manager.h>
#include <physical_devices/tables/physical_device.h>
#include <string>

class IFMapDependencyManager;

namespace AGENT {
struct PhysicalPortKey : public AgentKey {
    explicit PhysicalPortKey(const boost::uuids::uuid &id) :
        AgentKey(), uuid_(id) { }
    virtual ~PhysicalPortKey() { }

    boost::uuids::uuid uuid_;
};

struct PhysicalPortData : public AgentData {
    PhysicalPortData(const std::string &name, const boost::uuids::uuid &dev) :
        name_(name), device_(dev) { }
    virtual ~PhysicalPortData() { }

    std::string name_;
    boost::uuids::uuid device_;
};

class PhysicalPortEntry : AgentRefCount<PhysicalPortEntry>, public AgentDBEntry{
 public:
    explicit PhysicalPortEntry(const boost::uuids::uuid &id) :
        uuid_(id), name_(""), device_() { }
    virtual ~PhysicalPortEntry() { }

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual std::string ToString() const;
    uint32_t GetRefCount() const {
        return AgentRefCount<PhysicalPortEntry>::GetRefCount();
    }

    const boost::uuids::uuid &uuid() const { return uuid_; }
    const std::string &name() const { return name_; }
    PhysicalDeviceEntry *device() const { return device_.get(); }

    bool Copy(const PhysicalPortData *data);

    void SendObjectLog(AgentLogEvent::type event) const;
    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;

 private:
    friend class PhysicalPortTable;
    boost::uuids::uuid uuid_;
    std::string name_;
    PhysicalDeviceEntryRef device_;
    DISALLOW_COPY_AND_ASSIGN(PhysicalPortEntry);
};

class PhysicalPortTable : public AgentDBTable {
 public:
    PhysicalPortTable(DB *db, const std::string &name) :
        AgentDBTable(db, name) { }
    virtual ~PhysicalPortTable() { }

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;}
    virtual size_t Hash(const DBRequestKey *key) const {return 0;}

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual void Delete(DBEntry *entry, const DBRequest *req);
    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req);

    PhysicalDeviceTable *device_table() const { return device_table_; }
    PhysicalPortEntry *Find(const boost::uuids::uuid &u);

    void ConfigEventHandler(DBEntry *entry);
    void RegisterDBClients(IFMapDependencyManager *dep);
    static DBTableBase *CreateTable(DB *db, const std::string &name);

 private:
    PhysicalDeviceTable *device_table_;
    DISALLOW_COPY_AND_ASSIGN(PhysicalPortTable);
};
};  // namespace AGENT

#endif  // SRC_VNSW_AGENT_PHYSICAL_DEVICES_TABLES_PHYSICAL_PORT_H_
