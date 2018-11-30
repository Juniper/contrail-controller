/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include "base/os.h"
#include "testing/gunit.h"
#include "test/test_cmn_util.h"
#include "port_ipc/port_ipc_handler.h"
#include "port_ipc/port_subscribe_table.h"

using namespace std;
namespace fs = boost::filesystem;

void RouterIdDepInit(Agent *agent) {
}

class PortIpcTest : public ::testing::Test {
 public:
     PortIpcTest() : agent_(Agent::GetInstance()) {
     }

     virtual void SetUp() {
         pih_ = agent_->port_ipc_handler();
     }

     Agent *agent() { return agent_; }
     bool AddPort(const char *port_id, const char *vm_id,
                  const char *vn_id, const char *project_id,
                  const char *vm_name, const char *tap_name,
                  const char *ip4, const char *ip6, const char *mac,
                  VmiSubscribeEntry::Type type, uint16_t tx_vlan,
                  uint16_t rx_vlan) {
         boost::system::error_code ec;
         Ip4Address ip4_addr = Ip4Address::from_string(ip4, ec);
         Ip6Address ip6_addr = Ip6Address::from_string(ip6, ec);

         return PortSubscribe(tap_name, StringToUuid(port_id),
                              StringToUuid(vm_id), vm_name,
                              StringToUuid(vn_id), StringToUuid(project_id),
                              ip4_addr, ip6_addr, mac);
     }

     void Sync() {
         pih_->SyncHandler();
     }
     bool IsUUID(const string &file) {
         return pih_->IsUUID(file);
     }
     void DeleteAllPorts(const string &dir) {
         string err_str;
         fs::path ports_dir(dir);
         fs::directory_iterator end_iter;
         //Pass directory as something different from where the files are
         //present. Otherwise it will result in deletion of files
         if (!fs::exists(ports_dir) || !fs::is_directory(ports_dir)) {
             return;
         }

         fs::directory_iterator it(ports_dir);
         BOOST_FOREACH(fs::path const &p, std::make_pair(it, end_iter)) {
             if (!fs::is_regular_file(p)) {
                 continue;
             }
             if (!IsUUID(p.filename().string())) {
                 continue;
             }
             pih_->DeleteVmiUuidEntry(StringToUuid(p.filename().string()),
                                      err_str);
         }
     }

 protected:
     Agent *agent_;
     PortIpcHandler *pih_;
};

/* Add/delete a port */
TEST_F(PortIpcTest, Port_Add_Del) {

    const string dir = "/tmp/";
    uint32_t port_count = PortSubscribeSize(agent_);
    std::string err_str;

    AddPort("ea73b285-01a7-4d3e-8322-50976e8913da",
            "ea73b285-01a7-4d3e-8322-50976e8913db",
            "fa73b285-01a7-4d3e-8322-50976e8913de",
            "b02a3bfb-7946-4b1c-8cc4-bf8cedcbc48d", "vm1", "tap1af4bee3-04",
            "11.0.0.3", "", "02:1a:f4:be:e3:04", VmiSubscribeEntry::VMPORT,
            -1, -1);
    client->WaitForIdle(2);
    WAIT_FOR(500, 1000, ((port_count + 1) == PortSubscribeSize(agent_)));

    pih_->DeleteVmiUuidEntry(StringToUuid("ea73b285-01a7-4d3e-8322-50976e8913da"),
                          err_str);
    client->WaitForIdle(2);
    WAIT_FOR(500, 1000, ((port_count) == PortSubscribeSize(agent_)));
}

/* Verify that AddPort fails when IPv6 Ip is sent in IPv4 address field */
TEST_F(PortIpcTest, Port_Add_Invalid_Ip) {

    const string dir = "/tmp/";

    string vmi_uuid = "ea73b285-01a7-4d3e-8322-50976e8913da";
    AddPort(vmi_uuid.c_str(),
            "ea73b285-01a7-4d3e-8322-50976e8913db",
            "fa73b285-01a7-4d3e-8322-50976e8913de",
            "b02a3bfb-7946-4b1c-8cc4-bf8cedcbc48d", "vm1",
            "tap1af4bee3-04", "fd11::3", "", "02:1a:f4:be:e3:04",
            VmiSubscribeEntry::VMPORT, -1, -1);
    client->WaitForIdle();

    InterfaceConstRef vmi_ref =
        agent_->interface_table()->FindVmi(StringToUuid(vmi_uuid));
    const VmInterface *vmi = dynamic_cast<const VmInterface *>(vmi_ref.get());
    EXPECT_TRUE(vmi->primary_ip_addr() == Ip4Address(0));

    std::string err_str;
    pih_->DeleteVmiUuidEntry(StringToUuid(vmi_uuid), err_str);
    client->WaitForIdle();
}

