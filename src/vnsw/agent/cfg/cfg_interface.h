/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_INTERFACE_CFG_H__
#define __AGENT_INTERFACE_CFG_H__

#include <vector>
#include <boost/uuid/uuid.hpp>
#include <net/address.h>
#include <db/db_table.h>
#include <db/db_entry.h>

/****************************************************************************
 * DBTable containing interface config messages. Interface-Config entries
 * are framed based on the external trigger to add a port.
 * Example:
 *    - JSON messages on port-ipc channel
 *    - VGW configuration
 *
 * The DBTable contains two type of entries,
 *    - VMI-UUID messages. These messages contain UUID of the interface being
 *      added
 *    - LABEL message. These messages are used in kubenetes and mesos. Ports
 *      in this case are addressed by <vm-label, network-label>. Kubernetes
 *      does not support networks, hence network-label will be NULL
 ****************************************************************************/
class Agent;
class InterfaceConfigEntry;
class InterfaceConfigKey;
class InterfaceConfigData;

class InterfaceConfigVmiEntry;
class InterfaceConfigVmiKey;
class InterfaceConfigVmiData;

class InterfaceConfigEntry : public DBEntry {
public:
    static const int32_t kMaxVersion = -1;
    enum KeyType {
        VMI_UUID,
        LABELS,
        KEY_TYPE_INVALID
    };

    InterfaceConfigEntry(KeyType key_type) : key_type_(key_type) { }
    virtual ~InterfaceConfigEntry() { }

    virtual bool Compare(const InterfaceConfigEntry &rhs) const = 0;
    virtual bool Change(const InterfaceConfigData *rhs);
    virtual void Set(const InterfaceConfigData *data);
    virtual bool OnAdd(const InterfaceConfigData *rhs) = 0;
    virtual bool OnChange(const InterfaceConfigData *rhs) = 0;
    virtual bool OnDelete() = 0;

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual void SetKey(const InterfaceConfigKey *k);

    KeyType key_type() const { return key_type_; }
    const std::string &tap_name() const { return tap_name_; }
    const std::string &last_update_time() const { return last_update_time_; }
    bool do_subscribe() const { return do_subscribe_; }
    int32_t version() const { return version_; }

    static const std::string TypeToString(KeyType key_type);
private:
    KeyType key_type_;
    std::string tap_name_;
    std::string last_update_time_;
    bool do_subscribe_;
    int32_t version_;
    static std::string type_to_name_[];
};

class InterfaceConfigKey : public DBRequestKey {
public:
    InterfaceConfigKey(InterfaceConfigEntry::KeyType key_type) :
        key_type_(key_type) {
    }
    virtual ~InterfaceConfigKey() { }
    bool IsLess(const InterfaceConfigKey *rhs) const;
    virtual bool Compare(const InterfaceConfigKey *rhs) const = 0;

    virtual InterfaceConfigEntry *AllocEntry() const = 0;
    const InterfaceConfigEntry::KeyType key_type() const { return key_type_; }
private:
    friend class InterfaceConfigEntry;
    InterfaceConfigEntry::KeyType key_type_;
};

class InterfaceConfigData : public DBRequestData {
public:
    InterfaceConfigData(const std::string &tap_name, bool do_subscribe,
                    int32_t version) :
        tap_name_(tap_name), do_subscribe_(do_subscribe),
        version_(version) {
    }
    virtual ~InterfaceConfigData() { }

    const std::string &tap_name() const { return tap_name_; }
    bool do_subscribe() const { return do_subscribe_; }
    int32_t version() const { return version_; }
private:
    friend class InterfaceConfigEntry;
    std::string tap_name_;
    bool do_subscribe_;
    int32_t version_;
};

class InterfaceConfigVmiEntry : public InterfaceConfigEntry {
public:
    enum VmiType {
        VM_INTERFACE,
        NAMESPACE_PORT,
        REMOTE_PORT,
        TYPE_INVALID,
    };

    InterfaceConfigVmiEntry(const boost::uuids::uuid &vmi_uuid) :
        InterfaceConfigEntry(InterfaceConfigEntry::VMI_UUID),
        vmi_uuid_(vmi_uuid) {
    }
    virtual ~InterfaceConfigVmiEntry() { }

    virtual bool Compare(const InterfaceConfigEntry &rhs) const;
    virtual bool OnAdd(const InterfaceConfigData *data);
    virtual bool OnChange(const InterfaceConfigData *rhs);
    virtual bool OnDelete();

    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual std::string ToString() const;

