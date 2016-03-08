/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <vrouter/ksync/ksync_init.h>
#include <cmn/agent_cmn.h>
#include <base/task.h>
#include <io/event_manager.h>
#include <base/util.h>
#include <ifmap/ifmap_agent_parser.h>
#include <ifmap/ifmap_agent_table.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface_common.h>
#include <oper/mirror_table.h>
#include <uve/agent_uve.h>

#include "testing/gunit.h"
#include "test_cmn_util.h"
#include "pkt/test/test_flow_util.h"
#include "uve/test/test_uve_util.h"
#include "uve/test/prouter_uve_table_test.h"
#include "ksync/ksync_sock_user.h"
#include <uve/test/vrouter_uve_entry_test.h>

using namespace std;

void RouterIdDepInit(Agent *agent) {
}

class UveProuterUveTest : public ::testing::Test {
public:
    UveProuterUveTest() : util_(), agent_(Agent::GetInstance()) {
    }
    TestUveUtil util_;
    Agent *agent_;
};

TEST_F(UveProuterUveTest, ProuterAddDel_1) {
    AgentUveStats *u = static_cast<AgentUveStats *>(Agent::GetInstance()->uve());
    ProuterUveTableTest *pr = static_cast<ProuterUveTableTest *>
        (u->prouter_uve_table());
    pr->ClearCount();
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (u->vrouter_uve_entry());
    vr->clear_count();

    AddPhysicalDevice("prouter1", 1);
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) != NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();

    WAIT_FOR(1000, 500, (pr->send_count() == 1U));
    WAIT_FOR(1000, 500, (vr->vrouter_msg_count() == 1U));
    const VrouterAgent &uve = vr->last_sent_vrouter();
    EXPECT_EQ(1U, uve.get_embedded_prouter_list().size());
    const ProuterData &pr_uve = pr->last_sent_uve();
    EXPECT_STREQ(pr_uve.get_agent_name().c_str(), agent_->agent_name().c_str());

    DeletePhysicalDevice("prouter1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) == NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 0U));

    WAIT_FOR(1000, 500, (pr->delete_count() == 1U));
    const VrouterAgent &uve2 = vr->last_sent_vrouter();
    EXPECT_EQ(0U, uve2.get_embedded_prouter_list().size());
}

//Verify that no Prouter UVEs are sent when physical interfaces added/removed.
//(when physical interfaces are not associated with physical devices)
TEST_F(UveProuterUveTest, PhysicalInterfaceAddDel_1) {
    AgentUveStats *u = static_cast<AgentUveStats *>(Agent::GetInstance()->uve());
    ProuterUveTableTest *pr = static_cast<ProuterUveTableTest *>
        (u->prouter_uve_table());
    pr->ClearCount();

    AddPhysicalInterface("pi1", 1, "pid1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") != NULL));

    DeletePhysicalInterface("pi1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") == NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 0U));

    EXPECT_EQ(0U, pr->send_count());
    EXPECT_EQ(0U, pr->delete_count());

    util_.EnqueueSendPIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->PhysicalIntfListCount() == 0U));
}

TEST_F(UveProuterUveTest, PhysicalInterfaceAddDel_2) {
    AgentUveStats *u = static_cast<AgentUveStats *>(Agent::GetInstance()->uve());
    ProuterUveTableTest *pr = static_cast<ProuterUveTableTest *>
        (u->prouter_uve_table());
    pr->ClearCount();

    //Add physical-device and physical-interface and add their association
    AddPhysicalDevice("prouter1", 1);
    AddPhysicalInterface("pi1", 1, "pid1");
    AddLink("physical-router", "prouter1", "physical-interface", "pi1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) != NULL));
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") != NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 1U));

    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_physical_interface_list().
                         size() == 1U));

    //Disassociate physical-device with physical-interface
    DelLink("physical-router", "prouter1", "physical-interface", "pi1");
    client->WaitForIdle();

    uint32_t send_count = pr->send_count();
    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->send_count() > send_count));

    //Verify the Uve
    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_physical_interface_list().
                         size() == 0U));
    //Delete physical-device and physical-interface
    DeletePhysicalInterface("pi1");
    DeletePhysicalDevice("prouter1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") == NULL));
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) == NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 0U));
    //Verify that Delete UVE is sent out
    WAIT_FOR(1000, 500, (pr->delete_count() == 1U));

    util_.EnqueueSendPIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->PhysicalIntfListCount() == 0U));
}