/* Reads files in a directory and adds port info into agent */
TEST_F(PortIpcTest, PortReload) {

    const string dir = "controller/src/vnsw/agent/port_ipc/test/";
    uint32_t port_count = PortSubscribeSize(agent_);

    //There are 2 files present in controller/src/vnsw/agent/port_ipc/test/
    pih_->ReloadAllPorts(dir, false, false);
    client->WaitForIdle(2);

    // Port count should increase by 2 as we have 2 ports
    WAIT_FOR(500, 1000, ((port_count + 2) == PortSubscribeSize(agent_)));

    //cleanup
    DeleteAllPorts(dir);
    client->WaitForIdle(2);
    WAIT_FOR(500, 1000, ((port_count) == PortSubscribeSize(agent_)));
}

/* Reads files in a directory and adds port info into agent - 2 ports for
   the same VM.
   Subsequently, add port for different VM using the same VMI's. Ensure that
   previous port information is removed and now port infomation is used.
*/
TEST_F(PortIpcTest, PortReloadAndAddWithDifferentVMSameVMI) {

    const string dir = "controller/src/vnsw/agent/port_ipc/test/data";
    uint32_t port_count = PortSubscribeSize(agent_);

    //There are 2 files present in controller/src/vnsw/agent/port_ipc/test/data
    pih_->ReloadAllPorts(dir, false, false);
    client->WaitForIdle(2);

    // Port count should increase by 2 as we have 2 ports
    WAIT_FOR(500, 1000, ((port_count + 2) == PortSubscribeSize(agent_)));

    // Call Add port for the same VMI's with a different VM.

    AddPort("c8c0a3dd-ae36-4d0d-8952-72623f3f1a83",
            "fefe5292-e2d4-4d9b-8982-6483bac44252",
            "56297dc0-5553-4e6a-b87f-8423afc21e9d",
            "9970147c-33b1-4bf7-8944-8968e708d430", "vm2", "tapc8c0a3dd-ae",
            "11.0.0.4", "", "02:c8:c0:a3:dd:ae", VmiSubscribeEntry::REMOTE_PORT,
            -1, -1);
    client->WaitForIdle(2);
    WAIT_FOR(500, 1000, ((port_count + 2) == PortSubscribeSize(agent_)));
    AddPort("93c2c386-6e79-485e-bfa4-b9daa713133b",
            "fefe5292-e2d4-4d9b-8982-6483bac44252",
            "56297dc0-5553-4e6a-b87f-8423afc21e9d",
            "9970147c-33b1-4bf7-8944-8968e708d430", "vm2", "tap93c2c386-6e",
            "11.0.0.3", "", "02:93:c2:c3:86:6e", VmiSubscribeEntry::REMOTE_PORT,
            -1, -1);
    client->WaitForIdle(2);
    WAIT_FOR(500, 1000, ((port_count + 2) == PortSubscribeSize(agent_)));
    AddPort("c8c0a3dd-ae36-4d0d-8952-72623f3f1a83",
            "69ce06bd-6d1e-43d0-8e48-a30043a49ed8",
            "56297dc0-5553-4e6a-b87f-8423afc21e9d",
            "9970147c-33b1-4bf7-8944-8968e708d430", "vm1", "tapc8c0a3dd-ae",
            "11.0.0.4", "", "02:c8:c0:a3:dd:ae", VmiSubscribeEntry::REMOTE_PORT,
            -1, -1);
    client->WaitForIdle(2);
    WAIT_FOR(500, 1000, ((port_count + 2) == PortSubscribeSize(agent_)));
    AddPort("93c2c386-6e79-485e-bfa4-b9daa713133b",
            "69ce06bd-6d1e-43d0-8e48-a30043a49ed8",
            "56297dc0-5553-4e6a-b87f-8423afc21e9d",
            "9970147c-33b1-4bf7-8944-8968e708d430", "vm1", "tap93c2c386-6e",
            "11.0.0.3", "", "02:93:c2:c3:86:6e", VmiSubscribeEntry::REMOTE_PORT,
            -1, -1);
    client->WaitForIdle(2);
    WAIT_FOR(500, 1000, ((port_count + 2) == PortSubscribeSize(agent_)));
    //cleanup
    DeleteAllPorts(dir);
    client->WaitForIdle(2);
    WAIT_FOR(500, 1000, ((port_count) == PortSubscribeSize(agent_)));
}

