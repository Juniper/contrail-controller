/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef _ROOT_PORT_IPC_HANDLER_H_
#define _ROOT_PORT_IPC_HANDLER_H_

#include <string>
#include <cmn/agent.h>

class PortIpcHandler {
 public:
    struct AddPortParams {
        AddPortParams(std::string pid, std::string iid, std::string vid,
                      std::string vm_pid, std::string vname, std::string tname,
                      std::string ip, std::string ip6, std::string mac,
                      int ptype, int tx_vid, int rx_vid);
        std::string port_id;
        std::string instance_id;
        std::string vn_id;
        std::string vm_project_id;
        std::string vm_name;
        std::string tap_name;
        std::string ip_address;
        std::string ip6_address;
        std::string mac_address;
        int port_type;
        int tx_vlan_id;
        int rx_vlan_id;
    };
    static const std::string kPortsDir;

    explicit PortIpcHandler(Agent *agent);
    PortIpcHandler(Agent *agent, const std::string &dir);
    virtual ~PortIpcHandler();
    void ReloadAllPorts() const;
    void AddPortFromJson(const std::string &json) const;
    void DeletePort(const std::string &uuid_str) const;
    friend class PortIpcTest;
 private:
    void ProcessFile(const std::string &file) const;
    bool ValidateMac(const std::string &mac) const;
    void AddPort(const PortIpcHandler::AddPortParams &req) const;
    bool IsUUID(const std::string &uuid_str) const;

    Agent *agent_;
    std::string ports_dir_;

    DISALLOW_COPY_AND_ASSIGN(PortIpcHandler);
};
#endif  // _ROOT_PORT_IPC_HANDLER_H_
