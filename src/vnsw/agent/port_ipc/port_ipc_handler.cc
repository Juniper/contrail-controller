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
#include "base/logging.h"
#include "base/task.h"
#include "base/string_util.h"
#include "db/db.h"
#include "db/db_entry.h"
#include "db/db_table.h"
#include "cmn/agent_cmn.h"
#include "init/agent_param.h"
#include "cfg/cfg_init.h"
#include "cfg/cfg_interface.h"
#include "oper/interface_common.h"
#include "port_ipc/port_ipc_handler.h"
#include "port_ipc/port_ipc_types.h"

using namespace std;
namespace fs = boost::filesystem;

const std::string PortIpcHandler::kPortsDir = "/var/lib/contrail/ports";

PortIpcHandler::AddPortParams::AddPortParams
    (const string &pid, const string &iid, const string &vid,
     const string &vm_pid, const string &vname, const string &ifname,
     const string &ip, const string &ip6, const string &mac, int ptype,
     int tx_vid, int rx_vid) :
     port_id(pid), instance_id(iid), vn_id(vid), vm_project_id(vm_pid),
     vm_name(vname), system_name(ifname), ip_address(ip), ip6_address(ip6),
     mac_address(mac), port_type(ptype), tx_vlan_id(tx_vid),
     rx_vlan_id(rx_vid) {
}

void PortIpcHandler::AddPortParams::Set
    (const string &pid, const string &iid, const string &vid,
     const string &vm_pid, const string &vname, const string &ifname,
     const string &ip, const string &ip6, const string &mac, int ptype,
     int tx_vid, int rx_vid) {
     port_id = pid;
     instance_id = iid;
     vn_id = vid;
     vm_project_id = vm_pid;
     vm_name= vname;
     system_name = ifname;
     ip_address = ip;
     ip6_address = ip6;
     mac_address = mac;
     port_type = ptype;
     tx_vlan_id = tx_vid;
     rx_vlan_id = rx_vid;
}