/* Add/delete a port */
TEST_F(PortIpcTest, Vcenter_Port_Add_Del) {

    const string dir = "/tmp/";
    uint32_t port_count = PortSubscribeSize(agent_);
    std::string err_str;

    AddPort("ea73b285-01a7-4d3e-8322-50976e8913da",
            "ea73b285-01a7-4d3e-8322-50976e8913db",
            "fa73b285-01a7-4d3e-8322-50976e8913de",
            "b02a3bfb-7946-4b1c-8cc4-bf8cedcbc48d", "vm1", "tap1af4bee3-04",
            "11.0.0.3", "", "02:1a:f4:be:e3:04", VmiSubscribeEntry::REMOTE_PORT,
            -1, -1);
    client->WaitForIdle(2);
    WAIT_FOR(500, 1000, ((port_count + 1) == PortSubscribeSize(agent_)));

    pih_->DeleteVmiUuidEntry
        (StringToUuid("ea73b285-01a7-4d3e-8322-50976e8913da"), err_str);
    client->WaitForIdle(2);
    WAIT_FOR(500, 1000, ((port_count) == PortSubscribeSize(agent_)));
}

TEST_F(PortIpcTest, Port_Sync) {
    const string dir = "/tmp/";
    uint32_t port_count = PortSubscribeSize(agent_);
    PortIpcHandler *ipc = agent()->port_ipc_handler();
    std::string err_str;

    //ADD a port
    AddPort("ea73b285-01a7-4d3e-8322-50976e8913da",
            "ea73b285-01a7-4d3e-8322-50976e8913db",
            "fa73b285-01a7-4d3e-8322-50976e8913de",
            "b02a3bfb-7946-4b1c-8cc4-bf8cedcbc48d", "vm1", "tap1af4bee3-04",
            "11.0.0.3", "", "02:1a:f4:be:e3:04", VmiSubscribeEntry::VMPORT,
            -1, -1);
    client->WaitForIdle(2);
    WAIT_FOR(500, 1000, ((port_count + 1) == PortSubscribeSize(agent_)));

    //Add one more port
    AddPort("ea73b285-01a7-4d3e-8322-50976e8913db",
            "ea73b285-01a7-4d3e-8322-50976e8913dc",
            "fa73b285-01a7-4d3e-8322-50976e8913dd",
            "b02a3bfb-7946-4b1c-8cc4-bf8cedcbc48e", "vm2", "tap1af4bee3-05",
            "11.0.0.4", "", "02:1a:f4:be:e3:05", VmiSubscribeEntry::VMPORT,
            -1, -1);
    client->WaitForIdle(2);
    WAIT_FOR(500, 1000, ((port_count + 2) == PortSubscribeSize(agent_)));

    InterfaceConfigStaleCleaner *cleaner = pih_->interface_stale_cleaner();
    EXPECT_TRUE(cleaner != NULL);
    cleaner->set_timeout(1);
    Sync();
    AddPort("ea73b285-01a7-4d3e-8322-50976e8913db",
            "ea73b285-01a7-4d3e-8322-50976e8913dc",
            "fa73b285-01a7-4d3e-8322-50976e8913dd",
            "b02a3bfb-7946-4b1c-8cc4-bf8cedcbc48e", "vm2", "tap1af4bee3-05",
            "11.0.0.4", "", "02:1a:f4:be:e3:05", VmiSubscribeEntry::VMPORT,
            -1, -1);
    client->WaitForIdle(2);

    WAIT_FOR(1000, 1000, (cleaner->TimersCount() == 0));
    client->WaitForIdle(2);
    WAIT_FOR(500, 1000, ((port_count + 1) == PortSubscribeSize(agent_)));
    //cleanup
    cleaner->set_timeout(ConfigStaleCleaner::kConfigStaleTimeout);
    pih_->DeleteVmiUuidEntry
        (StringToUuid("ea73b285-01a7-4d3e-8322-50976e8913db"), err_str);
    client->WaitForIdle(2);
    WAIT_FOR(500, 1000, ((port_count) == PortSubscribeSize(agent_)));
    agent()->set_port_ipc_handler(ipc);
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
