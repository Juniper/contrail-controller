/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vm_interface_hpp
#define vnsw_agent_vm_interface_hpp

#include <oper/oper_dhcp_options.h>
#include <oper/audit_list.h>
#include <oper/ecmp_load_balance.h>
/////////////////////////////////////////////////////////////////////////////
// VmInterface is implementation of VM Port interfaces
//
// All modification to an VmInterface is done only thru the DBTable Request
// handling.
//
// The DBTable request can be generated from multiple events.
// Example : Config update, link-state change, port-ipc message etc.
//
// Only two types of DBTable request can create/delete a VmInterface
// - VmInterfaceConfigData :
//   This request type is generated from config processing
//   Request of this type of mandatory for all interfaces
// - VmInterfaceNovaData :
//   This request type is generated from port-ipc messages to agent
//   Request of this type are optional
//
// An VmInterface can be of many types. Different type of VmInterfaces with
// corresponding values of DeviceType and VmiType are given below,
//
// DeviceType = VM_ON_TAP, VmiType = INSTANCE
//   Typically represents an interface inside a VM/Container.
//   Examples:QEMU VM, Containers, Service-Chain interface etc.
//   It will have a TAP interface associated with it. The interface be
//   created only by port-ipc message. The config message only updates the
//   attributes and does not create interface of this type
//
// DeviceType = VM_PHYSICAL_VLAN, VmiType = INTANCE
//   This configuration is used in case of VmWare ESXi.
//   Each ESXi VM is assigned a VLAN (by nova-compute). The compute node has
//   an uplink port that is member of all VLANs. Agent/VRouter can identify
//   VmInterface by tuple <uplink-port, vlan>. The uplink port is seen as a
//   phyiscal port on compute node
//   The agent->hypervisor_mode_ is also set to ESXi in this case
//
// DeviceType = VM_PHYSICAL_MAC, VmiType = INTANCE and
//   This configuration is used in VmWare VCenter
//   VCenter:
//     Contrail uses distributed vswitch to support VCenter. Each VN is
//     allocated a PVLAN. The port on compute node is member of primary-vlan
//     and VMs are member of secondary-vlans. Packets from all VMs are
//     received on primary-vlan on compute node, hence they are classified
//     using source-mac address (which is unique to every VM)
//     agent->hypervisor_mode_ is set to VCENTER
// DeviceType = LOCAL_DEVICE, VmiType = GATEWAY
//   This configuraiton is used in case of Gateway interfaces connected locally
//
// DeviceType = REMOTE_VM_VLAN_ON_VMI, VmiType = REMOTE_VM
//   This configuration is used to model an end-point connected on local
//   interface
//   The VmInterface is classified based on vlan-tag of packet
//
// DeviceType = TOR, VmiType = BAREMETAL
//   This configuration is used to model ToR ports managed with OVS
//
// DeviceType = VM_VLAN_ON_VMI, VmiType = INSTANCE
//   This configuration is used to model vlan sub-interfaces and
//   Nested Containers (both K8S and Mesos)
//   Sub-Interfaces:
//     Typically, the sub-interfaces are created over other VmInterfaces with
//     DeviceType = VM_ON_TAP
//     The VmInterface is classified based on vlan-tag
//
//   Nested Containers:
//     In case of nested-containers, the first level Container ports are
//     created as regular tap-interfaces. The second level containers are
//     created as MAC-VLAN ports. Since all second level containers share same
//     tap-interface, they are classified using vlan-tag
//
// DBRequest Handling
// ------------------
// All DBRequest are handed off to VmInterface module via methods
// VmInterface::Add() and VmInterface::Delete(). VmInterface modules processes
// request in following stages,
// 1. Config Processing
//    This stage is responsible to update VmInterface fields with latest
//    information according. The fields updated will depend on type of
//    request.
//
//    Every DBRequest type implements a method "OnResync". This method must
//    update VmInterface fields according to the request
//    The same OnResync() is called for both "Add", "Change" and "Resync"
//    operation
//
// 2. Apply Config
//    This stage is responsible to generated other oper-db states including
//    MPLS Labels, NextHops, Routes etc... based on the latest configuration
//    It must also take care of removing derived states based on old-state
//
//    VmInterfaceState() class is responsible to manage derived states
//    such as routes, nexthops, labels etc...).
//    old states and create new states based on latest interface configuration
/////////////////////////////////////////////////////////////////////////////

typedef std::vector<boost::uuids::uuid> SgUuidList;
typedef std::vector<SgEntryRef> SgList;
typedef std::vector<AclDBEntryConstRef> FirewallPolicyList;
struct VmInterfaceData;
struct VmInterfaceConfigData;
struct VmInterfaceNovaData;
struct VmInterfaceIpAddressData;
struct VmInterfaceOsOperStateData;
struct VmInterfaceMirrorData;
class OperDhcpOptions;
class PathPreference;
class MetaDataIp;
class HealthCheckInstanceBase;

class LocalVmPortPeer;
class VmInterface;

/////////////////////////////////////////////////////////////////////////////
// VmInterfaceState manages dervied states from VmInterface. On any config
// change to interface, this VmInterfaceState gets invoked after modifying
// VmInterface with latest configuration. Each VmInterfaceState must be
// self-contained and idempotent. The class must store old variables it needs.
//
// There are 2 type of states managed by the class L2-State (EVPN-Route,
// L2-NextHop etc..) and L3-State(inet-route, l3-nexthop etc...). The class
// assumes each both L2 and L3 states are managed by each attribute. However,
// one of them can be dummy as necessary
//
// The class supports 3 different operations,
// - ADD : Must Add state resulting from this attribute
// - DEL : Must Delete state resulting from this attribute
// - DEL_ADD :
//   DEL_ADD operation results if there is change in key for the state
//   resulting from the attribute. In this case, the old state must be
//   Delete and then followed by Addd of state
//
// Guildelines for GetOpL2 and GetOpL3:
// - Compute DEL cases
// - Compute DEL_ADD cases next
// - Compute ADD cases last
// - Return INVALID to ignore the operation
//
// The DEL operation must be based on latest configuration on interface
// The ADD operation must be based on latest configuration on interface
// The DEL_ADD operation must be based on old values stored in the class and
// latest value in the interface
/////////////////////////////////////////////////////////////////////////////
struct VmInterfaceState {
    // Keep order of Operation in enum sorted. RecomputeOp relies on it
    enum Op {
        INVALID,
        ADD,
        DEL_ADD,
        DEL
    };

    explicit VmInterfaceState() :
        l2_installed_(false), l3_installed_(false) {
    }
    VmInterfaceState(bool l2_installed, bool l3_installed) :
        l2_installed_(l2_installed), l3_installed_(l3_installed) {
    }
    virtual ~VmInterfaceState() {
    }

    bool Installed() const {
        return l3_installed_ || l2_installed_;
    }

    virtual bool Update(const Agent *agent, VmInterface *vmi,
                        Op l2_force_op, Op l3_force_op) const;

    // Update Operation. In cases where operations are computed in stages,
    // computes operation based on old value and new value
    static Op RecomputeOp(Op old_op, Op new_op);

    // Get operation for Layer-2 state. Generally,
    // ADD/DEL operaton are based on current state of VmInterface
    // ADD_DEL operation is based on old-value and new value in VmInterface
    virtual Op GetOpL2(const Agent *agent, const VmInterface *vmi) const {
        return INVALID;
    }

    // Get operation for Layer-3 state. Generally,
    // ADD/DEL operaton are based on current state of VmInterface
    // ADD_DEL operation is based on old-value and new value in VmInterface
    virtual Op GetOpL3(const Agent *agent, const VmInterface *vmi) const {
        return INVALID;
    }

    // Copy attributes from VmInterface to local copy
    virtual void Copy(const Agent *agent, const VmInterface *vmi) const {
        return;
    }

    virtual bool AddL2(const Agent *agent, VmInterface *vmi) const {
        assert(0);
        return false;
    }

    virtual bool DeleteL2(const Agent *agent, VmInterface *vmi) const {
        assert(0);
        return false;
    }

    virtual bool AddL3(const Agent *agent, VmInterface *vmi) const {
        assert(0);
        return false;
    }

    virtual bool DeleteL3(const Agent *agent, VmInterface *vmi) const {
        assert(0);
        return false;
    }

    mutable bool l2_installed_;
    mutable bool l3_installed_;
};

struct MacVmBindingState : public VmInterfaceState {
    MacVmBindingState();
    virtual ~MacVmBindingState();

    VmInterfaceState::Op GetOpL3(const Agent *agent,
                                 const VmInterface *vmi) const;
    bool DeleteL3(const Agent *agent, VmInterface *vmi) const;
    void Copy(const Agent *agent, const VmInterface *vmi) const;
    bool AddL3(const Agent *agent, VmInterface *vmi) const;

    mutable const VrfEntry *vrf_;
    mutable bool dhcp_enabled_;
};

struct VrfTableLabelState : public VmInterfaceState {
    VrfTableLabelState();
    virtual ~VrfTableLabelState();

    VmInterfaceState::Op GetOpL3(const Agent *agent,
                                 const VmInterface *vmi) const;
    bool AddL3(const Agent *agent, VmInterface *vmi) const;
};

struct NextHopState : public VmInterfaceState {
    NextHopState();
    virtual ~NextHopState();

    VmInterfaceState::Op GetOpL2(const Agent *agent,
                                 const VmInterface *vmi) const;
    bool DeleteL2(const Agent *agent, VmInterface *vmi) const;
    bool AddL2(const Agent *agent, VmInterface *vmi) const;

    VmInterfaceState::Op GetOpL3(const Agent *agent,
                                 const VmInterface *vmi) const;
    bool DeleteL3(const Agent *agent, VmInterface *vmi) const;
    bool AddL3(const Agent *agent, VmInterface *vmi) const;

    uint32_t l2_label() const { return l2_label_; }
    uint32_t l3_label() const { return l3_label_; }

    mutable NextHopRef l2_nh_policy_;
    mutable NextHopRef l2_nh_no_policy_;
    mutable uint32_t l2_label_;

    mutable NextHopRef l3_nh_policy_;
    mutable NextHopRef l3_nh_no_policy_;
    mutable uint32_t l3_label_;

    mutable NextHopRef l3_mcast_nh_no_policy_;

    mutable NextHopRef receive_nh_;
};

struct MetaDataIpState : public VmInterfaceState {
    MetaDataIpState();
    virtual ~MetaDataIpState();

