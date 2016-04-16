/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vm_uve_entry_base_h
#define vnsw_agent_vm_uve_entry_base_h

#include <string>
#include <vector>
#include <set>
#include <map>
#include <tbb/mutex.h>
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

//The class that defines data-structures to store VirtualMachine information
//required for sending VirtualMachine UVE.
class VmUveEntryBase {
public:

    typedef std::set<std::string> InterfaceSet;

    VmUveEntryBase(Agent *agent, const std::string &vm_name);
    virtual ~VmUveEntryBase();
    const std::string &vm_config_name() const  { return vm_config_name_; }
    bool add_by_vm_notify() const { return add_by_vm_notify_; }
    void set_add_by_vm_notify(bool value) { add_by_vm_notify_ = value; }

    void InterfaceAdd(const std::string &intf_cfg_name);
    void InterfaceDelete(const std::string &intf_cfg_name);
    bool FrameVmMsg(const boost::uuids::uuid &u, UveVirtualMachineAgent *uve);
    bool Update(const VmEntry *vm);
    void set_changed(bool val) { changed_ = val; }
    bool changed() const { return changed_; }
    void set_deleted(bool value) { deleted_ = value; }
    bool deleted() const { return deleted_; }
    void set_renewed(bool value) { renewed_ = value; }
    bool renewed() const { return renewed_; }
    void set_vm_name(const std::string name) { vm_name_.assign(name); }
    virtual void Reset();
protected:

    Agent *agent_;
    InterfaceSet interface_tree_;
    UveVirtualMachineAgent uve_info_;
    // UVE entry is changed. Timer must generate UVE for this entry
    bool changed_;
    bool deleted_;
    bool renewed_;
    /* For exclusion between kTaskFlowStatsCollector and Agent::Uve */
    tbb::mutex mutex_;
private:
    bool UveVmInterfaceListChanged
        (const std::vector<std::string> &new_l) const;
    bool UveVmVRouterChanged(const std::string &new_value) const;

    bool add_by_vm_notify_;
    std::string vm_config_name_;
    std::string vm_name_; /* Name given by Nova during port notification */
    DISALLOW_COPY_AND_ASSIGN(VmUveEntryBase);
};
#endif // vnsw_agent_vm_uve_entry_base_h