//Verify PhysicalInterface UVE is send on PhysicalInterface Add/del
TEST_F(UveProuterUveTest, PhysicalInterfaceAddDel_3) {
    AgentUveStats *u = static_cast<AgentUveStats *>(Agent::GetInstance()->uve());
    ProuterUveTableTest *pr = static_cast<ProuterUveTableTest *>
        (u->prouter_uve_table());
    pr->ClearCount();

    AddPhysicalInterface("pi1", 1, "pid1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") != NULL));

    util_.EnqueueSendPIUveTask();
    client->WaitForIdle();

    //Verify that PhysicalInterface UVE is sent
    WAIT_FOR(1000, 500, (pr->pi_send_count() > 0U));

    DeletePhysicalInterface("pi1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") == NULL));

    util_.EnqueueSendPIUveTask();
    client->WaitForIdle();

    //Verify that PhysicalInterface 'delete' UVE is sent
    WAIT_FOR(1000, 500, (pr->pi_delete_count() == 1U));
}

//Verify that no UVEs are sent when logical interfaces added/removed.
//(when logical interfaces are not associated with physical interfaces)
TEST_F(UveProuterUveTest, LogicalInterfaceAddDel_1) {
    AgentUveStats *u = static_cast<AgentUveStats *>(Agent::GetInstance()->uve());
    ProuterUveTableTest *pr = static_cast<ProuterUveTableTest *>
        (u->prouter_uve_table());
    pr->ClearCount();

    AddLogicalInterface("li1", 1, "lid1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") != NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 0U));

    DeleteLogicalInterface("li1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") == NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 0U));

    EXPECT_EQ(0U, pr->send_count());
    EXPECT_EQ(0U, pr->delete_count());

    //The following is required for cleanup of logical interface list
    util_.EnqueueSendLIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->LogicalIntfListCount() == 0U));
}

TEST_F(UveProuterUveTest, LogicalInterfaceAddDel_2) {
    AgentUveStats *u = static_cast<AgentUveStats *>(Agent::GetInstance()->uve());
    ProuterUveTableTest *pr = static_cast<ProuterUveTableTest *>
        (u->prouter_uve_table());
    pr->ClearCount();

    //Add physical-device and physical-interface and add their association
    AddPhysicalDevice("prouter1", 1);
    AddPhysicalInterface("pi1", 1, "pid1");
    AddLogicalInterface("li1", 1, "lid1");
    AddLink("physical-router", "prouter1", "physical-interface", "pi1");
    AddLink("physical-interface", "pi1", "logical-interface", "li1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) != NULL));
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") != NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") != NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 1U));

    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_physical_interface_list().
                         size() == 1U));

    //Disassociate logical-interface from physical_interface
    //uint32_t send_count = pr->send_count();
    DelLink("physical-interface", "pi1", "logical-interface", "li1");
    client->WaitForIdle();
    LogicalInterface *li = LogicalInterfaceGet(1, "li1");
    WAIT_FOR(1000, 500, (li->physical_interface() == NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();

    //Verify that Prouter UVE is not sent
    //EXPECT_TRUE((pr->send_count() == send_count));

    //Disassociate physical-device from physical-interface
    DelLink("physical-router", "prouter1", "physical-interface", "pi1");
    //Delete physical-device and physical-interface
    DeleteLogicalInterface("li1");
    DeletePhysicalInterface("pi1");
    DeletePhysicalDevice("prouter1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") == NULL));
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) == NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") == NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 0U));

    WAIT_FOR(1000, 500, (pr->delete_count() == 1U));

    util_.EnqueueSendPIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->PhysicalIntfListCount() == 0U));

    //The following is required for cleanup of logical interface list
    util_.EnqueueSendLIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->LogicalIntfListCount() == 0U));
}

