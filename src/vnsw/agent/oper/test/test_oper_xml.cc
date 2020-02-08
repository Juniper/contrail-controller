#include <base/os.h>
#include <iostream>
#include <boost/program_options.hpp>
#include <testing/gunit.h>
#include <test/test_cmn_util.h>
#include "test-xml/test_xml.h"
#include "test-xml/test_xml_oper.h"

using namespace std;
namespace opt = boost::program_options;

void RouterIdDepInit(Agent *agent) {
}

static void GetArgs(char *test_file, int argc, char *argv[]) {
    test_file[0] = '\0';
    opt::options_description desc("Options");
    opt::variables_map vars;
    desc.add_options()
        ("help", "Print help message")
        ("test-data", opt::value<string>(), "Specify test data file");

    opt::store(opt::parse_command_line(argc, argv, desc), vars);
    opt::notify(vars);
    if (vars.count("test-data")) {
        strcpy(test_file, vars["test-data"].as<string>().c_str());
    }
    return;
}

class TestVrf : public ::testing::Test {
};

TEST_F(TestVrf, vrf_1) {
    AgentUtXmlTest test("controller/src/vnsw/agent/oper/test/vrf.xml");
    AgentUtXmlOperInit(&test);
    if (test.Load() == true) {
        test.ReadXml();

        string str;
        test.ToString(&str);
        cout << str << endl;
        test.Run();
    }
}

TEST_F(TestVrf, vm_sub_if) {
    AgentUtXmlTest test("controller/src/vnsw/agent/oper/test/vmi-sub-if.xml");
    AgentUtXmlOperInit(&test);
    if (test.Load() == true) {
        test.ReadXml();

        string str;
        test.ToString(&str);
        cout << str << endl;
        test.Run();
    }
}

TEST_F(TestVrf, vxlan_1) {
    AgentUtXmlTest test("controller/src/vnsw/agent/oper/test/vxlan.xml");
    AgentUtXmlOperInit(&test);
    if (test.Load() == true) {
        test.ReadXml();

        string str;
        test.ToString(&str);
        cout << str << endl;
        test.Run();
    }
}

TEST_F(TestVrf, vrouter_1) {
    AgentUtXmlTest test("controller/src/vnsw/agent/oper/test/global_vrouter.xml");
    AgentUtXmlOperInit(&test);
    if (test.Load() == true) {
        test.ReadXml();

        string str;
        test.ToString(&str);
        cout << str << endl;
        test.Run();
    }
}

TEST_F(TestVrf, vm_sub_if_oper_state) {
    AgentUtXmlTest test("controller/src/vnsw/agent/oper/test/vmi-sub-if-add.xml");
    AgentUtXmlOperInit(&test);
    if (test.Load() == true) {
        test.ReadXml();

        string str;
        test.ToString(&str);
        cout << str << endl;
        test.Run();
    }
    VmInterface *vm_interface = static_cast<VmInterface *>(VmPortGet(1));
    VmInterface *vm_sub_interface = static_cast<VmInterface *>(VmPortGet(2));
    EXPECT_TRUE(vm_interface != NULL);
    EXPECT_TRUE(vm_interface->IsActive());
    EXPECT_TRUE(vm_sub_interface->parent() != NULL);
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new VmInterfaceKey(AgentKey::RESYNC, vm_interface->GetUuid(),
                  vm_interface->name()));
    req.data.reset(new VmInterfaceOsOperStateData(false));
    vm_interface->set_test_oper_state(false);
    Agent::GetInstance()->interface_table()->Enqueue(&req);
    client->WaitForIdle();
    EXPECT_FALSE(vm_interface->IsActive());
    EXPECT_FALSE(vm_sub_interface->IsActive());
    AgentUtXmlTest test1("controller/src/vnsw/agent/oper/test/vmi-sub-if-del.xml");
    AgentUtXmlOperInit(&test1);
    if (test1.Load() == true) {
        test1.ReadXml();

        string str;
        test1.ToString(&str);
        cout << str << endl;
        test1.Run();
    }
}

// Check that virtual-machine-sub-interface with zero
// mac should not be add routes in evpn and bridge table
TEST_F(TestVrf, vm_sub_if_zero_mac) {
    AgentUtXmlTest test("controller/src/vnsw/agent/oper/test/vmi-sub-if-zeromac-add.xml");
    AgentUtXmlOperInit(&test);
    if (test.Load() == true) {
        test.ReadXml();

        string str;
        test.ToString(&str);
        cout << str << endl;
        test.Run();
    }

    VmInterface *vm_interface = static_cast<VmInterface *>(VmPortGet(1));
    VmInterface *vm_sub_interface = static_cast<VmInterface *>(VmPortGet(2));
    EXPECT_TRUE(vm_interface != NULL);
    EXPECT_TRUE(vm_interface->IsActive());
    EXPECT_TRUE(vm_sub_interface->parent() != NULL);
    EXPECT_FALSE(vm_sub_interface->IsActive());
    BridgeRouteEntry *l2_rt_non_zero_mac = L2RouteGet("vrf1", vm_interface->vm_mac());
    EvpnRouteEntry *evpn_rt_non_zero_mac = EvpnRouteGet("vrf1", vm_interface->vm_mac(),  Ip4Address::from_string("0.0.0.0"),
                                           vm_interface->ethernet_tag());
    EXPECT_TRUE(l2_rt_non_zero_mac != NULL);
    EXPECT_TRUE(evpn_rt_non_zero_mac != NULL);
    BridgeRouteEntry *l2_rt_zero_mac = L2RouteGet("vrf1", vm_sub_interface->vm_mac());
    EvpnRouteEntry *evpn_rt_zero_mac = EvpnRouteGet("vrf1", vm_sub_interface->vm_mac(),  Ip4Address::from_string("0.0.0.0"),
                                           vm_sub_interface->ethernet_tag());
    EXPECT_TRUE(l2_rt_zero_mac == NULL);
    EXPECT_TRUE(evpn_rt_zero_mac == NULL);

    AgentUtXmlTest test1("controller/src/vnsw/agent/oper/test/vmi-sub-if-del.xml");
    AgentUtXmlOperInit(&test1);
    if (test1.Load() == true) {
        test1.ReadXml();

        string str;
        test1.ToString(&str);
        cout << str << endl;
        test1.Run();
    }
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
