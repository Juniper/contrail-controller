/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#ifndef SRC_VNSW_AGENT_OPER_PHYSICAL_DEVICE_H_
#define SRC_VNSW_AGENT_OPER_PHYSICAL_DEVICE_H_

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <nexthop.h>
#include <agent_types.h>
#include <oper_db.h>
#include <string>

class IFMapDependencyManager;
class PhysicalDevice;
class PhysicalDeviceTable;

struct PhysicalDeviceKey : public AgentOperDBKey {
    PhysicalDeviceKey(const boost::uuids::uuid &id) :
        AgentOperDBKey(), uuid_(id) { }
    PhysicalDeviceKey(const boost::uuids::uuid &id, DBSubOperation sub_op) :
        AgentOperDBKey(sub_op), uuid_(id) { }
    virtual ~PhysicalDeviceKey() { }

    boost::uuids::uuid uuid_;
};

struct PhysicalDeviceDataBase : public AgentOperDBData {
    PhysicalDeviceDataBase(Agent *agent, IFMapNode *ifmap_node) :
        AgentOperDBData(agent, ifmap_node) {
    }
    virtual ~PhysicalDeviceDataBase() { }
};

struct PhysicalDeviceData : public PhysicalDeviceDataBase {
    PhysicalDeviceData(Agent *agent, const std::string &fq_name,
                       const std::string &name, const std::string &vendor,
                       const IpAddress &ip, const IpAddress &mgmt_ip,
                       const std::string &protocol, IFMapNode *ifmap_node) :
        PhysicalDeviceDataBase(agent, ifmap_node), fq_name_(fq_name),
        name_(name), vendor_(vendor), ip_(ip), management_ip_(mgmt_ip),
        protocol_(protocol) {
    }
    virtual ~PhysicalDeviceData() { }

    std::string fq_name_;
    std::string name_;
    std::string vendor_;
    IpAddress ip_;
    IpAddress management_ip_;
    std::string protocol_;
};

struct PhysicalDeviceTsnManagedData : public PhysicalDeviceDataBase {
    PhysicalDeviceTsnManagedData(Agent *agent, bool master) :
        PhysicalDeviceDataBase(agent, NULL), master_(master) {
    }
    virtual ~PhysicalDeviceTsnManagedData() { }

    bool master_;
};

class PhysicalDevice : AgentRefCount<PhysicalDevice>, public AgentOperDBEntry {
 public:
    typedef enum {
        INVALID,
        OVS
    } ManagementProtocol;

    explicit PhysicalDevice(const boost::uuids::uuid &id) :
        AgentOperDBEntry(), uuid_(id), name_(""), vendor_(""), ip_(),
        protocol_(INVALID), master_(false) { }
    virtual ~PhysicalDevice() { }

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual std::string ToString() const;
    uint32_t GetRefCount() const {
        return AgentRefCount<PhysicalDevice>::GetRefCount();
    }

    bool Copy(PhysicalDeviceTable *table, const PhysicalDeviceData *data);
    const boost::uuids::uuid &uuid() const { return uuid_; }
    const std::string &fq_name() const { return fq_name_; }
    const std::string &name() const { return name_; }
    const std::string &vendor() const { return vendor_; }
    const IpAddress &ip() const { return ip_; }
    const IpAddress &management_ip() const { return management_ip_; }
    ManagementProtocol protocol() const { return protocol_; }
    bool master() const { return master_; }
    void set_master(bool value) { master_ = value; }

    void SendObjectLog(AgentLogEvent::type event) const;
    bool DBEntrySandesh(Sandesh *resp, std::string &name) const;

 private:
    friend class PhysicalDeviceTable;
    boost::uuids::uuid uuid_;
    std::string fq_name_;
    std::string name_;
    std::string vendor_;
    IpAddress ip_;
    IpAddress management_ip_;
    ManagementProtocol protocol_;
    bool master_;
    DISALLOW_COPY_AND_ASSIGN(PhysicalDevice);
};

class PhysicalDeviceTable : public AgentOperDBTable {
 public:
    typedef std::set<boost::uuids::uuid> PhysicalDeviceSet;
    typedef std::map<const std::string, PhysicalDeviceSet> VrfDevicesMap;
    typedef std::pair<const std::string, PhysicalDeviceSet> VrfDevicesPair;
    typedef std::set<std::string> VrfSet;
    typedef std::map<const boost::uuids::uuid, VrfSet> DeviceVrfMap;
    typedef std::pair<const boost::uuids::uuid, VrfSet> DeviceVrfPair;
    typedef std::map<IpAddress, PhysicalDevice *> IpToDeviceMap;
    typedef std::pair<IpAddress, PhysicalDevice *> IpToDevicePair;

    PhysicalDeviceTable(DB *db, const std::string &name) :
        AgentOperDBTable(db, name), ip_tree_(), managed_pd_set_(),
        vrf2devices_map_(), device2vrf_map_() { }
    virtual ~PhysicalDeviceTable() { }

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;}
    virtual size_t Hash(const DBRequestKey *key) const {return 0;}

    virtual DBEntry *OperDBAdd(const DBRequest *req);
    virtual bool OperDBOnChange(DBEntry *entry, const DBRequest *req);
    virtual bool OperDBDelete(DBEntry *entry, const DBRequest *req);
    virtual bool OperDBResync(DBEntry *entry, const DBRequest *req);
    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u);
    bool ProcessConfig(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u);
    virtual bool IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u);
    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context);

    PhysicalDevice *Find(const boost::uuids::uuid &u);

    void RegisterDBClients(IFMapDependencyManager *dep);
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    void UpdateIpToDevMap(IpAddress old, IpAddress new_ip, PhysicalDevice *p);
    void DeleteIpToDevEntry(IpAddress ip);
    PhysicalDevice *IpToPhysicalDevice(IpAddress ip);
    const PhysicalDeviceSet &managed_pd_set() const { return managed_pd_set_; }
    void EnqueueDeviceChange(const boost::uuids::uuid &u, bool master);
    void UpdateDeviceMastership(const std::string &vrf, ComponentNHList clist,
                                bool del);

 private:
    void ResetDeviceMastership(const boost::uuids::uuid &u,
                               const std::string &vrf);
    void AddDeviceToVrfEntry(const boost::uuids::uuid &u,
                             const std::string &vrf);
    bool RemoveDeviceToVrfEntry(const boost::uuids::uuid &u,
                                const std::string &vrf);
    IpToDeviceMap ip_tree_;
    PhysicalDeviceSet managed_pd_set_;
    VrfDevicesMap vrf2devices_map_;
    DeviceVrfMap device2vrf_map_;
    DISALLOW_COPY_AND_ASSIGN(PhysicalDeviceTable);
};

#endif  // SRC_VNSW_AGENT_OPER_PHYSICAL_DEVICE_H_