TEST_F(UveProuterUveTest, LogicalInterfaceAddDel_3) {
    AgentUveStats *u = static_cast<AgentUveStats *>(Agent::GetInstance()->uve());
    ProuterUveTableTest *pr = static_cast<ProuterUveTableTest *>
        (u->prouter_uve_table());
    pr->ClearCount();

    //Add physical-device and physical-interface and add their association
    AddPhysicalDevice("prouter1", 1);
    AddPhysicalInterface("pi1", 1, "pid1");
    AddLogicalInterface("li1", 1, "lid1");
    AddLink("physical-router", "prouter1", "physical-interface", "pi1");
    AddLink("physical-interface", "pi1", "logical-interface", "li1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) != NULL));
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") != NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") != NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 1U));

    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_physical_interface_list().
                         size() == 1U));

    //Disassociate logical-interface from physical_interface
    uint32_t send_count = pr->send_count();
    DelLink("physical-interface", "pi1", "logical-interface", "li1");
    //Disassociate physical-device from physical-interface
    DelLink("physical-router", "prouter1", "physical-interface", "pi1");
    client->WaitForIdle();

    //Verify disassociations have happened
    LogicalInterface *li = LogicalInterfaceGet(1, "li1");
    WAIT_FOR(1000, 500, (li->physical_interface() == NULL));

    PhysicalInterface *pi = PhysicalInterfaceGet("pi1");
    WAIT_FOR(1000, 500, (pi->physical_device() == NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();

    WAIT_FOR(1000, 500, (pr->send_count() > send_count));
    //Verify the Uve
    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_physical_interface_list().
                         size() == 0U));

    //Delete physical-device and physical-interface
    DeleteLogicalInterface("li1");
    DeletePhysicalInterface("pi1");
    DeletePhysicalDevice("prouter1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") == NULL));
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) == NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") == NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 0U));
    WAIT_FOR(1000, 500, (pr->delete_count() == 1U));

    util_.EnqueueSendPIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->PhysicalIntfListCount() == 0U));

    //The following is required for cleanup of logical interface list
    util_.EnqueueSendLIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->LogicalIntfListCount() == 0U));
}

//Associate logical interface with prouter (instead of physical-interface)
TEST_F(UveProuterUveTest, LogicalInterfaceAddDel_4) {
    AgentUveStats *u = static_cast<AgentUveStats *>(Agent::GetInstance()->uve());
    ProuterUveTableTest *pr = static_cast<ProuterUveTableTest *>
        (u->prouter_uve_table());
    pr->ClearCount();

    //Add physical-device and logical-interface and add their association
    AddPhysicalDevice("prouter1", 1);
    AddLogicalInterface("li1", 1, "lid1");
    AddLink("physical-router", "prouter1", "logical-interface", "li1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) != NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") != NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 1U));

    EXPECT_EQ(0U, (pr->last_sent_uve().get_physical_interface_list().size()));
    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_logical_interface_list().
                         size() == 1U));

    uint32_t send_count = pr->send_count();
    //Disassociate physical-device from logical-interface
    DelLink("physical-router", "prouter1", "logical-interface", "li1");
    client->WaitForIdle();
    LogicalInterface *li = LogicalInterfaceGet(1, "li1");
    WAIT_FOR(1000, 500, (li->physical_device() == NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();

    WAIT_FOR(1000, 500, (pr->send_count() > send_count));
    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_logical_interface_list().
                         size() == 0U));

    //Delete physical-device and physical-interface
    DeleteLogicalInterface("li1");
    DeletePhysicalDevice("prouter1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) == NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") == NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 0U));
    WAIT_FOR(1000, 500, (pr->delete_count() == 1U));

    util_.EnqueueSendPIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->PhysicalIntfListCount() == 0U));

    //The following is required for cleanup of logical interface list
    util_.EnqueueSendLIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->LogicalIntfListCount() == 0U));
}

