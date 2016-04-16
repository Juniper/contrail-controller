/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vm_uve_table_base_h
#define vnsw_agent_vm_uve_table_base_h

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
#include <uve/vm_stat.h>
#include <uve/vm_stat_data.h>
#include <oper/vm.h>
#include <oper/peer.h>
#include <cmn/index_vector.h>
#include <uve/vm_uve_entry_base.h>
#include "base/queue_task.h"

//The container class for objects representing VirtualMachine UVEs
//Defines routines for storing and managing (add, delete, change and send)
//VirtualMachine UVEs
class VmUveTableBase {
public:
    struct VmUveInterfaceState :public DBState {
        VmUveInterfaceState(const boost::uuids::uuid u,
                            const std::string cfg_name)
            : vm_uuid_(u), interface_cfg_name_(cfg_name) {}
        boost::uuids::uuid vm_uuid_;
        std::string interface_cfg_name_;
        std::string vm_name_;
    };

    class VmUveVmState: public DBState {
        public:
            VmUveVmState() : stat_(NULL) { };
            VmStat *stat_;
    };
    typedef boost::shared_ptr<VmUveEntryBase> VmUveEntryPtr;
    typedef std::map<const boost::uuids::uuid, VmUveEntryPtr> UveVmMap;
    typedef std::pair<const boost::uuids::uuid, VmUveEntryPtr> UveVmPair;

    VmUveTableBase(Agent *agent, uint32_t default_intvl);
    virtual ~VmUveTableBase();
    void RegisterDBClients();
    void Shutdown(void);
    virtual void DispatchVmMsg(const UveVirtualMachineAgent &uve);
    bool TimerExpiry();

protected:
    virtual void VmStatCollectionStart(VmUveVmState *state, const VmEntry *vm);
    virtual void VmStatCollectionStop(VmUveVmState *state);
    VmUveEntryBase* UveEntryFromVm(const boost::uuids::uuid &u);
    virtual void SendVmDeleteMsg(const std::string &vm_config_name);

    UveVmMap uve_vm_map_;
    Agent *agent_;
    /* For exclusion between kTaskFlowStatsCollector and kTaskDBExclude */
    tbb::mutex uve_vm_map_mutex_;
private:
    virtual VmUveEntryPtr Allocate(const VmEntry *vm);
    void InterfaceNotify(DBTablePartBase *partition, DBEntryBase *e);
    void VmNotify(DBTablePartBase *partition, DBEntryBase *e);
    void InterfaceAddHandler(const VmEntry* vm, const VmInterface *vmi);
    void InterfaceDeleteHandler(const boost::uuids::uuid &u,
                                const std::string &intf_cfg_name);
    void UpdateVmName(const boost::uuids::uuid &u, const std::string &vm_name);
    void MarkChanged(const boost::uuids::uuid &u);
    VmUveEntryBase* Add(const VmEntry *vm, bool vm_notify);
    void Delete(const boost::uuids::uuid &u);
    void Change(const VmEntry *vm);
    void set_expiry_time(int time);
    void SendVmMsg(VmUveEntryBase *entry, const boost::uuids::uuid &u);

    DBTableBase::ListenerId intf_listener_id_;
    DBTableBase::ListenerId vm_listener_id_;
    // Last visited VmEntry by timer
    boost::uuids::uuid timer_last_visited_;
    Timer *timer_;
    int expiry_time_;
    DISALLOW_COPY_AND_ASSIGN(VmUveTableBase);
};

#endif // vnsw_agent_vm_uve_table_base_h
