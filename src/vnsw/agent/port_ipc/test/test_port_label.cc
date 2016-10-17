/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include "base/os.h"
#include "testing/gunit.h"
#include "test/test_cmn_util.h"
#include "port_ipc/port_ipc_handler.h"
#include "cfg/cfg_interface.h"
#include "cfg/cfg_interface_listener.h"

using namespace std;
namespace fs = boost::filesystem;

class PortIpcLabelTest : public ::testing::Test {
public:
    PortIpcLabelTest() :
        agent_(Agent::GetInstance()), pih_(agent_->port_ipc_handler()),
        table_(agent_->interface_config_table()),
        client_(agent_->cfg()->cfg_interface_client()) {
    }
    virtual ~PortIpcLabelTest() { }

    Agent *agent() { return agent_; }

    string MakeLabelJson(const string &vm_label, const string &vn_label,
                         const string tap_name) {
        rapidjson::Document json;
        json.SetObject();
        rapidjson::Document::AllocatorType &a = json.GetAllocator();

        json.AddMember("ifname", tap_name.c_str(), a);
        json.AddMember("vm-label", vm_label.c_str(), a);
        json.AddMember("network-label", vn_label.c_str(), a);
        json.AddMember("vm-ifname", "eth0", a);
        json.AddMember("namespace", vm_label.c_str(), a);

        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter< rapidjson::StringBuffer > writer(buffer);
        json.Accept(writer);
        return buffer.GetString();
    }

    bool AddVmiLabel(const string &vm_label, const string &vn_label,
                     const string tap_name, string &err_msg) {
        string json = MakeLabelJson(vm_label, vn_label, tap_name);
        bool ret = pih_->AddPortFromJson(json, false, err_msg, false);
        client->WaitForIdle();
        return ret;
    }

    bool DelVmiLabel(const string &vm_label, const string &vn_label,
                     const string tap_name, string &err_msg) {
        string json = MakeLabelJson(vm_label, vn_label, tap_name);
        bool ret = pih_->DeletePort(json, vm_label, err_msg);
        client->WaitForIdle();
        return ret;
    }

