/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#include <ctype.h>
#include <stdio.h>
#include <sstream>
#include <fstream>
#include <net/if.h>
#include <boost/uuid/uuid.hpp>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include "base/logging.h"
#include "base/task.h"
#include "base/string_util.h"
#include "db/db.h"
#include "db/db_entry.h"
#include "db/db_table.h"
#include "cmn/agent_cmn.h"
#include "init/agent_param.h"
#include "cfg/cfg_init.h"
#include "oper/interface_common.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "port_ipc/port_ipc_handler.h"
#include "port_ipc/port_ipc_types.h"
#include "port_ipc/port_subscribe_table.h"

using namespace std;
namespace fs = boost::filesystem;

const std::string PortIpcHandler::kPortsDir = "/var/lib/contrail/ports";

/////////////////////////////////////////////////////////////////////////////
// Utility methods
/////////////////////////////////////////////////////////////////////////////
static bool GetStringMember(const contrail_rapidjson::Value &d, const char *member,
                            std::string *data, std::string *err) {
    if (!d.HasMember(member) || !d[member].IsString()) {
        if (err) {
            *err += "Invalid or missing field for <" + string(member) + ">";
        }
        return false;
    }

    *data = d[member].GetString();
    return true;
}

static bool GetUint32Member(const contrail_rapidjson::Value &d, const char *member,
                            uint32_t *data, std::string *err) {
    if (!d.HasMember(member) || !d[member].IsInt()) {
        if (err) {
            *err += "Invalid or missing field for <" + string(member) + ">";
        }
        return false;
    }

    *data = d[member].GetInt();
    return true;
}

