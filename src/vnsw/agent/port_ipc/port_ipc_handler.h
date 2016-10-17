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
class VmInterface;

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
    bool DeletePort(const std::string &body, const std::string &url,
                    std::string &err_msg) const;
    bool DeleteVmiUuidEntry(const std::string &body, const std::string &url,
                            std::string &err_msg) const;
    bool DeleteVmiLabelEntry(const std::string &body, const std::string &url,
                             std::string &err_msg) const;
    bool GetPortInfo(const std::string &body, const std::string &uuid_str,
                     std::string &info) const;
    bool GetVmiUuidInfo(const std::string &body, const std::string &uuid_str,
                        std::string &info) const;
    bool GetVmiLabelInfo(const std::string &body, const std::string &uuid_str,
                         std::string &info) const;
    bool AddVgwFromJson(const std::string &json, std::string &err_msg) const;
    bool DelVgwFromJson(const std::string &json, std::string &err_msg) const;
 private:
    friend class PortIpcTest;
    bool InterfaceExists(const std::string &name) const;

    void MakeVmiUuidJson(const InterfaceConfigVmiEntry *vmi,
                         std::string &info) const;
    bool MakeAddVmiUuidRequest(const rapidjson::Value &d,
                               const std::string &json, bool check_port,
                               std::string &err_msg, DBRequest *req) const;

    bool MakeAddVmiLabelRequest(const rapidjson::Value &d,
                                const std::string &json, bool check_port,
                                std::string &err_msg, DBRequest *req) const;
    bool AddVmiLabelEntry(DBRequest *req, rapidjson::Value &d,
                          bool write_file, string &err_msg) const;
    bool GetLabelData(const rapidjson::Value &d, const std::string &json,
                      string &resp) const;

    bool BuildGateway(const rapidjson::Value &d, const std::string &json,
                      std::string &err_msg, VirtualGatewayInfo *req) const;
    bool HasAllGatewayFields(const rapidjson::Value &d,
                             std::string &member_err,
                             VirtualGatewayInfo *req) const;
    bool ValidGatewayJsonString(const rapidjson::Value &d,
                                VirtualGatewayConfig::SubnetList *list) const;
    bool BuildGatewayArrayElement(const rapidjson::Value &d,
                                  VirtualGatewayConfig::Subnet *entry) const;

    bool AddVmiUuidEntry(DBRequest *req, rapidjson::Value &d,
                         bool write_file, std::string &err_msg) const;

    bool ValidateMac(const std::string &mac) const;
    void ProcessFile(const std::string &file, bool check_port);
    bool WriteJsonToFile(rapidjson::Value &v,
                         const std::string &fname) const;

    Agent *agent_;
    std::string ports_dir_;
    int version_;
    boost::scoped_ptr<InterfaceConfigStaleCleaner> interface_stale_cleaner_;

    DISALLOW_COPY_AND_ASSIGN(PortIpcHandler);
};
#endif  // _ROOT_PORT_IPC_HANDLER_H_
