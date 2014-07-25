/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/test/vn_uve_table_test.h>
#include <uve/test/vn_uve_entry_test.h>

VnUveTableTest::VnUveTableTest(Agent *agent) 
    : VnUveTable(agent), send_count_(0), delete_count_(0), uve_() {
}

const VnUveEntry::VnStatsSet* VnUveTableTest::FindInterVnStats
    (const string &vn) {
    UveVnMap::iterator it = uve_vn_map_.find(vn);
    if (it == uve_vn_map_.end()) {
        return NULL;
    }

    VnUveEntryPtr vn_uve_entry(it->second);
    VnUveEntryTest *uve = static_cast<VnUveEntryTest *>(vn_uve_entry.get());
    return uve->inter_vn_stats();
}

const VnUveEntry* VnUveTableTest::GetVnUveEntry(const string &vn) {
    UveVnMap::iterator it = uve_vn_map_.find(vn);
    if (it == uve_vn_map_.end()) {
        return NULL;
    }

    VnUveEntryPtr vn_uve_entry(it->second);
    return vn_uve_entry.get();
}

int VnUveTableTest::GetVnUveInterfaceCount(const std::string &vn) {
    UveVnMap::iterator it = uve_vn_map_.find(vn);
    if (it == uve_vn_map_.end()) {
        return 0;
    }

    VnUveEntryPtr vn_uve_entry(it->second);
    VnUveEntryTest *uve = static_cast<VnUveEntryTest *>(vn_uve_entry.get());
    return uve->InterfaceCount();
}

int VnUveTableTest::GetVnUveVmCount(const std::string &vn) {
    UveVnMap::iterator it = uve_vn_map_.find(vn);
    if (it == uve_vn_map_.end()) {
        return 0;
    }

    VnUveEntryPtr vn_uve_entry(it->second);
    VnUveEntryTest *uve = static_cast<VnUveEntryTest *>(vn_uve_entry.get());
    return uve->VmCount();
}

L4PortBitmap* VnUveTableTest::GetVnUvePortBitmap(const std::string &vn) {
    UveVnMap::iterator it = uve_vn_map_.find(vn);
    if (it == uve_vn_map_.end()) {
        return NULL;
    }

    VnUveEntryPtr vn_uve_entry(it->second);
    VnUveEntryTest *uve = static_cast<VnUveEntryTest *>(vn_uve_entry.get());
    return uve->port_bitmap();
}

UveVirtualNetworkAgent* VnUveTableTest::VnUveObject(const string &vn) {
    UveVnMap::iterator it = uve_vn_map_.find(vn);
    if (it == uve_vn_map_.end()) {
        return NULL;
    }

    VnUveEntryTest *uve = static_cast<VnUveEntryTest *>(it->second.get());
    return uve->uve_info();
}

void VnUveTableTest::DispatchVnMsg(const UveVirtualNetworkAgent &uve) { 
    send_count_++; 
    if (uve.get_deleted()) {
        delete_count_++;
    }
    uve_ = uve;
}

void VnUveTableTest::ClearCount() {
    send_count_ = 0;
    delete_count_ = 0;
}

VnUveTable::VnUveEntryPtr VnUveTableTest::Allocate(const VnEntry *vn) {
    VnUveEntryPtr uve(new VnUveEntryTest(agent_, vn));
    return uve;
}

VnUveTable::VnUveEntryPtr VnUveTableTest::Allocate() {
    VnUveEntryPtr uve(new VnUveEntryTest(agent_));
    return uve;
}

void VnUveTableTest::SendVnStatsMsg_Test(const VnEntry *vn,
                                         bool only_vrf_stats) {
    SendVnStatsMsg(vn, only_vrf_stats);
}
