/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vm_uve_table_h
#define vnsw_agent_vm_uve_table_h

#include <string>
#include <vector>
#include <set>
#include <map>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <virtual_machine_types.h>
#include <uve/l4_port_bitmap.h>
#include <uve/vm_stat.h>
#include <uve/vm_stat_data.h>
#include <oper/vm.h>
#include <oper/peer.h>
#include <cmn/index_vector.h>
#include <uve/vm_uve_entry.h>
#include "base/queue_task.h"
#include "pkt/flow_proto.h"
#include "pkt/flow_table.h"

//The container class for objects representing VirtualMachine UVEs
//Defines routines for storing and managing (add, delete, change and send)
//VirtualMachine UVEs
class VmUveTable {
public:
    struct VmUveInterfaceState :public DBState {
        VmUveInterfaceState(const boost::uuids::uuid u, bool ipv4_active, 
                            bool l2_active,
                            const VmInterface::FloatingIpSet &fip_l)
            : vm_uuid_(u), ipv4_active_(ipv4_active), l2_active_(l2_active),
              fip_list_(fip_l) {}
        boost::uuids::uuid vm_uuid_;
        bool ipv4_active_;
        bool l2_active_;
        VmInterface::FloatingIpSet fip_list_;
    };

    class VmUveVmState: public DBState {
        public:
            VmUveVmState() : stat_(NULL) { };
            VmStat *stat_;
    };
    typedef boost::shared_ptr<VmUveEntry> VmUveEntryPtr;
    typedef std::map<const VmEntry*, VmUveEntryPtr> UveVmMap;
    typedef std::pair<const VmEntry*, VmUveEntryPtr> UveVmPair;

    VmUveTable(Agent *agent);
    virtual ~VmUveTable();
    void RegisterDBClients();
    void Shutdown(void);
    void SendVmStats(void);
    void UpdateBitmap(const VmEntry* vm, uint8_t proto, uint16_t sport, 
                      uint16_t dport);
    virtual void DispatchVmMsg(const UveVirtualMachineAgent &uve);
    virtual void DispatchVmStatsMsg(const VirtualMachineStats &uve);
    void EnqueueVmStatData(VmStatData *data);
    bool Process(VmStatData *vm_stat_data);
    void UpdateFloatingIpStats(const FlowEntry *flow, uint64_t bytes,
                               uint64_t pkts);

protected:
    virtual void VmStatCollectionStart(VmUveVmState *state, const VmEntry *vm);
    virtual void VmStatCollectionStop(VmUveVmState *state);
    UveVmMap uve_vm_map_;
    Agent *agent_;
private:
    virtual VmUveEntryPtr Allocate(const VmEntry *vm);
    VmUveEntry* UveEntryFromVm(const VmEntry *vm);
    void InterfaceNotify(DBTablePartBase *partition, DBEntryBase *e);
    void VmNotify(DBTablePartBase *partition, DBEntryBase *e);
    void InterfaceAddHandler(const VmEntry* vm, const Interface* intf,
                             const VmInterface::FloatingIpSet &old_list);
    void InterfaceDeleteHandler(const VmEntry* vm, const Interface* intf);
    void SendVmMsg(const VmEntry *vm);
    void SendVmStatsMsg(const VmEntry *vm);
    VmUveEntry* Add(const VmEntry *vm, bool vm_notify);
    void Delete(const VmEntry *vm);
    void BuildVmDeleteMsg(const VmEntry *vm, UveVirtualMachineAgent *uve,
                          VirtualMachineStats *stats_uve);
    VmUveEntry::FloatingIp *ReverseFlowFip(const VmUveEntry::FipInfo &fip_info);
    VmUveEntry *InterfaceIdToVmUveEntry(uint32_t id);

    DBTableBase::ListenerId intf_listener_id_;
    DBTableBase::ListenerId vm_listener_id_;
    boost::scoped_ptr<WorkQueue<VmStatData *> > event_queue_;
    DISALLOW_COPY_AND_ASSIGN(VmUveTable);
};

#endif // vnsw_agent_vm_uve_table_h
