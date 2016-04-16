/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/vm_uve_entry.h>
#include <uve/agent_uve_base.h>

using namespace std;

VmUveEntryBase::VmUveEntryBase(Agent *agent, const string &vm_name)
    : agent_(agent), interface_tree_(), uve_info_(), changed_(true),
    deleted_(false), renewed_(false), mutex_(), add_by_vm_notify_(false),
    vm_config_name_(vm_name), vm_name_() {
}

VmUveEntryBase::~VmUveEntryBase() {
}

bool VmUveEntryBase::Update(const VmEntry *vm) {
    if (vm_config_name_ != vm->GetCfgName()) {
        vm_config_name_ = vm->GetCfgName();
        return true;
    }
    return false;
}

void VmUveEntryBase::InterfaceAdd(const std::string &intf_cfg_name) {
    InterfaceSet::iterator it = interface_tree_.find(intf_cfg_name);
    if (it == interface_tree_.end()) {
        interface_tree_.insert(intf_cfg_name);
    }
}

void VmUveEntryBase::InterfaceDelete(const std::string &intf_cfg_name) {
    InterfaceSet::iterator intf_it = interface_tree_.find(intf_cfg_name);
    if (intf_it != interface_tree_.end()) {
        interface_tree_.erase(intf_it);
    }
    if (!add_by_vm_notify_ && (interface_tree_.size() == 0)) {
        renewed_ = false;
        deleted_ = true;
    }
    if (interface_tree_.size() == 0) {
        vm_name_ = "";
    }
}

bool VmUveEntryBase::FrameVmMsg(const boost::uuids::uuid &u,
                                UveVirtualMachineAgent *uve) {
    bool changed = false;
    assert(!deleted_);
    uve->set_name(vm_config_name_);
    vector<string> s_intf_list;

    if (!uve_info_.__isset.uuid) {
        uve->set_uuid(to_string(u));
        uve_info_.set_uuid(to_string(u));
        changed = true;
    }
    if (!uve_info_.__isset.vm_name ||
        (uve_info_.get_vm_name() != vm_name_)) {
        uve->set_vm_name(vm_name_);
        uve_info_.set_vm_name(vm_name_);
    }

    InterfaceSet::iterator it = interface_tree_.begin();
    while(it != interface_tree_.end()) {
        s_intf_list.push_back(*it);
        ++it;
    }

    if (UveVmInterfaceListChanged(s_intf_list)) {
        uve->set_interface_list(s_intf_list);
        uve_info_.set_interface_list(s_intf_list);
        changed = true;
    }

    string hostname = agent_->host_name();
    if (UveVmVRouterChanged(hostname)) {
        uve->set_vrouter(hostname);
        uve_info_.set_vrouter(hostname);
        changed = true;
    }

    return changed;
}

bool VmUveEntryBase::UveVmVRouterChanged(const string &new_value) const {
    if (!uve_info_.__isset.vrouter) {
        return true;
    }
    if (new_value.compare(uve_info_.get_vrouter()) == 0) {
        return false;
    }
    return true;
}

bool VmUveEntryBase::UveVmInterfaceListChanged
    (const vector<string> &new_list)
    const {
    if (new_list != uve_info_.get_interface_list()) {
        return true;
    }
    return false;
}

void VmUveEntryBase::Reset() {
    UveVirtualMachineAgent uve;

    interface_tree_.clear();
    uve_info_ = uve;

    deleted_ = true;
    renewed_ = false;
    add_by_vm_notify_ = false;
}
