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

MacVmBinding::MacVmBinding() : mac_vm_binding_set_() {
}

MacVmBinding::~MacVmBinding() {
}

void MacVmBinding::AddMacVmBinding(const VmInterface *vm_interface) {
    UpdateBinding(vm_interface, false);
}

void MacVmBinding::DeleteMacVmBinding(const VmInterface *vm_interface) {
    UpdateBinding(vm_interface, true);
}

void MacVmBinding::UpdateBinding(const VmInterface *vm_interface,
                                 bool del) {
    if (vm_interface->vm_mac().empty())
        return;

    boost::system::error_code ec;
    MacAddress address(vm_interface->vm_mac(), &ec);
    if (ec) {
        return;
    }

    MacVmBindingSet::iterator it = FindInterfaceUsingMac(address, vm_interface);
    if (it != mac_vm_binding_set_.end())
        mac_vm_binding_set_.erase(it);

    if (!del) {
        if (!vm_interface->vn() || vm_interface->vn()->GetVxLanId() == 0) {
            return;
        }
        // assumed that VM mac does not change
        MacVmBindingKey key(address, vm_interface->vn()->GetVxLanId(),
                            vm_interface);
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