    VmInterfaceState::Op GetOpL3(const Agent *agent,
                                 const VmInterface *vmi) const;
    bool DeleteL3(const Agent *agent, VmInterface *vmi) const;
    bool AddL3(const Agent *agent, VmInterface *vmi) const;

    mutable std::auto_ptr<MetaDataIp> mdata_ip_;
};

struct ResolveRouteState : public VmInterfaceState {
    ResolveRouteState();
    virtual ~ResolveRouteState();

    VmInterfaceState::Op GetOpL2(const Agent *agent,
                                 const VmInterface *vmi) const;
    bool DeleteL2(const Agent *agent, VmInterface *vmi) const;
    bool AddL2(const Agent *agent, VmInterface *vmi) const;
    VmInterfaceState::Op GetOpL3(const Agent *agent,
                                 const VmInterface *vmi) const;
    bool DeleteL3(const Agent *agent, VmInterface *vmi) const;
    bool AddL3(const Agent *agent, VmInterface *vmi) const;
    void Copy(const Agent *agent, const VmInterface *vmi) const;

    mutable const VrfEntry *vrf_;
    mutable Ip4Address subnet_;
    mutable uint8_t plen_;
};

struct VmiRouteState : public VmInterfaceState {
    VmiRouteState();
    virtual ~VmiRouteState();

    VmInterfaceState::Op GetOpL2(const Agent *agent,
                                 const VmInterface *vmi) const;
    bool DeleteL2(const Agent *agent, VmInterface *vmi) const;
    bool AddL2(const Agent *agent, VmInterface *vmi) const;
    VmInterfaceState::Op GetOpL3(const Agent *agent,
                                 const VmInterface *vmi) const;
    bool DeleteL3(const Agent *agent, VmInterface *vmi) const;
    bool AddL3(const Agent *agent, VmInterface *vmi) const;
    void Copy(const Agent *agent, const VmInterface *vmi) const;

    mutable const VrfEntry *vrf_;
    mutable Ip4Address ip_;
    mutable uint32_t ethernet_tag_;
    mutable bool do_dhcp_relay_;
};

/////////////////////////////////////////////////////////////////////////////
// Definition for VmInterface
// Agent supports multiple type of VMInterfaces
// - VMI for a virtual-machine spawned on KVM compute node. It will have a TAP
//   interface associated with it. Agent also expects a INSTANCE_MSG from
//    nova-compute/equivalent to add this port
//    DeviceType = VM_ON_TAP, VmiType = INSTANCE
// - VMI for a virtual-machine spawned on VMware ESXi. All virtual-machines
//   are connected on a physical-port and VLAN is used to distinguish them
//   DeviceType = VM_PHYSICAL_VLAN, VmiType = INTANCE, agent->hypervisor_mode = ESXi
// - VMI for a virtual-machine spawned on VMWare VCenter . All virtual-machines
//   are connected on a physical-port and they are classified by the smac
//   DeviceType = VM_PHYSICAL_MAC, VmiType = INTANCE and
//   agent hypervisor mode = VCenter
// - VMI for service-chain virtual-machines
// - VMI for service-chain virtual-machine spawned on KVM compute node.
//   It will have a TAP interface associated with it. Agent also expects a
//   INSTANCE_MSG from interface associated with it.
//   DeviceType = VM_ON_TAP, VmiType = SERVICE_CHAIN
// - VMI for service-instances spawned by agent itself.
//   It will have a TAP interface associated with it. Agent also expects a
//   INSTANCE_MSG from interface associated with it.
//   DeviceType = VM, VmiType = SERVICE_INTANCE
//
/////////////////////////////////////////////////////////////////////////////
class VmInterface : public Interface {
public:
    static const uint32_t kInvalidVlanId = 0xFFFF;
    static const uint32_t kInvalidPmdId = 0xFFFF;
    static const uint32_t kInvalidIsid = 0xFFFFFF;
    static const uint8_t vHostUserClient = 0;
    static const uint8_t vHostUserServer = 1;

    // Interface route type
    static const char *kInterface;
    static const char *kServiceInterface;
    static const char *kInterfaceStatic;

    enum Configurer {
        INSTANCE_MSG,
        CONFIG
    };

    // Type of VMI Port
    enum DeviceType {
        DEVICE_TYPE_INVALID,
        VM_ON_TAP,          // VMI on TAP/physial port interface
                            // VMI is created based on the INSTANCE_MSG
        VM_VLAN_ON_VMI,     // VMI on TAP port with VLAN as classifier
                            // VMI is created based on config message
        VM_PHYSICAL_VLAN,   // VMI classified with VLAN on a physical-port
                            // (used in VMWare ESXi)
                            // VMI is created based on the INSTANCE_MSG
        VM_PHYSICAL_MAC,    // VMI classified with MAC on a physical-port
                            // (used in VMWare VCenter)
                            // VMI is created based on the INSTANCE_MSG
        TOR,                // Baremetal connected to ToR
        LOCAL_DEVICE,       // VMI on a local port. Used in GATEWAY
        REMOTE_VM_VLAN_ON_VMI, // VMI on a local phy-port with VLAN as classifier
        VM_SRIOV,           // VMI on an SRIOV VM
        VMI_ON_LR           // VMI configured on logical-router
    };

    // Type of VM on the VMI
    enum VmiType {
        VMI_TYPE_INVALID,
        INSTANCE,
        SERVICE_CHAIN,
        SERVICE_INSTANCE,
        BAREMETAL,
        GATEWAY,
        REMOTE_VM,
        SRIOV,
        VHOST,
        ROUTER
    };

    // Interface uses different type of labels. Enumeration of different
    // types is given below
    enum LabelType {
        LABEL_TYPE_INVALID = 0,
        LABEL_TYPE_L2,
        LABEL_TYPE_L3,
        LABEL_TYPE_AAP,
        LABEL_TYPE_SERVICE_VLAN,
        LABEL_TYPE_MAX
    };

    enum ProxyArpMode {
        PROXY_ARP_NONE,
        PROXY_ARP_UNRESTRICTED,
        PROXY_ARP_INVALID
    };

    /*
     * NOTE: This value should be in sync with vrouter code.
     *       THe order of elements is important since it is used
     *       while searching the rules in the set in agent.
     */
    enum FatFlowIgnoreAddressType {
        IGNORE_SOURCE = 1,
        IGNORE_ADDRESS_MIN_VAL = IGNORE_SOURCE,
        IGNORE_DESTINATION,
        IGNORE_NONE,
        IGNORE_ADDRESS_MAX_VAL = IGNORE_NONE,
    };

    /*
     * NOTE: This value should be in sync with vrouter code.
     *       THe order of elements is important since it is used
     *       while searching the rules in the set in agent.
     */
    enum FatFlowPrefixAggregateType {
        AGGREGATE_NONE = 0,
        AGGREGATE_PREFIX_MIN_VAL = AGGREGATE_NONE,
        AGGREGATE_DST_IPV6,
        AGGREGATE_SRC_IPV6,
        AGGREGATE_SRC_DST_IPV6,
        AGGREGATE_DST_IPV4,
        AGGREGATE_SRC_IPV4,
        AGGREGATE_SRC_DST_IPV4,
        AGGREGATE_PREFIX_MAX_VAL = AGGREGATE_SRC_DST_IPV4,
    };

    enum HbsIntfType {
        HBS_INTF_INVALID = 0,
        HBS_INTF_LEFT,
        HBS_INTF_RIGHT,
        HBS_INTF_MGMT,
    };

    typedef std::map<Ip4Address, MetaDataIp*> MetaDataIpMap;
    typedef std::set<HealthCheckInstanceBase *> HealthCheckInstanceSet;

    struct List {
    };

    struct ListEntry {
        ListEntry() : del_pending_(false) { }
        ListEntry(bool del_pending) : del_pending_(del_pending) { }
        virtual ~ListEntry()  {}

        bool del_pending() const { return del_pending_; }
        void set_del_pending(bool val) const { del_pending_ = val; }

        VmInterfaceState::Op GetOp(VmInterfaceState::Op op) const;
        mutable bool del_pending_;
    };

    struct FloatingIpList;
    // A unified structure for storing FloatingIp information for both
    // operational and config elements
    struct FloatingIp : public ListEntry, VmInterfaceState {
        enum Direction {
            DIRECTION_BOTH,
            DIRECTION_INGRESS,
            DIRECTION_EGRESS,
            DIRECTION_INVALID
        };

        struct PortMapKey {
            uint8_t protocol_;
            uint16_t port_;
            PortMapKey(): protocol_(0), port_(0) { }
            PortMapKey(uint8_t protocol, uint16_t port) :
                protocol_(protocol), port_(port) { }
            ~PortMapKey() { }

            bool operator()(const PortMapKey &lhs, const PortMapKey &rhs) const {
                if (lhs.protocol_ != rhs.protocol_)
                    return lhs.protocol_ < rhs.protocol_;
                return lhs.port_ < rhs.port_;
            }
        };

        typedef std::map<PortMapKey, uint16_t, PortMapKey> PortMap;
        typedef PortMap::iterator PortMapIterator;
        FloatingIp();
        FloatingIp(const FloatingIp &rhs);
        FloatingIp(const IpAddress &addr, const std::string &vrf,
                   const boost::uuids::uuid &vn_uuid, const IpAddress &ip,
                   Direction direction, bool port_mappng_enabled,
                   const PortMap &src_port_map, const PortMap &dst_port_map,
                   bool port_nat);
        virtual ~FloatingIp();

        bool operator() (const FloatingIp &lhs, const FloatingIp &rhs) const;
        bool IsLess(const FloatingIp *rhs) const;

        bool port_map_enabled() const;
        Direction direction() const { return direction_; }

        const IpAddress GetFixedIp(const VmInterface *) const;
        uint32_t PortMappingSize() const;
        int32_t GetSrcPortMap(uint8_t protocol, uint16_t src_port) const;
        int32_t GetDstPortMap(uint8_t protocol, uint16_t dst_port) const;
        // direction_ is based on packet direction. Allow DNAT if direction is
        // "both or ingress"
        bool AllowDNat() const {
            return (direction_ == DIRECTION_BOTH ||
                    direction_ == DIRECTION_INGRESS);
        }
        // direction_ is based on packet direction. Allow SNAT if direction is
        // "both or egress"
        bool AllowSNat() const {
            return (direction_ == DIRECTION_BOTH ||
                    direction_ == DIRECTION_EGRESS);
        }