PortIpcHandler::PortIpcHandler(Agent *agent, const std::string &dir)
    : agent_(agent), ports_dir_(dir), version_(0),
      interface_stale_cleaner_(new InterfaceConfigStaleCleaner(agent)) {
    interface_stale_cleaner_->set_callback(
        boost::bind(&InterfaceConfigStaleCleaner::OnInterfaceConfigStaleTimeout,
                    interface_stale_cleaner_.get(), _1));
    fs::path ports_dir(ports_dir_);
    if (fs::exists(ports_dir)) {
        return;
    }
    if (!fs::create_directories(ports_dir)) {
        string err_msg = "Creating directory " + ports_dir_ + " failed";
        CONFIG_TRACE(PortInfo, err_msg.c_str());
    }

    uint32_t timeout_secs = agent->params()->stale_interface_cleanup_timeout();
    //Set timeout in milliseconds
    interface_stale_cleaner_->set_timeout(timeout_secs * 1000);
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
        /* Skip if filename is not in UUID format */
        if (!IsUUID(p.filename().string())) {
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
    
    AddPortFromJson(json, check_port, err_msg);
}

bool PortIpcHandler::HasAllMembers(const rapidjson::Value &d,
                                   std::string &member_err) const {
    if (!d.HasMember("id") || !d["id"].IsString()) {
        member_err = "id";
        return false;
    }
    if (!d.HasMember("instance-id") || !d["instance-id"].IsString()) {
        member_err = "instance-id";
        return false;
    }
    if (!d.HasMember("vn-id") || !d["vn-id"].IsString()) {
        member_err = "vn-id";
        return false;
    }
    if (!d.HasMember("vm-project-id") || !d["vm-project-id"].IsString()) {
        member_err = "vm-project-id";
        return false;
    }
    if (!d.HasMember("display-name") || !d["display-name"].IsString()) {
        member_err = "display-name";
        return false;
    }
    if (!d.HasMember("system-name") || !d["system-name"].IsString()) {
        member_err = "system-name";
        return false;
    }
    if (!d.HasMember("ip-address") || !d["ip-address"].IsString()) {
        member_err = "ip-address";
        return false;
    }
    if (!d.HasMember("ip6-address") || !d["ip6-address"].IsString()) {
        member_err = "ip6-address";
        return false;
    }
    if (!d.HasMember("mac-address") || !d["mac-address"].IsString()) {
        member_err = "mac-address";
        return false;
    }
    if (!d.HasMember("type") || !d["type"].IsInt()) {
        member_err = "type";
        return false;
    }
    if (!d.HasMember("rx-vlan-id") || !d["rx-vlan-id"].IsInt()) {
        member_err = "rx-vlan-id";
        return false;
    }
    if (!d.HasMember("tx-vlan-id") || !d["tx-vlan-id"].IsInt()) {
        member_err = "tx-vlan-id";
        return false;
    }
    return true;
}

bool PortIpcHandler::AddPortFromJson(const string &json, bool check_port,
                                     string &err_msg) {
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

    if (d.IsArray()) {
        /* When Json Array is passed, we do 'All or None'. We add all the
         * elements of the array or none are added. So we do validation in
         * first pass and addition in second pass */
        vector<PortIpcHandler::AddPortParams> req_list;
        for (size_t i = 0; i < d.Size(); i++) {
            const rapidjson::Value& elem = d[i];
            if (elem.IsObject()) {
                PortIpcHandler::AddPortParams req;
                if (!ValidateRequest(elem, json, check_port, err_msg, req)) {
                    return false;
                }
                req_list.push_back(req);
            } else {
                err_msg = "Json Array has invalid element ==> " + json;
                CONFIG_TRACE(PortInfo, err_msg.c_str());
                return false;
            }
        }
        vector<PortIpcHandler::AddPortParams>::iterator it = req_list.begin();
        while (it != req_list.end()) {
            if (!AddPort(*it, err_msg)) {
                /* Stop adding additional ports after first error is
                 * encountered */
                return false;
            }
            ++it;
        }
        return true;
    }

    PortIpcHandler::AddPortParams req;
    if (ValidateRequest(d, json, check_port, err_msg, req)) {
        return AddPort(req, err_msg);
    }
    return false;
}

bool PortIpcHandler::ValidateRequest(const rapidjson::Value &d,
                                     const string &json, bool check_port,
                                     string &err_msg,
                                     PortIpcHandler::AddPortParams &req) const {
    string member_err;
    if (!HasAllMembers(d, member_err)) {
        err_msg = "Json string does not have all required members, "
                         + member_err + " is missing ==> "
                         + json;
        CONFIG_TRACE(PortInfo, err_msg.c_str());
        return false;
    }

    req.Set(d["id"].GetString(),
        d["instance-id"].GetString(), d["vn-id"].GetString(),
        d["vm-project-id"].GetString(), d["display-name"].GetString(),
        d["system-name"].GetString(), d["ip-address"].GetString(),
        d["ip6-address"].GetString(), d["mac-address"].GetString(),
        d["type"].GetInt(), d["tx-vlan-id"].GetInt(),
        d["rx-vlan-id"].GetInt());
    return CanAdd(req, check_port, err_msg);
}

bool PortIpcHandler::CanAdd(PortIpcHandler::AddPortParams &r,
                            bool check_port, string &resp_str) const {
    bool err = false;

    r.port_uuid = StringToUuid(r.port_id);
    r.instance_uuid = StringToUuid(r.instance_id);

    /* VN UUID is optional for CfgIntNameSpacePort */
    r.vn_uuid = nil_uuid();
    if (r.vn_id.length() != 0) {
        r.vn_uuid = StringToUuid(r.vn_id);
    }
    /* VM Project UUID is optional for CfgIntNameSpacePort */
    r.vm_project_uuid = nil_uuid();
    if (r.vm_project_id.length() != 0) {
        r.vm_project_uuid = StringToUuid(r.vm_project_id);
    }
    boost::system::error_code ec, ec6;
    /* from_string returns default constructor IP (all zeroes) when there is
     * error in passed IP for both v4 and v6 */
    r.ip = Ip4Address::from_string(r.ip_address, ec);
    r.ip6 = Ip6Address::from_string(r.ip6_address, ec6);
    /* We permit port-add with all zeroes IP address but not with
     * invalid IP */
    if ((ec != 0) && (ec6 != 0)) {
        resp_str += "Neither Ipv4 nor IPv6 address is correct, ";
        err = true;
    }

    if (r.port_uuid == nil_uuid()) {
        resp_str += "invalid port uuid, ";
        err = true;
    }
    if (r.instance_uuid == nil_uuid()) {
        resp_str += "invalid instance uuid, ";
        err = true;
    }

    int16_t port_type = r.port_type;

    r.intf_type = CfgIntEntry::CfgIntVMPort;
    if (port_type == 1) {
        r.intf_type = CfgIntEntry::CfgIntNameSpacePort;
    } else if (port_type == 2) {
        r.intf_type = CfgIntEntry::CfgIntRemotePort;
    }
    if ((r.intf_type == CfgIntEntry::CfgIntVMPort) &&
        (r.vn_uuid == nil_uuid())) {
        resp_str += "invalid VN uuid, ";
        err = true;
    }
    if ((r.intf_type == CfgIntEntry::CfgIntVMPort) &&
        (r.vm_project_uuid == nil_uuid())) {
        resp_str += "invalid VM project uuid, ";
        err = true;
    }
    if (!ValidateMac(r.mac_address)) {
        resp_str += "Invalid MAC, Use xx:xx:xx:xx:xx:xx format";
        err = true;
    }

    // Sanity check. We should not have isolated_vlan_id set and vlan_id unset
    if ((r.rx_vlan_id != -1) && (r.tx_vlan_id == -1)) {
        resp_str += "Invalid request. RX (isolated) vlan set, "
                    "but TX vlan not set";
        err = true;
    }

    // No Validation required if port_type is VCenter
    if (r.intf_type != CfgIntEntry::CfgIntRemotePort) {
        // Verify that interface exists in OS
        if (check_port && !InterfaceExists(r.system_name)) {
            resp_str += "Interface does not exist in OS";
            err = true;
        }
    }

    if (err) {
        CONFIG_TRACE(PortInfo, resp_str.c_str());
        return false;
    }
    return true;
}

bool PortIpcHandler::AddPort(const PortIpcHandler::AddPortParams &r,
                             string &resp_str) {
    uint16_t tx_vlan_id = VmInterface::kInvalidVlanId;
    // Set vlan_id as tx_vlan_id
    if (r.tx_vlan_id != -1) {
        tx_vlan_id = r.tx_vlan_id;
    }

    // Backward compatibility. If only vlan_id is specified, set both
    // rx_vlan_id and tx_vlan_id to same value
    uint16_t rx_vlan_id = VmInterface::kInvalidVlanId;
    if (r.rx_vlan_id) {
        rx_vlan_id = r.rx_vlan_id;
    } else {
        rx_vlan_id = tx_vlan_id;
    }

    // If Writing to file fails return error
    if(!WriteJsonToFile(r)) {
        resp_str = "Writing of Json string to file failed for " + r.port_id;
        return false;
    }

    CfgIntTable *ctable = agent_->interface_config_table();
    assert(ctable);

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new CfgIntKey(r.port_uuid));
    CfgIntData *cfg_int_data = new CfgIntData();
    cfg_int_data->Init(r.instance_uuid, r.vn_uuid, r.vm_project_uuid,
                       r.system_name, r.ip, r.ip6, r.mac_address, r.vm_name,
                       tx_vlan_id, rx_vlan_id, r.intf_type, version_);
    req.data.reset(cfg_int_data);
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    ctable->Enqueue(&req);
    CONFIG_TRACE(AddPortEnqueue, "Add", r.port_id, r.instance_id, r.vn_id,
                 r.ip_address, r.system_name, r.mac_address, r.vm_name,
                 tx_vlan_id, rx_vlan_id, r.vm_project_id,
                 CfgIntEntry::CfgIntTypeToString(r.intf_type), r.ip6_address);
    return true;
}

