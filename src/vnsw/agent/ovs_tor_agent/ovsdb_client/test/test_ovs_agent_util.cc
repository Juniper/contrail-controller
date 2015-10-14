/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <stdlib.h>
#include <string>
#include <iostream>
#include <base/string_util.h>

#include <cmn/agent_cmn.h>
#include "test_ovs_agent_util.h"

#define VTEP_CMD_PREPEND "build/third_party/openvswitch/vtep/vtep-ctl"

using namespace std;

extern uint32_t ovsdb_port;

bool execute_vtep_cmd(const string &cmd) {
    string vtep_cmd = VTEP_CMD_PREPEND;
    vtep_cmd += " --db=tcp:127.0.0.1:" +
                      integerToString(ovsdb_port) + " " + cmd ;
    int ret = system(vtep_cmd.c_str());
    return (ret == 0);
}

bool add_physical_port(const string &physical_switch,
                       const string &physical_port) {
    return execute_vtep_cmd("add-port " + physical_switch + " " + physical_port);
}

bool del_physical_port(const string &physical_switch,
                       const string &physical_port) {
    return execute_vtep_cmd("del-port " + physical_switch + " " + physical_port);
}

bool add_ucast_mac_local(const string &logical_switch, const string &mac,
                         const string &dest_ip) {
    execute_vtep_cmd("add-ucast-local Contrail-" + logical_switch +
                     " " + mac + " " + dest_ip);
    return execute_vtep_cmd("list-local-macs Contrail-" + logical_switch +
                            " | grep " + mac);
}

bool del_ucast_mac_local(const string &logical_switch, const string &mac) {
    execute_vtep_cmd("del-ucast-local Contrail-" + logical_switch +
                     " " + mac);
    return !execute_vtep_cmd("list-local-macs Contrail-" + logical_switch +
                            " | grep " + mac);
}

bool add_mcast_mac_local(const string &logical_switch, const string &mac,
                         const string &dest_ip) {
    execute_vtep_cmd("add-mcast-local Contrail-" + logical_switch +
                     " " + mac + " " + dest_ip);
    return execute_vtep_cmd("list-local-macs Contrail-" + logical_switch +
                            " | grep " + mac + " | grep " + dest_ip);
}

bool del_mcast_mac_local(const string &logical_switch, const string &mac,
                         const string &dest_ip) {
    execute_vtep_cmd("del-mcast-local Contrail-" + logical_switch +
                     " " + mac + " " + dest_ip);
    return !execute_vtep_cmd("list-local-macs Contrail-" + logical_switch +
                            " | grep " + mac + " | grep " + dest_ip);
}

