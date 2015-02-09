/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef _ROOT_PORT_IPC_HANDLER_H_
#define _ROOT_PORT_IPC_HANDLER_H_

#include <string>
#include <rapidjson/document.h>

class Agent;

class PortIpcHandler {
 public:
    struct AddPortParams {
        AddPortParams(std::string pid, std::string iid, std::string vid,
                      std::string vm_pid, std::string vname, std::string tname,
                      std::string ip, std::string ip6, std::string mac,
                      int ptype, int tx_vid, int rx_vid, bool persist);
        std::string port_id;
        std::string instance_id;
        std::string vn_id;
        std::string vm_project_id;
        std::string vm_name;
        std::string system_name;
        std::string ip_address;
        std::string ip6_address;
        std::string mac_address;
        int port_type;
        int tx_vlan_id;
        int rx_vlan_id;
        bool persist;
    };
    static const std::string kPortsDir;

    explicit PortIpcHandler(Agent *agent);
    PortIpcHandler(Agent *agent, const std::string &dir, bool check_port);
    virtual ~PortIpcHandler();
    void ReloadAllPorts() const;
    bool AddPortFromJson(const std::string &json, bool chk_port) const;
    void DeletePort(const std::string &uuid_str, const std::string &json) const;
    std::string GetPortInfo(const std::string &uuid_str) const;
    friend class PortIpcTest;
 private:
    void ProcessFile(const std::string &file) const;
    bool ValidateMac(const std::string &mac) const;
    bool AddPort(const PortIpcHandler::AddPortParams &req, bool chk_p) const;
    bool IsUUID(const std::string &uuid_str) const;
    bool ValidateMembers(const rapidjson::Document &d) const;
    bool WriteJsonToFile(const PortIpcHandler::AddPortParams &r) const;

    Agent *agent_;
    std::string ports_dir_;
    bool check_port_on_reload_;

    DISALLOW_COPY_AND_ASSIGN(PortIpcHandler);
};
#endif  // _ROOT_PORT_IPC_HANDLER_H_
