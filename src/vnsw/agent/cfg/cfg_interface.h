/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_INTERFACE_CFG_H__
#define __AGENT_INTERFACE_CFG_H__

#include <vector>
#include <net/address.h>

#include <cmn/agent_cmn.h>

struct CfgIntKey : public DBRequestKey {
    CfgIntKey(const boost::uuids::uuid id) : id_(id) {};
    boost::uuids::uuid id_;
};

struct CfgIntData : public DBRequestData {
    CfgIntData() {};
    virtual ~CfgIntData() {};
    void Init(const boost::uuids::uuid &vm_id, const boost::uuids::uuid &vn_id,
              const boost::uuids::uuid &vm_project_id,
              const std::string &tname, const IpAddress &ip,
              const std::string &mac, const std::string &vm_name,
              uint16_t vlan_id, const int32_t version);
    boost::uuids::uuid vm_id_;
    boost::uuids::uuid vn_id_;
    boost::uuids::uuid vm_project_id_;
    std::string tap_name_;
    IpAddress ip_addr_;
    std::string mac_addr_;
    std::string vm_name_;
    uint16_t vlan_id_;
    int32_t version_;
};

class CfgIntEntry : public DBEntry {
public:
    CfgIntEntry();
    CfgIntEntry(const boost::uuids::uuid &id);
    virtual  ~CfgIntEntry();

    void Init(const CfgIntData &data);
    bool IsLess(const DBEntry &rhs) const;
    KeyPtr GetDBRequestKey() const;
    void SetKey(const DBRequestKey *key);
    const boost::uuids::uuid &GetUuid() const {return port_id_;};
    const boost::uuids::uuid &GetVmUuid() const {return vm_id_;};
    const boost::uuids::uuid &GetVnUuid() const {return vn_id_;};
    const boost::uuids::uuid &vm_project_uuid() const { return vm_project_id_; }
    const std::string &GetIfname() const {return tap_name_;};
    const IpAddress &ip_addr() const {return ip_addr_;};
    const std::string &GetMacAddr() const {return mac_addr_;};
    const std::string &vm_name() const {return vm_name_;};
    uint16_t vlan_id() const {return vlan_id_;};
    const int32_t &GetVersion() const {return version_;};
    void SetVersion(int32_t version) {version_ = version;};
    std::string ToString() const;

private:
    boost::uuids::uuid port_id_;
    boost::uuids::uuid vm_id_;
    boost::uuids::uuid vn_id_;
    boost::uuids::uuid vm_project_id_;
    std::string tap_name_;
    IpAddress ip_addr_;
    std::string mac_addr_;
    std::string vm_name_;
    uint16_t vlan_id_;
    int32_t version_;
};

class CfgIntTable : public DBTable {
public:
    // Key of type <vn-uuid, port-uuid>
    typedef std::pair<boost::uuids::uuid, boost::uuids::uuid> CfgVnPortKey;
    // Map with CfgVnPortKey
    typedef std::map<CfgVnPortKey, CfgIntEntry *> CfgVnPortTree;

    CfgIntTable(DB *db, const std::string &name) : DBTable(db, name){ };
    virtual ~CfgIntTable() { };

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t Hash(const DBRequestKey *key) const {return 0;};

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual void Delete(DBEntry *entry, const DBRequest *req);

    static DBTableBase *CreateTable(DB *db, const std::string &name);
    const CfgVnPortTree &GetVnPortTree() const {return uuid_tree_;};

private:
    CfgVnPortTree uuid_tree_;
    DISALLOW_COPY_AND_ASSIGN(CfgIntTable);
};

#endif // __AGENT_INTERFACE_CFG_H__
