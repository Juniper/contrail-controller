/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
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


    //Add the link and verify that Edge VM<->VMI is propogted to SI
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

    //Remove our change event handle get back the original
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

    //Change the VM object and verify that node event is propogated to
    //SI
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

TEST_F(IFMapDependencyManagerTest, NodeEventAddDeleteAddPropogate) {
    typedef IFMapDependencyManagerTest_NodeEventAddDeleteAddPropogate_Test TestClass;

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

    IFMapTable *vm_cfg_table = IFMapTable::FindTable(database_, "virtual-machine");
    ASSERT_TRUE(vm_cfg_table);
    IFMapNode *vm = vm_cfg_table->FindNode("vm-1");
    ASSERT_TRUE(vm);

    boost::uuids::uuid vm_uuid;
    agent_->vm_table()->IFNodeToUuid(vm, vm_uuid);
    ASSERT_TRUE(vm_uuid != boost::uuids::nil_uuid());

    VmKey key(vm_uuid);
    VmEntry *vme = static_cast<VmEntry *>(agent_->vm_table()->FindActiveEntry(&key));
    ASSERT_TRUE(vme);

    //Take a ref to VM's Config node to ensure that Delete does not free
    //config node
    IFMapDependencyManager *dep =
        agent_->oper_db()->dependency_manager();

    IFMapDependencyManager::IFMapNodePtr vm_node_ref = dep->SetState(vm);

    //Delete the VM object and verify that change on SI is invoked
    ifmap_test_util::IFMapMsgNodeDelete(database_, "virtual-machine", "vm-1");
    task_util::WaitForIdle();

    //Make sure config node is delete marked and oper db entry is deleted
    ASSERT_TRUE(vm->IsDeleted());
    vme = static_cast<VmEntry *>(agent_->vm_table()->FindActiveEntry(&key));
    ASSERT_FALSE(vme);

    //Add the VM Object again and ensure that SI is invoked, config node
    //is no more delete marked and oper entry exists
    ASSERT_NE(0, change_list_.size());
    ifmap_test_util::IFMapMsgNodeAdd(database_, "virtual-machine", "vm-1");
    task_util::WaitForIdle();

    ASSERT_FALSE(vm->IsDeleted());
    agent_->vm_table()->IFNodeToUuid(vm, vm_uuid);
    ASSERT_TRUE(vm_uuid != boost::uuids::nil_uuid());
    VmKey new_key(vm_uuid);
    vme = static_cast<VmEntry *>(agent_->vm_table()->FindActiveEntry(&new_key));
    ASSERT_TRUE(vme);

    std::vector<IFMapNode *>::iterator it;
    int seen = 0;
    for (it = change_list_.begin(); it != change_list_.end(); it++) {
        IFMapNode *n = *it;
        if (n->name() == "si-1")
            seen = 1;
    }
    EXPECT_EQ(seen, 1);

    vm_node_ref = NULL;
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
