/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

//
// sandesh_rw_test.cc
//
// Sandesh Read-Write Test
//

#include "base/time_util.h"
#include <cmn/agent_cmn.h>
#include <cfg/cfg_init.h>
#include "testing/gunit.h"
#include <cmn/agent.h>
#include "base/logging.h"
#include "base/util.h"
#include <test/test_cmn_util.h>
#include <resource_manager/resource_manager.h>
#include "resource_manager/resource_backup.h"
#include <resource_manager/resource_table.h>
#include <resource_manager/mpls_index.h>

static std::string backup_file = "";

class SandeshDecodeBackupFileTest : public ::testing::Test {
protected:
    SandeshDecodeBackupFileTest() {
    }
    virtual void SetUp() {
        agent = Agent::GetInstance();
    }
    virtual void TearDown() {
    }
    void SetFileParams(BackUpResourceTable *table, const std::string& dir,
                       const std::string& file, const std::string& prefix) {
        table->set_backup_dir(dir);
        table->set_file_name_str(file);
        table->set_file_name_prefix(prefix);
    }

    Agent *agent;
};

TEST_F(SandeshDecodeBackupFileTest, DecodeBackupFile) {
    struct stat fstat;
    if(stat(backup_file.c_str(), &fstat) != 0) {
        cout << "Incorrect file path provided\n";
        EXPECT_TRUE(0);
    } else {
        std::string b_dir, b_file;
        std::size_t found = backup_file.find_last_of("/");
        b_dir = backup_file.substr(0,found);
        b_file = backup_file.substr(found+1);

        if (b_file.find("vrf") != std::string::npos) {
            VrfMplsBackUpResourceTable &vrf_table =
                agent->resource_manager()->backup_mgr()->sandesh_maps().
                vrf_mpls_index_table();
            SetFileParams(&vrf_table, b_dir, b_file, "contrail_vrf_resource");
            vrf_table.ReadFromFile();
            std::map<uint32_t, VrfMplsResource> vrf_map;
            vrf_map = vrf_table.map();

            //Print Vrf map
            std::map<uint32_t, VrfMplsResource>::iterator it;
            for (it = vrf_map.begin(); it != vrf_map.end(); it++) {
                cout << "index = " << it->first << endl;
                VrfMplsResource sandesh_key = it->second;
                cout << "\tvrf_name: " << sandesh_key.get_name()
                     << "\tvxlan_flag: " << sandesh_key.get_vxlan_nh() << endl;
            }
        } else if (b_file.find("route") != std::string::npos) {
            RouteMplsBackUpResourceTable &route_table =
                agent->resource_manager()->backup_mgr()->sandesh_maps().
                route_mpls_index_table();
            SetFileParams(&route_table, b_dir, b_file,
                          "contrail_route_resource");
            route_table.ReadFromFile();
            std::map<uint32_t, RouteMplsResource> route_map;
            route_map = route_table.map();

            //Print Route map
            std::map<uint32_t, RouteMplsResource>::iterator it;
            for (it = route_map.begin(); it != route_map.end(); it++) {
                cout << "index = " << it->first << endl;
                RouteMplsResource sandesh_key = it->second;
                cout << "\tvrf_name: " << sandesh_key.get_vrf_name()
                     << "\troute_prefix: " << sandesh_key.get_route_prefix()
                     << endl;
            }
        } else if (b_file.find("interface") != std::string::npos) {
            InterfaceMplsBackUpResourceTable &intf_table =
                agent->resource_manager()->backup_mgr()->sandesh_maps().
                interface_mpls_index_table();
            SetFileParams(&intf_table, b_dir, b_file,
                          "contrail_interface_resource");
            intf_table.ReadFromFile();
            std::map<uint32_t, InterfaceIndexResource> intf_map;
            intf_map = intf_table.map();

            //Print Interface map
            std::map<uint32_t, InterfaceIndexResource>::iterator it;
            for (it = intf_map.begin(); it != intf_map.end(); it++) {
                cout << "index = " << it->first << endl;
                InterfaceIndexResource sandesh_key = it->second;
                cout << "\ttype: " << sandesh_key.get_type()
                     << "\tMac: " << sandesh_key.get_mac()
                     << "\tName: " << sandesh_key.get_name()
                     << "\tPolicy: " << sandesh_key.get_policy()
                     << "\tUuid: " << sandesh_key.get_uuid()
                     << "\tFlags: " << sandesh_key.get_flags()
                     << endl;
            }
        } else if (b_file.find("vlan") != std::string::npos) {
            VlanMplsBackUpResourceTable &vlan_table =
                agent->resource_manager()->backup_mgr()->sandesh_maps().
                vlan_mpls_index_table();
            SetFileParams(&vlan_table, b_dir, b_file,
                          "contrail_vlan_resource");
            vlan_table.ReadFromFile();
            std::map<uint32_t, VlanMplsResource> vlan_map;
            vlan_map = vlan_table.map();

            //Print Vlan map
            std::map<uint32_t, VlanMplsResource>::iterator it;
            for (it = vlan_map.begin(); it != vlan_map.end(); it++) {
                cout << "index = " << it->first << endl;
                VlanMplsResource sandesh_key = it->second;
                cout << "\tvlan_tag: " << sandesh_key.get_tag()
                     << "\tUuid: " << sandesh_key.get_uuid()
                     << endl;
            }
        }
    }
}

int main(int argc, char **argv) {
    GETUSERARGS();
    if (argc != 2) {
        cout << "Backup Filename not provided, exiting!\n";
        return 0;
    }
    backup_file = argv[1];
    client = TestInit(init_file, ksync_init, true, true, true,
                      30000, 1000, true, true, 30000, true);
    AgentParam *param = client->param();
    param->set_restart_backup_enable(true);
    usleep(100000);
    bool success = RUN_ALL_TESTS();
    client->WaitForIdle();
    param->set_restart_backup_enable(false);
    client->WaitForIdle();
    TestShutdown();
    delete client;
    usleep(100000);
    return success;
}
