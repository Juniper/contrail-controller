/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef _ROOT_PORT_IPC_HANDLER_H_
#define _ROOT_PORT_IPC_HANDLER_H_

#include <string>
#include <rapidjson/document.h>
#include <base/timer.h>
#include <base/address.h>
#include <port_ipc/config_stale_cleaner.h>
#include <vgw/cfg_vgw.h>

class Agent;
class PortSubscribeTable;
class PortSubscribeEntry;
class VmiSubscribeEntry;
class VmVnPortSubscribeEntry;
typedef boost::shared_ptr<PortSubscribeEntry> PortSubscribeEntryPtr;
typedef std::vector<PortSubscribeEntryPtr> VmiSubscribeEntryPtrList;

class PortIpcHandler {
public:
    static const std::string kPortsDir;

    PortIpcHandler(Agent *agent, const std::string &dir);
    virtual ~PortIpcHandler();

    void InitDone();
    void Shutdown();
    void ReloadAllPorts(const std::string &dir, bool check_port,
                        bool vm_vn_port);
    void ReloadAllPorts(bool check_port);
    void SyncHandler();

    InterfaceConfigStaleCleaner *interface_stale_cleaner() const {
        return interface_stale_cleaner_.get();
    }

    bool AddPortArrayFromJson(const contrail_rapidjson::Value &d,
                              const std::string &json,
                              VmiSubscribeEntryPtrList &req_list,
                              bool check_port, std::string &err_msg);
    bool AddPortFromJson(const string &json, bool check_port, string &err_msg,
                         bool write_file);
    bool DeletePort(const string &url, string &err_msg);
    void DeleteVmiUuidEntry(const boost::uuids::uuid &u, std::string &err_str);
    bool GetPortInfo(const std::string &uuid_str, std::string &info) const;
    bool AddVgwFromJson(const std::string &json, std::string &err_msg) const;
    bool DelVgwFromJson(const std::string &json, std::string &err_msg) const;
    std::string MakeVmiUuidJson(const VmiSubscribeEntry *entry,
                                bool meta_info) const;
    bool EnablePort(const string &url, string &err_msg);
    bool DisablePort(const string &url, string &err_msg);

    // VM+Vn message handlers
    bool AddVmVnPort(const std::string &json, bool check_port,
                     std::string &err_msg, bool write_file);
    bool DeleteVmVnPort(const boost::uuids::uuid &vmi_uuid, string &err_msg);
    bool DeleteVmVnPort(const std::string &json, const std::string &vm,
                        string &err_msg);
    bool GetVmVnPort(const std::string &vm_uuid, const std::string &vmi_uuid,
                     std::string &info) const;
    bool GetVmVnCfgPort(const string &vm, string &info) const;


    boost::uuids::uuid VmVnToVmi(const boost::uuids::uuid &vm_uuid) const;
    bool MakeJsonFromVmi(const boost::uuids::uuid &vmi_uuid,
                         std::string &resp) const;
    bool MakeJsonFromVmiConfig(const boost::uuids::uuid &vmi_uuid,
                               string &resp) const;
    PortSubscribeTable *port_subscribe_table() const {
        return port_subscribe_table_.get();
    }
private:
    friend class PortIpcTest;
    bool InterfaceExists(const std::string &name) const;

    VmiSubscribeEntry *MakeAddVmiUuidRequest(const contrail_rapidjson::Value &d,
                                             bool check_port,
                                             std::string &err_msg) const;

    VmVnPortSubscribeEntry *MakeAddVmVnPortRequest(const contrail_rapidjson::Value &d,
                                                   bool check_port,
                                                   std::string &err_msg) const;

    bool BuildGateway(const contrail_rapidjson::Value &d, const std::string &json,
                      std::string &err_msg, VirtualGatewayInfo *req) const;
    bool HasAllGatewayFields(const contrail_rapidjson::Value &d,
                             std::string &member_err,
                             VirtualGatewayInfo *req) const;
    bool ValidGatewayJsonString(const contrail_rapidjson::Value &d,
                                VirtualGatewayConfig::SubnetList *list) const;
    bool BuildGatewayArrayElement(const contrail_rapidjson::Value &d,
                                  VirtualGatewayConfig::Subnet *entry) const;

    bool AddVmiUuidEntry(PortSubscribeEntryPtr entry, const contrail_rapidjson::Value &d,
                         bool write_file, std::string &err_msg) const;
    bool AddVmVnPortEntry(PortSubscribeEntryPtr entry,
                          const contrail_rapidjson::Value &d, bool write_file,
                          std::string &err_msg) const;

    bool ValidateMac(const std::string &mac) const;
    bool IsUUID(const std::string &uuid_str) const;
    void ProcessFile(const std::string &file, bool check_port, bool vm_vn_port);
    void AddMember(const char *key, const char *value,
                   contrail_rapidjson::Document *doc) const;
    bool WriteJsonToFile(VmiSubscribeEntry *entry, bool overwrite) const;
    bool WriteJsonToFile(VmVnPortSubscribeEntry *entry) const;

    void MakeVmVnPortJson(const VmVnPortSubscribeEntry *entry, string &info,
                          bool meta_info) const;

    Agent *agent_;
    std::string ports_dir_;
    std::string vmvn_dir_;
    int version_;
    boost::scoped_ptr<InterfaceConfigStaleCleaner> interface_stale_cleaner_;
    std::auto_ptr<PortSubscribeTable> port_subscribe_table_;

    DISALLOW_COPY_AND_ASSIGN(PortIpcHandler);
};
#endif  // _ROOT_PORT_IPC_HANDLER_H_
