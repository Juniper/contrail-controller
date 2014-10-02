/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#ifndef SRC_VNSW_AGENT_PHYSICAL_DEVICES_TABLES_PHYSICAL_DEVICE_VN_H_
#define SRC_VNSW_AGENT_PHYSICAL_DEVICES_TABLES_PHYSICAL_DEVICE_VN_H_

/////////////////////////////////////////////////////////////////////////////
// Manages DB Table of "Physical Device and Virtual-Network" membership. The
// table is built based on the IFMap schema
/////////////////////////////////////////////////////////////////////////////
#include <cmn/agent.h>
#include <physical_devices/tables/device_manager.h>
#include <string>

namespace AGENT {
struct PhysicalDeviceVnKey;
struct PhysicalDeviceVnData;

struct PhysicalDeviceVnKey : public AgentKey {
    explicit PhysicalDeviceVnKey(const boost::uuids::uuid &id) :
        AgentKey(), uuid_(id) { }
    virtual ~PhysicalDeviceVnKey() { }

    boost::uuids::uuid uuid_;
};

struct PhysicalDeviceVnData : public AgentData {
    PhysicalDeviceVnData(const boost::uuids::uuid &device,
                         const boost::uuids::uuid &vn) :
        device_(device), vn_(vn) { }
    virtual ~PhysicalDeviceVnData() { }

    boost::uuids::uuid device_;
    boost::uuids::uuid vn_;
};

class PhysicalDeviceVnEntry : AgentRefCount<PhysicalDeviceVnEntry>,
    public AgentDBEntry {
 public:
    explicit PhysicalDeviceVnEntry(const boost::uuids::uuid &id) :
        uuid_(id), device_(), vn_() { }
    virtual ~PhysicalDeviceVnEntry() { }

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual void SetKey(const DBRequestKey *key);
    virtual std::string ToString() const;
    uint32_t GetRefCount() const {
        return AgentRefCount<PhysicalDeviceVnEntry>::GetRefCount();
    }
    DBEntryBase::KeyPtr GetDBRequestKey() const;

    const boost::uuids::uuid &uuid() const { return uuid_; }
    PhysicalDeviceEntry *device() const { return device_.get(); }
    VnEntry *vn() const { return vn_.get(); }

    bool Copy(const PhysicalDeviceVnData *data);
    void SendObjectLog(AgentLogEvent::type event) const;
    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;

 private:
    friend class PhysicalDeviceVnTable;
    boost::uuids::uuid uuid_;
    PhysicalDeviceEntryRef device_;
    VnEntryRef vn_;
    DISALLOW_COPY_AND_ASSIGN(PhysicalDeviceVnEntry);
};

class PhysicalDeviceVnTable : public AgentDBTable {
 public:
    PhysicalDeviceVnTable(DB *db, const std::string &name) :
        AgentDBTable(db, name) { }
    virtual ~PhysicalDeviceVnTable() { }

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;}
    virtual size_t Hash(const DBRequestKey *key) const {return 0;}

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual void Delete(DBEntry *entry, const DBRequest *req);
    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req);

    PhysicalDeviceTable *physical_device_table() const {
        return physical_device_table_;
    }
    VnTable *vn_table() const { return vn_table_; }
    void RegisterDBClients();
    static DBTableBase *CreateTable(DB *db, const std::string &name);

 private:
    PhysicalDeviceTable *physical_device_table_;
    VnTable *vn_table_;
    DISALLOW_COPY_AND_ASSIGN(PhysicalDeviceVnTable);
};

};  // namespace AGENT

#endif  // SRC_VNSW_AGENT_PHYSICAL_DEVICES_TABLES_PHYSICAL_DEVICE_VN_H_