//Associate logical interface with prouter. Verify change of prouter for a
//logical-interface from one prouter to another.
TEST_F(UveProuterUveTest, LogicalInterfaceAddDel_5) {
    AgentUveStats *u = static_cast<AgentUveStats *>(Agent::GetInstance()->uve());
    ProuterUveTableTest *pr = static_cast<ProuterUveTableTest *>
        (u->prouter_uve_table());
    pr->ClearCount();

    //Add physical-device and logical-interface and add their association
    AddPhysicalDevice("prouter1", 1);
    AddLogicalInterface("li1", 1, "lid1");
    AddLink("physical-router", "prouter1", "logical-interface", "li1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) != NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") != NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 1U));

    EXPECT_EQ(0U, (pr->last_sent_uve().get_physical_interface_list().size()));
    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_logical_interface_list().
                         size() == 1U));

    //Add One more physical-device
    AddPhysicalDevice("prouter2", 2);
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(2) != NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 2U));

    uint32_t send_count = pr->send_count();
    //Change the prouter for logical-interface
    DelLink("physical-router", "prouter1", "logical-interface", "li1");
    AddLink("physical-router", "prouter2", "logical-interface", "li1");
    DeletePhysicalDevice("prouter1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) == NULL));
    LogicalInterface *li = LogicalInterfaceGet(1, "li1");
    WAIT_FOR(1000, 500, (li->physical_device() != NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();

    WAIT_FOR(1000, 500, (pr->send_count() > send_count));
    ProuterData uve = pr->last_sent_uve();
    if (uve.get_name() == "prouter1") {
        EXPECT_EQ(0U, (uve.get_logical_interface_list().size()));
    } else if (uve.get_name() == "prouter2") {
        EXPECT_EQ(1U, (uve.get_logical_interface_list().size()));
    }

    //Delete physical-device and logical-interface
    DelLink("physical-router", "prouter2", "logical-interface", "li1");
    DeleteLogicalInterface("li1");
    DeletePhysicalDevice("prouter2");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(2) == NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") == NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 0U));
    WAIT_FOR(1000, 500, (pr->delete_count() == 2U));

    //The following is required for cleanup of logical interface list
    util_.EnqueueSendLIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->LogicalIntfListCount() == 0U));
}

TEST_F(UveProuterUveTest, LogicalInterfaceAddDel_6) {
    AgentUveStats *u = static_cast<AgentUveStats *>(Agent::GetInstance()->uve());
    ProuterUveTableTest *pr = static_cast<ProuterUveTableTest *>
        (u->prouter_uve_table());
    pr->ClearCount();

    AddLogicalInterface("li1", 1, "lid1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") != NULL));

    util_.EnqueueSendLIUveTask();
    client->WaitForIdle();

    //Verify LogicalInterface UVE is sent
    WAIT_FOR(1000, 500, (pr->li_send_count() == 1U));

    DeleteLogicalInterface("li1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") == NULL));

    util_.EnqueueSendLIUveTask();
    client->WaitForIdle();

    //Verify LogicalInterface 'delete' UVE is sent
    WAIT_FOR(1000, 500, (pr->li_delete_count() == 1U));
}

