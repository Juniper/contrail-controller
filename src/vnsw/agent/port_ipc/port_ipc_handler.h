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
                      int ptype, int tx_vid, int rx_vid);
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
    };
    static const std::string kPortsDir;

    PortIpcHandler(Agent *agent, const std::string &dir, bool check_port);
    virtual ~PortIpcHandler();
    void ReloadAllPorts() const;
    bool AddPortFromJson(const std::string &json, bool chk_port,
                         std::string &err_msg) const;
    bool DeletePort(const std::string &uuid_str, std::string &err) const;
    std::string GetPortInfo(const std::string &uuid_str) const;
    bool InterfaceExists(const std::string &name) const;
    friend class PortIpcTest;
 private:
    void ProcessFile(const std::string &file) const;
    bool ValidateMac(const std::string &mac) const;
    bool AddPort(const PortIpcHandler::AddPortParams &req, bool chk_p,
                 std::string &err_msg) const;
    bool IsUUID(const std::string &uuid_str) const;
    bool ValidateMembers(const rapidjson::Document &d,
                 std::string &member_err) const;
    bool WriteJsonToFile(const PortIpcHandler::AddPortParams &r) const;
    std::string GetJsonString(const PortIpcHandler::AddPortParams &r,
                              bool meta_info) const;

    Agent *agent_;
    std::string ports_dir_;
    bool check_port_on_reload_;

    DISALLOW_COPY_AND_ASSIGN(PortIpcHandler);
};
#endif  // _ROOT_PORT_IPC_HANDLER_H_
