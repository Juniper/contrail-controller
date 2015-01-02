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
};

TEST_F(UveProuterUveTest, ProuterAddDel_1) {
    AgentUve *u = static_cast<AgentUve *>(Agent::GetInstance()->uve());
    ProuterUveTableTest *pr = static_cast<ProuterUveTableTest *>
        (u->prouter_uve_table());
    pr->ClearCount();
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (u->vrouter_uve_entry());
    vr->clear_count();

    AddPhysicalDevice("prouter1", 1);
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) != NULL));
    WAIT_FOR(1000, 500, (pr->send_count() == 1U));
    WAIT_FOR(1000, 500, (vr->vrouter_msg_count() == 1U));
    const VrouterAgent &uve = vr->last_sent_vrouter();
    EXPECT_EQ(1U, uve.get_embedded_prouter_list().size());

    DeletePhysicalDevice("prouter1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) == NULL));
    WAIT_FOR(1000, 500, (pr->delete_count() == 1U));
    const VrouterAgent &uve2 = vr->last_sent_vrouter();
    EXPECT_EQ(0U, uve2.get_embedded_prouter_list().size());
}

//Verify that no UVEs are sent when physical interfaces added/removed.
//(when physical interfaces are not associated with physical devices)
TEST_F(UveProuterUveTest, PhysicalInterfaceAddDel_1) {
    AgentUve *u = static_cast<AgentUve *>(Agent::GetInstance()->uve());
    ProuterUveTableTest *pr = static_cast<ProuterUveTableTest *>
        (u->prouter_uve_table());
    pr->ClearCount();

    AddPhysicalInterface("pi1", 1, "pid1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") != NULL));

    DeletePhysicalInterface("pi1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") == NULL));
    EXPECT_EQ(0U, pr->send_count());
    EXPECT_EQ(0U, pr->delete_count());
}

TEST_F(UveProuterUveTest, PhysicalInterfaceAddDel_2) {
    AgentUve *u = static_cast<AgentUve *>(Agent::GetInstance()->uve());
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

    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_physical_interface_list().
                         size() == 1U));

    //Disassociate physical-device with physical-interface
    DelLink("physical-router", "prouter1", "physical-interface", "pi1");
    client->WaitForIdle();
    //Verify the Uve
    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_physical_interface_list().
                         size() == 0U));
    //Delete physical-device and physical-interface
    DeletePhysicalInterface("pi1");
    DeletePhysicalDevice("prouter1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") == NULL));
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) == NULL));

    //Verify that Delete UVE is sent out
    WAIT_FOR(1000, 500, (pr->delete_count() == 1U));
}

//Verify that no UVEs are sent when logical interfaces added/removed.
//(when logical interfaces are not associated with physical interfaces)
TEST_F(UveProuterUveTest, LogicalInterfaceAddDel_1) {
    AgentUve *u = static_cast<AgentUve *>(Agent::GetInstance()->uve());
    ProuterUveTableTest *pr = static_cast<ProuterUveTableTest *>
        (u->prouter_uve_table());
    pr->ClearCount();

    AddLogicalInterface("li1", 1, "lid1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") != NULL));

    DeleteLogicalInterface("li1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") == NULL));
    EXPECT_EQ(0U, pr->send_count());
    EXPECT_EQ(0U, pr->delete_count());
}

TEST_F(UveProuterUveTest, LogicalInterfaceAddDel_2) {
    AgentUve *u = static_cast<AgentUve *>(Agent::GetInstance()->uve());
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

    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_physical_interface_list().
                         size() == 1U));
    UvePhysicalInterfaceData data = pr->last_sent_uve().
                                    get_physical_interface_list().front();
    EXPECT_EQ(1U, data.get_logical_interface_list().size());

    //Disassociate logical-interface from physical_interface
    uint32_t send_count = pr->send_count();
    DelLink("physical-interface", "pi1", "logical-interface", "li1");
    client->WaitForIdle();
    LogicalInterface *li = LogicalInterfaceGet(1, "li1");
    WAIT_FOR(1000, 500, (li->physical_interface() == NULL));
    WAIT_FOR(1000, 500, (pr->send_count() > send_count));
    data = pr->last_sent_uve().get_physical_interface_list().front();
    EXPECT_EQ(0U, data.get_logical_interface_list().size());

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
    EXPECT_EQ(1U, pr->delete_count());
}

TEST_F(UveProuterUveTest, LogicalInterfaceAddDel_3) {
    AgentUve *u = static_cast<AgentUve *>(Agent::GetInstance()->uve());
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

    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_physical_interface_list().
                         size() == 1U));
    UvePhysicalInterfaceData data = pr->last_sent_uve().
                                    get_physical_interface_list().front();
    EXPECT_EQ(1U, data.get_logical_interface_list().size());

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
    EXPECT_EQ(1U, pr->delete_count());
}

//Associate logical interface with prouter (instead of physical-interface)
TEST_F(UveProuterUveTest, LogicalInterfaceAddDel_4) {
    AgentUve *u = static_cast<AgentUve *>(Agent::GetInstance()->uve());
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

    EXPECT_EQ(0U, (pr->last_sent_uve().get_physical_interface_list().size()));
    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_logical_interface_list().
                         size() == 1U));

    uint32_t send_count = pr->send_count();
    //Disassociate physical-device from logical-interface
    DelLink("physical-router", "prouter1", "logical-interface", "li1");
    client->WaitForIdle();
    LogicalInterface *li = LogicalInterfaceGet(1, "li1");
    WAIT_FOR(1000, 500, (li->physical_device() == NULL));
    WAIT_FOR(1000, 500, (pr->send_count() > send_count));
    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_logical_interface_list().
                         size() == 0U));

    //Delete physical-device and physical-interface
    DeleteLogicalInterface("li1");
    DeletePhysicalDevice("prouter1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) == NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") == NULL));
    EXPECT_EQ(1U, pr->delete_count());
}

