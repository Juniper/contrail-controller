/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef _VNSW_AGENT_PORT_IPC_PORT_SUBSCRIBE_TABLE_H_
#define _VNSW_AGENT_PORT_IPC_PORT_SUBSCRIBE_TABLE_H_

#include <map>
#include <memory>
#include <vector>
#include <string>
#include <string>
#include <boost/uuid/uuid.hpp>
#include <boost/shared_ptr.hpp>
#include <base/util.h>
#include <tbb/mutex.h>
#include <net/address.h>
#include <db/db_table.h>
#include <db/db_entry.h>

class InterfaceTable;
class Agent;
class VmiSubscribeEntry;
class PortSubscribeTable;
typedef boost::shared_ptr<VmiSubscribeEntry> VmiSubscribeEntryPtr;

/****************************************************************************
 * The class is responsible to manage port subscribe/unsubscribe from IPC
 * channels.
 ****************************************************************************/
class PortSubscribeEntry {
public:
    enum Type {
        VMPORT,
        NAMESPACE,
        REMOTE_PORT
    };

    PortSubscribeEntry(Type type, const std::string &ifname, int32_t version);
    virtual ~PortSubscribeEntry();

    virtual void OnAdd(Agent *agent, PortSubscribeTable *table) const = 0;
    virtual void OnDelete(Agent *agent, PortSubscribeTable *table) const = 0;
    virtual void Update(const PortSubscribeEntry *rhs);

    static const char *TypeToString(Type type);
    Type type() const { return type_; }
    const std::string &ifname() const { return ifname_; }
    uint32_t version() const { return version_; }

    virtual bool MatchVn(const boost::uuids::uuid &u) const = 0;
    virtual const boost::uuids::uuid &vn_uuid() const = 0;
    virtual bool MatchVm(const boost::uuids::uuid &u) const = 0;
    virtual const boost::uuids::uuid &vm_uuid() const = 0;

protected:
    Type type_;
    std::string ifname_;
    uint16_t version_;
    DISALLOW_COPY_AND_ASSIGN(PortSubscribeEntry);
};

class VmiSubscribeEntry : public PortSubscribeEntry {
public:
    VmiSubscribeEntry(PortSubscribeEntry::Type type, const std::string &ifname,
                      uint32_t version, const boost::uuids::uuid &vmi_uuid,
                      const boost::uuids::uuid vm_uuid,
                      const std::string &vm_name,
                      const boost::uuids::uuid &vn_uuid,
                      const boost::uuids::uuid &project_uuid,
                      const Ip4Address &ip4_addr, const Ip6Address &ip6_addr,
                      const std::string &mac_addr, uint16_t tx_vlan_id,
                      uint16_t rx_vlan_id);
    ~VmiSubscribeEntry();

    virtual bool MatchVn(const boost::uuids::uuid &u) const;
    virtual bool MatchVm(const boost::uuids::uuid &u) const;

    virtual void Update(const PortSubscribeEntry *rhs);
    void OnAdd(Agent *agent, PortSubscribeTable *table) const;
    void OnDelete(Agent *agent, PortSubscribeTable *table) const;

    const boost::uuids::uuid &vmi_uuid() const { return vmi_uuid_; }
    const boost::uuids::uuid &vm_uuid() const { return vm_uuid_; }
    const std::string &vm_name() const { return vm_name_; }
    const boost::uuids::uuid &vn_uuid() const { return vn_uuid_; }
    const boost::uuids::uuid &project_uuid() const { return project_uuid_; }
    const Ip4Address &ip4_addr() const { return ip4_addr_; }
    const Ip6Address &ip6_addr() const { return ip6_addr_; }
    const std::string &mac_addr() const { return mac_addr_; }
    uint16_t tx_vlan_id() const { return tx_vlan_id_; }
    uint16_t rx_vlan_id() const { return rx_vlan_id_; }
private:
    boost::uuids::uuid vmi_uuid_;
    boost::uuids::uuid vm_uuid_;
    std::string vm_name_;
    boost::uuids::uuid vn_uuid_;
    boost::uuids::uuid project_uuid_;
    Ip4Address ip4_addr_;
    Ip6Address ip6_addr_;
    std::string mac_addr_;
    uint16_t tx_vlan_id_;
    uint16_t rx_vlan_id_;
    DISALLOW_COPY_AND_ASSIGN(VmiSubscribeEntry);
};

class PortSubscribeTable {
public:
    PortSubscribeTable(Agent *agent);
    virtual ~PortSubscribeTable();

    struct State : DBState {
        State() : uuid_() { }
        ~State() { }

        boost::uuids::uuid uuid_;
    };

    struct Cmp {
        bool operator()(const boost::uuids::uuid &lhs,
                        const boost::uuids::uuid &rhs) const {
            return lhs < rhs;
        }
    };
    typedef std::map<boost::uuids::uuid, VmiSubscribeEntryPtr, Cmp> VmiTree;
    typedef std::map<boost::uuids::uuid, IFMapNode *> UuidToIFNodeTree;

    void InitDone();
    void Shutdown();
    IFMapNode *UuidToIFNode(const boost::uuids::uuid &u) const;
    void Notify(DBTablePartBase *partition, DBEntryBase *e);
    void StaleWalk(uint64_t version);
    uint32_t Size() const { return vmi_tree_.size(); }

    void Add(const boost::uuids::uuid &u, VmiSubscribeEntryPtr entry);
    void Delete(const boost::uuids::uuid &u);
    VmiSubscribeEntryPtr Get(const boost::uuids::uuid &u) const;

private:
    Agent *agent_;
    InterfaceTable *interface_table_;
    VNController *controller_;
    mutable tbb::mutex mutex_;
    VmiTree vmi_tree_;
    IFMapAgentTable *vmi_config_table_;
    DBTableBase::ListenerId vmi_config_listener_id_;
    UuidToIFNodeTree uuid_ifnode_tree_;
    DISALLOW_COPY_AND_ASSIGN(PortSubscribeTable);
};

#endif //  _VNSW_AGENT_PORT_IPC_PORT_SUBSCRIBE_TABLE_H_
