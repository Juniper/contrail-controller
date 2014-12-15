/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/test/prouter_uve_table_test.h>

ProuterUveTableTest::ProuterUveTableTest(Agent *agent) :
    ProuterUveTableBase(agent) {
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
}

