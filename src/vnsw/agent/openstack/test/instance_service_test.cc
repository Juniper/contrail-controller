/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "testing/gunit.h"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/string_generator.hpp>

#include <protocol/TBinaryProtocol.h>

#include <iostream>
#include <stdexcept>
#include <sstream>
#include <time.h>

#include <async/TAsioAsync.h>
#include <async/TFuture.h>

#include <io/event_manager.h>
#include <db/db.h>

#include <cmn/agent_cmn.h>
#include <cfg/cfg_interface.h>

using namespace apache::thrift;
using boost::shared_ptr;

extern void InstanceInfoServiceServerInit(EventManager &evm, DB *db);

void RouterIdDepInit() {
}

namespace {
class NovaInfoServerTest : public ::testing::Test {
protected:
};

DB *db;

TEST_F(NovaInfoServerTest, Basic) {
    EventManager *evm = new EventManager();

    db = new DB();
    DB::RegisterFactory("db.cfg_int.0", &CfgIntTable::CreateTable);
    CfgIntTable *ctable = static_cast<CfgIntTable *>(db->CreateTable("db.cfg_int.0"));
    assert(ctable);
    InstanceInfoServiceServerInit(*evm, db);

    evm->Run();
}

} //namespace

int main (int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
