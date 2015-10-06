/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_TEST__TEST_OVS_AGENT_UTIL_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_TEST__TEST_OVS_AGENT_UTIL_H_

// Physical port utility cmd
bool add_physical_port(const std::string &physical_switch,
                       const std::string &physical_port);
bool del_physical_port(const std::string &physical_switch,
                       const std::string &physical_port);

// Unicast mac local utility cmd
bool add_ucast_mac_local(const std::string &logical_switch,
                         const std::string &mac,
                         const std::string &dest_ip);
bool del_ucast_mac_local(const std::string &logical_switch,
                         const std::string &mac);

// Multicast mac local utility cmd
bool add_mcast_mac_local(const std::string &logical_switch,
                         const std::string &mac,
                         const std::string &dest_ip);
bool del_mcast_mac_local(const std::string &logical_switch,
                         const std::string &mac,
                         const std::string &dest_ip);

#endif  // SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_TEST__TEST_OVS_AGENT_UTIL_H_