//Delete PhysicalDevice before deleting logical interface
TEST_F(UveProuterUveTest, PhysicalDeviceDel_1) {
    AgentUveStats *u = static_cast<AgentUveStats *>(Agent::GetInstance()->uve());
    ProuterUveTableTest *pr = static_cast<ProuterUveTableTest *>
        (u->prouter_uve_table());
    pr->ClearCount();

    //Add physical-device and physical-interface and add their association
    AddPhysicalDevice("prouter1", 1);
    AddPhysicalInterface("pi1", 1, "pid1");
    AddLogicalInterface("li1", 1, "lid1");
    AddLink("physical-router", "prouter1", "physical-interface", "pi1");
    AddLink("physical-interface", "pi1", "logical-interface", "li1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) != NULL));
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") != NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") != NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 1U));

    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_physical_interface_list().
                         size() == 1U));

    //Disassociate physical-device from physical-interface and delete
    //physical-device
    DelLink("physical-router", "prouter1", "physical-interface", "pi1");
    DeletePhysicalDevice("prouter1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) == NULL));
    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 0U));

    WAIT_FOR(1000, 500, (1U == pr->delete_count()));

    //Disassociate logical-interface from physical_interface
    //Delete physical-device and physical-interface
    uint32_t send_count = pr->send_count();
    DelLink("physical-interface", "pi1", "logical-interface", "li1");
    DeleteLogicalInterface("li1");
    DeletePhysicalInterface("pi1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) == NULL));
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") == NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") == NULL));

    //Verify that no more UVEs are sent
    EXPECT_EQ(send_count, pr->send_count());

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->delete_count() == 1U));

    util_.EnqueueSendPIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->PhysicalIntfListCount() == 0U));

    //The following is required for cleanup of logical interface list
    util_.EnqueueSendLIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->LogicalIntfListCount() == 0U));
}

//Delete PhysicalDevice and associate Physical interface with different
//PhysicalDevice
TEST_F(UveProuterUveTest, PhysicalDeviceDel_2) {
    AgentUveStats *u = static_cast<AgentUveStats *>(Agent::GetInstance()->uve());
    ProuterUveTableTest *pr = static_cast<ProuterUveTableTest *>
        (u->prouter_uve_table());
    pr->ClearCount();

    //Add physical-device and physical-interface and add their association
    AddPhysicalDevice("prouter1", 1);
    AddPhysicalInterface("pi1", 1, "pid1");
    AddLogicalInterface("li1", 1, "lid1");
    AddLink("physical-router", "prouter1", "physical-interface", "pi1");
    AddLink("physical-interface", "pi1", "logical-interface", "li1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) != NULL));
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") != NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") != NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 1U));

    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_physical_interface_list().
                         size() == 1U));

    //Disassociate physical-device from physical-interface and delete
    //physical-device
    DelLink("physical-router", "prouter1", "physical-interface", "pi1");
    DeletePhysicalDevice("prouter1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) == NULL));
    PhysicalInterface *pi = PhysicalInterfaceGet("pi1");
    WAIT_FOR(1000, 500, (pi->physical_device() == NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 0U));
    WAIT_FOR(1000, 500, (1U == pr->delete_count()));

    //Re-add PhysicalDevice and its association to physical-interface
    uint32_t send_count = pr->send_count();
    AddPhysicalDevice("prouter2", 2);
    AddLink("physical-router", "prouter2", "physical-interface", "pi1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(2) != NULL));
    WAIT_FOR(1000, 500, (pi->physical_device() != NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 1U));

    WAIT_FOR(1000, 500, (pr->send_count() > send_count));

    //Disassociate logical-interface from physical_interface
    //Delete physical-device and physical-interface
    DelLink("physical-interface", "pi1", "logical-interface", "li1");
    DelLink("physical-router", "prouter2", "physical-interface", "pi1");
    DeleteLogicalInterface("li1");
    DeletePhysicalInterface("pi1");
    DeletePhysicalDevice("prouter2");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(2) == NULL));
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") == NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") == NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 0U));
    WAIT_FOR(1000, 500, (2U == pr->delete_count()));

    util_.EnqueueSendPIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->PhysicalIntfListCount() == 0U));

    //The following is required for cleanup of logical interface list
    util_.EnqueueSendLIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->LogicalIntfListCount() == 0U));
}

