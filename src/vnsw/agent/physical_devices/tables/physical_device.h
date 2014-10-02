/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#ifndef SRC_VNSW_AGENT_PHYSICAL_DEVICES_TABLES_PHYSICAL_DEVICE_H_
#define SRC_VNSW_AGENT_PHYSICAL_DEVICES_TABLES_PHYSICAL_DEVICE_H_

#include <physical_devices/tables/device_manager.h>
#include <string>

class IFMapDependencyManager;

namespace AGENT {
struct PhysicalDeviceKey : public AgentKey {
    explicit PhysicalDeviceKey(boost::uuids::uuid id) :
        AgentKey(), uuid_(id) { }
    virtual ~PhysicalDeviceKey() { }

    boost::uuids::uuid uuid_;
};

struct PhysicalDeviceData : public AgentData {
    PhysicalDeviceData(const std::string &name, const std::string &vendor,
                    const IpAddress &ip, const std::string &protocol) :
        AgentData(), name_(name), vendor_(vendor), ip_(ip),
        protocol_(protocol) { }
    virtual ~PhysicalDeviceData() { }

    std::string name_;
    std::string vendor_;
    IpAddress ip_;
    std::string protocol_;
};

class PhysicalDeviceEntry : AgentRefCount<PhysicalDeviceEntry>,
    public AgentDBEntry {
 public:
    typedef enum {
        INVALID,
        OVS
    } ManagementProtocol;

    explicit PhysicalDeviceEntry(const boost::uuids::uuid &id) :
        uuid_(id), name_(""), vendor_(""), ip_(), protocol_(INVALID) { }
    virtual ~PhysicalDeviceEntry() { }

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual std::string ToString() const;
    uint32_t GetRefCount() const {
        return AgentRefCount<PhysicalDeviceEntry>::GetRefCount();
    }

    bool Copy(const PhysicalDeviceData *data);
    const boost::uuids::uuid &uuid() const { return uuid_; }
    const std::string &name() const { return name_; }
    const std::string &vendor() const { return vendor_; }
    const IpAddress &ip() const { return ip_; }
    ManagementProtocol protocol() const { return protocol_; }

    void SendObjectLog(AgentLogEvent::type event) const;
    bool DBEntrySandesh(Sandesh *resp, std::string &name) const;

 private:
    friend class PhysicalDeviceTable;
    boost::uuids::uuid uuid_;
    std::string name_;
    std::string vendor_;
    IpAddress ip_;
    ManagementProtocol protocol_;
    DISALLOW_COPY_AND_ASSIGN(PhysicalDeviceEntry);
};

class PhysicalDeviceTable : public AgentDBTable {
 public:
    PhysicalDeviceTable(DB *db, const std::string &name) :
        AgentDBTable(db, name) { }
    virtual ~PhysicalDeviceTable() { }

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;}
    virtual size_t Hash(const DBRequestKey *key) const {return 0;}

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual void Delete(DBEntry *entry, const DBRequest *req);
    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req);

    PhysicalDeviceEntry *Find(const boost::uuids::uuid &u);

    void ConfigEventHandler(DBEntry *entry);
    void RegisterDBClients(IFMapDependencyManager *dep);
    static DBTableBase *CreateTable(DB *db, const std::string &name);

 private:
    DISALLOW_COPY_AND_ASSIGN(PhysicalDeviceTable);
};
};  // namespace AGENT

#endif  // SRC_VNSW_AGENT_PHYSICAL_DEVICES_TABLES_PHYSICAL_DEVICE_H_
