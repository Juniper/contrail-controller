/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef _ROOT_PORT_IPC_HANDLER_H_
#define _ROOT_PORT_IPC_HANDLER_H_

#include <string>
#include <rapidjson/document.h>
#include <base/timer.h>
#include <net/address.h>
#include <cfg/cfg_interface.h>

class Agent;

class PortIpcHandler {
 public:
    struct AddPortParams {
        AddPortParams() {}
        AddPortParams(std::string pid, std::string iid, std::string vid,
            std::string vm_pid, std::string vname, std::string tname,
            std::string ip, std::string ip6, std::string mac,
            int ptype, int tx_vid, int rx_vid);
        void Set(std::string pid, std::string iid, std::string vid,
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
        boost::uuids::uuid port_uuid;
        boost::uuids::uuid instance_uuid;
        boost::uuids::uuid vn_uuid;
        boost::uuids::uuid vm_project_uuid;
        Ip4Address ip;
        Ip6Address ip6;
        CfgIntEntry::CfgIntType intf_type;
    };
    static const uint32_t kPortSyncInterval = (30 * 1000); //time-millisecs
    static const std::string kPortsDir;

    typedef std::map<const boost::uuids::uuid, bool> PortMap;
    typedef std::pair<const boost::uuids::uuid, bool> PortPair;

    PortIpcHandler(Agent *agent, const std::string &dir);
    virtual ~PortIpcHandler();
    void ReloadAllPorts(bool check_port);
    bool AddPortFromJson(const std::string &json, bool chk_port,
                         std::string &err_msg);
    bool DeletePort(const std::string &uuid_str, std::string &err);
    std::string GetPortInfo(const std::string &uuid_str) const;
    bool InterfaceExists(const std::string &name) const;
    void Shutdown();
    void SyncHandler();
    friend class PortIpcTest;
 private:
    void ProcessFile(const std::string &file, bool check_port);
    bool ValidateMac(const std::string &mac) const;
    bool CanAdd(PortIpcHandler::AddPortParams &r,
                bool check_port, std::string &resp_str) const;
    void AddPort(const PortIpcHandler::AddPortParams &r);
    bool ValidateRequest(const rapidjson::Value &d, const std::string &json,
                         bool check_port, std::string &err_msg,
                         PortIpcHandler::AddPortParams &req) const;
    bool IsUUID(const std::string &uuid_str) const;
    bool HasAllMembers(const rapidjson::Value &d,
                       std::string &member_err) const;
    bool WriteJsonToFile(const PortIpcHandler::AddPortParams &r) const;
    std::string GetJsonString(const PortIpcHandler::AddPortParams &r,
                              bool meta_info) const;
    void InsertPort(const boost::uuids::uuid &u);
    void ErasePort(const boost::uuids::uuid &u);
    void ErasePortUnlocked(const boost::uuids::uuid &u);
    void DeletePortInternal(const boost::uuids::uuid &u, std::string &err_str);
    void MarkPortsAsUnseen();
    bool TimerExpiry();

    Agent *agent_;
    std::string ports_dir_;
    PortMap port_list_;
    Timer *timer_;
    tbb::mutex mutex_; //to regulate port_list_ modification

    DISALLOW_COPY_AND_ASSIGN(PortIpcHandler);
};
#endif  // _ROOT_PORT_IPC_HANDLER_H_
