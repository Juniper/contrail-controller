/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane__ctrl_node_h

#define ctrlplane__ctrl_node_h

#include<string>

class ControlNode {
public:
    static void SetDefaultSchedulingPolicy();
    static void SetCollector(const std::string ip) { collector_ip_ = ip; }
    static const std::string GetCollector() { return collector_ip_; }
    static void SetHostname(const std::string name) { hostname_ = name; }
    static const std::string GetHostname() { return hostname_; }
    static const std::string &GetProgramName() { return prog_name_; }
    static void SetProgramName(const char *name) { prog_name_ = name; }
    static std::string GetSelfIp() { return self_ip_; }
    static void SetSelfIp(std::string ip) { self_ip_ = ip; }

private:
    static std::string collector_ip_;
    static std::string hostname_;
    static std::string prog_name_;
    static std::string self_ip_;

};

void ControlNodeShutdown();

#endif // ctrlplane__ctrl_node_h
