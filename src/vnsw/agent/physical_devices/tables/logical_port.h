/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#ifndef SRC_VNSW_AGENT_PHYSICAL_DEVICES_TABLES_LOGICAL_PORT_H_
#define SRC_VNSW_AGENT_PHYSICAL_DEVICES_TABLES_LOGICAL_PORT_H_

#include <physical_devices/tables/device_manager.h>
#include <physical_devices/tables/physical_port.h>
#include <string>

class IFMapDependencyManager;

namespace AGENT {
struct LogicalPortKey;
struct LogicalPortData;

class LogicalPortEntry : AgentRefCount<LogicalPortEntry>, public AgentDBEntry {
 public:
    enum Type {
        DEFAULT,
        VLAN
    };

    explicit LogicalPortEntry(const boost::uuids::uuid &id) :
        uuid_(id), name_(), physical_port_() { }
    virtual ~LogicalPortEntry() { }

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual void SetKey(const DBRequestKey *key);
    virtual std::string ToString() const;
    uint32_t GetRefCount() const {
        return AgentRefCount<LogicalPortEntry>::GetRefCount();
    }

    const boost::uuids::uuid &uuid() const { return uuid_; }
    const std::string &name() const { return name_; }
    PhysicalPortEntry *physical_port() const { return physical_port_.get(); }

    bool CopyBase(LogicalPortTable *table, const LogicalPortData *data);
    virtual bool Copy(LogicalPortTable *table, const LogicalPortData *data) = 0;

    void SendObjectLog(AgentLogEvent::type event) const;
    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;

 private:
    friend class LogicalPortTable;
    boost::uuids::uuid uuid_;
    std::string name_;
    PhysicalPortEntryRef physical_port_;
    DISALLOW_COPY_AND_ASSIGN(LogicalPortEntry);
};

class LogicalPortTable : public AgentDBTable {
 public:
    LogicalPortTable(DB *db, const std::string &name) :
        AgentDBTable(db, name) { }
    virtual ~LogicalPortTable() { }

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;}
    virtual size_t Hash(const DBRequestKey *key) const {return 0;}

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual void Delete(DBEntry *entry, const DBRequest *req);
    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req);

    PhysicalPortTable *physical_port_table() const {
        return physical_port_table_;
    }

    void ConfigEventHandler(DBEntry *entry);
    void RegisterDBClients(IFMapDependencyManager *dep);
    static DBTableBase *CreateTable(DB *db, const std::string &name);

 private:
    PhysicalPortTable *physical_port_table_;
    DISALLOW_COPY_AND_ASSIGN(LogicalPortTable);
};

struct LogicalPortKey : public AgentKey {
    explicit LogicalPortKey(const boost::uuids::uuid &id) :
        AgentKey(), uuid_(id) { }
    virtual ~LogicalPortKey() { }

    virtual LogicalPortEntry *AllocEntry(const LogicalPortTable *table)
        const = 0;

    boost::uuids::uuid uuid_;
};

struct LogicalPortData : public AgentData {
    LogicalPortData(const std::string &name, const boost::uuids::uuid &port,
                    const boost::uuids::uuid &vif) :
        name_(name), physical_port_(port), vif_(vif) { }
    virtual ~LogicalPortData() { }

    std::string name_;
    boost::uuids::uuid physical_port_;
    boost::uuids::uuid vif_;
};

struct VlanLogicalPortKey : public LogicalPortKey {
    explicit VlanLogicalPortKey(const boost::uuids::uuid &u) :
        LogicalPortKey(u) { }
    virtual ~VlanLogicalPortKey() { }

    virtual LogicalPortEntry *AllocEntry(const LogicalPortTable *table) const;
};

struct VlanLogicalPortData : public LogicalPortData {
    VlanLogicalPortData(const std::string &name, const boost::uuids::uuid &port,
                        const boost::uuids::uuid &vif, uint16_t vlan) :
        LogicalPortData(name, port, vif), vlan_(vlan) { }
    virtual ~VlanLogicalPortData() { }

    uint16_t vlan_;
};

class VlanLogicalPortEntry : public LogicalPortEntry {
 public:
    explicit VlanLogicalPortEntry(const boost::uuids::uuid &id) :
        LogicalPortEntry(id), vlan_(0) { }
    virtual ~VlanLogicalPortEntry() { }
    virtual DBEntryBase::KeyPtr GetDBRequestKey() const;

    uint16_t vlan() const { return vlan_; }
    bool Copy(LogicalPortTable *table, const LogicalPortData *data);

 private:
    uint16_t vlan_;
    DISALLOW_COPY_AND_ASSIGN(VlanLogicalPortEntry);
};

struct DefaultLogicalPortKey : public LogicalPortKey {
    explicit DefaultLogicalPortKey(const boost::uuids::uuid &u) :
        LogicalPortKey(u) { }
    virtual ~DefaultLogicalPortKey() { }

    virtual LogicalPortEntry *AllocEntry(const LogicalPortTable *table) const;
};

struct DefaultLogicalPortData : public LogicalPortData {
    DefaultLogicalPortData(const std::string &name,
                           const boost::uuids::uuid &port,
                           const boost::uuids::uuid &vif) :
        LogicalPortData(name, port, vif) { }
    virtual ~DefaultLogicalPortData() { }
};

class DefaultLogicalPortEntry : public LogicalPortEntry {
 public:
    explicit DefaultLogicalPortEntry(const boost::uuids::uuid &id) :
        LogicalPortEntry(id) { }
    virtual ~DefaultLogicalPortEntry() { }
    virtual DBEntryBase::KeyPtr GetDBRequestKey() const;

    bool Copy(LogicalPortTable *table, const LogicalPortData *data);

 private:
    DISALLOW_COPY_AND_ASSIGN(DefaultLogicalPortEntry);
};
};  // namespace AGENT

#endif  // SRC_VNSW_AGENT_PHYSICAL_DEVICES_TABLES_LOGICAL_PORT_H_
