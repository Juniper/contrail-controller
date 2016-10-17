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
#include "oper/interface_common.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "cfg/cfg_init.h"
#include "cfg/cfg_interface.h"
#include "cfg/cfg_interface_listener.h"
#include "port_ipc/port_ipc_handler.h"
#include "port_ipc/port_ipc_types.h"

using namespace std;
namespace fs = boost::filesystem;

const std::string PortIpcHandler::kPortsDir = "/var/lib/contrail/ports";

/////////////////////////////////////////////////////////////////////////////
// Utility methods
/////////////////////////////////////////////////////////////////////////////
static bool GetStringMember(const rapidjson::Value &d, const char *member,
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

static bool GetUint32Member(const rapidjson::Value &d, const char *member,
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

static bool GetUuidMember(const rapidjson::Value &d, const char *member,
                          boost::uuids::uuid *u, std::string *err) {
    if (!d.HasMember(member) || !d[member].IsString()) {
        if (err) {
            *err += "Invalid or missing data for <" + string(member) + ">";
        }
        return false;
    }

    *u = StringToUuid(d[member].GetString());
    if (*u == nil_uuid()) {
        if (err) {
            *err += "Invalid data for <" + string(member) + ">";
        }
        return false;
    }
    return true;
}

static bool IsVmiUuidRequest(const rapidjson::Value &v) {
    return v.HasMember("id");
}

static bool IsUUID(const string &uuid_str) {
    uuid port_uuid = StringToUuid(uuid_str);
    if (port_uuid == nil_uuid()) {
        return false;
    }
    return true;
}

static string LabelToFileName(const string &vm_label, const string &nw_label) {
    if (nw_label.empty())
        return "label-" + vm_label;
    else
        return "label-" + vm_label + "-" + nw_label;
}

static bool IsValidFile(const string &fname) {
    if (IsUUID(fname)) {
        return true;
    }

    if (fname.find("label-") == 0) {
        return true;
    }

    return false;
}

/////////////////////////////////////////////////////////////////////////////
// PortIpcHandler methods
/////////////////////////////////////////////////////////////////////////////
PortIpcHandler::PortIpcHandler(Agent *agent, const std::string &dir)
    : agent_(agent), ports_dir_(dir), version_(0),
      interface_stale_cleaner_(new InterfaceConfigStaleCleaner(agent)) {
    interface_stale_cleaner_->set_callback(
        boost::bind(&InterfaceConfigStaleCleaner::OnInterfaceConfigStaleTimeout,
                    interface_stale_cleaner_.get(), _1));
    uint32_t timeout_secs = agent->params()->stale_interface_cleanup_timeout();
    //Set timeout in milliseconds
    interface_stale_cleaner_->set_timeout(timeout_secs * 1000);

    fs::path ports_dir(ports_dir_);
    if (fs::exists(ports_dir)) {
        return;
    }
    if (!fs::create_directories(ports_dir)) {
        string err_msg = "Creating directory " + ports_dir_ + " failed";
        CONFIG_TRACE(PortInfo, err_msg.c_str());
    }
}

PortIpcHandler::~PortIpcHandler() {
}

void PortIpcHandler::ReloadAllPorts(bool check_port) {
    fs::path ports_dir(ports_dir_);
    fs::directory_iterator end_iter;

    if (!fs::exists(ports_dir) || !fs::is_directory(ports_dir)) {
        return;
    }

    fs::directory_iterator it(ports_dir);
    BOOST_FOREACH(fs::path const &p, std::make_pair(it, end_iter)) {
        if (!fs::is_regular_file(p)) {
            continue;
        }

        // Skip if filename is not valid
        if (IsValidFile(p.filename().string()) == false) {
            continue;
        }

        ProcessFile(p.string(), check_port);
    }
}

void PortIpcHandler::ProcessFile(const string &file, bool check_port) {
    string err_msg;
    ifstream f(file.c_str());
    if (!f.good()) {
        return;
    }
    ostringstream tmp;
    tmp<<f.rdbuf();
    string json = tmp.str();
    f.close();
    
    AddPortFromJson(json, check_port, err_msg, false);
}

bool PortIpcHandler::AddPortFromJson(const string &json, bool check_port,
                                     string &err_msg, bool write_file) {
    rapidjson::Document d;
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

    std::vector<DBRequest> req_list;
    if (d.IsArray()) {
        /* When Json Array is passed, we do 'All or None'. We add all the
         * elements of the array or none are added. So we do validation in
         * first pass and addition in second pass */
        for (size_t i = 0; i < d.Size(); i++) {
            const rapidjson::Value& elem = d[i];
            if (elem.IsObject()) {
                DBRequest req;
                if (MakeAddVmiUuidRequest(elem, json, check_port, err_msg,
                                          &req) == false) {
                    CONFIG_TRACE(PortInfo, err_msg.c_str());
                    return false;
                }
            } else {
                err_msg = "Json Array has invalid element ==> " + json;
                CONFIG_TRACE(PortInfo, err_msg.c_str());
                return false;
            }
        }

        for (size_t i = 0; i < req_list.size(); i++) {
            AddVmiUuidEntry(&req_list[i], d, write_file, err_msg);
        }

        return true;
    }

    if (IsVmiUuidRequest(d)) {
        DBRequest req;
        if (MakeAddVmiUuidRequest(d, json, check_port, err_msg, &req)
            == false) {
            CONFIG_TRACE(PortInfo, err_msg.c_str());
            return false;
        }

        AddVmiUuidEntry(&req, d, write_file, err_msg);
    } else {
        DBRequest req;
        if (MakeAddVmiLabelRequest(d, json, check_port, err_msg, &req)
            == false) {
            CONFIG_TRACE(PortInfo, err_msg.c_str());
            return false;
        }

        AddVmiLabelEntry(&req, d, write_file, err_msg);
    }
    return true;
}

bool PortIpcHandler::MakeAddVmiUuidRequest(const rapidjson::Value &d,
                                           const std::string &json,
                                           bool check_port,
                                           std::string &err_msg,
                                           DBRequest *req) const {
    boost::uuids::uuid port_uuid;
    if (GetUuidMember(d, "id", &port_uuid, &err_msg) == false) {
        return false;
    }

    boost::uuids::uuid instance_uuid;
    if (GetUuidMember(d, "instance-id", &instance_uuid, &err_msg) == false) {
        return false;
    }

    boost::uuids::uuid vn_uuid;
    if (GetUuidMember(d, "vn-id", &vn_uuid, &err_msg) == false) {
        return false;
    }

    boost::uuids::uuid project_uuid;
    if (GetUuidMember(d, "vm-project-id", &project_uuid, &err_msg) == false) {
        return false;
    }

    string vm_name;
    if (GetStringMember(d, "display-name", &vm_name, &err_msg) == false) {
        return false;
    }

    string ip4_str;
    if (GetStringMember(d, "ip-address", &ip4_str, &err_msg) == false) {
        return false;
    }
    boost::system::error_code ec;
    Ip4Address ip4 = Ip4Address::from_string(ip4_str, ec);

    string ip6_str;
    if (GetStringMember(d, "ip6-address", &ip6_str, &err_msg) == false) {
        return false;
    }
    boost::system::error_code ec6;
    Ip6Address ip6 = Ip6Address::from_string(ip6_str, ec6);

    // from_string returns default constructor IP (all zeroes) when there is
    // error in passed IP for both v4 and v6
    // We permit port-add with all zeroes IP address but not with invalid IP
    if ((ec != 0) && (ec6 != 0)) {
        err_msg = "Neither Ipv4 nor IPv6 address is correct, ";
        return false;
    }

    uint32_t val;
    if (GetUint32Member(d, "type", &val, &err_msg) == false) {
        return false;
    }
    InterfaceConfigVmiEntry::VmiType vmi_type =
        InterfaceConfigVmiEntry::VM_INTERFACE;
    if (val == 1) {
        vmi_type = InterfaceConfigVmiEntry::NAMESPACE_PORT;
    } else if (val == 2) {
        vmi_type = InterfaceConfigVmiEntry::REMOTE_PORT;
    }

    if (vmi_type == InterfaceConfigVmiEntry::VM_INTERFACE) {
        if (vn_uuid == nil_uuid()) {
            err_msg = "Invalid VN uuid";
            return false;
        }

        if (project_uuid == nil_uuid()) {
            err_msg = "Invalid VM project uuid";
            return false;
        }

    }

    string ifname;
    if (GetStringMember(d, "system-name", &ifname, &err_msg) == false) {
        return false;
    }
    // Verify that interface exists in OS
    if (vmi_type != InterfaceConfigVmiEntry::REMOTE_PORT) {
        if (check_port && !InterfaceExists(ifname)) {
            err_msg = "Interface does not exist in OS";
            return false;
        }
    }

    string mac;
    if (GetStringMember(d, "mac-address", &mac, &err_msg) == false) {
        return false;
    }
    if (ValidateMac(mac) == false) {
        err_msg = "Invalid MAC, Use xx:xx:xx:xx:xx:xx format";
        return false;
    }

    // Sanity check. We should not have isolated_vlan_id set and vlan_id unset
    uint32_t rx_vlan_id = VmInterface::kInvalidVlanId;
    if (GetUint32Member(d, "rx-vlan-id", &rx_vlan_id, &err_msg) == false) {
        return false;
    }

    uint32_t tx_vlan_id = VmInterface::kInvalidVlanId;
    if (GetUint32Member(d, "tx-vlan-id", &tx_vlan_id, &err_msg) == false) {
        return false;
    }

    if ((rx_vlan_id != VmInterface::kInvalidVlanId) &&
        (tx_vlan_id == VmInterface::kInvalidVlanId-1)) {
        err_msg += "Invalid request. RX (isolated) vlan set, "
            "but TX vlan not set";
        return false;
    }

    req->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req->key.reset(new InterfaceConfigVmiKey(port_uuid));
    req->data.reset
        (new InterfaceConfigVmiData(vmi_type, ifname, instance_uuid, vn_uuid,
                                    project_uuid, ip4, ip6, mac, vm_name,
                                    tx_vlan_id, rx_vlan_id, version_));
    CONFIG_TRACE(AddPortEnqueue, "Add", UuidToString(port_uuid),
                 UuidToString(instance_uuid), UuidToString(vn_uuid),
                 ip4.to_string(), ifname, mac, vm_name, tx_vlan_id, rx_vlan_id,
                 UuidToString(project_uuid),
                 InterfaceConfigVmiEntry::VmiTypeToString(vmi_type),
                 ip6.to_string(), version_);
    return true;
}

bool PortIpcHandler::AddVmiUuidEntry(DBRequest *req, rapidjson::Value &d,
                                     bool write_file, string &err_msg) const {
    InterfaceConfigVmiData *data = static_cast<InterfaceConfigVmiData *>
        (req->data.get());
    InterfaceConfigVmiKey *key = static_cast<InterfaceConfigVmiKey *>
        (req->key.get());

    // If Writing to file fails return error
    if(write_file && WriteJsonToFile(d, UuidToString(key->vmi_uuid()))
       == false) {
        err_msg += "Writing of Json string to file failed for " +
            UuidToString(key->vmi_uuid());
        CONFIG_TRACE(PortInfo, err_msg.c_str());
        return false;
    }

    InterfaceConfigTable *table = agent_->interface_config_table();
    table->Enqueue(req);

    CONFIG_TRACE(AddPortEnqueue, "Add", UuidToString(key->vmi_uuid()),
                 UuidToString(data->vm_uuid()), UuidToString(data->vn_uuid()),
                 data->ip4_addr().to_string(), data->tap_name(),
                 data->mac_addr(), data->vm_name(), data->tx_vlan_id(),
                 data->rx_vlan_id(), UuidToString(data->project_uuid()),
                 InterfaceConfigVmiEntry::VmiTypeToString(data->vmi_type()),
                 data->ip6_addr().to_string(), data->version());
    return true;
}

static void AddJsonString(rapidjson::Document &doc, rapidjson::Value &v,
                          const char *member) {
    if (v.HasMember(member) && v[member].IsString()) {
        doc.AddMember(member, v[member].GetString(), doc.GetAllocator());
    }
}

static void AddJsonInt(rapidjson::Document &doc, rapidjson::Value &v,
                       const char *member) {
    if (v.HasMember(member) && v[member].IsInt()) {
        doc.AddMember(member, v[member].GetInt(), doc.GetAllocator());
    }
}

bool PortIpcHandler::WriteJsonToFile(rapidjson::Value &v,
                                     const string &fname) const {
    string filename = ports_dir_ + "/" + fname;
    fs::path file_path(filename);

    // Don't overwrite if the file already exists
    if (fs::exists(file_path)) {
        return true;
    }

    // Add author and time fields
    rapidjson::Document new_doc;
    new_doc.SetObject();
    rapidjson::Document::AllocatorType &a = new_doc.GetAllocator();

    new_doc.AddMember("author", agent_->program_name().c_str(), a);
    string now = duration_usecs_to_string(UTCTimestampUsec()).c_str();
    new_doc.AddMember("time", now.c_str(), a);

    AddJsonString(new_doc, v, "id");
    AddJsonString(new_doc, v, "instance-id");
    AddJsonString(new_doc, v, "vn-id");
    AddJsonString(new_doc, v, "vm-project-id");
    AddJsonString(new_doc, v, "display-name");
    AddJsonString(new_doc, v, "system-name");
    AddJsonString(new_doc, v, "ip-address");
    AddJsonString(new_doc, v, "ip6-address");
    AddJsonString(new_doc, v, "mac-address");
    AddJsonInt(new_doc, v, "type");
    AddJsonInt(new_doc, v, "rx-vlan-id");
    AddJsonInt(new_doc, v, "tx-vlan-id");
    AddJsonString(new_doc, v, "vm-label");
    AddJsonString(new_doc, v, "network-label");
    AddJsonString(new_doc, v, "ifname");
    AddJsonString(new_doc, v, "vm-ifname");
    AddJsonString(new_doc, v, "namespace");

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter< rapidjson::StringBuffer > writer(buffer);
    new_doc.Accept(writer);

    ofstream fs(filename.c_str());
    if (fs.fail()) {
        return false;
    }

    fs << buffer.GetString();
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
    return true;
}

bool PortIpcHandler::DeletePort(const string &body, const string &url,
                                string &err_msg) const {
    if (IsUUID(url)) {
        return DeleteVmiUuidEntry(body, url, err_msg);
    }

    return DeleteVmiLabelEntry(body, url, err_msg);
}

bool PortIpcHandler::DeleteVmiUuidEntry(const string &body, const string &url,
                                        string &err_msg) const {
    boost::uuids::uuid u = StringToUuid(url);
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new InterfaceConfigVmiKey(u));
    agent_->interface_config_table()->Enqueue(&req);

    string uuid_str = UuidToString(u);
    CONFIG_TRACE(DeletePortEnqueue, "Delete", uuid_str, version_);

    string file = ports_dir_ + "/" + uuid_str;
    fs::path file_path(file);

    if (fs::exists(file_path) && fs::is_regular_file(file_path)) {
        if (remove(file.c_str())) {
            err_msg = "Error deleting file " + file;
        }
        return false;
    }
    return true;
}

void PortIpcHandler::MakeVmiUuidJson(const InterfaceConfigVmiEntry *vmi,
                                     string &info) const {
    rapidjson::Document doc;
    doc.SetObject();
    rapidjson::Document::AllocatorType &a = doc.GetAllocator();

    string str1 = UuidToString(vmi->vmi_uuid());
    doc.AddMember("id", str1.c_str(), a);
    string str2 = UuidToString(vmi->vm_uuid());
    doc.AddMember("instance-id", str2.c_str(), a);
    string str3 = vmi->ip4_addr().to_string();
    doc.AddMember("ip-address", str3.c_str(), a);
    string str4 = vmi->ip6_addr().to_string();
    doc.AddMember("ip6-address", str4.c_str(), a);
    string str5 = UuidToString(vmi->vn_uuid());
    doc.AddMember("vn-id", str5.c_str(), a);
    string str6 = UuidToString(vmi->project_uuid());
    doc.AddMember("vm-project-id", str6.c_str(), a);
    doc.AddMember("mac-address", vmi->mac_addr().c_str(), a);
    doc.AddMember("system-name", vmi->tap_name().c_str(), a);
    doc.AddMember("type", (int)vmi->vmi_type(), a);
    doc.AddMember("rx-vlan-id", (int)vmi->rx_vlan_id(), a);
    doc.AddMember("tx-vlan-id", (int)vmi->tx_vlan_id(), a);
    doc.AddMember("author", agent_->program_name().c_str(), a);
    string now = duration_usecs_to_string(UTCTimestampUsec());
    doc.AddMember("time", now.c_str(), a);

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    info = buffer.GetString();
    return;
}

bool PortIpcHandler::GetVmiUuidInfo(const string &body, const string &uuid_str,
                                    string &info) const {
    DBRequestKey *key = static_cast<DBRequestKey *>
        (new InterfaceConfigVmiKey(StringToUuid(uuid_str)));
    InterfaceConfigTable *table = agent_->interface_config_table();
    InterfaceConfigVmiEntry *vmi = dynamic_cast<InterfaceConfigVmiEntry *>
        (table->FindActiveEntry(key));
    if (vmi == NULL) {
        return false;
    }

    MakeVmiUuidJson(vmi, info);
    return true;
}

bool PortIpcHandler::GetPortInfo(const string &body, const string &uuid_str,
                                 string &info) const {
    if (IsUUID(uuid_str)) {
        return GetVmiUuidInfo(body, uuid_str, info);
    }

    return GetVmiLabelInfo(body, uuid_str, info);
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

void PortIpcHandler::Shutdown() {
}

/////////////////////////////////////////////////////////////////////////////
// Label JSON handlers
/////////////////////////////////////////////////////////////////////////////
bool PortIpcHandler::MakeAddVmiLabelRequest(const rapidjson::Value &d,
                                           const std::string &json,
                                           bool check_port,
                                           std::string &err_msg,
                                           DBRequest *req) const {
    string ifname;
    if (GetStringMember(d, "ifname", &ifname, &err_msg) == false) {
        return false;
    }

    string vm_label;
    if (GetStringMember(d, "vm-label", &vm_label, &err_msg) == false) {
        return false;
    }

    string nw_label;
    if (GetStringMember(d, "network-label", &nw_label, &err_msg) == false) {
        return false;
    }

    string vm_ifname;
    if (GetStringMember(d, "vm-ifname", &vm_ifname, &err_msg) == false) {
        return false;
    }

    string nw_namespace;
    if (GetStringMember(d, "namespace", &nw_namespace, &err_msg) == false) {
        return false;
    }

    req->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req->key.reset(new InterfaceConfigLabelKey(vm_label, nw_label));
    req->data.reset(new InterfaceConfigLabelData(ifname, nw_namespace,
                                                 vm_ifname));
    return true;
}

bool PortIpcHandler::AddVmiLabelEntry(DBRequest *req,
                                      rapidjson::Value &d,
                                      bool write_file, string &err_msg) const {
    InterfaceConfigLabelData *data = static_cast<InterfaceConfigLabelData *>
        (req->data.get());
    InterfaceConfigLabelKey *key = static_cast<InterfaceConfigLabelKey *>
        (req->key.get());

    // If Writing to file fails return error
    string fname = LabelToFileName(key->vm_label(), key->network_label());
    if(write_file && WriteJsonToFile(d, fname) == false) {
        err_msg += "Writing of Json string to file failed for " + fname;
        CONFIG_TRACE(PortInfo, err_msg.c_str());
        return false;
    }

    agent_->interface_config_table()->Enqueue(req);
    CONFIG_TRACE(AddPortLabelEnqueue, "Add", key->vm_label(),
                 key->network_label(), data->tap_name(),
                 data->vm_ifname(), data->vm_namespace());
    return true;
}

bool PortIpcHandler::DeleteVmiLabelEntry(const string &body, const string &url,
                                         string &err_msg) const {
    rapidjson::Document d;
    string vm_label = url;
    string nw_label = "";
    if (body.empty() == false) {
        if (d.Parse<0>(const_cast<char *>(body.c_str())).HasParseError()) {
            err_msg = "Invalid Json string ==> " + body;
            CONFIG_TRACE(PortInfo, err_msg);
            return false;
        }

        if (d.IsObject() == false) {
            err_msg = "Unexpected Json string ==> " + body;
            CONFIG_TRACE(PortInfo, err_msg);
            return false;
        }

        if (GetStringMember(d, "vm-label", &vm_label, &err_msg) == false) {
            return false;
        }

        if (GetStringMember(d, "network-label", &nw_label, &err_msg) == false) {
            return false;
        }
    }

    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new InterfaceConfigLabelKey(vm_label, nw_label));
    agent_->interface_config_table()->Enqueue(&req);
    CONFIG_TRACE(DelPortLabelEnqueue, "Delete", vm_label, nw_label);

    string fname = LabelToFileName(vm_label, nw_label);
    string file = ports_dir_ + "/" + fname;
    fs::path file_path(file);

    if (fs::exists(file_path) && fs::is_regular_file(file_path)) {
        if (remove(file.c_str())) {
            err_msg = "Error deleting file " + file;
            CONFIG_TRACE(PortInfo, err_msg);
            return false;
        }
    }
    return true;
}

void MakeVmiLabelJson(Agent *agent, const VmInterface *vmi, const VmEntry *vm,
                      const VnEntry *vn, const VnIpam *ipam,
                      const std::string &vm_label, const std::string nw_label,
                      string &info) {
    rapidjson::Document doc;
    doc.SetObject();
    rapidjson::Document::AllocatorType &a = doc.GetAllocator();

    string str1 = UuidToString(vmi->GetUuid());
    doc.AddMember("id", str1.c_str(), a);

    string str2 = UuidToString(vm->GetUuid());
    doc.AddMember("instance-id", str2.c_str(), a);

    string str3 = vmi->primary_ip_addr().to_string();
    doc.AddMember("ip-address", str3.c_str(), a);
    doc.AddMember("plen", ipam->plen, a);

    string str4 = vmi->primary_ip6_addr().to_string();
    doc.AddMember("ip6-address", str4.c_str(), a);

    string str5 = UuidToString(vn->GetUuid());
    doc.AddMember("vn-id", str5.c_str(), a);

    string str6 = UuidToString(vmi->vm_project_uuid());
    doc.AddMember("vm-project-id", str6.c_str(), a);

    string str7 = vmi->vm_mac().ToString();
    doc.AddMember("mac-address", str7.c_str(), a);
    doc.AddMember("system-name", vmi->name().c_str(), a);
    doc.AddMember("rx-vlan-id", (int)vmi->rx_vlan_id(), a);
    doc.AddMember("tx-vlan-id", (int)vmi->tx_vlan_id(), a);
    doc.AddMember("vm-label", vm_label.c_str(), a);
    doc.AddMember("network-label", nw_label.c_str(), a);
    string str8 = ipam->dns_server.to_v4().to_string();
    doc.AddMember("dns-server", str8.c_str(), a);
    string str9 = ipam->default_gw.to_v4().to_string();
    doc.AddMember("gateway", str9.c_str(), a);

    doc.AddMember("author", agent->program_name().c_str(), a);
    string now = duration_usecs_to_string(UTCTimestampUsec());
    doc.AddMember("time", now.c_str(), a);

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    info = buffer.GetString();
    return;
}

bool PortIpcHandler::GetVmiLabelInfo(const string &body,
                                     const string &url,
                                     string &resp) const {
    rapidjson::Document d;
    string vm_label = url;
    string nw_label = "";
    if (body.empty() == false) {
        if (d.Parse<0>(const_cast<char *>(body.c_str())).HasParseError()) {
            resp = "Invalid Json string ==> " + body;
            CONFIG_TRACE(PortInfo, resp);
            return false;
        }

        if (d.IsObject() == false) {
            resp = "Unexpected Json string ==> " + body;
            CONFIG_TRACE(PortInfo, resp);
            return false;
        }

        GetStringMember(d, "vm-label", &vm_label, NULL);
        GetStringMember(d, "network-label", &nw_label, NULL);
    }

    InterfaceCfgClient *client = agent_->cfg()->cfg_interface_client();
    const VmInterface *vmi = client->LabelToVmi(vm_label, nw_label);
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

    MakeVmiLabelJson(agent_, vmi, vm, vn, ipam, vm_label, nw_label, resp);
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// Gateway handlers
/////////////////////////////////////////////////////////////////////////////
bool PortIpcHandler::BuildGatewayArrayElement
(const rapidjson::Value &d, VirtualGatewayConfig::Subnet *entry) const {
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
(const rapidjson::Value &d, VirtualGatewayConfig::SubnetList *list) const {
    for (size_t i = 0; i < d.Size(); i++) {
        const rapidjson::Value& elem = d[i];
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

bool PortIpcHandler::HasAllGatewayFields(const rapidjson::Value &d,
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

bool PortIpcHandler::BuildGateway(const rapidjson::Value &d,
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
    rapidjson::Document d;
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
        const rapidjson::Value& elem = d[i];
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
    rapidjson::Document d;
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
        const rapidjson::Value& elem = d[i];
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
