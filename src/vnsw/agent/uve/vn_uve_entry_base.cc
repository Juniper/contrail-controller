/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/vn_uve_entry_base.h>
#include <uve/agent_uve_base.h>

VnUveEntryBase::VnUveEntryBase(Agent *agent, const VnEntry *vn)
    : agent_(agent), vn_(vn), uve_info_(), interface_tree_(), vm_tree_(),
    add_by_vn_notify_(false), changed_(true), deleted_(false),
    renewed_(false) {
}

VnUveEntryBase::VnUveEntryBase(Agent *agent)
    : agent_(agent), vn_(NULL), uve_info_(), interface_tree_(),  vm_tree_(),
    add_by_vn_notify_(false), changed_(true), deleted_(false),
    renewed_(false) {
}

VnUveEntryBase::~VnUveEntryBase() {
}

void VnUveEntryBase::VmAdd(const string &vm) {
    if (vm != agent_->NullString()) {
        VmSet::iterator it = vm_tree_.find(vm);
        if (it == vm_tree_.end()) {
            vm_tree_.insert(vm);
        }
    }
}

void VnUveEntryBase::VmDelete(const string &vm) {
    if (vm != agent_->NullString()) {
        VmSet::iterator vm_it = vm_tree_.find(vm);
        if (vm_it != vm_tree_.end()) {
            vm_tree_.erase(vm_it);
        }
    }
}

void VnUveEntryBase::InterfaceAdd(const Interface *intf) {
    InterfaceSet::iterator it = interface_tree_.find(intf);
    if (it == interface_tree_.end()) {
        interface_tree_.insert(intf);
    }
}

void VnUveEntryBase::InterfaceDelete(const Interface *intf) {
    InterfaceSet::iterator intf_it = interface_tree_.find(intf);
    if (intf_it != interface_tree_.end()) {
        interface_tree_.erase(intf_it);
    }
    if (!add_by_vn_notify_ && (interface_tree_.size() == 0)) {
        renewed_ = false;
        deleted_ = true;
    }
}

bool VnUveEntryBase::BuildInterfaceVmList(UveVirtualNetworkAgent &s_vn) {
    bool changed = false;
    assert(!deleted_);

    s_vn.set_name(vn_->GetName());
    vector<string> vm_list;

    VmSet::iterator vm_it = vm_tree_.begin();
    while (vm_it != vm_tree_.end()) {
        vm_list.push_back(*vm_it);
        ++vm_it;
    }
    if (UveVnVmListChanged(vm_list)) {
        s_vn.set_virtualmachine_list(vm_list);
        uve_info_.set_virtualmachine_list(vm_list);
        changed = true;
    }

    vector<string> intf_list;
    InterfaceSet::iterator intf_it = interface_tree_.begin();
    while (intf_it != interface_tree_.end()) {
        const Interface *intf = *intf_it;
        const VmInterface *vm_port =
            static_cast<const VmInterface *>(intf);
        intf_list.push_back(vm_port->cfg_name());
        intf_it++;
    }
    if (UveVnInterfaceListChanged(intf_list)) {
        s_vn.set_interface_list(intf_list);
        uve_info_.set_interface_list(intf_list);
        changed = true;
    }

    return changed;
}

bool VnUveEntryBase::UveVnAclChanged(const string &name) const {
    if (!uve_info_.__isset.acl) {
        return true;
    }
    if (name.compare(uve_info_.get_acl()) != 0) {
        return true;
    }
    return false;
}

bool VnUveEntryBase::UveVnMirrorAclChanged(const string &name) const  {
    if (!uve_info_.__isset.mirror_acl) {
        return true;
    }
    if (name.compare(uve_info_.get_mirror_acl()) != 0) {
        return true;
    }
    return false;
}

bool VnUveEntryBase::UveVnInterfaceListChanged(const vector<string> &new_list)
                                           const {
    if (!uve_info_.__isset.interface_list) {
        return true;
    }
    if (new_list != uve_info_.get_interface_list()) {
        return true;
    }
    return false;
}

bool VnUveEntryBase::UveVnVmListChanged(const vector<string> &new_list) const {
    if (!uve_info_.__isset.virtualmachine_list) {
        return true;
    }
    if (new_list != uve_info_.get_virtualmachine_list()) {
        return true;
    }
    return false;
}

bool VnUveEntryBase::UveVnAclRuleCountChanged(int32_t size) const {
    if (!uve_info_.__isset.total_acl_rules) {
        return true;
    }
    if (size != uve_info_.get_total_acl_rules()) {
        return true;
    }
    return false;
}

bool VnUveEntryBase::FrameVnAclRuleCountMsg(const VnEntry *vn,
                                            UveVirtualNetworkAgent *uve) {
    bool changed = false;
    assert(!deleted_);
    uve->set_name(vn->GetName());

    int acl_rule_count;
    if (vn->GetAcl()) {
        acl_rule_count = vn->GetAcl()->Size();
    } else {
        acl_rule_count = 0;
    }

    if (UveVnAclRuleCountChanged(acl_rule_count)) {
        uve->set_total_acl_rules(acl_rule_count);
        uve_info_.set_total_acl_rules(acl_rule_count);
        changed = true;
    }

    return changed;
}

bool VnUveEntryBase::FrameVnMsg(const VnEntry *vn, UveVirtualNetworkAgent &uve) {
    bool changed = false;
    assert(!deleted_);
    uve.set_name(vn->GetName());

    string acl_name;
    int acl_rule_count;
    if (vn->GetAcl()) {
        acl_name = vn->GetAcl()->GetName();
        acl_rule_count = vn->GetAcl()->Size();
    } else {
        acl_name = agent_->NullString();
        acl_rule_count = 0;
    }

    if (UveVnAclChanged(acl_name)) {
        uve.set_acl(acl_name);
        uve_info_.set_acl(acl_name);
        changed = true;
    }

    if (UveVnAclRuleCountChanged(acl_rule_count)) {
        uve.set_total_acl_rules(acl_rule_count);
        uve_info_.set_total_acl_rules(acl_rule_count);
        changed = true;
    }

    if (vn->GetMirrorCfgAcl()) {
        acl_name = vn->GetMirrorCfgAcl()->GetName();
    } else {
        acl_name = agent_->NullString();
    }
    if (UveVnMirrorAclChanged(acl_name)) {
        uve.set_mirror_acl(acl_name);
        uve_info_.set_mirror_acl(acl_name);
        changed = true;
    }

    if (BuildInterfaceVmList(uve)) {
        changed = true;
    }
    return changed;
}

void VnUveEntryBase::Reset() {
    UveVirtualNetworkAgent uve;

    interface_tree_.clear();
    vm_tree_.clear();
    uve_info_ = uve;
    vn_ = NULL;

    deleted_ = true;
    renewed_ = false;
    add_by_vn_notify_ = false;
}