        void Copy(const Agent *agent, const VmInterface *vmi) const;
        VmInterfaceState::Op GetOpL3(const Agent *agent,
                                     const VmInterface *vmi) const;
        bool AddL3(const Agent *agent, VmInterface *vmi) const;
        bool DeleteL3(const Agent *agent, VmInterface *vmi) const;
        VmInterfaceState::Op GetOpL2(const Agent *agent,
                                     const VmInterface *vmi) const;
        bool AddL2(const Agent *agent, VmInterface *vmi) const;
        bool DeleteL2(const Agent *agent, VmInterface *vmi) const;
        bool port_nat() const;

        IpAddress floating_ip_;
        mutable VnEntryRef vn_;
        mutable VrfEntryRef vrf_;
        std::string vrf_name_;
        boost::uuids::uuid vn_uuid_;
        mutable IpAddress fixed_ip_;
        mutable Direction direction_;
        mutable bool port_map_enabled_;
        mutable PortMap src_port_map_;
        mutable PortMap dst_port_map_;
        mutable uint32_t ethernet_tag_;
        mutable bool port_nat_;
    };
    typedef std::set<FloatingIp, FloatingIp> FloatingIpSet;

    struct FloatingIpList : public List {
        FloatingIpList() :
            List(), v4_count_(0), v6_count_(0), list_() {
        }
        ~FloatingIpList() { }

        void Insert(const FloatingIp *rhs);
        void Update(const FloatingIp *lhs, const FloatingIp *rhs);
        void Remove(FloatingIpSet::iterator &it);
        bool UpdateList(const Agent *agent, VmInterface *vmi,
                        VmInterfaceState::Op l2_force_op,
                        VmInterfaceState::Op l3_force_op);

        uint16_t v4_count_;
        uint16_t v6_count_;
        FloatingIpSet list_;
    };

    // A unified structure for storing AliasIp information for both
    // operational and config elements
    struct AliasIpList;
    struct AliasIp : public ListEntry, VmInterfaceState {
        AliasIp();
        AliasIp(const AliasIp &rhs);
        AliasIp(const IpAddress &addr, const std::string &vrf,
                const boost::uuids::uuid &vn_uuid);
        virtual ~AliasIp();

        bool operator() (const AliasIp &lhs, const AliasIp &rhs) const;
        bool IsLess(const AliasIp *rhs) const;

        VmInterfaceState::Op GetOpL3(const Agent *agent,
                                     const VmInterface *vmi) const;
        bool AddL3(const Agent *agent, VmInterface *vmi) const;
        bool DeleteL3(const Agent *agent, VmInterface *vmi) const;
        void Copy(const Agent *agent, const VmInterface *vmi) const;

        IpAddress alias_ip_;
        mutable VnEntryRef vn_;
        mutable VrfEntryRef vrf_;
        std::string vrf_name_;
        boost::uuids::uuid vn_uuid_;
    };
    typedef std::set<AliasIp, AliasIp> AliasIpSet;

    struct AliasIpList : public List {
        AliasIpList() : v4_count_(0), v6_count_(0), list_() { }
        ~AliasIpList() { }

        void Insert(const AliasIp *rhs);
        void Update(const AliasIp *lhs, const AliasIp *rhs);
        void Remove(AliasIpSet::iterator &it);
        bool UpdateList(const Agent *agent, VmInterface *vmi,
                        VmInterfaceState::Op l2_force_op,
                        VmInterfaceState::Op l3_force_op);

        uint16_t v4_count_;
        uint16_t v6_count_;
        AliasIpSet list_;
    };

    struct ServiceVlan : ListEntry {
        ServiceVlan();
        ServiceVlan(const ServiceVlan &rhs);
        ServiceVlan(uint16_t tag, const std::string &vrf_name,
                    const Ip4Address &addr, const Ip6Address &addr6,
                    const MacAddress &smac, const MacAddress &dmac);
        virtual ~ServiceVlan();

        bool operator() (const ServiceVlan &lhs, const ServiceVlan &rhs) const;
        bool IsLess(const ServiceVlan *rhs) const;
        void Update(const Agent *agent, VmInterface *vmi) const;
        void DeleteCommon(const VmInterface *vmi) const;
        void AddCommon(const Agent *agent, const VmInterface *vmi) const;

        void Copy(const Agent *agent, const VmInterface *vmi) const;
        VmInterfaceState::Op GetOpL3(const Agent *agent,
                                     const VmInterface *vmi) const;
        bool AddL3(const Agent *agent, VmInterface *vmi) const;
        bool DeleteL3(const Agent *agent, VmInterface *vmi) const;

        uint16_t tag_;
        mutable std::string vrf_name_;
        mutable Ip4Address addr_;
        mutable Ip4Address old_addr_;
        mutable Ip6Address addr6_;
        mutable Ip6Address old_addr6_;
        mutable MacAddress smac_;
        mutable MacAddress dmac_;
        mutable VrfEntryRef vrf_;
        mutable uint32_t label_;
        mutable bool v4_rt_installed_;
        mutable bool v6_rt_installed_;
        mutable bool del_add_;
    };
    typedef std::set<ServiceVlan, ServiceVlan> ServiceVlanSet;

    struct ServiceVlanList : List {
        ServiceVlanList() : List(), list_() { }
        ~ServiceVlanList() { }
        void Insert(const ServiceVlan *rhs);
        void Update(const ServiceVlan *lhs, const ServiceVlan *rhs);
        void Remove(ServiceVlanSet::iterator &it);
        bool UpdateList(const Agent *agent, VmInterface *vmi,
                        VmInterfaceState::Op l2_force_op,
                        VmInterfaceState::Op l3_force_op);

        ServiceVlanSet list_;
    };

    struct StaticRoute : ListEntry, VmInterfaceState {
        StaticRoute();
        StaticRoute(const StaticRoute &rhs);
        StaticRoute(const IpAddress &addr, uint32_t plen, const IpAddress &gw,
                    const CommunityList &communities);
        virtual ~StaticRoute();

        bool operator() (const StaticRoute &lhs, const StaticRoute &rhs) const;
        bool IsLess(const StaticRoute *rhs) const;

        void Copy(const Agent *agent, const VmInterface *vmi) const;
        VmInterfaceState::Op GetOpL3(const Agent *agent,
                                     const VmInterface *vmi) const;
        bool AddL3(const Agent *agent, VmInterface *vmi) const;
        bool DeleteL3(const Agent *agent, VmInterface *vmi) const;

        mutable const VrfEntry *vrf_;
        IpAddress  addr_;
        uint32_t    plen_;
        IpAddress  gw_;
        CommunityList communities_;
    };
    typedef std::set<StaticRoute, StaticRoute> StaticRouteSet;

    struct StaticRouteList : List {
        StaticRouteList() : List(), list_() { }
        ~StaticRouteList() { }
        void Insert(const StaticRoute *rhs);
        void Update(const StaticRoute *lhs, const StaticRoute *rhs);
        void Remove(StaticRouteSet::iterator &it);

        bool UpdateList(const Agent *agent, VmInterface *vmi,
                        VmInterfaceState::Op l2_force_op,
                        VmInterfaceState::Op l3_force_op);
        StaticRouteSet list_;
    };

    struct AllowedAddressPair : ListEntry, VmInterfaceState {
        AllowedAddressPair();
        AllowedAddressPair(const AllowedAddressPair &rhs);
        AllowedAddressPair(const IpAddress &addr, uint32_t plen, bool ecmp,
                           const MacAddress &mac);
        virtual ~AllowedAddressPair();

        bool operator() (const AllowedAddressPair &lhs,
                         const AllowedAddressPair &rhs) const;
        bool operator == (const  AllowedAddressPair &rhs) const {
            return ((mac_ == rhs.mac_) && (addr_ == rhs.addr_) &&
                    (plen_ == rhs.plen_));
        }
        bool IsLess(const AllowedAddressPair *rhs) const;

        void Copy(const Agent *agent, const VmInterface *vmi) const;
        VmInterfaceState::Op GetOpL3(const Agent *agent,
                                     const VmInterface *vmi) const;
        bool AddL3(const Agent *agent, VmInterface *vmi) const;
        bool DeleteL3(const Agent *agent, VmInterface *vmi) const;
        VmInterfaceState::Op GetOpL2(const Agent *agent,
                                     const VmInterface *vmi) const;
        bool AddL2(const Agent *agent, VmInterface *vmi) const;
        bool DeleteL2(const Agent *agent, VmInterface *vmi) const;

        IpAddress   addr_;
        uint32_t    plen_;
        mutable bool ecmp_;
        MacAddress  mac_;
        mutable bool        ecmp_config_changed_;
        mutable IpAddress  service_ip_;
        mutable uint32_t label_;
        mutable NextHopRef policy_enabled_nh_;
        mutable NextHopRef policy_disabled_nh_;
        mutable VrfEntry *vrf_;
        mutable uint32_t ethernet_tag_;
    };
    typedef std::set<AllowedAddressPair, AllowedAddressPair>
        AllowedAddressPairSet;

    struct AllowedAddressPairList : public List {
        AllowedAddressPairList() : List(), list_() { }
        ~AllowedAddressPairList() { }
        void Insert(const AllowedAddressPair *rhs);
        void Update(const AllowedAddressPair *lhs,
                    const AllowedAddressPair *rhs);
        void Remove(AllowedAddressPairSet::iterator &it);

        bool UpdateList(const Agent *agent, VmInterface *vmi,
                        VmInterfaceState::Op l2_force_op,
                        VmInterfaceState::Op l3_force_op);

        AllowedAddressPairSet list_;
    };

    struct SecurityGroupEntry : ListEntry, VmInterfaceState {
        SecurityGroupEntry();
        SecurityGroupEntry(const SecurityGroupEntry &rhs);
        SecurityGroupEntry(const boost::uuids::uuid &uuid);
        virtual ~SecurityGroupEntry();

        bool operator == (const SecurityGroupEntry &rhs) const;
        bool operator() (const SecurityGroupEntry &lhs,
                         const SecurityGroupEntry &rhs) const;
        bool IsLess(const SecurityGroupEntry *rhs) const;

        VmInterfaceState::Op GetOpL3(const Agent *agent,
                                     const VmInterface *vmi) const;
        bool AddL3(const Agent *agent, VmInterface *vmi) const;
        bool DeleteL3(const Agent *agent, VmInterface *vmi) const;

        mutable SgEntryRef sg_;
        boost::uuids::uuid uuid_;
    };
    typedef std::set<SecurityGroupEntry, SecurityGroupEntry>
        SecurityGroupEntrySet;
    typedef std::vector<boost::uuids::uuid> SecurityGroupUuidList;

    struct SecurityGroupEntryList {
        SecurityGroupEntryList() : list_() { }
        ~SecurityGroupEntryList() { }