string PortIpcHandler::GetJsonString(const PortIpcHandler::AddPortParams &r,
                                     bool meta_info) const {
    ostringstream out;
    out << "{\"id\": \"" << r.port_id << "\""
        << ", \"instance-id\": \"" << r.instance_id << "\""
        << ", \"ip-address\": \"" << r.ip_address << "\""
        << ", \"ip6-address\": \"" << r.ip6_address << "\""
        << ", \"vn-id\": \""<< r.vn_id << "\""
        << ", \"display-name\": \"" << r.vm_name << "\""
        << ", \"vm-project-id\": \"" << r.vm_project_id << "\""
        << ", \"mac-address\": \"" << r.mac_address << "\""
        << ", \"system-name\": \""<< r.system_name << "\""
        << ", \"type\": " << r.port_type
        << ", \"rx-vlan-id\": " << r.rx_vlan_id
        << ", \"tx-vlan-id\": " << r.tx_vlan_id;
    if (meta_info) {
        out << ", \"author\": \"" << agent_->program_name() << "\""
            << ", \"time \": \"" << duration_usecs_to_string(UTCTimestampUsec())
            << "\"";
    }
    out << "}";
    return out.str();
}

bool PortIpcHandler::WriteJsonToFile(const PortIpcHandler::AddPortParams &r)
    const {
    string filename = ports_dir_ + "/" + r.port_id;
    fs::path file_path(filename);

    /* Don't overwrite if the file already exists */
    if (fs::exists(file_path)) {
        return true;
    }

    ofstream fs(filename.c_str());
    if (fs.fail()) {
        return false;
    }
    string json_str = GetJsonString(r, true);
    fs << json_str;
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

bool PortIpcHandler::DeletePort(const string &uuid_str, string &err_str) {
    uuid port_uuid = StringToUuid(uuid_str);
    if (port_uuid == nil_uuid()) {
        CONFIG_TRACE(PortInfo, "Invalid port uuid");
        err_str = "Invalid port uuid " + uuid_str;
        return false;
    }

    DeletePortInternal(port_uuid, err_str);
    return true;
}

void PortIpcHandler::DeletePortInternal(const uuid &u, string &err_str) {
    CfgIntTable *ctable = agent_->interface_config_table();
    assert(ctable);

    DBRequest req;
    req.key.reset(new CfgIntKey(u));
    req.oper = DBRequest::DB_ENTRY_DELETE;
    ctable->Enqueue(&req);

    string uuid_str = UuidToString(u);
    CONFIG_TRACE(DeletePortEnqueue, "Delete", uuid_str, version_);

    string file = ports_dir_ + "/" + uuid_str;
    fs::path file_path(file);

    if (fs::exists(file_path) && fs::is_regular_file(file_path)) {
        if (remove(file.c_str())) {
            err_str = "Error deleting file " + file;
        }
    }
}

bool PortIpcHandler::IsUUID(const string &uuid_str) const {
    uuid port_uuid = StringToUuid(uuid_str);
    if (port_uuid == nil_uuid()) {
        return false;
    }
    return true;
}

bool PortIpcHandler::GetPortInfo(const string &uuid_str, string &info) const {
    CfgIntTable *ctable = agent_->interface_config_table();
    assert(ctable);

    DBRequestKey *key = static_cast<DBRequestKey *>
        (new CfgIntKey(StringToUuid(uuid_str)));
    CfgIntEntry *entry = static_cast<CfgIntEntry *>(ctable->Find(key));
    if (entry != NULL) {
        PortIpcHandler::AddPortParams req(UuidToString(entry->GetUuid()),
            UuidToString(entry->GetVmUuid()), UuidToString(entry->GetVnUuid()),
            UuidToString(entry->vm_project_uuid()), entry->vm_name(),
            entry->GetIfname(), entry->ip_addr().to_string(),
            entry->ip6_addr().to_string(), entry->GetMacAddr(),
            entry->port_type(), entry->tx_vlan_id(), entry->rx_vlan_id());
        info = GetJsonString(req, false);
        return true;
    }

    return false;
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
