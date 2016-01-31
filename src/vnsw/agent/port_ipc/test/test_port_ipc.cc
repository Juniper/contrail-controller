/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include "base/os.h"
#include "testing/gunit.h"
#include "test/test_cmn_util.h"
#include "port_ipc/port_ipc_handler.h"

using namespace std;
namespace fs = boost::filesystem;

void RouterIdDepInit(Agent *agent) {
}

class PortIpcTest : public ::testing::Test {
 public:
     PortIpcTest() : agent_(Agent::GetInstance()) {
     }
     Agent *agent() { return agent_; }
     bool AddPort(const PortIpcHandler &pih,
         const PortIpcHandler::AddPortParams &req) {
         string err_msg;
         return pih.AddPort(req, false, err_msg);
     }
     bool IsUUID(const PortIpcHandler &pih, const string &file) {
         return pih.IsUUID(file);
     }
     void DeleteAllPorts(const string &dir) {
         string err_str;
         fs::path ports_dir(dir);
         fs::directory_iterator end_iter;
         //Pass directory as something different from where the files are
         //present. Otherwise it will result in deletion of files
         PortIpcHandler pih(agent(), "dummy", false);

         if (!fs::exists(ports_dir) || !fs::is_directory(ports_dir)) {
             return;
         }

         fs::directory_iterator it(ports_dir);
         BOOST_FOREACH(fs::path const &p, std::make_pair(it, end_iter)) {
             if (!fs::is_regular_file(p)) {
                 continue;
             }
             if (!IsUUID(pih, p.filename().string())) {
                 continue;
             }
             pih.DeletePort(p.filename().string(), err_str);
         }
     }

 private:
     Agent *agent_;
};

/* Add/delete a port */
TEST_F(PortIpcTest, Port_Add_Del) {

    const string dir = "/tmp/";
    CfgIntTable *ctable = agent()->interface_config_table();
    assert(ctable);
    uint32_t port_count = ctable->Size();
    std::string err_str;

    PortIpcHandler::AddPortParams req("ea73b285-01a7-4d3e-8322-50976e8913da",
        "ea73b285-01a7-4d3e-8322-50976e8913db",
        "fa73b285-01a7-4d3e-8322-50976e8913de",
        "b02a3bfb-7946-4b1c-8cc4-bf8cedcbc48d", "vm1", "tap1af4bee3-04",
        "11.0.0.3", "", "02:1a:f4:be:e3:04", 0, -1, -1);
    PortIpcHandler pih(agent(), dir, false);
    AddPort(pih, req);
    client->WaitForIdle(2);
    WAIT_FOR(500, 1000, ((port_count + 1) == ctable->Size()));

    pih.DeletePort(req.port_id, err_str);
    client->WaitForIdle(2);
    WAIT_FOR(500, 1000, ((port_count) == ctable->Size()));
}

/* Verify that AddPort fails when IPv6 Ip is sent in IPv4 address field */
TEST_F(PortIpcTest, Port_Add_Invalid_Ip) {

    const string dir = "/tmp/";
    CfgIntTable *ctable = agent()->interface_config_table();
    assert(ctable);

    PortIpcHandler::AddPortParams req("ea73b285-01a7-4d3e-8322-50976e8913da",
        "ea73b285-01a7-4d3e-8322-50976e8913db",
        "fa73b285-01a7-4d3e-8322-50976e8913de",
        "b02a3bfb-7946-4b1c-8cc4-bf8cedcbc48d", "vm1", "tap1af4bee3-04",
        "fd11::3", "", "02:1a:f4:be:e3:04", 0, -1, -1);
    PortIpcHandler pih(agent(), dir, false);
    bool ret = AddPort(pih, req);
    EXPECT_FALSE(ret);
}

/* Reads files in a directory and adds port info into agent */
TEST_F(PortIpcTest, PortReload) {

    const string dir = "controller/src/vnsw/agent/port_ipc/test/";
    CfgIntTable *ctable = agent()->interface_config_table();
    assert(ctable);
    uint32_t port_count = ctable->Size();

    //There are 2 files present in controller/src/vnsw/agent/port_ipc/test/
    PortIpcHandler pih(agent(), dir, false);
    pih.ReloadAllPorts();
    client->WaitForIdle(2);

    // Port count should increase by 2 as we have 2 ports
    WAIT_FOR(500, 1000, ((port_count + 2) == ctable->Size()));

    //cleanup
    DeleteAllPorts(dir);
    client->WaitForIdle(2);
    WAIT_FOR(500, 1000, ((port_count) == ctable->Size()));
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