        void Insert(const SecurityGroupEntry *rhs);
        void Update(const SecurityGroupEntry *lhs,
                    const SecurityGroupEntry *rhs);
        void Remove(SecurityGroupEntrySet::iterator &it);
        bool UpdateList(const Agent *agent, VmInterface *vmi,
                        VmInterfaceState::Op l2_force_op,
                        VmInterfaceState::Op l3_force_op);

        SecurityGroupEntrySet list_;
    };

    struct TagEntry : ListEntry, VmInterfaceState {
        TagEntry();
        TagEntry(const TagEntry &rhs);
        TagEntry(uint32_t tag_type, const boost::uuids::uuid &uuid);
        virtual ~TagEntry();

        bool operator == (const TagEntry &rhs) const;
        bool operator() (const TagEntry &lhs, const TagEntry &rhs) const;
        bool IsLess(const TagEntry *rhs) const;

        VmInterfaceState::Op GetOpL3(const Agent *agent,
                                     const VmInterface *vmi) const;
        bool AddL3(const Agent *agent, VmInterface *vmi) const;
        bool DeleteL3(const Agent *agent, VmInterface *vmi) const;

        mutable TagEntryRef tag_;
        uint32_t type_;
        mutable boost::uuids::uuid uuid_;
    };
    typedef std::set<TagEntry, TagEntry> TagEntrySet;
    typedef std::vector<boost::uuids::uuid> TagGroupUuidList;

    struct TagEntryList {
        TagEntryList() : list_() { }
        ~TagEntryList() { }

        void Insert(const TagEntry *rhs);
        void Update(const TagEntry *lhs,
                    const TagEntry *rhs);
        void Remove(TagEntrySet::iterator &it);
        bool UpdateList(const Agent *agent, VmInterface *vmi,
                        VmInterfaceState::Op l2_force_op,
                        VmInterfaceState::Op l3_force_op);

        TagEntrySet list_;
    };

    struct VrfAssignRule : ListEntry {
        VrfAssignRule();
        VrfAssignRule(const VrfAssignRule &rhs);
        VrfAssignRule(uint32_t id,
                      const autogen::MatchConditionType &match_condition_,
                      const std::string &vrf_name, bool ignore_acl);
        ~VrfAssignRule();
        bool operator == (const VrfAssignRule &rhs) const;
        bool operator() (const VrfAssignRule &lhs,
                         const VrfAssignRule &rhs) const;
        bool IsLess(const VrfAssignRule *rhs) const;
        void Update(const Agent *agent, VmInterface *vmi);

        const uint32_t id_;
        mutable std::string vrf_name_;
        mutable bool ignore_acl_;
        mutable autogen::MatchConditionType match_condition_;
    };
    typedef std::set<VrfAssignRule, VrfAssignRule> VrfAssignRuleSet;

    struct VrfAssignRuleList : public List {
        VrfAssignRuleList() :
            List(), list_(), vrf_assign_acl_(NULL) {
        }
        ~VrfAssignRuleList() { }
        void Insert(const VrfAssignRule *rhs);
        void Update(const VrfAssignRule *lhs, const VrfAssignRule *rhs);
        void Remove(VrfAssignRuleSet::iterator &it);
        bool UpdateList(const Agent *agent, VmInterface *vmi,
                        VmInterfaceState::Op l2_force_op,
                        VmInterfaceState::Op l3_force_op);

        VrfAssignRuleSet list_;
        AclDBEntryRef vrf_assign_acl_;
    };

    struct InstanceIpList;
    struct InstanceIp : ListEntry, VmInterfaceState {
        InstanceIp();
        InstanceIp(const InstanceIp &rhs);
        InstanceIp(const IpAddress &ip, uint8_t plen, bool ecmp,
                   bool is_primary, bool is_service_ip,
                   bool is_service_health_check_ip,
                   bool is_local, const IpAddress &tracking_ip);
        ~InstanceIp();
        bool operator == (const InstanceIp &rhs) const;
        bool operator() (const InstanceIp &lhs,
                         const InstanceIp &rhs) const;
        InstanceIp operator = (const InstanceIp &rhs) const {
            InstanceIp ret(rhs);
            return ret;
        }
        bool IsLess(const InstanceIp *rhs) const;

        void Update(const Agent *agent, VmInterface *vmi,
                    const VmInterface::InstanceIpList *list) const;
        void SetPrefixForAllocUnitIpam(VmInterface *intrface) const;

        VmInterfaceState::Op GetOpL3(const Agent *agent,
                                     const VmInterface *vmi) const;
        bool AddL3(const Agent *agent, VmInterface *vmi) const;
        bool DeleteL3(const Agent *agent, VmInterface *vmi) const;
        VmInterfaceState::Op GetOpL2(const Agent *agent,
                                     const VmInterface *vmi) const;
        bool AddL2(const Agent *agent, VmInterface *vmi) const;
        bool DeleteL2(const Agent *agent, VmInterface *vmi) const;
        void Copy(const Agent *agent, const VmInterface *vmi) const;

        bool is_force_policy() const {
            return is_service_health_check_ip_;
        }

        bool IsL3Only() const {
            return is_service_health_check_ip_;
        }

        const IpAddress ip_;
        mutable uint8_t plen_;
        mutable bool ecmp_;
        mutable bool is_primary_;
        mutable bool is_service_ip_;  // used for service chain nexthop
        mutable bool is_service_health_check_ip_;
        mutable bool is_local_;
        mutable IpAddress tracking_ip_;
        mutable const VrfEntry *vrf_;
        mutable uint32_t ethernet_tag_;
    };
    typedef std::set<InstanceIp, InstanceIp> InstanceIpSet;

    struct InstanceIpList : public List {
        InstanceIpList(bool is_ipv4) :
            List(), is_ipv4_(is_ipv4), list_() {
        }
        ~InstanceIpList() { }
        void Insert(const InstanceIp *rhs);
        void Update(const InstanceIp *lhs, const InstanceIp *rhs);
        void Remove(InstanceIpSet::iterator &it);

        virtual bool UpdateList(const Agent *agent, VmInterface *vmi,
                                VmInterfaceState::Op l2_force_op,
                                VmInterfaceState::Op l3_force_op);

        bool is_ipv4_;
        InstanceIpSet list_;
    };

    typedef std::map<std::string, FatFlowIgnoreAddressType> IgnoreAddressMap;

    struct FatFlowEntry : ListEntry {

#define FAT_FLOW_ENTRY_MIN_PREFIX_LEN  8

        FatFlowEntry(): protocol(0), port(0),
            ignore_address(IGNORE_NONE), prefix_aggregate(AGGREGATE_NONE),
            src_prefix(), src_prefix_mask(0), src_aggregate_plen(0),
            dst_prefix(), dst_prefix_mask(0), dst_aggregate_plen(0) {}

        FatFlowEntry(const FatFlowEntry &rhs):
            protocol(rhs.protocol), port(rhs.port),
            ignore_address(rhs.ignore_address), prefix_aggregate(rhs.prefix_aggregate),
            src_prefix(rhs.src_prefix), src_prefix_mask(rhs.src_prefix_mask), src_aggregate_plen(rhs.src_aggregate_plen),
            dst_prefix(rhs.dst_prefix), dst_prefix_mask(rhs.dst_prefix_mask), dst_aggregate_plen(rhs.dst_aggregate_plen) {}

        FatFlowEntry(const uint8_t proto, const uint16_t p) :
                protocol(proto), port(p),
                ignore_address(IGNORE_NONE),
                prefix_aggregate(AGGREGATE_NONE),
                src_prefix(), src_prefix_mask(0), src_aggregate_plen(0),
                dst_prefix(), dst_prefix_mask(0), dst_aggregate_plen(0) { }

        FatFlowEntry(const uint8_t proto, const uint16_t p,
                     FatFlowIgnoreAddressType ignore_addr,
                     FatFlowPrefixAggregateType prefix_aggr) :
                protocol(proto), port(p),
                ignore_address(ignore_addr),
                prefix_aggregate(prefix_aggr),
                src_prefix(), src_prefix_mask(0), src_aggregate_plen(0),
                dst_prefix(), dst_prefix_mask(0), dst_aggregate_plen(0) { }

        FatFlowEntry(const uint8_t proto, const uint16_t p,
            std::string ignore_addr, FatFlowPrefixAggregateType prefix_aggregate,
            IpAddress src_prefix, uint8_t src_prefix_mask, uint8_t src_aggregate_plen,
            IpAddress dst_prefix, uint8_t dst_prefix_mask, uint8_t dst_aggregate_plen);

        static FatFlowEntry MakeFatFlowEntry(const std::string &protocol, const int &port,
                                             const std::string &ignore_addr_str,
                                             const std::string &src_prefix_str, const int &src_prefix_mask,
                                             const int &src_aggregate_plen,
                                             const std::string &dst_prefix_str, const int &dst_prefix_mask,
                                             const int &dst_aggregate_plen);

        virtual ~FatFlowEntry(){}
        bool operator == (const FatFlowEntry &rhs) const {
            return (rhs.protocol == protocol && rhs.port == port &&
                    rhs.ignore_address == ignore_address && rhs.prefix_aggregate == prefix_aggregate &&
                    rhs.src_prefix == src_prefix && rhs.src_prefix_mask == src_prefix_mask &&
                    rhs.src_aggregate_plen == src_aggregate_plen &&
                    rhs.dst_prefix == dst_prefix && rhs.dst_prefix_mask == dst_prefix_mask &&
                    rhs.dst_aggregate_plen == dst_aggregate_plen);
        }

        bool operator() (const FatFlowEntry  &lhs,
                         const FatFlowEntry &rhs) const {
            return lhs.IsLess(&rhs);
        }

        bool IsLess(const FatFlowEntry *rhs) const {
            if (protocol != rhs->protocol) {
                return protocol < rhs->protocol;
            }
            if (port != rhs->port) {
                return port < rhs->port;
            }
            if (ignore_address != rhs->ignore_address) {
                return ignore_address < rhs->ignore_address;
            }
            if (prefix_aggregate != rhs->prefix_aggregate) {
                return prefix_aggregate < rhs->prefix_aggregate;
            }
            if (src_prefix != rhs->src_prefix) {
                return src_prefix < rhs->src_prefix;
            }
            if (src_prefix_mask != rhs->src_prefix_mask) {
                return src_prefix_mask < rhs->src_prefix_mask;
            }
            if (src_aggregate_plen != rhs->src_aggregate_plen) {
                return src_aggregate_plen < rhs->src_aggregate_plen;
            }
            if (dst_prefix != rhs->dst_prefix) {
                return dst_prefix < rhs->dst_prefix;
            }
            if (dst_prefix_mask != rhs->dst_prefix_mask) {
                return dst_prefix_mask < rhs->dst_prefix_mask;
            }
            return dst_aggregate_plen < rhs->dst_aggregate_plen;
        }
        void print(void) const;

