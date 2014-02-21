/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane__ctrl_node_h

#define ctrlplane__ctrl_node_h

#include<string>
#include "sandesh/sandesh_trace.h"
#include "discovery/client/discovery_client.h"

class ControlNode {
public:
    static void SetDefaultSchedulingPolicy();
    static void SetHostname(const std::string name) { hostname_ = name; }
    static const std::string GetHostname() { return hostname_; }
    static const std::string &GetProgramName() { return prog_name_; }
    static void SetProgramName(const char *name) { prog_name_ = name; }
    static std::string GetSelfIp() { return self_ip_; }
    static void SetSelfIp(std::string ip) { self_ip_ = ip; }
    static void SetDiscoveryServiceClient(DiscoveryServiceClient *ds) { 
        ds_client_ = ds;
    }
    static DiscoveryServiceClient *GetControlNodeDiscoveryServiceClient() { 
        return ds_client_;
    }

private:
    static std::string hostname_;
    static std::string prog_name_;
    static std::string self_ip_;
    static DiscoveryServiceClient *ds_client_;

};

void ControlNodeShutdown();

#endif // ctrlplane__ctrl_node_h
