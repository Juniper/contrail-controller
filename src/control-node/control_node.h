/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane__ctrl_node_h

#define ctrlplane__ctrl_node_h

#include<string>
#include "base/sandesh/process_info_types.h"
#include "sandesh/sandesh_trace.h"
#include "io/event_manager.h"

class BgpServer;
class BgpXmppChannelManager;
class ConfigClientManager;
class IFMapServer;
class TaskTrigger;

class ControlNode {
public:
    static void SetDefaultSchedulingPolicy();
    static void SetHostname(const std::string name) { hostname_ = name; }
    static const std::string GetHostname() { return hostname_; }
    static const std::string &GetProgramName() { return prog_name_; }
    static void SetProgramName(const char *name) { prog_name_ = name; }
    static std::string GetSelfIp() { return self_ip_; }
    static void SetSelfIp(std::string ip) { self_ip_ = ip; }
    static void SetTestMode(const bool flag) { test_mode_ = flag; }
    static bool GetTestMode() { return test_mode_; }
    static void StartControlNodeInfoLogger(
        EventManager &evm, uint64_t period_msecs, const BgpServer *server,
        const BgpXmppChannelManager *xmpp_channel_mgr,
        const IFMapServer *ifmap_server, const string &build_info);
    static void Shutdown();
    static void Exit(int status, bool do_assert = false);
    static std::string GetProcessState(const BgpServer *bgp_server,
        const ConfigClientManager *config_client_manager,
        process::ProcessState::type *state, std::string *message);

private:
    static bool ControlNodeInfoLogger(const BgpServer *server,
        const BgpXmppChannelManager *xmpp_channel_mgr,
        const IFMapServer *ifmap_server, const std::string &build_info);
    static bool ControlNodeInfoLogTimer(TaskTrigger *node_info_trigger);

    static std::string hostname_;
    static std::string prog_name_;
    static std::string self_ip_;
    static bool test_mode_;

};

void ControlNodeShutdown();

#endif // ctrlplane__ctrl_node_h