        uint8_t protocol;
        uint16_t port;
        mutable FatFlowIgnoreAddressType ignore_address;
        mutable FatFlowPrefixAggregateType prefix_aggregate;
        mutable IpAddress src_prefix;
        mutable uint8_t src_prefix_mask;
        mutable uint8_t src_aggregate_plen;
        mutable IpAddress dst_prefix;
        mutable uint8_t dst_prefix_mask;
        mutable uint8_t dst_aggregate_plen;
    };
    /* All the fields in FatFlowEntry are considered as part of key */
    typedef std::set<FatFlowEntry, FatFlowEntry> FatFlowEntrySet;

    struct FatFlowList {
        FatFlowList(): list_() {}
        ~FatFlowList() {}
        void Insert(const FatFlowEntry *rhs);
        void Update(const FatFlowEntry *lhs, const FatFlowEntry *rhs);
        void Remove(FatFlowEntrySet::iterator &it);
        bool UpdateList(const Agent *agent, VmInterface *vmi);
        void DumpList() const;

        FatFlowEntrySet list_;
    };

    struct FatFlowExcludeList {
        std::vector<uint64_t> fat_flow_v4_exclude_list_;
        std::vector<uint64_t> fat_flow_v6_exclude_upper_list_;
        std::vector<uint64_t> fat_flow_v6_exclude_lower_list_;
        std::vector<uint16_t> fat_flow_v6_exclude_plen_list_;
        void Clear() {
            fat_flow_v4_exclude_list_.clear();
            fat_flow_v6_exclude_upper_list_.clear();
            fat_flow_v6_exclude_lower_list_.clear();
            fat_flow_v6_exclude_plen_list_.clear();
        }
    };
    struct BridgeDomain : ListEntry {
        BridgeDomain(): uuid_(boost::uuids::nil_uuid()), vlan_tag_(0),
            bridge_domain_(NULL) {}
        BridgeDomain(const BridgeDomain &rhs):
            uuid_(rhs.uuid_), vlan_tag_(rhs.vlan_tag_),
            bridge_domain_(rhs.bridge_domain_) {}
        BridgeDomain(const boost::uuids::uuid &uuid, uint32_t vlan_tag):
            uuid_(uuid), vlan_tag_(vlan_tag), bridge_domain_(NULL) {}
        virtual ~BridgeDomain(){}
        bool operator == (const BridgeDomain &rhs) const {
            return (uuid_ == rhs.uuid_);
        }

        bool operator() (const BridgeDomain &lhs,
                         const BridgeDomain &rhs) const {
            return lhs.IsLess(&rhs);
        }

        bool IsLess(const BridgeDomain *rhs) const {
            return uuid_ < rhs->uuid_;
        }

        boost::uuids::uuid uuid_;
        uint32_t vlan_tag_;
        mutable BridgeDomainConstRef bridge_domain_;
    };
    typedef std::set<BridgeDomain, BridgeDomain> BridgeDomainEntrySet;

    struct BridgeDomainList {
        BridgeDomainList(): list_() {}
        ~BridgeDomainList() {}
        void Insert(const BridgeDomain *rhs);
        void Update(const BridgeDomain *lhs, const BridgeDomain *rhs);
        void Remove(BridgeDomainEntrySet::iterator &it);

        bool Update(const Agent *agent, VmInterface *vmi);
        BridgeDomainEntrySet list_;
    };

    struct VmiReceiveRoute : ListEntry, VmInterfaceState {
        VmiReceiveRoute();
        VmiReceiveRoute(const VmiReceiveRoute &rhs);
        VmiReceiveRoute(const IpAddress &addr, uint32_t plen, bool add_l2);
        virtual ~VmiReceiveRoute() {}

        bool operator() (const VmiReceiveRoute &lhs,
                         const VmiReceiveRoute &rhs) const;
        bool IsLess(const VmiReceiveRoute *rhs) const;

        VmInterfaceState::Op GetOpL2(const Agent *agent,
                const VmInterface *vmi) const;
        bool DeleteL2(const Agent *agent, VmInterface *vmi) const;
        bool AddL2(const Agent *agent, VmInterface *vmi) const;

        void Copy(const Agent *agent, const VmInterface *vmi) const;
        VmInterfaceState::Op GetOpL3(const Agent *agent,
                                     const VmInterface *vmi) const;
        bool AddL3(const Agent *agent, VmInterface *vmi) const;
        bool DeleteL3(const Agent *agent, VmInterface *vmi) const;

        IpAddress  addr_;
        uint32_t   plen_;
        bool       add_l2_; //Pick mac from interface and then add l2 route
        mutable VrfEntryRef vrf_;
    };

    typedef std::set<VmiReceiveRoute, VmiReceiveRoute> VmiReceiveRouteSet;

    struct VmiReceiveRouteList : List {
        VmiReceiveRouteList() : List(), list_() { }
        ~VmiReceiveRouteList() { }
        void Insert(const VmiReceiveRoute *rhs);
        void Update(const VmiReceiveRoute *lhs, const VmiReceiveRoute *rhs);
        void Remove(VmiReceiveRouteSet::iterator &it);

        bool UpdateList(const Agent *agent, VmInterface *vmi,
                        VmInterfaceState::Op l2_force_op,
                        VmInterfaceState::Op l3_force_op);
        VmiReceiveRouteSet list_;
    };

    enum Trace {
        ADD,
        DEL,
        ACTIVATED_IPV4,
        ACTIVATED_IPV6,
        ACTIVATED_L2,
        DEACTIVATED_IPV4,
        DEACTIVATED_IPV6,
        DEACTIVATED_L2,
        FLOATING_IP_CHANGE,
        SERVICE_CHANGE,
    };

    VmInterface(const boost::uuids::uuid &uuid,
                const std::string &name,
                bool os_oper_state,
                const boost::uuids::uuid &logical_router_uuid);
    VmInterface(const boost::uuids::uuid &uuid, const std::string &name,
                const Ip4Address &addr, const MacAddress &mac,
                const std::string &vm_name,
                const boost::uuids::uuid &vm_project_uuid, uint16_t tx_vlan_id,
                uint16_t rx_vlan_id, Interface *parent,
                const Ip6Address &addr6, DeviceType dev_type, VmiType vmi_type,
                uint8_t vhostuser_mode, bool os_oper_state,
                const boost::uuids::uuid &logical_router_uuid);
    virtual ~VmInterface();

    virtual bool CmpInterface(const DBEntry &rhs) const;
    virtual void GetOsParams(Agent *agent);
    void SendTrace(const AgentDBTable *table, Trace event) const;
    bool Delete(const DBRequest *req);
    void Add();

    // DBEntry virtual methods
    KeyPtr GetDBRequestKey() const;
    std::string ToString() const;
    bool Resync(const InterfaceTable *table, const VmInterfaceData *data);
    bool OnChange(VmInterfaceData *data);
    void PostAdd();

    // get/set accessor functions
    const VmEntry *vm() const { return vm_.get(); }
    const VnEntry *vn() const { return vn_.get(); }
    VnEntry *GetNonConstVn() const { return vn_.get(); }
    const Ip4Address &primary_ip_addr() const { return primary_ip_addr_; }
    void set_primary_ip_addr(const Ip4Address &addr) { primary_ip_addr_ = addr; }

    bool policy_enabled() const { return policy_enabled_; }
    const Ip4Address &subnet_bcast_addr() const { return subnet_bcast_addr_; }
    const Ip4Address &vm_ip_service_addr() const { return vm_ip_service_addr_; }
    const Ip6Address &primary_ip6_addr() const { return primary_ip6_addr_; }
    const MacAddress &vm_mac() const { return vm_mac_; }
    bool fabric_port() const { return fabric_port_; }
    bool need_linklocal_ip() const { return  need_linklocal_ip_; }
    bool drop_new_flows() const { return drop_new_flows_; }
    void set_device_type(VmInterface::DeviceType type) {device_type_ = type;}
    void set_vmi_type(VmInterface::VmiType type) {vmi_type_ = type;}
    VmInterface::DeviceType device_type() const {return device_type_;}
    VmInterface::VmiType vmi_type() const {return vmi_type_;}
    VmInterface::HbsIntfType hbs_intf_type() const {return hbs_intf_type_;}
    bool admin_state() const { return admin_state_; }
    const AclDBEntry* vrf_assign_acl() const {
        return vrf_assign_rule_list_.vrf_assign_acl_.get();
    }
    const Peer *peer() const;
    uint32_t ethernet_tag() const {return ethernet_tag_;}
    Ip4Address dhcp_addr() const { return dhcp_addr_; }
    IpAddress service_health_check_ip() const { return service_health_check_ip_; }
    const VmiEcmpLoadBalance &ecmp_load_balance() const {return ecmp_load_balance_;}
    bool is_vn_qos_config() const { return is_vn_qos_config_; }

    bool learning_enabled() const { return learning_enabled_; }
    void set_learning_enabled(bool val) { learning_enabled_ = val; }

    bool etree_leaf() const { return etree_leaf_; }
    void set_etree_leaf(bool val) { etree_leaf_ = val; }

    void set_hbs_intf_type(VmInterface::HbsIntfType val) { hbs_intf_type_ = val ;}
    bool pbb_interface() const { return pbb_interface_; }

    bool layer2_control_word() const { return layer2_control_word_; }
    void set_layer2_control_word(bool val) { layer2_control_word_ = val; }

    const NextHop* l3_interface_nh_no_policy() const;
    const NextHop* l2_interface_nh_no_policy() const;
    const NextHop* l2_interface_nh_policy() const;

    const std::string &cfg_name() const { return cfg_name_; }
    uint16_t tx_vlan_id() const { return tx_vlan_id_; }
    uint16_t rx_vlan_id() const { return rx_vlan_id_; }
    uint8_t vhostuser_mode() const { return vhostuser_mode_; }
    const Interface *parent() const { return parent_.get(); }
    bool ecmp() const { return ecmp_;}
    bool ecmp6() const { return ecmp6_;}
    Ip4Address service_ip() { return service_ip_;}
    bool service_ip_ecmp() const { return service_ip_ecmp_;}
    Ip6Address service_ip6() { return service_ip6_;}
    bool service_ip_ecmp6() const { return service_ip_ecmp6_;}
    bool flood_unknown_unicast() const { return flood_unknown_unicast_; }
    bool bridging() const { return bridging_; }
    bool layer3_forwarding() const { return layer3_forwarding_; }
    const std::string &vm_name() const { return vm_name_; }
    const boost::uuids::uuid &vm_project_uuid() const {return vm_project_uuid_;}

