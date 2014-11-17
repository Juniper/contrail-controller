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
                            const VmInterface::FloatingIpSet &fip_l)
            : vm_uuid_(u), fip_list_(fip_l) {}
        boost::uuids::uuid vm_uuid_;
        VmInterface::FloatingIpSet fip_list_;
    };

    class VmUveVmState: public DBState {
        public:
            VmUveVmState() : stat_(NULL) { };
            VmStat *stat_;
    };
    typedef boost::shared_ptr<VmUveEntryBase> VmUveEntryPtr;
    typedef std::map<const VmEntry*, VmUveEntryPtr> UveVmMap;
    typedef std::pair<const VmEntry*, VmUveEntryPtr> UveVmPair;

    VmUveTableBase(Agent *agent);
    virtual ~VmUveTableBase();
    void RegisterDBClients();
    void Shutdown(void);
    virtual void DispatchVmMsg(const UveVirtualMachineAgent &uve);

protected:
    virtual void VmStatCollectionStart(VmUveVmState *state, const VmEntry *vm);
    virtual void VmStatCollectionStop(VmUveVmState *state);
    VmUveEntryBase* UveEntryFromVm(const VmEntry *vm);
    UveVmMap uve_vm_map_;
    Agent *agent_;
private:
    virtual VmUveEntryPtr Allocate(const VmEntry *vm);
    const VmEntry *VmUuidToVm(const boost::uuids::uuid u);
    void InterfaceNotify(DBTablePartBase *partition, DBEntryBase *e);
    void VmNotify(DBTablePartBase *partition, DBEntryBase *e);
    void InterfaceAddHandler(const VmEntry* vm, const Interface* intf,
                             const VmInterface::FloatingIpSet &old_list);
    void InterfaceDeleteHandler(const VmEntry* vm, const Interface* intf);
    void SendVmMsg(const VmEntry *vm);
    VmUveEntryBase* Add(const VmEntry *vm, bool vm_notify);
    void Delete(const VmEntry *vm);
    virtual void SendVmDeleteMsg(const VmEntry *vm);

    DBTableBase::ListenerId intf_listener_id_;
    DBTableBase::ListenerId vm_listener_id_;
    DISALLOW_COPY_AND_ASSIGN(VmUveTableBase);
};

#endif // vnsw_agent_vm_uve_table_base_h
