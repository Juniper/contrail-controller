/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/test/prouter_uve_table_test.h>

ProuterUveTableTest::ProuterUveTableTest(Agent *agent, uint32_t default_intvl) :
    ProuterUveTable(agent, default_intvl) {
}

ProuterUveTableTest::~ProuterUveTableTest() {
}

void ProuterUveTableTest::DispatchProuterMsg(const ProuterData &uve) {
    send_count_++;
    if (uve.get_deleted()) {
        delete_count_++;
    }
    uve_ = uve;
}

void ProuterUveTableTest::ClearCount() {
    send_count_ = 0;
    delete_count_ = 0;
    pi_send_count_ = 0;
    pi_delete_count_ = 0;
    li_send_count_ = 0;
    li_delete_count_ = 0;
}

void ProuterUveTableTest::DispatchPhysicalInterfaceMsg
    (const UvePhysicalInterfaceAgent &uve) {
    pi_send_count_++;
    if (uve.get_deleted()) {
        pi_delete_count_++;
    }
}

void ProuterUveTableTest::DispatchLogicalInterfaceMsg
    (const UveLogicalInterfaceAgent &uve) {
    li_send_count_++;
    if (uve.get_deleted()) {
        li_delete_count_++;
    }
    li_uve_ = uve;
}

uint32_t ProuterUveTableTest::PhysicalIntfListCount() const {
    return uve_phy_interface_map_.size();
}

uint32_t ProuterUveTableTest::LogicalIntfListCount() const {
    return uve_logical_interface_map_.size();
}

uint32_t ProuterUveTableTest::VMIListCount(const LogicalInterface *itf) const {
    LogicalInterfaceMap::const_iterator it = uve_logical_interface_map_.find
        (itf->GetUuid());
    if (it != uve_logical_interface_map_.end()) {
        LogicalInterfaceUveEntry *entry = it->second.get();
        return entry->vmi_list_.size();
    }
    return 0;
}
