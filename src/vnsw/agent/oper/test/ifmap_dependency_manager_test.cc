/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "cmn/agent_cmn.h"
#include "oper/ifmap_dependency_manager.h"

#include <boost/bind.hpp>

#include "base/test/task_test_util.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "db/db_table_partition.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_agent_table.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/test/ifmap_test_util.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"
#include <test/test_cmn_util.h>
#include <oper/service_instance.h>

class IFMapDependencyManagerTest : public ::testing::Test {
  protected:
    IFMapDependencyManagerTest() {
    }

    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        database_ = agent_->db();
        dependency_manager_ = agent_->oper_db()->dependency_manager();
    }

    virtual void TearDown() {
        change_list_.clear();
    }

    void ChangeEventHandler(IFMapNode *node, DBEntry *entry) {
        change_list_.push_back(node);
    }

    Agent *agent_;
    DB *database_;
    std::vector<IFMapNode *> change_list_;
    IFMapDependencyManager *dependency_manager_;
};

TEST_F(IFMapDependencyManagerTest, EdgeEventPropogate) {
    typedef IFMapDependencyManagerTest_EdgeEventPropogate_Test TestClass;

    ifmap_test_util::IFMapMsgNodeAdd(database_, "service-instance", "si-1");
    ifmap_test_util::IFMapMsgNodeAdd(database_, "virtual-machine", "vm-1");
    ifmap_test_util::IFMapMsgNodeAdd(database_,
            "virtual-machine-interface", "vmi-1");
    task_util::WaitForIdle();

    ifmap_test_util::IFMapMsgLink(database_, "service-instance", "si-1",
                                  "virtual-machine", "vm-1",
                                  "virtual-machine-service-instance");
    task_util::WaitForIdle();

    dependency_manager_->Unregister("service-instance");
    dependency_manager_->Register(
        "service-instance",
        boost::bind(&TestClass::ChangeEventHandler, this, _1, _2));


    ifmap_test_util::IFMapMsgLink(database_, "virtual-machine", "vm-1",
                                  "virtual-machine-interface", "vmi-1",
                                  "virtual-machine-interface-virtual-machine");

    task_util::WaitForIdle();

    ASSERT_NE(0, change_list_.size());

    std::vector<IFMapNode *>::iterator it;
    int seen = 0;
    for (it = change_list_.begin(); it != change_list_.end(); it++) {
        IFMapNode *n = *it;
        if (n->name() == "si-1")
            seen = 1;
    }

    EXPECT_EQ(seen, 1);

    dependency_manager_->Unregister("service-instance");
    agent_->service_instance_table()->Initialize(agent_->cfg()->cfg_graph(),
                dependency_manager_);
    ifmap_test_util::IFMapMsgUnlink(database_, "service-instance", "si-1",
                                  "virtual-machine", "vm-1",
                                  "virtual-machine-service-instance");

    ifmap_test_util::IFMapMsgUnlink(database_, "virtual-machine", "vm-1",
                                  "virtual-machine-interface", "vmi-1",
                                  "virtual-machine-interface-virtual-machine");
    ifmap_test_util::IFMapMsgNodeDelete(database_, "service-instance", "si-1");
    ifmap_test_util::IFMapMsgNodeDelete(database_, "virtual-machine", "vm-1");
    ifmap_test_util::IFMapMsgNodeDelete(database_,
            "virtual-machine-interface", "vmi-1");

    task_util::WaitForIdle();
}

TEST_F(IFMapDependencyManagerTest, NodeEventPropogate) {
    typedef IFMapDependencyManagerTest_NodeEventPropogate_Test TestClass;

    ifmap_test_util::IFMapMsgNodeAdd(database_, "service-instance", "si-1");
    ifmap_test_util::IFMapMsgNodeAdd(database_, "virtual-machine", "vm-1");
    ifmap_test_util::IFMapMsgNodeAdd(database_,
            "virtual-machine-interface", "vmi-1");
    task_util::WaitForIdle();

    ifmap_test_util::IFMapMsgLink(database_, "service-instance", "si-1",
                                  "virtual-machine", "vm-1",
                                  "virtual-machine-service-instance");
    task_util::WaitForIdle();

    dependency_manager_->Unregister("service-instance");
    dependency_manager_->Register(
        "service-instance",
        boost::bind(&TestClass::ChangeEventHandler, this, _1, _2));


    ifmap_test_util::IFMapMsgLink(database_, "virtual-machine", "vm-1",
                                  "virtual-machine-interface", "vmi-1",
                                  "virtual-machine-interface-virtual-machine");

    task_util::WaitForIdle();

    change_list_.clear();
    ifmap_test_util::IFMapMsgNodeAdd(database_, "virtual-machine", "vm-1");
    task_util::WaitForIdle();

    ASSERT_NE(0, change_list_.size());

    std::vector<IFMapNode *>::iterator it;
    int seen = 0;
    for (it = change_list_.begin(); it != change_list_.end(); it++) {
        IFMapNode *n = *it;
        if (n->name() == "si-1")
            seen = 1;
    }

    EXPECT_EQ(seen, 1);

    dependency_manager_->Unregister("service-instance");
    agent_->service_instance_table()->Initialize(agent_->cfg()->cfg_graph(),
                dependency_manager_);
    ifmap_test_util::IFMapMsgUnlink(database_, "service-instance", "si-1",
                                  "virtual-machine", "vm-1",
                                  "virtual-machine-service-instance");

    ifmap_test_util::IFMapMsgUnlink(database_, "virtual-machine", "vm-1",
                                  "virtual-machine-interface", "vmi-1",
                                  "virtual-machine-interface-virtual-machine");
    ifmap_test_util::IFMapMsgNodeDelete(database_, "service-instance", "si-1");
    ifmap_test_util::IFMapMsgNodeDelete(database_, "virtual-machine", "vm-1");
    ifmap_test_util::IFMapMsgNodeDelete(database_,
            "virtual-machine-interface", "vmi-1");

    task_util::WaitForIdle();
}


static void SetUp() {
}

static void TearDown() {
}

int main(int argc, char **argv) {

    GETUSERARGS();

    client = TestInit(init_file, ksync_init, false, false, false);
    usleep(100000);
    client->WaitForIdle();

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    client->WaitForIdle();
    delete client;
    return ret;

}