TEST_F(UveProuterUveTest, VMI_Logical_Assoc_1) {
    AgentUveStats *u = static_cast<AgentUveStats *>(Agent::GetInstance()->uve());
    ProuterUveTableTest *pr = static_cast<ProuterUveTableTest *>
        (u->prouter_uve_table());
    struct PortInfo input[] = {
        {"vmi1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };
    IntfCfgAdd(input, 0);
    AddLogicalInterface("li1", 1, "lid1");
    AddPort("vmi1", 1);
    AddLink("virtual-machine-interface", "vmi1", "logical-interface", "li1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") != NULL));
    WAIT_FOR(1000, 500, (VmInterfaceGet(1) != NULL));
    const VmInterface *vmi = VmInterfaceGet(1);
    const LogicalInterface *li = LogicalInterfaceGet(1, "li1");
    EXPECT_TRUE(vmi->logical_interface() == li->GetUuid());

    DelLink("virtual-machine-interface", "vmi1", "logical-interface", "li1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vmi->logical_interface() == nil_uuid()));

    DelPort("vmi1");
    DeleteLogicalInterface("li1");
    IntfCfgDel(input, 0);
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") == NULL));
    WAIT_FOR(1000, 500, (VmInterfaceGet(1) == NULL));

    //The following is required for cleanup of logical interface list
    util_.EnqueueSendLIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->LogicalIntfListCount() == 0U));
}

TEST_F(UveProuterUveTest, VMIAddDel_1) {
    AgentUveStats *u = static_cast<AgentUveStats *>(Agent::GetInstance()->uve());
    ProuterUveTableTest *pr = static_cast<ProuterUveTableTest *>
        (u->prouter_uve_table());
    pr->ClearCount();
    struct PortInfo input[] = {
        {"vmi1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    //Add physical-device and physical-interface and add their association
    IntfCfgAdd(input, 0);
    AddPhysicalDevice("prouter1", 1);
    AddPhysicalInterface("pi1", 1, "pid1");
    AddLogicalInterface("li1", 1, "lid1");
    AddPort("vmi1", 1);
    AddLink("physical-router", "prouter1", "physical-interface", "pi1");
    AddLink("physical-interface", "pi1", "logical-interface", "li1");
    AddLink("virtual-machine-interface", "vmi1", "logical-interface", "li1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) != NULL));
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") != NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") != NULL));
    WAIT_FOR(1000, 500, (VmInterfaceGet(1) != NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 1U));

    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_physical_interface_list().
                         size() == 1U));

    //Disassociate logical-interface from physical_interface
    DelLink("physical-interface", "pi1", "logical-interface", "li1");
    //Disassociate physical-device from physical-interface
    DelLink("physical-router", "prouter1", "physical-interface", "pi1");
    DelLink("virtual-machine-interface", "vmi1", "logical-interface", "li1");
    //Delete physical-device and physical-interface
    DelPort("vmi1");
    DeleteLogicalInterface("li1");
    DeletePhysicalInterface("pi1");
    DeletePhysicalDevice("prouter1");
    IntfCfgDel(input, 0);

    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") == NULL));
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) == NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") == NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 0U));
    WAIT_FOR(1000, 500, (1U == pr->delete_count()));

    util_.EnqueueSendPIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->PhysicalIntfListCount() == 0U));

    util_.EnqueueSendLIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->LogicalIntfListCount() == 0U));
}