    uint32_t local_preference() const { return local_preference_; }
    void SetPathPreference(PathPreference *pref, bool ecmp,
                           const IpAddress &dependent_ip) const;

    const OperDhcpOptions &oper_dhcp_options() const {
        return oper_dhcp_options_;
    }
    bool dhcp_enable_config() const { return dhcp_enable_; }
    void set_dhcp_enable_config(bool dhcp_enable) {dhcp_enable_= dhcp_enable;}
    bool do_dhcp_relay() const { return do_dhcp_relay_; }

    bool cfg_igmp_enable() const { return cfg_igmp_enable_; }
    bool igmp_enabled() const { return igmp_enabled_; }
    uint32_t max_flows() const { return max_flows_; }
    void set_max_flows( uint32_t val) { max_flows_ = val;}
    ProxyArpMode proxy_arp_mode() const { return proxy_arp_mode_; }
    bool IsUnrestrictedProxyArp() const {
        return proxy_arp_mode_ == PROXY_ARP_UNRESTRICTED;
    }

    int vxlan_id() const { return vxlan_id_; }
    void set_vxlan_id(int vxlan_id) { vxlan_id_ = vxlan_id; }
    bool IsVxlanMode() const;

    uint8_t configurer() const {return configurer_;}
    bool IsConfigurerSet(VmInterface::Configurer type);
    void SetConfigurer(VmInterface::Configurer type);
    void ResetConfigurer(VmInterface::Configurer type);
    bool CanBeDeleted() const {return (configurer_ == 0);}

    const Ip4Address& subnet() const { return subnet_;}
    const uint8_t subnet_plen() const { return subnet_plen_;}
    const MacAddress& GetVifMac(const Agent*) const;
    const boost::uuids::uuid &logical_interface() const {
        return logical_interface_;
    }

    const MirrorEntry *mirror_entry() const { return mirror_entry_.get(); }
    void set_mirror_entry (MirrorEntry *entry) { mirror_entry_ = entry; }
    bool IsMirrorEnabled() const { return mirror_entry_.get() != NULL; }
    Interface::MirrorDirection mirror_direction() const {
        return mirror_direction_;
    }
    void set_mirror_direction(Interface::MirrorDirection mirror_direction) {
        mirror_direction_ = mirror_direction;
    }

    uint32_t FloatingIpCount() const {return floating_ip_list_.list_.size();}
    const FloatingIpList &floating_ip_list() const {return floating_ip_list_;}
    bool HasFloatingIp(Address::Family family) const;
    bool HasFloatingIp() const;
    bool IsFloatingIp(const IpAddress &ip) const;
    size_t GetFloatingIpCount() const {return floating_ip_list_.list_.size();}

    const AliasIpList &alias_ip_list() const {
        return alias_ip_list_;
    }
    VrfEntry *GetAliasIpVrf(const IpAddress &ip) const;
    size_t GetAliasIpCount() const { return alias_ip_list_.list_.size(); }
    void CleanupAliasIpList();

    const StaticRouteList &static_route_list() const {
        return static_route_list_;
    }
    const SecurityGroupEntryList &sg_list() const {
        return sg_list_;
    }
    void CopySgIdList(SecurityGroupList *sg_id_list) const;
    void CopyTagIdList(TagList *tag_id_list) const;

    const TagEntryList &tag_list() const {
        return tag_list_;
    }

    const VrfAssignRuleList &vrf_assign_rule_list() const {
        return vrf_assign_rule_list_;
    }

    const AllowedAddressPairList &allowed_address_pair_list() const {
        return allowed_address_pair_list_;
    }

    const InstanceIpList &instance_ipv4_list() const {
        return instance_ipv4_list_;
    }

    const InstanceIpList &instance_ipv6_list() const {
        return instance_ipv6_list_;
    }

    const FatFlowList &fat_flow_list() const {
        return fat_flow_list_;
    }
    bool IsFatFlowPortBased(uint8_t protocol, uint16_t port,
                            FatFlowIgnoreAddressType *ignore_addr) const;
    bool ExcludeFromFatFlow(Address::Family family, const IpAddress &sip,
                            const IpAddress &dip) const;
    bool MatchSrcPrefixPort(uint8_t protocol, uint16_t port, IpAddress *src_ip,
                            FatFlowIgnoreAddressType *ignore_addr) const;
    bool MatchSrcPrefixRule(uint8_t protocol, uint16_t *sport,
                            uint16_t *dport, bool *same_port_num,
                            IpAddress *SrcIP,
                            FatFlowIgnoreAddressType *ignore_addr) const;
    bool MatchDstPrefixPort(uint8_t protocol, uint16_t port, IpAddress *dst_ip,
                            FatFlowIgnoreAddressType *ignore_addr) const;
    bool MatchDstPrefixRule(uint8_t protocol, uint16_t *sport,
                            uint16_t *dport, bool *same_port_num,
                            IpAddress *DstIP,
                            FatFlowIgnoreAddressType *ignore_addr) const;
    bool MatchSrcDstPrefixPort(uint8_t protocol, uint16_t port, IpAddress *src_ip,
                               IpAddress *dst_ip) const;
    bool MatchSrcDstPrefixRule(uint8_t protocol, uint16_t *sport,
                               uint16_t *dport, bool *same_port_num,
                               IpAddress *SrcIP, IpAddress *DstIP) const;
    bool IsFatFlowPrefixAggregation(bool ingress, uint8_t protocol, uint16_t *sport,
                                    uint16_t *dport, bool *same_port_num,
                                    IpAddress *SrcIP, IpAddress *DstIP,
                                    bool *is_src_prefix, bool *is_dst_prefix,
                                    FatFlowIgnoreAddressType *ignore_addr) const;

    const BridgeDomainList &bridge_domain_list() const {
        return bridge_domain_list_;
    }

    const VmiReceiveRouteList &receive_route_list() const {
        return receive_route_list_;
    }

    void set_subnet_bcast_addr(const Ip4Address &addr) {
        subnet_bcast_addr_ = addr;
    }

    VrfEntry *forwarding_vrf() const {
        return forwarding_vrf_.get();
    }

    Ip4Address mdata_ip_addr() const;
    MetaDataIp *GetMetaDataIp(const Ip4Address &ip) const;
    void InsertMetaDataIpInfo(MetaDataIp *mip);
    void DeleteMetaDataIpInfo(MetaDataIp *mip);
    void UpdateMetaDataIpInfo();

    void InsertHealthCheckInstance(HealthCheckInstanceBase *hc_inst);
    void DeleteHealthCheckInstance(HealthCheckInstanceBase *hc_inst);
    const HealthCheckInstanceSet &hc_instance_set() const;
    bool IsHealthCheckEnabled() const;
    void UpdateInterfaceHealthCheckService();

    const ServiceVlanList &service_vlan_list() const {
        return service_vlan_list_;
    }
    bool HasServiceVlan() const { return service_vlan_list_.list_.size() != 0; }

    Agent *agent() const {
        return (static_cast<InterfaceTable *>(get_table()))->agent();
    }
    uint32_t GetServiceVlanLabel(const VrfEntry *vrf) const;
    const VrfEntry* GetServiceVlanVrf(uint16_t vlan_tag) const;
    bool OnResyncServiceVlan(VmInterfaceConfigData *data);
    void SetServiceVlanPathPreference(PathPreference *pref,
                                      const IpAddress &service_ip) const;

    const UuidList &slo_list() const {
        return slo_list_;
    }

    const std::string GetAnalyzer() const;
    bool IsL2Active() const;
    bool IsIpv6Active() const;

    bool WaitForTraffic() const;
    bool GetInterfaceDhcpOptions(
            std::vector<autogen::DhcpOptionType> *options) const;
    bool GetSubnetDhcpOptions(
            std::vector<autogen::DhcpOptionType> *options, bool ipv6) const;
    bool GetIpamDhcpOptions(
            std::vector<autogen::DhcpOptionType> *options, bool ipv6) const;
    IpAddress GetServiceIp(const IpAddress &ip) const;
    IpAddress GetGatewayIp(const IpAddress &ip) const;

    bool NeedDevice() const;
    bool NeedOsStateWithoutDevice() const;
    bool IsActive() const;
    bool InstallBridgeRoutes() const;
    bool IsBareMetal() const {return (vmi_type_ == BAREMETAL);}

    bool NeedMplsLabel() const;
    bool SgExists(const boost::uuids::uuid &id, const SgList &sg_l);
    const MacAddress& GetIpMac(const IpAddress &,
                              const uint8_t plen) const;
    bool MatchAapIp(const IpAddress &ip, uint8_t plen) const;
    void BuildIpStringList(Address::Family family,
                           std::vector<std::string> *vect) const;

    uint32_t GetIsid() const;
    uint32_t GetPbbVrf() const;
    uint32_t GetPbbLabel() const;

    void GetNextHopInfo();
    bool UpdatePolicySet(const Agent *agent);
    const FirewallPolicyList& fw_policy_list() const {
        return fw_policy_list_;
    }
    const FirewallPolicyList& fwaas_fw_policy_list() const {
        return fwaas_fw_policy_list_;
    }
    const boost::uuids::uuid& vmi_cfg_uuid() const {
        return vmi_cfg_uuid_;
    }

    bool is_left_si() const { return is_left_si_; }
    enum ServiceMode{
        BRIDGE_MODE,
        ROUTED_MODE,
        ROUTED_NAT_MODE,
        SERVICE_MODE_ERROR,
    };
    uint32_t service_mode() {
        return service_mode_;
    }
    const boost::uuids::uuid &si_other_end_vmi() const {
        return si_other_end_vmi_;
    }
    const std::string &service_intf_type() const { return service_intf_type_; }
    void set_service_intf_type(std::string type) { service_intf_type_ = type; }
    VmInterface * PortTuplePairedInterface() const;
    void BuildFatFlowExcludeList(FatFlowExcludeList *list) const;

