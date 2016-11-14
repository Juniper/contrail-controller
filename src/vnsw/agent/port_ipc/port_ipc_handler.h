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
#include <port_ipc/config_stale_cleaner.h>
#include <vgw/cfg_vgw.h>

class Agent;

class PortIpcHandler {
 public:
    static const std::string kPortsDir;

    PortIpcHandler(Agent *agent, const std::string &dir);
    virtual ~PortIpcHandler();

    void Shutdown();
    void ReloadAllPorts(bool check_port);
    void SyncHandler();

    InterfaceConfigStaleCleaner *interface_stale_cleaner() const {
        return interface_stale_cleaner_.get();
    }

    bool AddPortFromJson(const string &json, bool check_port, string &err_msg,
                         bool write_file);
    bool DeletePort(const string &json, const string &url, string &err_msg);
    void DeleteVmiUuidEntry(const boost::uuids::uuid &u, std::string &err_str);
    bool GetPortInfo(const std::string &uuid_str, std::string &info) const;
    bool AddVgwFromJson(const std::string &json, std::string &err_msg) const;
    bool DelVgwFromJson(const std::string &json, std::string &err_msg) const;
    void MakeVmiUuidJson(const CfgIntEntry *entry, string &info) const;
    void MakeVmiUuidJson(const DBRequest *req, string &info) const;
 private:
    friend class PortIpcTest;
    bool InterfaceExists(const std::string &name) const;

    bool MakeAddVmiUuidRequest(const rapidjson::Value &d,
                               const std::string &json, bool check_port,
                               std::string &err_msg, DBRequest *req) const;

    bool BuildGateway(const rapidjson::Value &d, const std::string &json,
                      std::string &err_msg, VirtualGatewayInfo *req) const;
    bool HasAllGatewayFields(const rapidjson::Value &d,
                             std::string &member_err,
                             VirtualGatewayInfo *req) const;
    bool ValidGatewayJsonString(const rapidjson::Value &d,
                                VirtualGatewayConfig::SubnetList *list) const;
    bool BuildGatewayArrayElement(const rapidjson::Value &d,
                                  VirtualGatewayConfig::Subnet *entry) const;

    bool AddVmiUuidEntry(DBRequest *req, const rapidjson::Value &d,
                         bool write_file, std::string &err_msg) const;

    bool ValidateMac(const std::string &mac) const;
    bool IsUUID(const std::string &uuid_str) const;
    void ProcessFile(const std::string &file, bool check_port);
    bool WriteJsonToFile(const rapidjson::Value &v, DBRequest *req) const;

    Agent *agent_;
    std::string ports_dir_;
    int version_;
    boost::scoped_ptr<InterfaceConfigStaleCleaner> interface_stale_cleaner_;

    DISALLOW_COPY_AND_ASSIGN(PortIpcHandler);
};
#endif  // _ROOT_PORT_IPC_HANDLER_H_
