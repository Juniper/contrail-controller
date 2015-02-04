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

void MacVmBinding::AddMacVmBinding(uint32_t vrf_id,
                                   const VmInterface *vm_interface) {
    UpdateBinding(vm_interface, false, vrf_id);
}

void MacVmBinding::DeleteMacVmBinding(uint32_t vrf_id,
                                      const VmInterface *vm_interface) {
    UpdateBinding(vm_interface, true, vrf_id);
}

void MacVmBinding::UpdateBinding(const VmInterface *vm_interface,
                                 bool del, uint32_t vrf_id) {
    //If add operation vm_mac should be set to process.
    if (!del && vm_interface->vm_mac().empty())
        return;

    boost::system::error_code ec;
    MacAddress address(vm_interface->vm_mac(), &ec);
    if (ec) {
        return;
    }

    //delete specified mac on interface
    MacVmBindingSet::iterator it = FindInterface(vm_interface);
    if (it != mac_vm_binding_set_.end())
        mac_vm_binding_set_.erase(it);

    //Add request
    if (!del) {
        // assumed that VM mac does not change
        MacVmBindingKey key(address, vrf_id, vm_interface);
        mac_vm_binding_set_.insert(key);
    }
}

MacVmBinding::MacVmBindingSet::iterator
MacVmBinding::FindInterface(const Interface *interface) {
    MacVmBindingSet::iterator it = mac_vm_binding_set_.begin();
    while (it != mac_vm_binding_set_.end()) {
        if (it->interface == interface)
            return it;
        it++;
    }

    return it;
}

const Interface *
MacVmBinding::FindMacVmBinding(const MacAddress &address,
                               uint32_t vrf_id) const {
    MacVmBindingKey key(address, vrf_id, NULL);
    MacVmBindingSet::iterator it = mac_vm_binding_set_.find(key);
    if (it == mac_vm_binding_set_.end())
        return NULL;
    return it->interface.get();
}
