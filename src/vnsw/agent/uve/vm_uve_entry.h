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
    struct FloatingIp;

    struct FipInfo {
        uint64_t bytes_;
        uint64_t packets_;
        const FlowEntry *flow_;
        FloatingIp *rev_fip_;
    };
    struct FloatingIp {
        FloatingIp(const Ip4Address &ip, const std::string &vn)
            : fip_(ip), vn_(vn) {
            in_bytes_ = 0;
            in_packets_ = 0;
            out_bytes_ = 0;
            out_packets_ = 0;
        }
        FloatingIp(const Ip4Address &ip, const std::string &vn, uint64_t in_b,
                   uint64_t in_p, uint64_t out_b, uint64_t out_p)
            : fip_(ip), vn_(vn), in_bytes_(in_b), in_packets_(in_p),
              out_bytes_(out_b), out_packets_(out_p) {
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
        FloatingIpSet prev_fip_tree_;
        /* For exclusion between Agent::StatsCollector and Agent::Uve tasks */
        tbb::mutex mutex_;

        UveInterfaceEntry(const Interface *i) : intf_(i), port_bitmap_(),
            fip_tree_(), prev_fip_tree_() { }
        virtual ~UveInterfaceEntry() {}
        void UpdateFloatingIpStats(const FipInfo &fip_info);
        bool FillFloatingIpStats(vector<VmFloatingIPStats> &result,
                                 vector<VmFloatingIPStatSamples> &diff_list);
        void SetStats(VmFloatingIPStats &fip, uint64_t in_bytes,
            uint64_t in_pkts, uint64_t out_bytes, uint64_t out_pkts) const;
        void SetDiffStats(VmFloatingIPStatSamples &fip, uint64_t in_bytes,
            uint64_t in_pkts, uint64_t out_bytes, uint64_t out_pkts) const;
        void RemoveFloatingIp(const VmInterface::FloatingIp &fip);
        VmUveEntry::FloatingIp *FipEntry(uint32_t ip, const std::string &vn);
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

    void InterfaceAdd(const Interface *intf, const VmInterface::FloatingIpSet &olist);
    void InterfaceDelete(const Interface *intf);
    void UpdatePortBitmap(uint8_t proto, uint16_t sport, uint16_t dport);
    bool FrameVmMsg(const VmEntry* vm, UveVirtualMachineAgent *uve);
    bool FrameVmStatsMsg(const VmEntry* vm, UveVirtualMachineAgent *uve,
                         VirtualMachineStats *stats_uve,
                         bool *stats_uve_changed);
    void UpdateFloatingIpStats(const FipInfo &fip_info);
    VmUveEntry::FloatingIp * FipEntry(uint32_t fip, const std::string &vn,
                                      Interface *intf);
protected:
    Agent *agent_;
    InterfaceSet interface_tree_;
    L4PortBitmap port_bitmap_;
    UveVirtualMachineAgent uve_info_;
private:
    bool UveVmInterfaceListChanged
        (const std::vector<VmInterfaceAgent> &new_l) const;
    bool UveVmVRouterChanged(const std::string &new_value) const;
    bool UveVmFipStatsListChanged(const vector<VmFloatingIPStats> &new_l) const;
    bool FrameInterfaceMsg(const VmInterface *intf, VmInterfaceAgent *itf) 
                           const;
    bool FrameInterfaceStatsMsg(const VmInterface *vm_intf, 
                                VmInterfaceStats *s_intf) const;
    bool FrameFipStatsMsg(const VmInterface *vm_intf,
                        std::vector<VmFloatingIPStats> &fip_list,
                        std::vector<VmFloatingIPStatSamples> &diff_list) const;
    bool GetVmInterfaceGateway(const VmInterface *intf, std::string &gw) const;
    uint64_t GetVmPortBandwidth
        (AgentStatsCollector::InterfaceStats *s, bool dir_in) const;
    bool SetVmPortBitmap(UveVirtualMachineAgent *uve);
    
    bool add_by_vm_notify_;
    DISALLOW_COPY_AND_ASSIGN(VmUveEntry);
};
#endif // vnsw_agent_vm_uve_entry_h