TEST_F(UveProuterUveTest, VMIAddDel_2) {
    AgentUveStats *u = static_cast<AgentUveStats *>(Agent::GetInstance()->uve());
    ProuterUveTableTest *pr = static_cast<ProuterUveTableTest *>
        (u->prouter_uve_table());
    pr->ClearCount();
    struct PortInfo input[] = {
        {"vmi1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    //Add physical-device and physical-interface and add their association
    IntfCfgAdd(input, 0);
    AddPhysicalDevice("prouter1", 1);
    AddPhysicalInterface("pi1", 1, "pid1");
    AddLogicalInterface("li1", 1, "lid1");
    AddPort("vmi1", 1);
    AddLink("physical-router", "prouter1", "physical-interface", "pi1");
    AddLink("physical-interface", "pi1", "logical-interface", "li1");
    AddLink("virtual-machine-interface", "vmi1", "logical-interface", "li1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) != NULL));
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") != NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") != NULL));
    WAIT_FOR(1000, 500, (VmInterfaceGet(1) != NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 1U));

    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_physical_interface_list().
                         size() == 1U));
    
    uint32_t send_count = pr->send_count();
    //Disassociate VMI from logical-interface
    DelLink("virtual-machine-interface", "vmi1", "logical-interface", "li1");
    client->WaitForIdle();

    //Verify disassociations have happened
    LogicalInterface *li = LogicalInterfaceGet(1, "li1");
    WAIT_FOR(1000, 500, (li->vm_interface() == NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();

    //Verify that no Prouter UVE is sent after disassociation
    EXPECT_TRUE((pr->send_count() == send_count));

    //Disassociate logical-interface from physical_interface
    DelLink("physical-interface", "pi1", "logical-interface", "li1");
    //Disassociate physical-device from physical-interface
    DelLink("physical-router", "prouter1", "physical-interface", "pi1");
    //Delete physical-device and physical-interface
    DelPort("vmi1");
    DeleteLogicalInterface("li1");
    DeletePhysicalInterface("pi1");
    DeletePhysicalDevice("prouter1");
    IntfCfgDel(input, 0);
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") == NULL));
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) == NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") == NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 0U));
    WAIT_FOR(1000, 500, (1U == pr->delete_count()));

    util_.EnqueueSendPIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->PhysicalIntfListCount() == 0U));

    util_.EnqueueSendLIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->LogicalIntfListCount() == 0U));
}

TEST_F(UveProuterUveTest, VMIAddDel_3) {
    AgentUveStats *u = static_cast<AgentUveStats *>(Agent::GetInstance()->uve());
    ProuterUveTableTest *pr = static_cast<ProuterUveTableTest *>
        (u->prouter_uve_table());
    pr->ClearCount();
    struct PortInfo input[] = {
        {"vmi1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vmi1", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2}
    };

    //Add physical-device and physical-interface and add their association
    IntfCfgAdd(input, 0);
    AddPhysicalDevice("prouter1", 1);
    AddPhysicalInterface("pi1", 1, "pid1");
    AddLogicalInterface("li1", 1, "lid1");
    AddPort("vmi1", 1);
    AddLink("physical-router", "prouter1", "physical-interface", "pi1");
    AddLink("physical-interface", "pi1", "logical-interface", "li1");
    AddLink("virtual-machine-interface", "vmi1", "logical-interface", "li1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) != NULL));
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") != NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") != NULL));
    WAIT_FOR(1000, 500, (VmInterfaceGet(1) != NULL));

    //Create one more VMI and associate with the above logical interface
    IntfCfgAdd(input, 1);
    AddPort("vmi2", 2);
    AddLink("virtual-machine-interface", "vmi2", "logical-interface", "li1");
    client->WaitForIdle();

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 1U));

    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_physical_interface_list().
                         size() == 1U));

    uint32_t send_count = pr->send_count();
    //Disassociate one of the VMI from Logical interface
    DelLink("virtual-machine-interface", "vmi2", "logical-interface", "li1");
    client->WaitForIdle();

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();

    //Verify that no Prouter UVE is sent
    EXPECT_TRUE((pr->send_count() == send_count));

    //Disassociate logical-interface from physical_interface
    DelLink("physical-interface", "pi1", "logical-interface", "li1");
    //Disassociate physical-device from physical-interface
    DelLink("physical-router", "prouter1", "physical-interface", "pi1");
    DelLink("virtual-machine-interface", "vmi1", "logical-interface", "li1");
    //Delete physical-device and physical-interface
    DelPort("vmi1");
    DelPort("vmi2");
    DeleteLogicalInterface("li1");
    DeletePhysicalInterface("pi1");
    DeletePhysicalDevice("prouter1");
    IntfCfgDel(input, 0);
    IntfCfgDel(input, 1);

    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") == NULL));
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) == NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") == NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 0U));
    WAIT_FOR(1000, 500, (1U == pr->delete_count()));

    util_.EnqueueSendPIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->PhysicalIntfListCount() == 0U));

    util_.EnqueueSendLIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->LogicalIntfListCount() == 0U));
}

