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
#include <base/address.h>
#include <tbb/mutex.h>
#include <db/db_table.h>
#include <db/db_entry.h>
#include <vnc_cfg_types.h>

class InterfaceTable;
class Agent;
struct VmInterfaceConfigData;
class VmiSubscribeEntry;
class VmVnPortSubscribeEntry;
class PortSubscribeEntry;
class PortSubscribeTable;
typedef boost::shared_ptr<PortSubscribeEntry> PortSubscribeEntryPtr;

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
                      uint16_t rx_vlan_id,
                      uint8_t vhostuser_mode, uint8_t link_state);
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
    uint8_t vhostuser_mode() const { return vhostuser_mode_; }
    uint8_t link_state() const { return link_state_; }
    void set_link_state(uint8_t value) { link_state_ = value; }
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
    uint8_t vhostuser_mode_;
    uint8_t link_state_;
    DISALLOW_COPY_AND_ASSIGN(VmiSubscribeEntry);
};

/****************************************************************************
 * In case of container orchestrators like k8s/mesos, we do not get VMI UUID
 * in port-subscribe message. Instead, the interface is addressed by
 * vm-uuid and vn. The VmVnPortSubscribeEntry class is used for these
 * scenarios
 ****************************************************************************/
class VmVnPortSubscribeEntry : public PortSubscribeEntry {
public:
    VmVnPortSubscribeEntry(PortSubscribeEntry::Type type,
                           const std::string &ifname, uint32_t version,
                           const boost::uuids::uuid &vm_uuid,
                           const boost::uuids::uuid &vn_uuid,
                           const boost::uuids::uuid &vmi_uuid,
                           const std::string &vm_name,
                           const std::string &vm_identifier,
                           const std::string &vm_ifname,
                           const std::string &vm_namespace);
    ~VmVnPortSubscribeEntry();

    virtual bool MatchVn(const boost::uuids::uuid &u) const;
    virtual const boost::uuids::uuid &vn_uuid() const { return vn_uuid_; }
    virtual bool MatchVm(const boost::uuids::uuid &u) const;
    virtual const boost::uuids::uuid &vm_uuid() const { return vm_uuid_; }

    virtual void Update(const PortSubscribeEntry *rhs);
    void OnAdd(Agent *agent, PortSubscribeTable *table) const;
    void OnDelete(Agent *agent, PortSubscribeTable *table) const;

    const std::string &vm_name() const { return vm_name_; }
    const std::string &vm_identifier() const { return vm_identifier_; }
    const std::string &vm_ifname() const { return vm_ifname_; }
    const std::string &vm_namespace() const { return vm_namespace_; }
    void set_vmi_uuid(const boost::uuids::uuid &u) { vmi_uuid_ = u; }
    const boost::uuids::uuid &vmi_uuid() const { return vmi_uuid_; }
private:
    boost::uuids::uuid vm_uuid_;
    boost::uuids::uuid vn_uuid_;
    std::string vm_name_;
    std::string vm_identifier_;
    std::string vm_ifname_;
    std::string vm_namespace_;
    boost::uuids::uuid vmi_uuid_;
    DISALLOW_COPY_AND_ASSIGN(VmVnPortSubscribeEntry);
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
    typedef std::map<boost::uuids::uuid, PortSubscribeEntryPtr, Cmp> VmiTree;
    typedef std::map<boost::uuids::uuid, IFMapNode *> UuidToIFNodeTree;

    // trees used for managing vm-vn port subscriptions
    // VmiToVmVnTree       : The tree from vmi-uuid to corresponding
    //                       vm+vn uuid
    // VmVnToVmiTree       : Reverse mapping for VmiToVmVnTree. Will be used
    //                       to find VMI when port-subscription message
    // Both the tree are built from VmInterfaceConfigData

    struct VmVnUuidEntry {
        boost::uuids::uuid vm_uuid_;
        boost::uuids::uuid vn_uuid_;
        boost::uuids::uuid vmi_uuid_;

        VmVnUuidEntry(const boost::uuids::uuid &vm_uuid,
                      const boost::uuids::uuid &vn_uuid,
                      const boost::uuids::uuid &vmi_uuid) :
            vm_uuid_(vm_uuid), vn_uuid_(vn_uuid), vmi_uuid_(vmi_uuid) {
        }
        VmVnUuidEntry() :
            vm_uuid_(boost::uuids::nil_uuid()),
            vn_uuid_(boost::uuids::nil_uuid()),
            vmi_uuid_(boost::uuids::nil_uuid()) {
        }
        virtual ~VmVnUuidEntry() { }
    };

