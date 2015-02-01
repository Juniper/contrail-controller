/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <ctype.h>
#include <rapidjson/document.h>
#include <sstream>
#include <fstream>
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
    (string pid, string iid, string vid, string vm_pid, string vname,
     string ifname, string ip, string ip6, string mac, int ptype, int tx_vid,
     int rx_vid) :
     port_id(pid), instance_id(iid), vn_id(vid), vm_project_id(vm_pid),
     vm_name(vname), system_name(ifname), ip_address(ip), ip6_address(ip6),
     mac_address(mac), port_type(ptype), tx_vlan_id(tx_vid),
     rx_vlan_id(rx_vid) {
}

PortIpcHandler::PortIpcHandler(Agent *agent) 
    : agent_(agent), ports_dir_(kPortsDir) {
}

PortIpcHandler::PortIpcHandler(Agent *agent, const std::string &dir)
    : agent_(agent), ports_dir_(dir) {
}

PortIpcHandler::~PortIpcHandler() {
}

void PortIpcHandler::ReloadAllPorts() const {
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

        ProcessFile(p.string());
    }
}

void PortIpcHandler::ProcessFile(const string &file) const {
    ifstream f(file.c_str());
    if (!f.good()) {
        return;
    }
    ostringstream tmp;
    tmp<<f.rdbuf();
    string json = tmp.str();
    f.close();
    
    AddPortFromJson(json);
}

void PortIpcHandler::AddPortFromJson(const string &json) const {
    rapidjson::Document d;
    if (d.Parse<0>(const_cast<char *>(json.c_str())).HasParseError()) {
        return;
    }
    PortIpcHandler::AddPortParams req(d["id"].GetString(),
        d["instance-id"].GetString(), d["vn-id"].GetString(),
        d["vm-project-id"].GetString(), d["display-name"].GetString(),
        d["system-name"].GetString(), d["ip-address"].GetString(),
        d["ip6-address"].GetString(), d["mac-address"].GetString(),
        d["type"].GetInt(), d["vlan-id"].GetInt(),
        d["isolated-vlan-id"].GetInt());
    AddPort(req);
}

void PortIpcHandler::AddPort(const PortIpcHandler::AddPortParams &r) const {
    string resp_str;
    bool err = false;

    uuid port_uuid = StringToUuid(r.port_id);
    uuid instance_uuid = StringToUuid(r.instance_id);

    /* VN UUID is optional for CfgIntNameSpacePort */
    uuid vn_uuid = nil_uuid();
    if (r.vn_id.length() != 0) {
        vn_uuid = StringToUuid(r.vn_id);
    }
    /* VM Project UUID is optional for CfgIntNameSpacePort */
    uuid vm_project_uuid = nil_uuid();
    if (r.vm_project_id.length() != 0) {
        vm_project_uuid = StringToUuid(r.vm_project_id);
    }
    int16_t port_type = r.port_type;
    CfgIntEntry::CfgIntType intf_type;

    intf_type = CfgIntEntry::CfgIntVMPort;
    if (port_type) {
        intf_type = CfgIntEntry::CfgIntNameSpacePort;
    }
    boost::system::error_code ec, ec6;
    IpAddress ip(IpAddress::from_string(r.ip_address, ec));
    Ip6Address ip6 = Ip6Address::from_string(r.ip6_address, ec6);
    if ((ec != 0) && (ec6 != 0)) {
        resp_str += "Neither Ipv4 nor IPv6 address is correct, ";
        err = true;
    }

    if (port_uuid == nil_uuid()) {
        resp_str += "invalid port uuid, ";
        err = true;
    }
    if (instance_uuid == nil_uuid()) {
        resp_str += "invalid instance uuid, ";
        err = true;
    }
    if ((intf_type == CfgIntEntry::CfgIntVMPort) && (vn_uuid == nil_uuid())) {
        resp_str += "invalid VN uuid, ";
        err = true;
    }
    if ((intf_type == CfgIntEntry::CfgIntVMPort) &&
        (vm_project_uuid == nil_uuid())) {
        resp_str += "invalid VM project uuid, ";
        err = true;
    }
    if (!ValidateMac(r.mac_address)) {
        resp_str += "Invalid MAC, Use xx:xx:xx:xx:xx:xx format";
        err = true;
    }

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

    // Sanity check. We should not have isolated_vlan_id set and vlan_id unset
    if ((r.rx_vlan_id != -1) && (r.tx_vlan_id == -1)) {
        resp_str += "Invalid request. isolated (RX) vlan set, "
                    "but TX vlan not set";
        err = true;
    }

    if (err) {
        CONFIG_TRACE(PortInfo, resp_str.c_str());
        return;
    }

    CfgIntTable *ctable = agent_->interface_config_table();
    assert(ctable);

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new CfgIntKey(port_uuid));
    CfgIntData *cfg_int_data = new CfgIntData();
    cfg_int_data->Init(instance_uuid, vn_uuid, vm_project_uuid, r.system_name,
                       ip, ip6, r.mac_address, r.vm_name, tx_vlan_id,
                       rx_vlan_id, intf_type, 0);
    req.data.reset(cfg_int_data);
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    ctable->Enqueue(&req);
    CONFIG_TRACE(AddPortEnqueue, "Add", r.port_id, r.instance_id, r.vn_id,
                 r.ip_address, r.system_name, r.mac_address, r.vm_name,
                 tx_vlan_id, rx_vlan_id, r.vm_project_id,
                 CfgIntEntry::CfgIntTypeToString(intf_type), r.ip6_address);
    return;
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