static bool GetUuidMember(const contrail_rapidjson::Value &d, const char *member,
                          boost::uuids::uuid *u, std::string *err) {
    if (!d.HasMember(member) || !d[member].IsString()) {
        if (err) {
            *err += "Invalid or missing data for <" + string(member) + ">";
        }
        return false;
    }

    *u = StringToUuid(d[member].GetString());
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// PortIpcHandler methods
/////////////////////////////////////////////////////////////////////////////
PortIpcHandler::PortIpcHandler(Agent *agent, const std::string &dir)
    : agent_(agent), ports_dir_(dir), vmvn_dir_(dir + "/vm"), version_(0),
      interface_stale_cleaner_(new InterfaceConfigStaleCleaner(agent)),
       port_subscribe_table_(new PortSubscribeTable(agent_)) {
    interface_stale_cleaner_->set_callback(
        boost::bind(&InterfaceConfigStaleCleaner::OnInterfaceConfigStaleTimeout,
                    interface_stale_cleaner_.get(), _1));
    uint32_t timeout_secs = agent->params()->stale_interface_cleanup_timeout();
    //Set timeout in milliseconds
    interface_stale_cleaner_->set_timeout(timeout_secs * 1000);

    fs::path ports_dir(ports_dir_);
    if (fs::exists(ports_dir) == false) {
        if (!fs::create_directories(ports_dir)) {
            string err_msg = "Creating directory " + ports_dir_ + " failed";
            CONFIG_TRACE(PortInfo, err_msg.c_str());
        }
    }

    // Create directory for vm-vn subscription config files
    fs::path vm_vn_dir(vmvn_dir_);
    if (fs::exists(vm_vn_dir) == false) {
        if (!fs::create_directories(vm_vn_dir)) {
            string err_msg = "Creating directory " + vmvn_dir_ + " failed";
            CONFIG_TRACE(PortInfo, err_msg.c_str());
        }
    }
}

PortIpcHandler::~PortIpcHandler() {
}

void PortIpcHandler::ReloadAllPorts(const std::string &dir, bool check_port,
                                    bool vm_vn_ports) {
    fs::path ports_dir(dir);
    fs::directory_iterator end_iter;

    if (!fs::exists(ports_dir) || !fs::is_directory(ports_dir)) {
        return;
    }

    fs::directory_iterator it(ports_dir);
    BOOST_FOREACH(fs::path const &p, std::make_pair(it, end_iter)) {
        if (!fs::is_regular_file(p)) {
            continue;
        }
        /* Skip if filename is not in UUID format */
        if (!IsUUID(p.filename().string())) {
            continue;
        }

        ProcessFile(p.string(), check_port, vm_vn_ports);
    }
}

void PortIpcHandler::ReloadAllPorts(bool check_port) {
    ReloadAllPorts(ports_dir_, check_port, false);
    ReloadAllPorts(vmvn_dir_, check_port, true);
}

void PortIpcHandler::ProcessFile(const string &file, bool check_port,
                                 bool vm_vn_ports) {
    string err_msg;
    ifstream f(file.c_str());
    if (!f.good()) {
        return;
    }
    ostringstream tmp;
    tmp<<f.rdbuf();
    string json = tmp.str();
    f.close();
    
    if (vm_vn_ports == false)
        AddPortFromJson(json, check_port, err_msg, false);
    else
        AddVmVnPort(json, check_port, err_msg, false);
}

bool PortIpcHandler::AddPortArrayFromJson(const contrail_rapidjson::Value &d,
                                          const string &json,
                                          VmiSubscribeEntryPtrList &req_list,
                                          bool check_port,
                                          string &err_msg) {
    for (size_t i = 0; i < d.Size(); i++) {
        const contrail_rapidjson::Value& elem = d[i];
        if (elem.IsObject() == false) {
            err_msg = "Json Array has invalid element ==> " + json;
            CONFIG_TRACE(PortInfo, err_msg.c_str());
            return false;
        }

        PortSubscribeEntry *entry =
            MakeAddVmiUuidRequest(elem, json, check_port, err_msg);
        if (entry == NULL) {
            CONFIG_TRACE(PortInfo, err_msg.c_str());
            return false;
        }

        req_list.push_back(PortSubscribeEntryPtr(entry));
    }
    return true;
}

bool PortIpcHandler::AddPortFromJson(const string &json, bool check_port,
                                     string &err_msg, bool write_file) {
    contrail_rapidjson::Document d;
    if (d.Parse<0>(const_cast<char *>(json.c_str())).HasParseError()) {
        err_msg = "Invalid Json string ==> " + json;
        CONFIG_TRACE(PortInfo, err_msg.c_str());
        return false;
    }
    if (!d.IsObject() && !d.IsArray()) {
        err_msg = "Unexpected Json string ==> " + json;
        CONFIG_TRACE(PortInfo, err_msg.c_str());
        return false;
    }

    if (d.IsArray()) {
        /* When Json Array is passed, we do 'All or None'. We add all the
         * elements of the array or none are added. So we do validation in
         * first pass and addition in second pass */
        VmiSubscribeEntryPtrList req_list;
        if (AddPortArrayFromJson(d, json, req_list, check_port, err_msg)) {
            for (size_t i = 0; i < req_list.size(); i++) {
                AddVmiUuidEntry(req_list[i], d, write_file, err_msg);
            }
        }

        req_list.clear();
        return true;
    }

    PortSubscribeEntryPtr entry(MakeAddVmiUuidRequest(d, json, check_port,
                                                      err_msg));
    if (entry.get() == NULL) {
        CONFIG_TRACE(PortInfo, err_msg.c_str());
        return false;
    }

    AddVmiUuidEntry(entry, d, write_file, err_msg);
    return true;
}

VmiSubscribeEntry *PortIpcHandler::MakeAddVmiUuidRequest
(const contrail_rapidjson::Value &d, const std::string &json, bool check_port,
 std::string &err_msg) const {
    boost::uuids::uuid vmi_uuid;
    if (GetUuidMember(d, "id", &vmi_uuid, &err_msg) == false) {
        return NULL;
    }

    boost::uuids::uuid instance_uuid;
    if (GetUuidMember(d, "instance-id", &instance_uuid, &err_msg) == false) {
        return NULL;
    }

    boost::uuids::uuid vn_uuid;
    if (GetUuidMember(d, "vn-id", &vn_uuid, &err_msg) == false) {
        return NULL;
    }

    boost::uuids::uuid project_uuid;
    if (GetUuidMember(d, "vm-project-id", &project_uuid, &err_msg) == false) {
        return NULL;
    }

    string vm_name;
    if (GetStringMember(d, "display-name", &vm_name, &err_msg) == false) {
        return NULL;
    }

    string ip4_str;
    if (GetStringMember(d, "ip-address", &ip4_str, &err_msg) == false) {
        return NULL;
    }
    boost::system::error_code ec;
    Ip4Address ip4 = Ip4Address::from_string(ip4_str, ec);

    string ip6_str;
    if (GetStringMember(d, "ip6-address", &ip6_str, &err_msg) == false) {
        return NULL;
    }
    boost::system::error_code ec6;
    Ip6Address ip6 = Ip6Address::from_string(ip6_str, ec6);

    uint32_t val;
    if (GetUint32Member(d, "type", &val, &err_msg) == false) {
        return NULL;
    }

    PortSubscribeEntry::Type vmi_type = PortSubscribeEntry::VMPORT;
    if (val == 1) {
        vmi_type = PortSubscribeEntry::NAMESPACE;
    } else if (val == 2) {
        vmi_type = PortSubscribeEntry::REMOTE_PORT;
    }
    if (vmi_type == PortSubscribeEntry::VMPORT) {
        if (vn_uuid == nil_uuid()) {
            err_msg = "Invalid VN uuid";
            return NULL;
        }
    }

    string ifname;
    if (GetStringMember(d, "system-name", &ifname, &err_msg) == false) {
        return NULL;
    }
    // Verify that interface exists in OS
    if (vmi_type != PortSubscribeEntry::REMOTE_PORT) {
        if (check_port && !InterfaceExists(ifname)) {
            err_msg = "Interface does not exist in OS";
            return NULL;
        }
    }

    string mac;
    if (GetStringMember(d, "mac-address", &mac, &err_msg) == false) {
        return NULL;
    }
    if (ValidateMac(mac) == false) {
        err_msg = "Invalid MAC, Use xx:xx:xx:xx:xx:xx format";
        return NULL;
    }

    // Sanity check. We should not have isolated_vlan_id set and vlan_id unset
    uint32_t rx_vlan_id = VmInterface::kInvalidVlanId;
    if (GetUint32Member(d, "rx-vlan-id", &rx_vlan_id, &err_msg) == false) {
        return NULL;
    }

    uint32_t tx_vlan_id = VmInterface::kInvalidVlanId;
    if (GetUint32Member(d, "tx-vlan-id", &tx_vlan_id, &err_msg) == false) {
        return NULL;
    }

    if ((rx_vlan_id != VmInterface::kInvalidVlanId) &&
        (tx_vlan_id == VmInterface::kInvalidVlanId-1)) {
        err_msg += "Invalid request. RX (isolated) vlan set, "
            "but TX vlan not set";
        return NULL;
    }

    CONFIG_TRACE(AddPortEnqueue, "Add", UuidToString(vmi_uuid),
                 UuidToString(instance_uuid), UuidToString(vn_uuid),
                 ip4.to_string(), ifname, mac, vm_name, tx_vlan_id, rx_vlan_id,
                 UuidToString(project_uuid),
                 PortSubscribeEntry::TypeToString(vmi_type),
                 ip6.to_string(), version_);
    return new VmiSubscribeEntry(vmi_type, ifname, version_, vmi_uuid,
                                 instance_uuid, vm_name, vn_uuid, project_uuid,
                                 ip4, ip6, mac, tx_vlan_id, rx_vlan_id);
}

bool PortIpcHandler::AddVmiUuidEntry(PortSubscribeEntryPtr entry_ref,
                                     const contrail_rapidjson::Value &d,
                                     bool write_file, string &err_msg) const {
    VmiSubscribeEntry *entry =
        dynamic_cast<VmiSubscribeEntry *>(entry_ref.get());
    // If Writing to file fails return error
    if(write_file && WriteJsonToFile(d, entry) == false) {
        err_msg += "Writing of Json string to file failed for " +
            UuidToString(entry->vmi_uuid());
        CONFIG_TRACE(PortInfo, err_msg.c_str());
        return false;
    }

    CONFIG_TRACE(AddPortEnqueue, "Add", UuidToString(entry->vmi_uuid()),
                 UuidToString(entry->vm_uuid()), UuidToString(entry->vn_uuid()),
                 entry->ip4_addr().to_string(), entry->ifname(),
                 entry->mac_addr(), entry->vm_name(), entry->tx_vlan_id(),
                 entry->rx_vlan_id(), UuidToString(entry->project_uuid()),
                 PortSubscribeEntry::TypeToString(entry->type()),
                 entry->ip6_addr().to_string(), entry->version());
    port_subscribe_table_->AddVmi(entry->vmi_uuid(), entry_ref);
    return true;
}

bool PortIpcHandler::WriteJsonToFile(const contrail_rapidjson::Value &v,
                                     VmiSubscribeEntry *entry) const {
    string filename = ports_dir_ + "/" + UuidToString(entry->vmi_uuid());
    fs::path file_path(filename);

    /* Don't overwrite if the file already exists */
    if (fs::exists(file_path)) {
        return true;
    }

    string info;
    MakeVmiUuidJson(entry, info, true);
    ofstream fs(filename.c_str());
    if (fs.fail()) {
        return false;
    }
    fs << info;
    fs.close();

    return true;
}

bool PortIpcHandler::ValidateMac(const string &mac) const {
    size_t pos = 0;
    int colon = 0;
    if (mac.empty()) {
        return false;
    }
    while ((pos = mac.find(':', pos)) != std::string::npos) {
        colon++;
        pos += 1;
    }
    if (colon != 5)
        return false;

    if (MacAddress::FromString(mac).IsZero())
        return false;

    return true;
}

bool PortIpcHandler::DeletePort(const string &json, const string &url,
                                string &err_msg) {
    uuid vmi_uuid = StringToUuid(url);
    if (vmi_uuid != nil_uuid()) {
        DeleteVmiUuidEntry(vmi_uuid, err_msg);
        return true;
    }

    return true;
}

void PortIpcHandler::DeleteVmiUuidEntry(const uuid &u, string &err_msg) {
    uint64_t version = 0;
    PortSubscribeEntryPtr entry = port_subscribe_table_->GetVmi(u);
    if (entry.get() != NULL) {
        version = entry->version();
    }
    port_subscribe_table_->DeleteVmi(u);
    CONFIG_TRACE(DeletePortEnqueue, "Delete", UuidToString(u), version);

    string file = ports_dir_ + "/" + UuidToString(u);
    fs::path file_path(file);

    if (fs::exists(file_path) && fs::is_regular_file(file_path)) {
        if (remove(file.c_str())) {
            err_msg = "Error deleting file " + file;
        }
    }
}

bool PortIpcHandler::IsUUID(const string &uuid_str) const {
    uuid vmi_uuid = StringToUuid(uuid_str);
    if (vmi_uuid == nil_uuid()) {
        return false;
    }
    return true;
}

void PortIpcHandler::AddMember(const char *key, const char *value,
        contrail_rapidjson::Document *doc) const {
    contrail_rapidjson::Document::AllocatorType &a = doc->GetAllocator();
    contrail_rapidjson::Value v;
    contrail_rapidjson::Value vk;
    doc->AddMember(vk.SetString(key, a), v.SetString(value, a), a);
}

void PortIpcHandler::MakeVmiUuidJson(const VmiSubscribeEntry *entry,
                                     string &info, bool meta_info) const {
    contrail_rapidjson::Document doc;
    doc.SetObject();
    contrail_rapidjson::Document::AllocatorType &a = doc.GetAllocator();

    string str1 = UuidToString(entry->vmi_uuid());
    AddMember("id", str1.c_str(), &doc);
    string str2 = UuidToString(entry->vm_uuid());
    AddMember("instance-id", str2.c_str(), &doc);
    AddMember("display-name", entry->vm_name().c_str(), &doc);
    string str3 = entry->ip4_addr().to_string();
    AddMember("ip-address", str3.c_str(), &doc);
    string str4 = entry->ip6_addr().to_string();
    AddMember("ip6-address", str4.c_str(), &doc);
    string str5 = UuidToString(entry->vn_uuid());
    AddMember("vn-id", str5.c_str(), &doc);
    string str6 = UuidToString(entry->project_uuid());
    AddMember("vm-project-id", str6.c_str(), &doc);
    AddMember("mac-address", entry->mac_addr().c_str(), &doc);
    AddMember("system-name", entry->ifname().c_str(), &doc);
    doc.AddMember("type", (int)entry->type(), a);
    doc.AddMember("rx-vlan-id", (int)entry->rx_vlan_id(), a);
    doc.AddMember("tx-vlan-id", (int)entry->tx_vlan_id(), a);
    if (meta_info) {
        AddMember("author", agent_->program_name().c_str(), &doc);
        string now = duration_usecs_to_string(UTCTimestampUsec());
        AddMember("time", now.c_str(), &doc);
    }

    contrail_rapidjson::StringBuffer buffer;
    contrail_rapidjson::PrettyWriter<contrail_rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    info = buffer.GetString();
    return;
}

bool PortIpcHandler::GetPortInfo(const string &uuid_str, string &info) const {
    uuid vmi_uuid = StringToUuid(uuid_str);
    if (vmi_uuid == nil_uuid()) {
        return false;
    }

    return MakeJsonFromVmi(vmi_uuid, info);
}

bool PortIpcHandler::InterfaceExists(const std::string &name) const {
    int indx  = if_nametoindex(name.c_str());
    if (indx == 0) {
        return false;
    }
    return true;
}

void PortIpcHandler::SyncHandler() {
    ++version_;
    interface_stale_cleaner_->StartStaleCleanTimer(version_);
    string msg = " Sync " + integerToString(version_);
    CONFIG_TRACE(PortInfo, msg);
}

void PortIpcHandler::InitDone() {
    port_subscribe_table_->InitDone();
}

void PortIpcHandler::Shutdown() {
    port_subscribe_table_->Shutdown();
}

bool PortIpcHandler::BuildGatewayArrayElement
(const contrail_rapidjson::Value &d, VirtualGatewayConfig::Subnet *entry) const {
    if (!d.HasMember("ip-address") || !d["ip-address"].IsString()) {
        return false;
    }

    if (!d.HasMember("prefix-len") || !d["prefix-len"].IsInt()) {
        return false;
    }
    boost::system::error_code ec;
    entry->ip_ = Ip4Address::from_string(d["ip-address"].GetString(), ec);
    if (ec != 0) {
        return false;
    }
    entry->plen_ = d["prefix-len"].GetInt();
    return true;
}

bool PortIpcHandler::ValidGatewayJsonString
(const contrail_rapidjson::Value &d, VirtualGatewayConfig::SubnetList *list) const {
    for (size_t i = 0; i < d.Size(); i++) {
        const contrail_rapidjson::Value& elem = d[i];
        if (!elem.IsObject()) {
            return false;
        }
        VirtualGatewayConfig::Subnet entry;
        if (!BuildGatewayArrayElement(elem, &entry)) {
            return false;
        }
        list->push_back(entry);
    }
    return true;
}

bool PortIpcHandler::HasAllGatewayFields(const contrail_rapidjson::Value &d,
                                         std::string &member_err,
                                         VirtualGatewayInfo *req) const {
    if (!d.HasMember("interface") || !d["interface"].IsString()) {
        member_err = "interface";
        return false;
    }

    if (!d.HasMember("routing-instance") || !d["routing-instance"].IsString()) {
        member_err = "routing-instance";
        return false;
    }

    if (!d.HasMember("subnets") || !d["subnets"].IsArray()) {
        member_err = "subnets";
        return false;
    }

    if (!ValidGatewayJsonString(d["subnets"], &req->subnets_)) {
        member_err = "subnets value";
        return false;
    }

    if (!d.HasMember("routes") || !d["routes"].IsArray()) {
        member_err = "routes";
        return false;
    }

    if (!ValidGatewayJsonString(d["routes"], &req->routes_)) {
        member_err = "routes value";
        return false;
    }
    req->interface_name_ = d["interface"].GetString();
    req->vrf_name_ = d["routing-instance"].GetString();

    return true;
}

bool PortIpcHandler::BuildGateway(const contrail_rapidjson::Value &d,
                                  const string &json, string &err_msg,
                                  VirtualGatewayInfo *req) const {
    string member_err;
    if (!HasAllGatewayFields(d, member_err, req)) {
        err_msg = "Json string does not have all required members, "
                         + member_err + " is missing ==> "
                         + json;
        CONFIG_TRACE(PortInfo, err_msg.c_str());
        return false;
    }
    return true;
}

bool PortIpcHandler::AddVgwFromJson(const string &json, string &err_msg) const {
    contrail_rapidjson::Document d;
    if (d.Parse<0>(const_cast<char *>(json.c_str())).HasParseError()) {
        err_msg = "Invalid Json string ==> " + json;
        CONFIG_TRACE(PortInfo, err_msg.c_str());
        return false;
    }
    if (!d.IsArray()) {
        err_msg = "Unexpected Json string (not an array) ==> " + json;
        CONFIG_TRACE(PortInfo, err_msg.c_str());
        return false;
    }

    std::vector<VirtualGatewayInfo> req_list;
    for (size_t i = 0; i < d.Size(); i++) {
        const contrail_rapidjson::Value& elem = d[i];
        if (elem.IsObject()) {
            VirtualGatewayInfo req("");
            if (!BuildGateway(elem, json, err_msg, &req)) {
                return false;
            }
            req_list.push_back(req);
        } else {
            err_msg = "Json Array has invalid element ==> " + json;
            CONFIG_TRACE(PortInfo, err_msg.c_str());
            return false;
        }
    }

    if (!req_list.empty()) {
        boost::shared_ptr<VirtualGatewayData>
            vgw_data(new VirtualGatewayData(VirtualGatewayData::Add, req_list,
                                            0));
        agent_->params()->vgw_config_table()->Enqueue(vgw_data);
        CONFIG_TRACE(VgwEnqueue, "Add", json);
    }
    return true;
}

bool PortIpcHandler::DelVgwFromJson(const string &json, string &err_msg) const {
    contrail_rapidjson::Document d;
    if (d.Parse<0>(const_cast<char *>(json.c_str())).HasParseError()) {
        err_msg = "Invalid Json string ==> " + json;
        CONFIG_TRACE(PortInfo, err_msg.c_str());
        return false;
    }
    if (!d.IsArray()) {
        err_msg = "Unexpected Json string (not an array) ==> " + json;
        CONFIG_TRACE(PortInfo, err_msg.c_str());
        return false;
    }

    std::vector<VirtualGatewayInfo> req_list;
    for (size_t i = 0; i < d.Size(); i++) {
        const contrail_rapidjson::Value& elem = d[i];
        if (elem.IsObject()) {
            if (!elem.HasMember("interface") || !elem["interface"].IsString()) {
                err_msg = "Json string does not have or has invalid value for "
                    "member interface ==> " + json;
                CONFIG_TRACE(PortInfo, err_msg.c_str());
                return false;
            }
            VirtualGatewayInfo req(elem["interface"].GetString());
            req_list.push_back(req);
        } else {
            err_msg = "Json Array has invalid element ==> " + json;
            CONFIG_TRACE(PortInfo, err_msg.c_str());
            return false;
        }
    }

    if (!req_list.empty()) {
        boost::shared_ptr<VirtualGatewayData>
            vgw_data(new VirtualGatewayData(VirtualGatewayData::Delete,
                                            req_list, 0));
        agent_->params()->vgw_config_table()->Enqueue(vgw_data);
        CONFIG_TRACE(VgwEnqueue, "Del", json);
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// VM based message related methods
/////////////////////////////////////////////////////////////////////////////
bool PortIpcHandler::AddVmVnPort(const string &json, bool check_port,
                                 string &err_msg, bool write_file) {
    contrail_rapidjson::Document d;
    if (d.Parse<0>(const_cast<char *>(json.c_str())).HasParseError()) {
        err_msg = "Invalid Json string ==> " + json;
        CONFIG_TRACE(VmVnPortInfo, err_msg.c_str());
        return false;
    }

    if (!d.IsObject()) {
        err_msg = "Unexpected Json string ==> " + json;
        CONFIG_TRACE(VmVnPortInfo, err_msg.c_str());
        return false;
    }

    if (d.IsArray()) {
        err_msg = "Arrays not supported in json for vm ==> " + json;
        CONFIG_TRACE(VmVnPortInfo, err_msg.c_str());
        return false;
    }

    PortSubscribeEntryPtr entry(MakeAddVmVnPortRequest(d, json, check_port,
                                                       err_msg));
    if (entry.get() == NULL) {
        CONFIG_TRACE(VmVnPortInfo, err_msg.c_str());
        return false;
    }

    AddVmVnPortEntry(entry, d, write_file, err_msg);
    return true;
}

bool PortIpcHandler::AddVmVnPortEntry(PortSubscribeEntryPtr entry_ref,
                                     const contrail_rapidjson::Value &d,
                                     bool write_file, string &err_msg) const {
    VmVnPortSubscribeEntry *entry =
        dynamic_cast<VmVnPortSubscribeEntry *>(entry_ref.get());
    // If Writing to file fails return error
    if(write_file && WriteJsonToFile(d, entry) == false) {
        err_msg += "Writing of Json string to file failed for Vm " +
            UuidToString(entry->vm_uuid());
        CONFIG_TRACE(VmVnPortInfo, err_msg.c_str());
        return false;
    }

    CONFIG_TRACE(AddVmVnPortEnqueue, "Add", UuidToString(entry->vm_uuid()),
                 UuidToString(nil_uuid()), entry->vm_name(),
                 entry->vm_identifier(), entry->ifname(), entry->vm_ifname(),
                 entry->vm_namespace(), version_);
    port_subscribe_table_->AddVmVnPort(entry->vm_uuid(), entry_ref);
    return true;
}

VmVnPortSubscribeEntry *PortIpcHandler::MakeAddVmVnPortRequest
(const contrail_rapidjson::Value &d, const std::string &json, bool check_port,
 std::string &err_msg) const {
    PortSubscribeEntry::Type vmi_type = PortSubscribeEntry::VMPORT;
    boost::uuids::uuid vm_uuid;
    if (GetUuidMember(d, "vm-uuid", &vm_uuid, &err_msg) == false) {
        return NULL;
    }

    string vm_name;
    if (GetStringMember(d, "vm-name", &vm_name, &err_msg) == false) {
        return NULL;
    }

    string vm_identifier;
    GetStringMember(d, "vm-id", &vm_identifier, NULL);

    string host_ifname;
    if (GetStringMember(d, "host-ifname", &host_ifname, &err_msg) == false) {
        return NULL;
    }
    // Verify that interface exists in OS
    if (vmi_type != PortSubscribeEntry::REMOTE_PORT) {
        if (check_port && !InterfaceExists(host_ifname)) {
            err_msg = "Interface does not exist in OS";
            return NULL;
        }
    }

    string vm_ifname;
    GetStringMember(d, "vm-ifname", &vm_ifname, NULL);

    string vm_namespace;
    GetStringMember(d, "vm-namespace", &vm_namespace, NULL);

    CONFIG_TRACE(AddVmVnPortEnqueue, "Add", UuidToString(vm_uuid),
                 UuidToString(nil_uuid()), vm_name, vm_identifier,
                 host_ifname, vm_ifname, vm_namespace, version_);

    return new VmVnPortSubscribeEntry(vmi_type, host_ifname, version_, vm_uuid,
                                      vm_name, vm_identifier, vm_ifname,
                                      vm_namespace);
}

void PortIpcHandler::MakeVmVnPortJson(const VmVnPortSubscribeEntry *entry,
                                      string &info, bool meta_info) const {
    contrail_rapidjson::Document doc;
    doc.SetObject();

    string str2 = UuidToString(entry->vm_uuid());
    AddMember("vm-uuid", str2.c_str(), &doc);
    AddMember("vm-id", entry->vm_identifier().c_str(), &doc);
    AddMember("vm-name", entry->vm_name().c_str(), &doc);
    AddMember("host-ifname", entry->ifname().c_str(), &doc);
    AddMember("vm-ifname", entry->vm_ifname().c_str(), &doc);
    AddMember("vm-namespace", entry->vm_namespace().c_str(), &doc);

    if (meta_info) {
        AddMember("author", agent_->program_name().c_str(), &doc);
        string now = duration_usecs_to_string(UTCTimestampUsec());
        AddMember("time", now.c_str(), &doc);
    }

    contrail_rapidjson::StringBuffer buffer;
    contrail_rapidjson::PrettyWriter<contrail_rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    info = buffer.GetString();
    return;
}

bool PortIpcHandler::WriteJsonToFile(const contrail_rapidjson::Value &v,
                                     VmVnPortSubscribeEntry *entry) const {
    string filename = vmvn_dir_ + "/" + UuidToString(entry->vm_uuid());
    fs::path file_path(filename);

    /* Don't overwrite if the file already exists */
    if (fs::exists(file_path)) {
        return true;
    }

    string info;
    MakeVmVnPortJson(entry, info, true);
    ofstream fs(filename.c_str());
    if (fs.fail()) {
        return false;
    }
    fs << info;
    fs.close();

    return true;
}

bool PortIpcHandler::DeleteVmVnPort(const boost::uuids::uuid &vm_uuid,
                                    string &err_msg) {
    uint64_t version = 0;
    PortSubscribeEntryPtr entry = port_subscribe_table_->GetVmVnPort(vm_uuid);
    if (entry.get() != NULL) {
        version = entry->version();
    }

    port_subscribe_table_->DeleteVmVnPort(vm_uuid);
    CONFIG_TRACE(DeleteVmVnPortEnqueue, "Delete", UuidToString(vm_uuid),
                 UuidToString(nil_uuid()), version);

    string file = vmvn_dir_ + "/" + UuidToString(vm_uuid);
    fs::path file_path(file);

    if (fs::exists(file_path) && fs::is_regular_file(file_path)) {
        if (remove(file.c_str())) {
            err_msg = "Error deleting file " + file;
        }
    }

    return true;
}

bool PortIpcHandler::DeleteVmVnPort(const string &json, const string &vm,
                                    string &err_msg) {
    uuid vm_uuid = StringToUuid(vm);
    if (vm_uuid == nil_uuid()) {
        return true;
    }

    DeleteVmVnPort(vm_uuid, err_msg);
    return true;
}

bool PortIpcHandler::GetVmVnCfgPort(const string &vm, string &info) const {
    uuid vm_uuid = StringToUuid(vm);
    if (vm_uuid == nil_uuid()) {
        return false;
    }

    uuid vmi_uuid = port_subscribe_table_->VmVnToVmi(vm_uuid);
    return MakeJsonFromVmiConfig(vmi_uuid, info);
}

bool PortIpcHandler::MakeJsonFromVmiConfig(const uuid &vmi_uuid,
                                           string &resp) const {
    const PortSubscribeTable::VmiEntry *vmi_entry =
        port_subscribe_table_->VmiToEntry(vmi_uuid);

    if (vmi_entry == NULL) {
        resp += "Interface not found for request. Retry";
        return false;
    }

    contrail_rapidjson::Document doc;
    doc.SetObject();
    contrail_rapidjson::Document::AllocatorType &a = doc.GetAllocator();

    string str1 = UuidToString(vmi_uuid);
    AddMember("id", str1.c_str(), &doc);

    string str2 = UuidToString(vmi_entry->vm_uuid_);
    AddMember("instance-id", str2.c_str(), &doc);

    string str5 = UuidToString(vmi_entry->vn_uuid_);
    AddMember("vn-id", str5.c_str(), &doc);

    AddMember("mac-address", vmi_entry->mac_.c_str(), &doc);
    doc.AddMember("sub-interface", (bool)vmi_entry->sub_interface_, a);
    doc.AddMember("vlan-id", (int)vmi_entry->vlan_tag_, a);

    contrail_rapidjson::StringBuffer buffer;
    contrail_rapidjson::PrettyWriter<contrail_rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    resp = buffer.GetString();
    return true;
}

bool PortIpcHandler::GetVmVnPort(const string &vm, string &info) const {
    uuid vm_uuid = StringToUuid(vm);
    if (vm_uuid == nil_uuid()) {
        return false;
    }

    uuid vmi_uuid = port_subscribe_table_->VmVnToVmi(vm_uuid);
    return MakeJsonFromVmi(vmi_uuid, info);
}

bool PortIpcHandler::MakeJsonFromVmi(const uuid &vmi_uuid, string &resp) const {
    InterfaceConstRef intf_ref = agent_->interface_table()->FindVmi(vmi_uuid);
    const VmInterface *vmi = static_cast<const VmInterface *>(intf_ref.get());
    if (vmi == NULL) {
        resp += "Interface not found for request. Retry";
        return false;
    }

    const VmEntry *vm = vmi->vm();
    if (vm == NULL) {
        resp += "VirtualMachine not found for request. Retry";
        return false;
    }

    const VnEntry *vn = vmi->vn();
    if (vn == NULL) {
        resp += "VirtualNetwork not found for request. Retry";
        return false;
    }

    const VnIpam *ipam = vn->GetIpam(vmi->primary_ip_addr());
    if (ipam == NULL) {
        resp += "Missing IPAM entry for request. Retry";
        return false;
    }

    contrail_rapidjson::Document doc;
    doc.SetObject();
    contrail_rapidjson::Document::AllocatorType &a = doc.GetAllocator();

    string str1 = UuidToString(vmi->GetUuid());
    AddMember("id", str1.c_str(), &doc);

    string str2 = UuidToString(vm->GetUuid());
    AddMember("instance-id", str2.c_str(), &doc);

    string str3 = vmi->primary_ip_addr().to_string();
    AddMember("ip-address", str3.c_str(), &doc);
    doc.AddMember("plen", ipam->plen, a);

    string str4 = vmi->primary_ip6_addr().to_string();
    AddMember("ip6-address", str4.c_str(), &doc);

    string str5 = UuidToString(vn->GetUuid());
    AddMember("vn-id", str5.c_str(), &doc);

    string str6 = UuidToString(vmi->vm_project_uuid());
    AddMember("vm-project-id", str6.c_str(), &doc);

    string str7 = vmi->vm_mac().ToString();
    AddMember("mac-address", str7.c_str(), &doc);
    AddMember("system-name", vmi->name().c_str(), &doc);
    doc.AddMember("rx-vlan-id", (int)vmi->rx_vlan_id(), a);
    doc.AddMember("tx-vlan-id", (int)vmi->tx_vlan_id(), a);
    string str8 = ipam->dns_server.to_v4().to_string();
    AddMember("dns-server", str8.c_str(), &doc);
    string str9 = ipam->default_gw.to_v4().to_string();
    AddMember("gateway", str9.c_str(), &doc);

    AddMember("author", agent_->program_name().c_str(), &doc);
    string now = duration_usecs_to_string(UTCTimestampUsec());
    AddMember("time", now.c_str(), &doc);

    contrail_rapidjson::StringBuffer buffer;
    contrail_rapidjson::PrettyWriter<contrail_rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    resp = buffer.GetString();
    return true;
}
