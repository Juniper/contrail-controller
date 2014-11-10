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

class IFMapDependencyManager;

namespace AGENT {
struct PhysicalDeviceVnKey;
struct PhysicalDeviceVnData;

struct PhysicalDeviceVnKey : public AgentKey {
    explicit PhysicalDeviceVnKey(const boost::uuids::uuid &dev_uuid,
                                 const boost::uuids::uuid &vn_uuid) :
        AgentKey(), device_uuid_(dev_uuid), vn_uuid_(vn_uuid) { }
    virtual ~PhysicalDeviceVnKey() { }

    boost::uuids::uuid device_uuid_;
    boost::uuids::uuid vn_uuid_;
};

struct PhysicalDeviceVnData : public AgentData {
    PhysicalDeviceVnData() { }
    virtual ~PhysicalDeviceVnData() { }
};

class PhysicalDeviceVnEntry : AgentRefCount<PhysicalDeviceVnEntry>,
    public AgentDBEntry {
 public:
    PhysicalDeviceVnEntry(const boost::uuids::uuid &device_uuid,
                          const boost::uuids::uuid &vn_uuid) :
        device_uuid_(device_uuid), vn_uuid_(vn_uuid), device_(), vn_() { }
    virtual ~PhysicalDeviceVnEntry() { }

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual void SetKey(const DBRequestKey *key);
    virtual std::string ToString() const;
    uint32_t GetRefCount() const {
        return AgentRefCount<PhysicalDeviceVnEntry>::GetRefCount();
    }
    DBEntryBase::KeyPtr GetDBRequestKey() const;

    const boost::uuids::uuid &device_uuid() const { return device_uuid_; }
    PhysicalDeviceEntry *device() const { return device_.get(); }

    const boost::uuids::uuid &vn_uuid() const { return vn_uuid_; }
    VnEntry *vn() const { return vn_.get(); }

    bool Copy(PhysicalDeviceVnTable *table, const PhysicalDeviceVnData *data);
    void SendObjectLog(AgentLogEvent::type event) const;
    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;

 private:
    friend class PhysicalDeviceVnTable;
    boost::uuids::uuid device_uuid_;
    boost::uuids::uuid vn_uuid_;

    PhysicalDeviceEntryRef device_;
    VnEntryRef vn_;
    DISALLOW_COPY_AND_ASSIGN(PhysicalDeviceVnEntry);
};

class PhysicalDeviceVnTable : public AgentDBTable {
 public:
    struct Compare {
        bool operator() (const PhysicalDeviceVnKey &left,
                         const PhysicalDeviceVnKey &right) {
            if (left.device_uuid_ != right.device_uuid_) {
                return left.device_uuid_ < right.device_uuid_;
            }
            return left.vn_uuid_ < right.vn_uuid_;
        }
    };

    typedef std::map<PhysicalDeviceVnKey, uint32_t, Compare> ConfigTree;
    typedef ConfigTree::iterator ConfigIterator;
    typedef std::pair<PhysicalDeviceVnKey, uint32_t> ConfigPair;

    PhysicalDeviceVnTable(DB *db, const std::string &name) :
        AgentDBTable(db, name) { }
    virtual ~PhysicalDeviceVnTable() { }

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;}
    virtual size_t Hash(const DBRequestKey *key) const {return 0;}

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual bool Delete(DBEntry *entry, const DBRequest *req);
    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req);

    PhysicalDeviceTable *physical_device_table() const {
        return physical_device_table_;
    }
    VnTable *vn_table() const { return vn_table_; }
    void RegisterDBClients(IFMapDependencyManager *dep);
    static DBTableBase *CreateTable(DB *db, const std::string &name);

    void ConfigEventHandler(DBEntry *entry);
    void ConfigUpdate(IFMapNode *node);
    const ConfigTree &config_tree() const { return config_tree_; }
    uint32_t config_version() const { return config_version_; }

 private:
    PhysicalDeviceTable *physical_device_table_;
    VnTable *vn_table_;
    ConfigTree config_tree_;
    uint32_t config_version_;
    DISALLOW_COPY_AND_ASSIGN(PhysicalDeviceVnTable);
};

};  // namespace AGENT

#endif  // SRC_VNSW_AGENT_PHYSICAL_DEVICES_TABLES_PHYSICAL_DEVICE_VN_H_