void PortIpcHandler::DeletePort(const string &uuid_str) const {

    uuid port_uuid = StringToUuid(uuid_str);
    if (port_uuid == nil_uuid()) {
        CONFIG_TRACE(PortInfo, "Invalid port uuid");
        return;
    }

    CfgIntTable *ctable = agent_->interface_config_table();
    assert(ctable);

    DBRequest req;
    req.key.reset(new CfgIntKey(port_uuid));
    req.oper = DBRequest::DB_ENTRY_DELETE;
    ctable->Enqueue(&req);
    CONFIG_TRACE(DeletePortEnqueue, "Delete", uuid_str);
    return;
}

bool PortIpcHandler::IsUUID(const string &uuid_str) const {
    uuid port_uuid = StringToUuid(uuid_str);
    if (port_uuid == nil_uuid()) {
        return false;
    }
    return true;
}

string PortIpcHandler::GetPortInfo(const string &uuid_str) const {
    CfgIntTable *ctable = agent_->interface_config_table();
    assert(ctable);

    DBRequestKey *key = static_cast<DBRequestKey *>
        (new CfgIntKey(StringToUuid(uuid_str)));
    CfgIntEntry *entry = static_cast<CfgIntEntry *>(ctable->Find(key));
    if (entry != NULL) {
        ostringstream out;
        out << "{\"ip-address\": \"" << entry->ip_addr().to_string() << "\""
            << ", \"vlan-id\": " << entry->tx_vlan_id()
            << ", \"display-name\": \"" << entry->vm_name() << "\""
            << ", \"id\": \"" << UuidToString(entry->GetUuid()) << "\""
            << ", \"instance-id\": \"" << UuidToString(entry->GetVmUuid())
                << "\""
            << ", \"ip6-address\": \"" << entry->ip6_addr().to_string() << "\""
            << ", \"isolated-vlan-id\": " << entry->rx_vlan_id()
            << ", \"system-name\": \""<< entry->GetIfname() << "\""
            << ", \"vn-id\": \""<< UuidToString(entry->GetVnUuid()) << "\""
            << ", \"vm-project-id\": \"" <<
                UuidToString(entry->vm_project_uuid()) << "\""
            << ", \"type\": " << entry->port_type()
            << ", \"mac-address\": \"" << entry->GetMacAddr() << "\""
            << "}";
        return out.str();
    }

    return "{}";
}

