/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <db/db.h>
#include <cmn/agent_cmn.h>
#include <oper/interface_common.h>
#include <oper/interface.h>
#include <oper/vm_interface.h>
#include <uve/test/interface_uve_table_test.h>


InterfaceUveTableTest::InterfaceUveTableTest(Agent *agent, uint32_t intvl) :
    InterfaceUveStatsTable(agent, intvl) {
    ClearCount();
}

void InterfaceUveTableTest::DispatchInterfaceMsg(const UveVMInterfaceAgent &u) {
    send_count_++;
    if (u.get_deleted()) {
        delete_count_++;
    }
    uve_ = u;
}

void InterfaceUveTableTest::DispatchVMIStatsMsg(const VMIStats  &uve) {
    vmi_stats_send_count_++;
    if (uve.get_deleted()) {
        vmi_stats_delete_count_++;
    }
    uve_stats_ = uve;
}

void InterfaceUveTableTest::ClearCount() {
    send_count_ = 0;
    delete_count_ = 0;
    vmi_stats_send_count_ = 0;
    vmi_stats_delete_count_ = 0;
}

L4PortBitmap* InterfaceUveTableTest::GetVmIntfPortBitmap
    (const VmInterface* itf) {
    InterfaceMap::iterator it = interface_tree_.find(itf->cfg_name());
    if (it != interface_tree_.end()) {
        UveInterfaceEntry *entry = it->second.get();
        return &(entry->port_bitmap_);
    }
    return NULL;
}

VMIStats* InterfaceUveTableTest::InterfaceUveObject(const VmInterface *itf) {
    InterfaceMap::iterator it = interface_tree_.find(itf->cfg_name());
    if (it != interface_tree_.end()) {
        UveInterfaceEntry *entry = it->second.get();
        return &(entry->uve_stats_);
    }
    return NULL;
}

uint32_t InterfaceUveTableTest::GetVmIntfFipCount(const VmInterface* itf) {
    InterfaceMap::iterator it = interface_tree_.find(itf->cfg_name());
    if (it != interface_tree_.end()) {
        UveInterfaceEntry *entry = it->second.get();
        return entry->fip_tree_.size();
    }
    return 0;
}

const InterfaceUveTable::FloatingIp *InterfaceUveTableTest::GetVmIntfFip
    (const VmInterface* itf, const string &fip, const string &vn) {
    InterfaceMap::iterator it = interface_tree_.find(itf->cfg_name());
    if (it != interface_tree_.end()) {
        UveInterfaceEntry *entry = it->second.get();
        boost::system::error_code ec;
        Ip4Address ip = Ip4Address::from_string(fip, ec);
        FloatingIpPtr key(new FloatingIp(ip, vn));
        FloatingIpSet::iterator fip_it = entry->fip_tree_.find(key);
        if (fip_it != entry->fip_tree_.end()) {
            return (*fip_it).get();
        }
    }
    return NULL;
}

InterfaceUveTable::UveInterfaceEntry*
InterfaceUveTableTest::GetUveInterfaceEntry(const string &name) {
    InterfaceMap::iterator it = interface_tree_.find(name);
    if (it == interface_tree_.end()) {
        return NULL;
    }
    InterfaceUveTable::UveInterfaceEntry* entry = it->second.get();
    return entry;
}