//Associate logical interface with prouter. Verify change of prouter for a
//logical-interface from one prouter to another.
TEST_F(UveProuterUveTest, LogicalInterfaceAddDel_5) {
    AgentUve *u = static_cast<AgentUve *>(Agent::GetInstance()->uve());
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

    EXPECT_EQ(0U, (pr->last_sent_uve().get_physical_interface_list().size()));
    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_logical_interface_list().
                         size() == 1U));

    //Add One more physical-device
    AddPhysicalDevice("prouter2", 2);
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(2) != NULL));

    uint32_t send_count = pr->send_count();
    //Change the prouter for logical-interface
    DelLink("physical-router", "prouter1", "logical-interface", "li1");
    AddLink("physical-router", "prouter2", "logical-interface", "li1");
    DeletePhysicalDevice("prouter1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) == NULL));
    LogicalInterface *li = LogicalInterfaceGet(1, "li1");
    WAIT_FOR(1000, 500, (li->physical_device() != NULL));
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
    EXPECT_EQ(2U, pr->delete_count());
}

//Delete PhysicalDevice before deleting logical interface
TEST_F(UveProuterUveTest, PhysicalDeviceDel_1) {
    AgentUve *u = static_cast<AgentUve *>(Agent::GetInstance()->uve());
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

    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_physical_interface_list().
                         size() == 1U));
    UvePhysicalInterfaceData data = pr->last_sent_uve().
                                    get_physical_interface_list().front();
    EXPECT_EQ(1U, data.get_logical_interface_list().size());

    //Disassociate physical-device from physical-interface and delete
    //physical-device
    DelLink("physical-router", "prouter1", "physical-interface", "pi1");
    DeletePhysicalDevice("prouter1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) == NULL));
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
}

//Delete PhysicalDevice and associate Physical interface with different
//PhysicalDevice
TEST_F(UveProuterUveTest, PhysicalDeviceDel_2) {
    AgentUve *u = static_cast<AgentUve *>(Agent::GetInstance()->uve());
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

    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_physical_interface_list().
                         size() == 1U));
    UvePhysicalInterfaceData data = pr->last_sent_uve().
                                    get_physical_interface_list().front();
    EXPECT_EQ(1U, data.get_logical_interface_list().size());

    //Disassociate physical-device from physical-interface and delete
    //physical-device
    DelLink("physical-router", "prouter1", "physical-interface", "pi1");
    DeletePhysicalDevice("prouter1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) == NULL));
    PhysicalInterface *pi = PhysicalInterfaceGet("pi1");
    WAIT_FOR(1000, 500, (pi->physical_device() == NULL));
    WAIT_FOR(1000, 500, (1U == pr->delete_count()));

    //Re-add PhysicalDevice and its association to physical-interface
    uint32_t send_count = pr->send_count();
    AddPhysicalDevice("prouter2", 2);
    AddLink("physical-router", "prouter2", "physical-interface", "pi1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(2) != NULL));
    WAIT_FOR(1000, 500, (pi->physical_device() != NULL));
    WAIT_FOR(1000, 500, (pr->send_count() > send_count));
    data = pr->last_sent_uve().get_physical_interface_list().front();
    EXPECT_EQ(1U, data.get_logical_interface_list().size());

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

    WAIT_FOR(1000, 500, (2U == pr->delete_count()));
}

TEST_F(UveProuterUveTest, VMIAddDel_1) {
    AgentUve *u = static_cast<AgentUve *>(Agent::GetInstance()->uve());
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

    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_physical_interface_list().
                         size() == 1U));
    UvePhysicalInterfaceData data = pr->last_sent_uve().
                                    get_physical_interface_list().front();
    EXPECT_EQ(1U, data.get_logical_interface_list().size());
    UveLogicalInterfaceData ldata = data.get_logical_interface_list().front();
    EXPECT_EQ(1U, ldata.get_vm_interface_list().size());

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
    EXPECT_EQ(1U, pr->delete_count());
}

TEST_F(UveProuterUveTest, VMIAddDel_2) {
    AgentUve *u = static_cast<AgentUve *>(Agent::GetInstance()->uve());
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

    WAIT_FOR(1000, 500, (pr->last_sent_uve().get_physical_interface_list().
                         size() == 1U));
    UvePhysicalInterfaceData data = pr->last_sent_uve().
                                    get_physical_interface_list().front();
    EXPECT_EQ(1U, data.get_logical_interface_list().size());
    UveLogicalInterfaceData ldata = data.get_logical_interface_list().front();
    EXPECT_EQ(1U, ldata.get_vm_interface_list().size());
    
    uint32_t send_count = pr->send_count();
    //Disassociate VMI from logical-interface
    DelLink("virtual-machine-interface", "vmi1", "logical-interface", "li1");
    client->WaitForIdle();

    //Verify disassociations have happened
    LogicalInterface *li = LogicalInterfaceGet(1, "li1");
    WAIT_FOR(1000, 500, (li->vm_interface() == NULL));

    //Verify Prouter UVE send has happened after disassociation
    WAIT_FOR(1000, 500, (pr->send_count() > send_count));

    //Verify that the sent UVE does not have any VMI
    data = pr->last_sent_uve().get_physical_interface_list().front();
    EXPECT_EQ(1U, data.get_logical_interface_list().size());
    ldata = data.get_logical_interface_list().front();
    EXPECT_EQ(0U, ldata.get_vm_interface_list().size());

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
    EXPECT_EQ(1U, pr->delete_count());
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