    virtual void SetUp() {
        pih_ = new PortIpcHandler(agent_, "/tmp/test_port_ipc");
        table_ = agent_->interface_config_table();
        client_ = agent_->cfg()->cfg_interface_client();
        struct PortInfo input1[] = {
            {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
        };

        CreateVmportEnv(input1, 1);
        client->WaitForIdle();

        IpamInfo ipam_info[] = {
            {"1.1.1.0", 24, "1.1.1.200", true},
            {"2.1.1.0", 24, "2.1.1.200", true},
        };
        AddIPAM("vn1", ipam_info, 2);
        client->WaitForIdle();

        WAIT_FOR(100, 1000, (VmPortFind(1)));
        EXPECT_EQ(1, table_->Size());
        EXPECT_EQ(2, client_->vmi_label_tree_size());
    }

    virtual void TearDown() {
        struct PortInfo input1[] = {
            {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
        };

        DeleteVmportEnv(input1, 1, true);
        client->WaitForIdle();

        DelIPAM("vn1");

        WAIT_FOR(100, 1000, (VmPortFindRetDel(1) == false));
        EXPECT_EQ(0, table_->Size());
        EXPECT_EQ(0, client_->vmi_label_tree_size());
        delete pih_;
        pih_ = NULL;
    }

protected:
    Agent *agent_;
    PortIpcHandler *pih_;
    InterfaceConfigTable *table_;
    InterfaceCfgClient *client_;
};

// Query for entry vmi-label-tree
TEST_F(PortIpcLabelTest, Get_1) {
    string info;
    EXPECT_TRUE(pih_->GetPortInfo("", "vm1", info));
    EXPECT_TRUE(pih_->GetPortInfo("", UuidToString(MakeUuid(1)), info));
}

// Query for entry not present in vmi-label-tree
TEST_F(PortIpcLabelTest, Get_2) {
    string info;
    EXPECT_FALSE(pih_->GetPortInfo("", "vm2", info));
    EXPECT_FALSE(pih_->GetPortInfo("", UuidToString(MakeUuid(2)), info));
}

// Add VMI without Nova message
TEST_F(PortIpcLabelTest, Add_1) {
    struct PortInfo input1[] = {
        {"vnet2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2}
    };

    CreateVmportWithoutNova(input1, 1);
    client->WaitForIdle();

    string info;
    EXPECT_TRUE(pih_->GetPortInfo("", "vm2", info));
    // UUID based entry will not be found without Nova message
    EXPECT_FALSE(pih_->GetPortInfo("", UuidToString(MakeUuid(2)), info));

    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
}

// Add VMI without Nova message
TEST_F(PortIpcLabelTest, Add_2) {
    struct PortInfo input1[] = {
        {"vnet2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2}
    };

    // Create VMI without nova message. Tap interface should not be set
    CreateVmportWithoutNova(input1, 1);
    client->WaitForIdle();

    const VmInterface *vmi = static_cast<const VmInterface *>(VmPortGet(2));
    EXPECT_TRUE(vmi != NULL);

    // Validate tap interface not set and also VMI is inactive
    EXPECT_TRUE(vmi->name().empty());
    EXPECT_FALSE(vmi->IsL2Active());

    // Simulate label-message
    string err_msg;
    EXPECT_TRUE(AddVmiLabel("vm2", "", "tap2", err_msg));
    EXPECT_TRUE(vmi->IsConfigurerSet(VmInterface::INSTANCE_MSG));

    // tap-interface must be set and interface must be active now
    EXPECT_STREQ("tap2", vmi->name().c_str());
    EXPECT_TRUE(vmi->IsL2Active());

    // The UUID based message still not found in interface-config table
    string info;
    EXPECT_TRUE(pih_->GetPortInfo("", "vm2", info));
    // UUID based entry will not be found without Nova message
    EXPECT_FALSE(pih_->GetPortInfo("", UuidToString(MakeUuid(2)), info));

    // Simulate label-delete message
    EXPECT_TRUE(DelVmiLabel("vm2", "", "tap2", err_msg));

    // post-delete validation
    EXPECT_STREQ("", vmi->name().c_str());
    EXPECT_FALSE(vmi->IsL2Active());
    EXPECT_FALSE(vmi->IsConfigurerSet(VmInterface::INSTANCE_MSG));

    // Teardown
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    WAIT_FOR(100, 1000, (VmPortFindRetDel(2) == false));
}

// Simulate nova message before VMI config message
TEST_F(PortIpcLabelTest, Add_3) {
    // Send port-label message before VMI. Message must fail
    // Simulate label-message
    string err_msg;
    EXPECT_TRUE(AddVmiLabel("vm2", "", "tap2", err_msg));

    struct PortInfo input1[] = {
        {"vnet2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2}
    };

    // Create VMI without nova message. Tap interface should not be set
    CreateVmportWithoutNova(input1, 1);
    client->WaitForIdle();

    const VmInterface *vmi = static_cast<const VmInterface *>(VmPortGet(2));
    EXPECT_TRUE(vmi != NULL);

    // tap-interface must be set and interface must be active now
    EXPECT_STREQ("tap2", vmi->name().c_str());
    EXPECT_TRUE(vmi->IsL2Active());

    // The UUID based message still not found in interface-config table
    string info;
    EXPECT_TRUE(pih_->GetPortInfo("", "vm2", info));
    // UUID based entry will not be found without Nova message
    EXPECT_FALSE(pih_->GetPortInfo("", UuidToString(MakeUuid(2)), info));

    // Simulate label-delete message
    EXPECT_TRUE(DelVmiLabel("vm2", "", "tap2", err_msg));

    // tap-interface must be reset and it must be inactive again
    EXPECT_TRUE(vmi->name().empty());
    EXPECT_FALSE(vmi->IsL2Active());

    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
}

int main (int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
