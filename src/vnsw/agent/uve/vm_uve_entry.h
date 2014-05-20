/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vm_uve_entry_h
#define vnsw_agent_vm_uve_entry_h

#include <string>
#include <vector>
#include <set>
#include <map>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <virtual_machine_types.h>
#include <uve/l4_port_bitmap.h>
#include <uve/vm_stat.h>
#include <uve/vm_stat_data.h>
#include <oper/interface_common.h>
#include <oper/interface.h>
#include <oper/vm.h>
#include <uve/agent_stats_collector.h>

//The class that defines data-structures to store VirtualMachine information 
//required for sending VirtualMachine UVE.
class VmUveEntry {
public:
    struct FipInfo {
        Ip4Address fip_;
        std::string vn_;
        uint64_t bytes_;
        uint64_t packets_;
        uint32_t interface_id_;
        const FlowEntry *flow_;
    };
    struct FloatingIp {
        FloatingIp(const Ip4Address &ip, const std::string &vn) 
            : fip_(ip), vn_(vn) {
            in_bytes_ = 0;
            in_packets_ = 0;
            out_bytes_ = 0;
            out_packets_ = 0;
        }
        void UpdateFloatingIpStats(const FipInfo &fip_info);
                   
        Ip4Address fip_;
        std::string vn_;
        uint64_t in_bytes_;
        uint64_t in_packets_;
        uint64_t out_bytes_;
        uint64_t out_packets_;
    };
    typedef boost::shared_ptr<FloatingIp> FloatingIpPtr;

    class FloatingIpCmp {
        public:
            bool operator()(const FloatingIpPtr &lhs, 
                            const FloatingIpPtr &rhs) const {
                if (lhs.get()->fip_ != rhs.get()->fip_) { 
                    return lhs.get()->fip_ < rhs.get()->fip_;
                }
                return (lhs.get()->vn_ < rhs.get()->vn_);
            }
    };
    typedef std::set<FloatingIpPtr, FloatingIpCmp> FloatingIpSet;

    struct UveInterfaceEntry {
        const Interface *intf_;
        L4PortBitmap port_bitmap_;
        FloatingIpSet fip_tree_;

        UveInterfaceEntry(const Interface *i) : intf_(i), port_bitmap_(),
            fip_tree_() { }
        virtual ~UveInterfaceEntry() {}
        void UpdateFloatingIpStats(const FipInfo &fip_info);
        bool FillFloatingIpStats(VmInterfaceAgentStats *s_intf) const;
        void SetStats(VmFloatingIPStats &fip, uint64_t in_bytes, 
            uint64_t in_pkts, uint64_t out_bytes, uint64_t out_pkts) const;
        void RemoveFloatingIp(const VmInterface::FloatingIp &fip);
    };
    typedef boost::shared_ptr<UveInterfaceEntry> UveInterfaceEntryPtr;

    class UveInterfaceEntryCmp {
        public:
            bool operator()(const UveInterfaceEntryPtr &lhs, 
                            const UveInterfaceEntryPtr &rhs) const {
                if (lhs.get()->intf_ < rhs.get()->intf_)
                    return true;
                return false;
            }
    };
    typedef std::set<UveInterfaceEntryPtr, UveInterfaceEntryCmp> InterfaceSet;

    VmUveEntry(Agent *agent);
    virtual ~VmUveEntry();
    bool add_by_vm_notify() const { return add_by_vm_notify_; }
    void set_add_by_vm_notify(bool value) { add_by_vm_notify_ = value; }

    void InterfaceAdd(const Interface *intf, VmInterface::FloatingIpSet &olist);
    void InterfaceDelete(const Interface *intf);
    void UpdatePortBitmap(uint8_t proto, uint16_t sport, uint16_t dport);
    bool FrameVmMsg(const VmEntry* vm, UveVirtualMachineAgent &uve);
    bool FrameVmStatsMsg(const VmEntry* vm, UveVirtualMachineAgent &uve);
    void UpdateFloatingIpStats(const FipInfo &fip_info);
protected:
    Agent *agent_;
    InterfaceSet interface_tree_;
    L4PortBitmap port_bitmap_;
    UveVirtualMachineAgent uve_info_;
private:
    bool UveVmInterfaceListChanged
        (const std::vector<VmInterfaceAgent> &new_l) const;
    bool UveVmVRouterChanged(const std::string &new_value) const;
    bool FrameInterfaceMsg(const VmInterface *intf, VmInterfaceAgent *itf) 
                           const;
    bool FrameInterfaceStatsMsg(const VmInterface *vm_intf, 
                           VmInterfaceAgentStats *s_intf) const;
    bool GetVmInterfaceGateway(const VmInterface *intf, std::string &gw) const;
    bool UveVmInterfaceStatsListChanged
         (const std::vector<VmInterfaceAgentStats> &l) const;
    uint64_t GetVmPortBandwidth
        (AgentStatsCollector::InterfaceStats *s, bool dir_in) const;
    bool SetVmPortBitmap(UveVirtualMachineAgent &uve);
    
    bool add_by_vm_notify_;
    DISALLOW_COPY_AND_ASSIGN(VmUveEntry);
};
#endif // vnsw_agent_vm_uve_entry_h