TEST_F(UveProuterUveTest, VMIAddDel_4) {
    AgentUveStats *u = static_cast<AgentUveStats *>(Agent::GetInstance()->uve());
    ProuterUveTableTest *pr = static_cast<ProuterUveTableTest *>
        (u->prouter_uve_table());
    pr->ClearCount();
    struct PortInfo input[] = {
        {"vmi1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vmi1", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2}
    };

    //Add physical-device and physical-interface and add their association
    IntfCfgAdd(input, 0);
    AddPhysicalDevice("prouter1", 1);
    AddPhysicalInterface("pi1", 1, "pid1");
    AddLogicalInterface("li1", 1, "lid1");
    AddPort("vmi1", 1);
    AddLink("physical-router", "prouter1", "physical-interface", "pi1");
    AddLink("physical-interface", "pi1", "logical-interface", "li1");
    AddLink("virtual-machine-interface", "vmi1", "logical-interface", "li1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) != NULL));
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") != NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") != NULL));
    WAIT_FOR(1000, 500, (VmInterfaceGet(1) != NULL));

    //Create one more VMI and associate with the above logical interface
    IntfCfgAdd(input, 1);
    AddPort("vmi2", 2);
    AddLink("virtual-machine-interface", "vmi2", "logical-interface", "li1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (VmInterfaceGet(2) != NULL));
    VmInterface *vm1 = VmInterfaceGet(1);
    VmInterface *vm2 = VmInterfaceGet(2);
    WAIT_FOR(1000, 500, (vm1->logical_interface() != nil_uuid()));
    WAIT_FOR(1000, 500, (vm2->logical_interface() != nil_uuid()));
    const LogicalInterface *li1 = LogicalInterfaceGet(1, "li1");

    util_.EnqueueSendLIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->LogicalIntfListCount() == 1U));
    WAIT_FOR(1000, 500, (pr->li_send_count() > 0U));
    WAIT_FOR(1000, 500, (pr->VMIListCount(li1) == 2U));

    WAIT_FOR(1000, 500, (pr->last_sent_li_uve().get_vm_interface_list().
                         size() == 2U));

    uint32_t send_count = pr->send_count();
    //Disassociate one of the VMI from Logical interface
    DelLink("virtual-machine-interface", "vmi2", "logical-interface", "li1");
    client->WaitForIdle();

    util_.EnqueueSendLIUveTask();
    client->WaitForIdle();

    //Verify that LI UVE is sent with one less VMI
    WAIT_FOR(1000, 500, (pr->last_sent_li_uve().get_vm_interface_list().
                         size() == 1U));
    EXPECT_TRUE((pr->li_send_count() > send_count));

    //Disassociate logical-interface from physical_interface
    DelLink("physical-interface", "pi1", "logical-interface", "li1");
    //Disassociate physical-device from physical-interface
    DelLink("physical-router", "prouter1", "physical-interface", "pi1");
    DelLink("virtual-machine-interface", "vmi1", "logical-interface", "li1");
    //Delete physical-device and physical-interface
    DelPort("vmi1");
    DelPort("vmi2");
    DeleteLogicalInterface("li1");
    DeletePhysicalInterface("pi1");
    DeletePhysicalDevice("prouter1");
    IntfCfgDel(input, 0);
    IntfCfgDel(input, 1);

    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") == NULL));
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) == NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") == NULL));

    util_.EnqueueSendProuterUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->ProuterUveCount() == 0U));

    util_.EnqueueSendPIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->PhysicalIntfListCount() == 0U));

    util_.EnqueueSendLIUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (pr->LogicalIntfListCount() == 0U));
    WAIT_FOR(1000, 500, (1U == pr->li_delete_count()));
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, false, true,
                      (10 * 60 * 1000), (10 * 60 * 1000),
                      true, true, (10 * 60 * 1000));

    usleep(10002);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
