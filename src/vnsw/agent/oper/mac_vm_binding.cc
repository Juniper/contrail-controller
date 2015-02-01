/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/types.h>
#include <net/ethernet.h>
#include <boost/uuid/uuid_io.hpp>

#include "base/logging.h"
#include "db/db.h"
#include "db/db_entry.h"
#include "db/db_table.h"
#include "net/address_util.h"

#include "oper/operdb_init.h"
#include "oper/agent_types.h"
#include "oper/mac_vm_binding.h"
#include "oper/interface.h"
#include "oper/vm_interface.h"
#include "oper/vn.h"

MacVmBinding::MacVmBinding(const Agent *agent) : agent_(agent),
    interface_listener_id_(DBTableBase::kInvalidId),
    mac_vm_binding_set_() {
}

MacVmBinding::~MacVmBinding() {
    if (interface_listener_id_ != DBTableBase::kInvalidId)
        agent_->interface_table()->Unregister(interface_listener_id_);
}

void MacVmBinding::Register() {
    interface_listener_id_ =
        agent_->interface_table()->
        Register(boost::bind(&MacVmBinding::InterfaceNotify,
                             this, _1, _2));
}

void MacVmBinding::InterfaceNotify(DBTablePartBase *partition, DBEntryBase *e) {
    Interface *interface = static_cast<Interface *>(e);
    if (interface->type() != Interface::VM_INTERFACE)
        return;

    const VmInterface *vm_interface = static_cast<VmInterface *>(e);
    if (vm_interface->vm_mac().empty())
        return;

    boost::system::error_code ec;
    MacAddress address(vm_interface->vm_mac(), &ec);
    if (ec) {
        return;
    }

    MacVmBindingSet::iterator it = FindInterfaceUsingMac(address, interface);
    if (it != mac_vm_binding_set_.end())
        mac_vm_binding_set_.erase(it);

    if (!e->IsDeleted()) {
        if (!vm_interface->vn() || vm_interface->vn()->GetVxLanId() == 0) {
            return;
        }
        // assumed that VM mac does not change
        MacVmBindingKey key(address, vm_interface->vn()->GetVxLanId(), interface);
        mac_vm_binding_set_.insert(key);
    }
}

MacVmBinding::MacVmBindingSet::iterator
MacVmBinding::FindInterfaceUsingMac(MacAddress &address,
                                    const Interface *interface) {
    MacVmBindingSet::iterator it =
        mac_vm_binding_set_.lower_bound(MacVmBindingKey(address, 0, NULL));
    while (it != mac_vm_binding_set_.end()) {
        if (it->interface == interface)
            return it;
        it++;
    }

    return it;
}

const Interface *
MacVmBinding::FindMacVmBinding(const MacAddress &address, int vxlan) const {
    MacVmBindingKey key(address, vxlan, NULL);
    MacVmBindingSet::iterator it = mac_vm_binding_set_.find(key);
    if (it == mac_vm_binding_set_.end())
        return NULL;
    return it->interface.get();
}