    VmiType vmi_type() const { return vmi_type_; }
    const boost::uuids::uuid &vmi_uuid() const { return vmi_uuid_; }
    const boost::uuids::uuid &vm_uuid() const { return vm_uuid_; }
    const boost::uuids::uuid &vn_uuid() const { return vn_uuid_; }
    const boost::uuids::uuid &project_uuid() const { return project_uuid_; }
    const Ip4Address &ip4_addr() const { return ip4_addr_; }
    const Ip6Address &ip6_addr() const { return ip6_addr_; }
    const std::string &mac_addr() const { return mac_addr_; }
    const std::string &vm_name() const { return vm_name_; }
    const uint16_t &tx_vlan_id() const { return tx_vlan_id_; }
    const uint16_t &rx_vlan_id() const { return rx_vlan_id_; }

    static std::string VmiTypeToString(VmiType type);
private:
    friend class InterfaceConfigVmiData;
    friend class InterfaceConfigVmiKey;
    VmiType vmi_type_;
    boost::uuids::uuid vmi_uuid_;
    boost::uuids::uuid vm_uuid_;
    boost::uuids::uuid vn_uuid_;
    boost::uuids::uuid project_uuid_;
    Ip4Address ip4_addr_;
    Ip6Address ip6_addr_;
    std::string mac_addr_;
    std::string vm_name_;
    uint16_t tx_vlan_id_; // VLAN-ID on packet tx
    uint16_t rx_vlan_id_; // VLAN-ID on packet rx
};

class InterfaceConfigVmiKey : public InterfaceConfigKey {
public:
    InterfaceConfigVmiKey(const boost::uuids::uuid &vmi_uuid) :
        InterfaceConfigKey(InterfaceConfigEntry::VMI_UUID),
        vmi_uuid_(vmi_uuid) {
    }
    virtual ~InterfaceConfigVmiKey() { }

    virtual InterfaceConfigEntry *AllocEntry() const;
    virtual bool Compare(const InterfaceConfigKey *rhs) const;

    const boost::uuids::uuid &vmi_uuid() const { return vmi_uuid_; }
private:
    friend class InterfaceConfigVmiEntry;
    boost::uuids::uuid vmi_uuid_;
};

class InterfaceConfigVmiData : public InterfaceConfigData {
public:
    InterfaceConfigVmiData(InterfaceConfigVmiEntry::VmiType vmi_type,
                           const std::string &tap_name,
                           const boost::uuids::uuid &vm_uuid,
                           const boost::uuids::uuid &vn_uuid,
                           const boost::uuids::uuid &project_uuid,
                           const Ip4Address &ip4_addr,
                           const Ip6Address &ip6_addr, const std::string &mac,
                           const std::string &vm_name, uint16_t tx_vlan_id,
                           uint16_t rx_vlan_id, int32_t version) :
    InterfaceConfigData(tap_name, true, version), vmi_type_(vmi_type),
    vm_uuid_(vm_uuid), vn_uuid_(vn_uuid), project_uuid_(project_uuid),
    ip4_addr_(ip4_addr), ip6_addr_(ip6_addr), mac_addr_(mac),
    vm_name_(vm_name), tx_vlan_id_(tx_vlan_id), rx_vlan_id_(rx_vlan_id) {
    }

    ~InterfaceConfigVmiData() { }

    InterfaceConfigVmiEntry::VmiType vmi_type() const { return vmi_type_; }
    boost::uuids::uuid vm_uuid() const { return vm_uuid_; }
    boost::uuids::uuid vn_uuid() const { return vn_uuid_; }
    boost::uuids::uuid project_uuid() const { return project_uuid_; }
    Ip4Address ip4_addr() const { return ip4_addr_; }
    Ip6Address ip6_addr() const { return ip6_addr_; }
    const std::string &mac_addr() const { return mac_addr_; }
    const std::string &vm_name() const { return vm_name_; }
    uint16_t tx_vlan_id() const { return tx_vlan_id_; }
    uint16_t rx_vlan_id() const { return rx_vlan_id_; }
private:
    friend class InterfaceConfigVmiEntry;
    InterfaceConfigVmiEntry::VmiType vmi_type_;
    boost::uuids::uuid vm_uuid_;
    boost::uuids::uuid vn_uuid_;
    boost::uuids::uuid project_uuid_;
    Ip4Address ip4_addr_;
    Ip6Address ip6_addr_;
    std::string mac_addr_;
    std::string vm_name_;
    uint16_t tx_vlan_id_;
    uint16_t rx_vlan_id_;
};

class InterfaceConfigTable : public DBTable {
public:
    InterfaceConfigTable(DB *db, const std::string &name) : DBTable(db, name) {
    }
    virtual ~InterfaceConfigTable() { }

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t Hash(const DBRequestKey *key) const {return 0;};

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual bool Delete(DBEntry *entry, const DBRequest *req);

    void set_agent(Agent *agent) { agent_ = agent; }
    Agent *agent() const { return agent_; }
    static DBTableBase *CreateTable(DB *db, const std::string &name);
private:
    Agent *agent_;
    DISALLOW_COPY_AND_ASSIGN(InterfaceConfigTable);
};

#endif //  __AGENT_INTERFACE_CFG_H__
