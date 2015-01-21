/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <sstream>
#include <fstream>
#include <boost/uuid/uuid.hpp>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <rapidjson/document.h>
#include <base/logging.h>
#include <base/task.h>
#include <db/db.h>
#include <db/db_entry.h>
#include <db/db_table.h>

#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <oper/interface_common.h>

#include <port_ipc/port_ipc_handler.h>

using namespace std;
namespace fs = boost::filesystem;

const std::string PortIpcHandler::kPortsDir = "/var/lib/contrail/ports";

PortIpcHandler::AddPortParams::AddPortParams(string pid, string iid, string vid
    , string vm_pid, std::string vname, string tname, string ip,
    string ip6, string mac, int ptype, int tx_vid, int rx_vid) :
    port_id(pid), instance_id(iid), vn_id(vid), vm_project_id(vm_pid),
    vm_name(vname), tap_name(tname), ip_address(ip), ip6_address(ip6),
    mac_address(mac), port_type(ptype), tx_vlan_id(tx_vid), rx_vlan_id(rx_vid) {
}

PortIpcHandler::PortIpcHandler() {
}

PortIpcHandler::~PortIpcHandler() {
}

void PortIpcHandler::ReloadAllPorts() const {
    fs::path ports_dir(kPortsDir);
    fs::directory_iterator end_iter;

    if (!fs::exists(ports_dir) || !fs::is_directory(ports_dir)) {
        return;
    }

    fs::directory_iterator it(ports_dir);
    BOOST_FOREACH(fs::path const &p, std::make_pair(it, end_iter)) {
        if (!fs::is_regular_file(p)) {
            continue;
        }
        /* File name is UUID. Make sure its length is equivalent to UUID
         * length */
        if (p.filename().string().length() != 36) {
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

    rapidjson::Document d;
    if (d.Parse<0>(const_cast<char *>(json.c_str())).HasParseError()) {
        return;
    }
    PortIpcHandler::AddPortParams req(d["id"].GetString(),
        d["instance-id"].GetString(), d["vn-id"].GetString(),
        d["vm-project-id"].GetString(), d["display-name"].GetString(),
        d["tap-name"].GetString(), d["ip-address"].GetString(),
        d["ip6-address"].GetString(), d["mac-address"].GetString(),
        d["type"].GetInt(), d["vlan-id"].GetInt(),
        d["isolated-vlan-id"].GetInt());
    AddPort(req);
}

string PortIpcHandler::AddPort(const PortIpcHandler::AddPortParams &r) const {
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
        resp_str += "Port uuid is not correct, ";
        err = true;
    }
    if (instance_uuid == nil_uuid()) {
        resp_str += "Instance uuid is not correct, ";
        err = true;
    }
    if ((intf_type == CfgIntEntry::CfgIntVMPort) && (vn_uuid == nil_uuid())) {
        resp_str += "Vn uuid is not correct, ";
        err = true;
    }
    if ((intf_type == CfgIntEntry::CfgIntVMPort) &&
        (vm_project_uuid == nil_uuid())) {
        resp_str += "Vm project uuid is not correct, ";
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
        CFG_TRACE(PortInfo, resp_str.c_str());
        return resp_str;
    }

    CfgIntTable *ctable = Agent::GetInstance()->interface_config_table();
    assert(ctable);

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new CfgIntKey(port_uuid));
    CfgIntData *cfg_int_data = new CfgIntData();
    cfg_int_data->Init(instance_uuid, vn_uuid, vm_project_uuid, r.tap_name, ip,
                       ip6, r.mac_address, r.vm_name, tx_vlan_id, rx_vlan_id,
                       intf_type, 0);
    req.data.reset(cfg_int_data);
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    ctable->Enqueue(&req);
    resp_str.assign("Success");
    CFG_TRACE(AddPortEnqueue, "Add", r.port_id, r.instance_id, r.vn_id,
              r.ip_address, r.tap_name, r.mac_address, r.vm_name, tx_vlan_id,
              rx_vlan_id, r.vm_project_id,
              CfgIntEntry::CfgIntTypeToString(intf_type), r.ip6_address);
    return resp_str;
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

void AddPortReq::HandleRequest() const {
    string resp_str;

    PortResp *resp = new PortResp();
    resp->set_context(context());

    PortIpcHandler::AddPortParams r(get_port_uuid(), get_instance_uuid(),
        get_vn_uuid(), get_vm_project_uuid(), get_vm_name(), get_tap_name(),
        get_ip_address(), get_ip6_address(), get_mac_address(), get_port_type(),
        get_tx_vlan_id(), get_rx_vlan_id());
    PortIpcHandler pih;
    resp_str = pih.AddPort(r);

    resp->set_resp(resp_str);
    resp->Response();
    return;
}

void DeletePortReq::HandleRequest() const {
    PortResp *resp = new PortResp();
    resp->set_context(context());

    uuid port_uuid = StringToUuid(get_port_uuid());
    if (port_uuid == nil_uuid()) {
        resp->set_resp(std::string("Port uuid is not correct."));
        resp->Response();
        CFG_TRACE(PortInfo, "Port uuid is not correct");
        return;
    }

    CfgIntTable *ctable = Agent::GetInstance()->interface_config_table();
    assert(ctable);

    DBRequest req;
    req.key.reset(new CfgIntKey(port_uuid));
    req.oper = DBRequest::DB_ENTRY_DELETE;
    ctable->Enqueue(&req);
    resp->set_resp(std::string("Success"));
    resp->Response();
    CFG_TRACE(DeletePortEnqueue, "Delete", get_port_uuid());
    return;
}