    // Static methods
    // Add a vm-interface
    static void NovaAdd(InterfaceTable *table,
                        const boost::uuids::uuid &intf_uuid,
                        const std::string &os_name, const Ip4Address &addr,
                        const std::string &mac, const std::string &vn_name,
                        const boost::uuids::uuid &vm_project_uuid,
                        uint16_t tx_vlan_id, uint16_t rx_vlan_id,
                        const std::string &parent, const Ip6Address &ipv6,
                        uint8_t vhostuser_mode,
                        Interface::Transport transport, uint8_t link_state);
    // Del a vm-interface
    static void Delete(InterfaceTable *table,
                       const boost::uuids::uuid &intf_uuid,
                       VmInterface::Configurer configurer);
    static void SetIfNameReq(InterfaceTable *table,
                             const boost::uuids::uuid &uuid,
                             const std::string &ifname);
    static void DeleteIfNameReq(InterfaceTable *table,
                                const boost::uuids::uuid &uuid);
    void update_flow_count(int val) const;
    uint32_t flow_count() const { return flow_count_; }

private:
    friend struct VmInterfaceConfigData;
    friend struct VmInterfaceNovaData;
    friend struct VmInterfaceIpAddressData;
    friend struct VmInterfaceOsOperStateData;
    friend struct VmInterfaceMirrorData;
    friend struct VmInterfaceGlobalVrouterData;
    friend struct VmInterfaceHealthCheckData;
    friend struct VmInterfaceNewFlowDropData;
    friend struct ResolveRouteState;
    friend struct VmiRouteState;

    virtual void ObtainOsSpecificParams(const std::string &name);

    bool IsMetaDataL2Active() const;
    bool IsMetaDataIPActive() const;
    bool IsIpv4Active() const;
    bool PolicyEnabled() const;
    void FillV4ExcludeIp(uint64_t plen, const Ip4Address &ip,
                         FatFlowExcludeList *list) const;
    void FillV6ExcludeIp(uint16_t plen, const IpAddress &ip,
                         FatFlowExcludeList *list) const;

    bool CopyConfig(const InterfaceTable *table,
                    const VmInterfaceConfigData *data, bool *sg_changed,
                    bool *ecmp_changed, bool *local_pref_changed,
                    bool *ecmp_load_balance_changed,
                    bool *static_route_config_changed,
                    bool *etree_leaf_mode_changed,
                    bool *tag_changed);
    void ApplyConfig(bool old_ipv4_active,bool old_l2_active,
                     bool old_ipv6_active,
                     const Ip4Address &old_subnet,
                     const uint8_t old_subnet_plen);

    void UpdateL2();
    void DeleteL2();

    void AddRoute(const std::string &vrf_name, const IpAddress &ip,
                  uint32_t plen, const std::string &vn_name, bool force_policy,
                  bool ecmp, bool is_local, bool proxy_arp,
                  const IpAddress &service_ip, const IpAddress &dependent_ip,
                  const CommunityList &communties, uint32_t label,
                  const string &intf_route_type);
    void DeleteRoute(const std::string &vrf_name, const IpAddress &ip,
                     uint32_t plen);

    bool OnResyncSecurityGroupList(VmInterfaceConfigData *data,
                                   bool new_ipv4_active);
    bool ResyncIpAddress(const VmInterfaceIpAddressData *data);
    bool ResyncOsOperState(const VmInterfaceOsOperStateData *data);
    bool ResyncConfig(VmInterfaceConfigData *data);
    bool CopyIpAddress(Ip4Address &addr);
    bool CopyIp6Address(const Ip6Address &addr);

    void CleanupFloatingIpList();

    bool OnResyncStaticRoute(VmInterfaceConfigData *data, bool new_ipv4_active);

    void AddL2InterfaceRoute(const IpAddress &ip, const MacAddress &mac,
                             const IpAddress &dependent_ip) const;
    void DeleteL2InterfaceRoute(const VrfEntry *vrf, uint32_t ethernet_tag,
                                const IpAddress &ip,
                                const MacAddress &mac) const;

    bool UpdateIsHealthCheckActive();
    void CopyEcmpLoadBalance(EcmpLoadBalance &ecmp_load_balance);

    bool UpdateState(const VmInterfaceState *attr,
                     VmInterfaceState::Op l2_force_op,
                     VmInterfaceState::Op l3_force_op);
    bool DeleteState(VmInterfaceState *attr);
    static IgnoreAddressMap InitIgnoreAddressMap() {
        IgnoreAddressMap value;
        value[""] = IGNORE_NONE;
        value["none"] = IGNORE_NONE;
        value["source"] = IGNORE_SOURCE;
        value["destination"] = IGNORE_DESTINATION;
        return value;
    }

    void SetInterfacesDropNewFlows(bool drop_new_flows) const;

private:
    static IgnoreAddressMap fatflow_ignore_addr_map_;
    VmEntryBackRef vm_;
    VnEntryRef vn_;
    Ip4Address primary_ip_addr_;
    Ip4Address subnet_bcast_addr_;
    Ip6Address primary_ip6_addr_;
    MacAddress vm_mac_;
    bool policy_enabled_;
    MirrorEntryRef mirror_entry_;
    Interface::MirrorDirection mirror_direction_;
    std::string cfg_name_;
    bool fabric_port_;
    bool need_linklocal_ip_;
    bool drop_new_flows_;
    mutable bool drop_new_flows_vmi_;
    // DHCP flag - set according to the dhcp option in the ifmap subnet object.
    // It controls whether the vrouter sends the DHCP requests from VM interface
    // to agent or if it would flood the request in the VN.
    bool dhcp_enable_;
    // true if IP is to be obtained from DHCP Relay and not learnt from fabric
    bool do_dhcp_relay_;
    // Proxy ARP mode for interface
    ProxyArpMode proxy_arp_mode_;
    // VM-Name. Used by DNS
    std::string vm_name_;
    // project uuid of the vm to which the interface belongs
    boost::uuids::uuid vm_project_uuid_;
    int vxlan_id_;
    bool bridging_;
    bool layer3_forwarding_;
    bool flood_unknown_unicast_;
    bool mac_set_;
    bool ecmp_;
    bool ecmp6_;
    Ip4Address service_ip_;
    bool service_ip_ecmp_;
    Ip6Address service_ip6_;
    bool service_ip_ecmp6_;
    // disable-policy configuration on VMI. When this is configured, policy
    // dependent features like flows, floating-IP and SG will not work on this
    // VMI. However metadata-services will work because metadata route will
    // still point to policy enabled NH.
    bool disable_policy_;
    // VLAN Tag and the parent interface when VLAN is enabled
    uint16_t tx_vlan_id_;
    uint16_t rx_vlan_id_;
    InterfaceBackRef parent_;
    uint32_t local_preference_;
    // DHCP options defined for the interface
    OperDhcpOptions oper_dhcp_options_;
    // IGMP Configuration
    bool cfg_igmp_enable_;
    bool igmp_enabled_;
    // Max flows for VMI
    uint32_t max_flows_;
    mutable tbb::atomic<int> flow_count_;

    // Attributes
    std::auto_ptr<MacVmBindingState> mac_vm_binding_state_;
    std::auto_ptr<NextHopState> nexthop_state_;
    std::auto_ptr<VrfTableLabelState> vrf_table_label_state_;
    std::auto_ptr<MetaDataIpState> metadata_ip_state_;
    std::auto_ptr<ResolveRouteState> resolve_route_state_;
    std::auto_ptr<VmiRouteState> interface_route_state_;

    // Lists
    SecurityGroupEntryList sg_list_;
    TagEntryList tag_list_;
    FloatingIpList floating_ip_list_;
    AliasIpList alias_ip_list_;
    ServiceVlanList service_vlan_list_;
    StaticRouteList static_route_list_;
    AllowedAddressPairList allowed_address_pair_list_;
    InstanceIpList instance_ipv4_list_;
    InstanceIpList instance_ipv6_list_;
    FatFlowList fat_flow_list_;
    BridgeDomainList bridge_domain_list_;
    VrfAssignRuleList vrf_assign_rule_list_;
    VmiReceiveRouteList receive_route_list_;

    // Peer for interface routes
    std::auto_ptr<LocalVmPortPeer> peer_;
    Ip4Address vm_ip_service_addr_;
    VmInterface::DeviceType device_type_;
    VmInterface::VmiType vmi_type_;
    VmInterface::HbsIntfType hbs_intf_type_;
    uint8_t configurer_;
    Ip4Address subnet_;
    uint8_t subnet_plen_;
    int ethernet_tag_;
    // Logical interface uuid to which the interface belongs
    boost::uuids::uuid logical_interface_;
    Ip4Address nova_ip_addr_;
    Ip6Address nova_ip6_addr_;
    Ip4Address dhcp_addr_;
    MetaDataIpMap metadata_ip_map_;
    HealthCheckInstanceSet hc_instance_set_;
    VmiEcmpLoadBalance ecmp_load_balance_;
    IpAddress service_health_check_ip_;
    bool is_vn_qos_config_;
    bool learning_enabled_;
    bool etree_leaf_;
    bool pbb_interface_;
    bool layer2_control_word_;
    //Includes global policy apply and application policy set
    FirewallPolicyList fw_policy_list_;
    FirewallPolicyList fwaas_fw_policy_list_;
    UuidList slo_list_;
    VrfEntryRef forwarding_vrf_;
    // vhostuser mode
    uint8_t vhostuser_mode_;
    // indicates if the VMI is the left interface of a service instance
    bool is_left_si_;
    uint32_t service_mode_;
    /* If current interface is SI VMI, then the below field indicates the VMI
     * uuid of the other end of SI. If current VMI is left VMI of SI si1, then
     * below field indicates right VMI of SI si1 and vice versa. This will have
     * nil_uuid if current VMI is not SI VMI.
     */
    boost::uuids::uuid si_other_end_vmi_;
    //In case Vhost interface, uuid_ is stored here
    boost::uuids::uuid vmi_cfg_uuid_;
    std::string service_intf_type_;
    DISALLOW_COPY_AND_ASSIGN(VmInterface);
};

/////////////////////////////////////////////////////////////////////////////
// Key for VM Interfaces
/////////////////////////////////////////////////////////////////////////////
struct VmInterfaceKey : public InterfaceKey {
    VmInterfaceKey(AgentKey::DBSubOperation sub_op,
                   const boost::uuids::uuid &uuid, const std::string &name);
    virtual ~VmInterfaceKey() { }

    Interface *AllocEntry(const InterfaceTable *table) const;
    Interface *AllocEntry(const InterfaceTable *table,
                          const InterfaceData *data) const;
    InterfaceKey *Clone() const;
};