    struct VmVnUuidEntryCmp {
        bool operator()(const VmVnUuidEntry &lhs,
                        const VmVnUuidEntry &rhs) const {
            if (lhs.vm_uuid_ != rhs.vm_uuid_) {
                return lhs.vm_uuid_ < rhs.vm_uuid_;
            }
            if (lhs.vn_uuid_ != rhs.vn_uuid_) {
                return lhs.vn_uuid_ < rhs.vn_uuid_;
            }
            return lhs.vmi_uuid_ < rhs.vmi_uuid_;
        }
    };

    struct VmiEntry {
        boost::uuids::uuid vm_uuid_;
        boost::uuids::uuid vn_uuid_;
        bool sub_interface_;
        boost::uuids::uuid parent_vmi_;
        uint16_t vlan_tag_;
        std::string mac_;
        uint8_t vhostuser_mode_;
        autogen::VirtualMachineInterface *vmi_cfg;

        VmiEntry() :
            vm_uuid_(), vn_uuid_(), sub_interface_(), parent_vmi_(),
            vlan_tag_(), mac_(), vhostuser_mode_() {
                vmi_cfg = NULL;
        }
    };

    typedef std::map<boost::uuids::uuid, VmiEntry> VmiToVmVnTree;
    typedef std::map<VmVnUuidEntry, boost::uuids::uuid, VmVnUuidEntryCmp>
        VmVnToVmiTree;
    typedef std::map<VmVnUuidEntry, PortSubscribeEntryPtr, VmVnUuidEntryCmp>
        VmVnTree;

    void InitDone();
    void Shutdown();
    IFMapNode *UuidToIFNode(const boost::uuids::uuid &u) const;
    void Notify(DBTablePartBase *partition, DBEntryBase *e);
    void StaleWalk(uint64_t version);
    uint32_t Size() const { return vmi_tree_.size(); }

    void AddVmi(const boost::uuids::uuid &u, PortSubscribeEntryPtr entry);
    void DeleteVmi(const boost::uuids::uuid &u);
    PortSubscribeEntryPtr GetVmi(const boost::uuids::uuid &u) const;

    void AddVmVnPort(const boost::uuids::uuid &vm_uuid,
                     const boost::uuids::uuid &vn_uuid,
                     const boost::uuids::uuid &vmi_uuid,
                     PortSubscribeEntryPtr entry);
    void DeleteVmVnPort(const boost::uuids::uuid &vm_uuid,
                        const boost::uuids::uuid &vn_uuid,
                        const boost::uuids::uuid &vmi_uuid);
    PortSubscribeEntryPtr GetVmVnPortNoLock(const boost::uuids::uuid &vm_uuid,
                                            const boost::uuids::uuid &vn_uuid,
                                            const boost::uuids::uuid &vmi_uuid);
    PortSubscribeEntryPtr GetVmVnPort(const boost::uuids::uuid &vm_uuid,
                                      const boost::uuids::uuid &vn_uuid,
                                      const boost::uuids::uuid &vmi_uuid);

    void HandleVmiIfnodeAdd(const boost::uuids::uuid &vmi_uuid,
                            const VmInterfaceConfigData *data);
    void HandleVmiIfnodeDelete(const boost::uuids::uuid &vmi_uuid);

    PortSubscribeEntryPtr Get(const boost::uuids::uuid &vmi_uuid,
                              const boost::uuids::uuid &vm_uuid,
                              const boost::uuids::uuid &vn_uuid) const;

    bool VmVnToVmiSetNoLock(const boost::uuids::uuid &vm_uuid,
                             std::set<boost::uuids::uuid> &vmi_uuid_set) const;
    bool VmVnToVmiSet(const boost::uuids::uuid &vm_uuid,
                       std::set<boost::uuids::uuid> &vmi_uuid_set) const;
    const VmiEntry *VmiToEntry(const boost::uuids::uuid &vmi_uuid) const;

private:
    void UpdateVmiIfnodeInfo(const boost::uuids::uuid &vmi_uuid,
                             const VmInterfaceConfigData *data);
    void DeleteVmiIfnodeInfo(const boost::uuids::uuid &vmi_uuid);
private:
    friend class SandeshVmiPortSubscribeTask;
    friend class SandeshVmVnPortSubscribeTask;
    friend class SandeshVmiToVmVnTask;
    friend class SandeshVmVnToVmiTask;

    Agent *agent_;
    InterfaceTable *interface_table_;
    VNController *controller_;
    mutable tbb::mutex mutex_;
    VmiTree vmi_tree_;
    IFMapAgentTable *vmi_config_table_;
    DBTableBase::ListenerId vmi_config_listener_id_;
    UuidToIFNodeTree uuid_ifnode_tree_;
    VmiToVmVnTree vmi_to_vmvn_tree_;
    VmVnToVmiTree vmvn_to_vmi_tree_;
    VmVnTree vmvn_subscribe_tree_;
    DISALLOW_COPY_AND_ASSIGN(PortSubscribeTable);
};

#endif //  _VNSW_AGENT_PORT_IPC_PORT_SUBSCRIBE_TABLE_H_