/////////////////////////////////////////////////////////////////////////////
// The base class for different type of InterfaceData used for VmInterfaces.
//
// Request for VM-Interface data are of 3 types
// - ADD_DEL_CHANGE
//   Message for ADD/DEL/CHANGE of an interface
// - MIRROR
//   Data for mirror enable/disable
// - IP_ADDR
//   In one of the modes, the IP address for an interface is not got from IFMap
//   or Nova. Agent will relay DHCP Request in such cases to IP Fabric network.
//   The IP address learnt with DHCP in this case is confgured with this type
// - OS_OPER_STATE
//   Update to oper state of the interface
/////////////////////////////////////////////////////////////////////////////
struct VmInterfaceData : public InterfaceData {
    enum Type {
        CONFIG,
        INSTANCE_MSG,
        MIRROR,
        IP_ADDR,
        OS_OPER_STATE,
        GLOBAL_VROUTER,
        HEALTH_CHECK,
        DROP_NEW_FLOWS
    };

    VmInterfaceData(Agent *agent, IFMapNode *node, Type type,
            Interface::Transport transport) :
        InterfaceData(agent, node, transport), type_(type) {
        VmPortInit();
    }
    virtual ~VmInterfaceData() { }

    virtual VmInterface *OnAdd(const InterfaceTable *table,
                               const VmInterfaceKey *key) const {
        return NULL;
    }
    virtual bool OnDelete(const InterfaceTable *table,
                          VmInterface *entry) const {
        return true;
    }
    virtual bool OnResync(const InterfaceTable *table, VmInterface *vmi,
                          bool *force_update) const = 0;

    Type type_;
};

// Structure used when type=IP_ADDR. Used to update IP-Address of VM-Interface
// The IP Address is picked up from the DHCP Snoop table
struct VmInterfaceIpAddressData : public VmInterfaceData {
    VmInterfaceIpAddressData() : VmInterfaceData(NULL, NULL, IP_ADDR,
                                     Interface::TRANSPORT_INVALID) { }
    virtual ~VmInterfaceIpAddressData() { }
    virtual bool OnResync(const InterfaceTable *table, VmInterface *vmi,
                          bool *force_update) const;
};

// Structure used when type=OS_OPER_STATE Used to update interface os oper-state
// The current oper-state is got by querying the device
struct VmInterfaceOsOperStateData : public VmInterfaceData {
    VmInterfaceOsOperStateData(bool status) :
        VmInterfaceData(NULL, NULL, OS_OPER_STATE,
                        Interface::TRANSPORT_INVALID), oper_state_(status) { }
    virtual ~VmInterfaceOsOperStateData() { }
    virtual bool OnResync(const InterfaceTable *table, VmInterface *vmi,
                          bool *force_update) const;
    bool oper_state_;
};

// Structure used when type=MIRROR. Used to update IP-Address of VM-Interface
struct VmInterfaceMirrorData : public VmInterfaceData {
    VmInterfaceMirrorData(bool mirror_enable, const std::string &analyzer_name):
        VmInterfaceData(NULL, NULL, MIRROR, Interface::TRANSPORT_INVALID),
        mirror_enable_(mirror_enable),
        analyzer_name_(analyzer_name) {
    }
    virtual ~VmInterfaceMirrorData() { }
    virtual bool OnResync(const InterfaceTable *table, VmInterface *vmi,
                          bool *force_update) const;

    bool mirror_enable_;
    std::string analyzer_name_;
};

// Definition for structures when request queued from IFMap config.
struct VmInterfaceConfigData : public VmInterfaceData {
    VmInterfaceConfigData(Agent *agent, IFMapNode *node);
    virtual ~VmInterfaceConfigData() { }
    virtual VmInterface *OnAdd(const InterfaceTable *table,
                               const VmInterfaceKey *key) const;
    virtual bool OnDelete(const InterfaceTable *table,
                          VmInterface *entry) const;
    virtual bool OnResync(const InterfaceTable *table, VmInterface *vmi,
                          bool *force_update) const;
    autogen::VirtualMachineInterface *GetVmiCfg() const;
    void CopyVhostData(const Agent *agent);

    Ip4Address addr_;
    Ip6Address ip6_addr_;
    std::string vm_mac_;
    std::string cfg_name_;
    boost::uuids::uuid vm_uuid_;
    std::string vm_name_;
    boost::uuids::uuid vn_uuid_;
    std::string vrf_name_;

    // Is this port on IP Fabric
    bool fabric_port_;
    // Does the port need link-local IP to be allocated
    bool need_linklocal_ip_;
    bool bridging_;
    bool layer3_forwarding_;
    bool mirror_enable_;
    //Is interface in active-active mode or active-backup mode
    bool ecmp_;
    bool ecmp6_;
    bool dhcp_enable_; // is DHCP enabled for the interface (from subnet config)
    VmInterface::ProxyArpMode proxy_arp_mode_;
    bool admin_state_;
    bool disable_policy_;
    std::string analyzer_name_;
    uint32_t local_preference_;
    OperDhcpOptions oper_dhcp_options_;
    Interface::MirrorDirection mirror_direction_;
    // IGMP Configuration
    bool cfg_igmp_enable_;
    bool igmp_enabled_;
    uint32_t max_flows_;

    VmInterface::SecurityGroupEntryList sg_list_;
    VmInterface::TagEntryList tag_list_;
    VmInterface::FloatingIpList floating_ip_list_;
    VmInterface::AliasIpList alias_ip_list_;
    VmInterface::ServiceVlanList service_vlan_list_;
    VmInterface::StaticRouteList static_route_list_;
    VmInterface::VrfAssignRuleList vrf_assign_rule_list_;
    VmInterface::AllowedAddressPairList allowed_address_pair_list_;
    VmInterface::InstanceIpList instance_ipv4_list_;
    VmInterface::InstanceIpList instance_ipv6_list_;
    VmInterface::FatFlowList fat_flow_list_;
    VmInterface::BridgeDomainList bridge_domain_list_;
    VmInterface::VmiReceiveRouteList receive_route_list_;
    VmInterface::DeviceType device_type_;
    VmInterface::VmiType vmi_type_;
    VmInterface::HbsIntfType hbs_intf_type_;
    // Parent physical-interface. Used in VMWare/ ToR logical-interface
    std::string physical_interface_;
    // Parent VMI. Set only for VM_VLAN_ON_VMI
    boost::uuids::uuid parent_vmi_;
    Ip4Address subnet_;
    uint8_t subnet_plen_;
    uint16_t rx_vlan_id_;
    uint16_t tx_vlan_id_;
    boost::uuids::uuid logical_interface_;
    VmiEcmpLoadBalance ecmp_load_balance_;
    IpAddress service_health_check_ip_;
    Ip4Address service_ip_;
    bool service_ip_ecmp_;
    Ip6Address service_ip6_;
    bool service_ip_ecmp6_;
    boost::uuids::uuid qos_config_uuid_;
    bool learning_enabled_;
    UuidList slo_list_;
    uint8_t vhostuser_mode_;
    bool is_left_si_;
    uint32_t service_mode_;
    boost::uuids::uuid si_other_end_vmi_;
    boost::uuids::uuid vmi_cfg_uuid_;
    std::string service_intf_type_;
};

// Definition for structures when request queued from Nova
struct VmInterfaceNovaData : public VmInterfaceData {
    VmInterfaceNovaData();
    VmInterfaceNovaData(const Ip4Address &ipv4_addr,
                        const Ip6Address &ipv6_addr,
                        const std::string &mac_addr,
                        const std::string vm_name,
                        boost::uuids::uuid vm_uuid,
                        boost::uuids::uuid vm_project_uuid,
                        const std::string &parent,
                        uint16_t tx_vlan_id,
                        uint16_t rx_vlan_id,
                        VmInterface::DeviceType device_type,
                        VmInterface::VmiType vmi_type,
                        uint8_t vhostuser_mode,
                        Interface::Transport transport,
                        uint8_t link_state);
    virtual ~VmInterfaceNovaData();
    virtual VmInterface *OnAdd(const InterfaceTable *table,
                               const VmInterfaceKey *key) const;
    virtual bool OnDelete(const InterfaceTable *table,
                          VmInterface *entry) const;
    virtual bool OnResync(const InterfaceTable *table, VmInterface *vmi,
                          bool *force_update) const;

    Ip4Address ipv4_addr_;
    Ip6Address ipv6_addr_;
    std::string mac_addr_;
    std::string vm_name_;
    boost::uuids::uuid vm_uuid_;
    boost::uuids::uuid vm_project_uuid_;
    std::string physical_interface_;
    uint16_t tx_vlan_id_;
    uint16_t rx_vlan_id_;
    VmInterface::DeviceType device_type_;
    VmInterface::VmiType vmi_type_;
    uint8_t vhostuser_mode_;
    uint8_t link_state_;
};

struct VmInterfaceGlobalVrouterData : public VmInterfaceData {
    VmInterfaceGlobalVrouterData(bool bridging,
                                 bool layer3_forwarding,
                                 int vxlan_id) :
        VmInterfaceData(NULL, NULL, GLOBAL_VROUTER, Interface::TRANSPORT_INVALID),
        bridging_(bridging),
        layer3_forwarding_(layer3_forwarding),
        vxlan_id_(vxlan_id) {
    }
    virtual ~VmInterfaceGlobalVrouterData() { }
    virtual bool OnResync(const InterfaceTable *table, VmInterface *vmi,
                          bool *force_update) const;

    bool bridging_;
    bool layer3_forwarding_;
    int vxlan_id_;
};

struct VmInterfaceHealthCheckData : public VmInterfaceData {
    VmInterfaceHealthCheckData();
    virtual ~VmInterfaceHealthCheckData();
    virtual bool OnResync(const InterfaceTable *table, VmInterface *vmi,
                          bool *force_update) const;
};

struct VmInterfaceNewFlowDropData : public VmInterfaceData {
    VmInterfaceNewFlowDropData(bool drop_new_flows);
    virtual ~VmInterfaceNewFlowDropData();
    virtual bool OnResync(const InterfaceTable *table, VmInterface *vmi,
                          bool *force_update) const;

    bool drop_new_flows_;
};

// Data used when interface added with only ifname in data
struct VmInterfaceIfNameData : public VmInterfaceData {
    VmInterfaceIfNameData();
    VmInterfaceIfNameData(const std::string &ifname);
    virtual ~VmInterfaceIfNameData();

    virtual VmInterface *OnAdd(const InterfaceTable *table,
                               const VmInterfaceKey *key) const;
    virtual bool OnDelete(const InterfaceTable *table, VmInterface *vmi) const;
    virtual bool OnResync(const InterfaceTable *table, VmInterface *vmi,
                          bool *force_update) const;

    std::string ifname_;
};
#endif // vnsw_agent_vm_interface_hpp
