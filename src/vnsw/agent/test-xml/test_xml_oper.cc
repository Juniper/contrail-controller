/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include <base/os.h>
#include <iostream>
#include <string>
#include <fstream>
#include <pugixml/pugixml.hpp>
#include <boost/uuid/uuid.hpp>

#include <test/test_cmn_util.h>
#include <pkt/test/test_pkt_util.h>
#include "test_xml.h"
#include "test_xml_oper.h"
#include "test_xml_validate.h"
#include "test_xml_packet.h"

using namespace std;
using namespace pugi;
using namespace boost::uuids;
using namespace AgentUtXmlUtils;

AgentUtXmlValidationNode *CreateValidateNode(const string &type,
                                             const string &name, const uuid &id,
                                             const xml_node &node) {
    if (type == "virtual-network" || type == "vn")
        return new AgentUtXmlVnValidate(name, id, node);
    if (type == "virtual-machine" || type == "vm")
        return new AgentUtXmlVmValidate(name, id, node);
    if (type == "vxlan")
        return new AgentUtXmlVxlanValidate(name, id, node);
    if (type == "virtual-machine-interface" || type == "vm-interface"
        || type == "vmi")
        return new AgentUtXmlVmInterfaceValidate(name, id, node);
    if (type == "ethernet-interface" || type == "eth-port")
        return new AgentUtXmlEthInterfaceValidate(name, id, node);
    if (type == "flow")
        return new AgentUtXmlFlowValidate(name, node);
    if (type == "routing-instance" || type == "vrf")
        return new AgentUtXmlVrfValidate(name, node);
    if (type == "access-control-list" || type == "acl")
        return new AgentUtXmlAclValidate(name, node);
    if (type == "pkt-parse")
        return new AgentUtXmlPktParseValidate(name, node);
    if (type == "fdb")
        return new AgentUtXmlL2RouteValidate(name, node);
}

AgentUtXmlNode *CreateNode(const string &type, const string &name,
                           const uuid &id, const xml_node &node,
                           AgentUtXmlTestCase *test_case) {
    if (type == "global-vrouter-config")
        return new AgentUtXmlGlobalVrouter(name, id, node, test_case);
    if (type == "virtual-network" || type == "vn")
        return new AgentUtXmlVn(name, id, node, test_case);
    if (type == "virtual-machine" || type == "vm")
        return new AgentUtXmlVm(name, id, node, test_case);
    if (type == "virtual-machine-interface" || type == "vm-interface"
        || type == "vmi")
        return new AgentUtXmlVmInterface(name, id, node, test_case);
    if (type == "ethernet-interface" || type == "eth-port")
        return new AgentUtXmlEthInterface(name, id, node, test_case);
    if (type == "routing-instance" || type == "vrf")
        return new AgentUtXmlVrf(name, id, node, test_case);
    if (type == "access-control-list" || type == "acl")
        return new AgentUtXmlAcl(name, id, node, test_case);
    if (type == "virtual-machine-interface-routing-instance" ||
        type == "vmi-vrf")
        return new AgentUtXmlVmiVrf(name, id, node, test_case);
    if (type == "fdb" || type == "l2-route")
        return new AgentUtXmlL2Route(name, id, node, test_case);
}

void AgentUtXmlOperInit(AgentUtXmlTest *test) {
    test->AddConfigEntry("global-vrouter-config", CreateNode);

    test->AddConfigEntry("virtual-network", CreateNode);
    test->AddConfigEntry("vn", CreateNode);

    test->AddConfigEntry("virtual-machine", CreateNode);
    test->AddConfigEntry("vm", CreateNode);

    test->AddConfigEntry("vxlan", CreateNode);

    test->AddConfigEntry("virtual-machine-interface", CreateNode);
    test->AddConfigEntry("vm-interface", CreateNode);
    test->AddConfigEntry("vmi", CreateNode);

    test->AddConfigEntry("ethernet-interface", CreateNode);
    test->AddConfigEntry("eth-port", CreateNode);

    test->AddConfigEntry("routing-instance", CreateNode);
    test->AddConfigEntry("vrf", CreateNode);

    test->AddConfigEntry("access-control-list", CreateNode);
    test->AddConfigEntry("acl", CreateNode);

    test->AddConfigEntry("virtual-machine-interface-routing-instance",
                         CreateNode);
    test->AddConfigEntry("vmi-vrf", CreateNode);

    test->AddValidateEntry("virtual-network", CreateValidateNode);
    test->AddValidateEntry("vn", CreateValidateNode);
    test->AddValidateEntry("vxlan", CreateValidateNode);
    test->AddValidateEntry("virtual-machine", CreateValidateNode);
    test->AddValidateEntry("vm", CreateValidateNode);
    test->AddValidateEntry("virtual-machine-interface", CreateValidateNode);
    test->AddValidateEntry("vm-interface", CreateValidateNode);
    test->AddValidateEntry("vmi", CreateValidateNode);
    test->AddValidateEntry("ethernet-interface", CreateValidateNode);
    test->AddValidateEntry("eth-port", CreateValidateNode);
    test->AddValidateEntry("flow", CreateValidateNode);
    test->AddValidateEntry("routing-instance", CreateValidateNode);
    test->AddValidateEntry("vrf", CreateValidateNode);
    test->AddValidateEntry("access-control-list", CreateValidateNode);
    test->AddValidateEntry("acl", CreateValidateNode);
    test->AddValidateEntry("pkt-parse", CreateValidateNode);
    test->AddValidateEntry("fdb", CreateValidateNode);
    test->AddValidateEntry("l2-route", CreateValidateNode);
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlGlobalVrouter routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlGlobalVrouter::AgentUtXmlGlobalVrouter(const std::string &name,
                                                 const uuid &id,
                                                 const xml_node &node,
                                                 AgentUtXmlTestCase *test_case) :
    AgentUtXmlConfig(name, id, node, test_case) {
}

AgentUtXmlGlobalVrouter::~AgentUtXmlGlobalVrouter() {
}

bool AgentUtXmlGlobalVrouter::ReadXml() {
    if (AgentUtXmlConfig::ReadXml() == false) {
        return false;
    }
    GetStringAttribute(node(), "vxlan-mode", &vxlan_mode_);
    return true;
}

bool AgentUtXmlGlobalVrouter::ToXml(xml_node *parent) {
    xml_node n = AddXmlNodeWithAttr(parent, NodeType().c_str());
    AddXmlNodeWithValue(&n, "name", name());
    if (!vxlan_mode().empty()) {
        AddXmlNodeWithValue(&n, "vxlan-network-identifier-mode", vxlan_mode());
    }
    AddIdPerms(&n);
    return true;
}

void AgentUtXmlGlobalVrouter::ToString(string *str) {
    AgentUtXmlConfig::ToString(str);
    *str += "\n";
    return;
}

string AgentUtXmlGlobalVrouter::NodeType() {
    return "global-vrouter-config";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlVn routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlVn::AgentUtXmlVn(const string &name, const uuid &id,
                           const xml_node &node,
                           AgentUtXmlTestCase *test_case) :
    AgentUtXmlConfig(name, id, node, test_case) {
}

AgentUtXmlVn::~AgentUtXmlVn() {
}


bool AgentUtXmlVn::ReadXml() {
    if (AgentUtXmlConfig::ReadXml() == false) {
        return false;
    }
    GetStringAttribute(node(), "vxlan-id", &vxlan_id_);
    GetStringAttribute(node(), "network-id", &network_id_);
    return true;
}

bool AgentUtXmlVn::ToXml(xml_node *parent) {
    xml_node n = AddXmlNodeWithAttr(parent, NodeType().c_str());
    AddXmlNodeWithValue(&n, "name", name());
    if (!network_id().empty()) {
        xml_node n1 = n.append_child("virtual-network-properties");
        AddXmlNodeWithValue(&n1, "network-id", network_id());
    }
    if (!vxlan_id().empty() && op_delete() == false) {
        xml_node n1 = n.append_child("virtual-network-properties");
        AddXmlNodeWithValue(&n1, "vxlan-network-identifier", vxlan_id());
    }
    AddIdPerms(&n);
    return true;
}

void AgentUtXmlVn::ToString(string *str) {
    AgentUtXmlConfig::ToString(str);
    *str += "\n";
    return;
}

string AgentUtXmlVn::NodeType() {
    return "virtual-network";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlVm routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlVm::AgentUtXmlVm(const string &name, const uuid &id,
                           const xml_node &node,
                           AgentUtXmlTestCase *test_case) :
    AgentUtXmlConfig(name, id, node, test_case) {
}

AgentUtXmlVm::~AgentUtXmlVm() {
}


bool AgentUtXmlVm::ReadXml() {
    return AgentUtXmlConfig::ReadXml();
}

bool AgentUtXmlVm::ToXml(xml_node *parent) {
    xml_node n = AddXmlNodeWithAttr(parent, NodeType().c_str());
    AddXmlNodeWithValue(&n, "name", name());
    AddIdPerms(&n);
    return true;
}

void AgentUtXmlVm::ToString(string *str) {
    AgentUtXmlConfig::ToString(str);
    *str += "\n";
    return;
}

string AgentUtXmlVm::NodeType() {
    return "virtual-machine";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlVmInterface routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlVmInterface::AgentUtXmlVmInterface(const string &name,
                                             const uuid &id,
                                             const xml_node &node,
                                             AgentUtXmlTestCase *test_case) :
    AgentUtXmlConfig(name, id, node, test_case) {
}

AgentUtXmlVmInterface::~AgentUtXmlVmInterface() {
}

bool AgentUtXmlVmInterface::ReadXml() {
    if (AgentUtXmlConfig::ReadXml() == false)
        return false;

    GetStringAttribute(node(), "mac", &mac_);

    vm_name_ = "";
    vn_uuid_ = nil_uuid();
    vm_uuid_ = nil_uuid();
    vrf_ = "";
    add_nova_ = false;
    ip_ = "0.0.0.0";

    GetStringAttribute(node(), "vn-name", &vn_name_);
    uint16_t id = 0;
    GetUintAttribute(node(), "vn-uuid", &id);
    if (id) {
        vn_uuid_ = MakeUuid(id);
    }

    GetStringAttribute(node(), "vm-name", &vm_name_);
    id = 0;
    GetUintAttribute(node(), "vm-uuid", &id);
    if (id) {
        vm_uuid_ = MakeUuid(id);
    }

    GetStringAttribute(node(), "vrf", &vrf_);
    GetStringAttribute(node(), "ip", &ip_);

    string str;
    if (GetStringAttribute(node(), "nova", &str) ||
        GetUintAttribute(node(), "nova", &id)) {
        add_nova_ = true;
    }

    return true;
}

bool AgentUtXmlVmInterface::ToXml(xml_node *parent) {
    xml_node n = AddXmlNodeWithAttr(parent, NodeType().c_str());
    AddXmlNodeWithValue(&n, "name", name());

    if (op_delete() == false) {
        xml_node n1 = n.append_child("virtual-machine-interface-mac-address");
        AddXmlNodeWithValue(&n1, "mac-address", mac_);
        AddIdPerms(&n);
    }

    if (add_nova_) {
        boost::system::error_code ec;
        IpAddress ip = Ip4Address::from_string(ip_, ec);
        NovaIntfAdd(op_delete(), id(), ip, vm_uuid_, vm_uuid_, name(), mac_,
                    vm_name_);
    }

    if (vm_name_ != "") {
        LinkXmlNode(parent, NodeType(), name(), "virtual-network", vn_name_);
    }

    if (vm_name_ != "") {
        LinkXmlNode(parent, NodeType(), name(), "virtual-machine", vm_name_);
    }

    if (vrf_ != "") {
        string str = name() + "-" + vm_name_;
        LinkXmlNode(parent, "virtual-machine-interface-routing-instance",
                    str, "routing-instance", vrf_);

        LinkXmlNode(parent, NodeType(), name(),
                    "virtual-machine-interface-routing-instance", str);
    }

    return true;
}

void AgentUtXmlVmInterface::ToString(string *str) {
    AgentUtXmlConfig::ToString(str);
    *str += "\n";
    return;
}

string AgentUtXmlVmInterface::NodeType() {
    return "virtual-machine-interface";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlEthInterface routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlEthInterface::AgentUtXmlEthInterface(const string &name,
                                               const uuid &id,
                                               const xml_node &node,
                                               AgentUtXmlTestCase *test_case) :
    AgentUtXmlConfig(name, id, node, false, test_case) {
}

AgentUtXmlEthInterface::~AgentUtXmlEthInterface() {
}

bool AgentUtXmlEthInterface::ReadXml() {
    return AgentUtXmlConfig::ReadXml();
}

bool AgentUtXmlEthInterface::ToXml(xml_node *parent) {
    assert(0);
    return true;
}

void AgentUtXmlEthInterface::ToString(string *str) {
    AgentUtXmlConfig::ToString(str);
    *str += "\n";
    return;
}

string AgentUtXmlEthInterface::NodeType() {
    return "physical-interface";
}

bool AgentUtXmlEthInterface::Run() {
    cout << "Create Ethernet Interface" << endl;
    return true;
}
/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlVrf routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlVrf::AgentUtXmlVrf(const string &name, const uuid &id,
                             const xml_node &node,
                             AgentUtXmlTestCase *test_case) :
    AgentUtXmlConfig(name, id, node, test_case) {
}

AgentUtXmlVrf::~AgentUtXmlVrf() {
}


bool AgentUtXmlVrf::ReadXml() {
    if (AgentUtXmlConfig::ReadXml() == false)
        return false;

    GetStringAttribute(node(), "vn", &vn_name_);
    return true;
}

bool AgentUtXmlVrf::ToXml(xml_node *parent) {
    xml_node n = AddXmlNodeWithAttr(parent, NodeType().c_str());
    AddXmlNodeWithValue(&n, "name", name());
    AddIdPerms(&n);
    if (vn_name_ != "") {
        LinkXmlNode(parent, NodeType(), name(), "virtual-network", vn_name_);
    }
    return true;
}

void AgentUtXmlVrf::ToString(string *str) {
    AgentUtXmlConfig::ToString(str);
    *str += "\n";
    return;
}

string AgentUtXmlVrf::NodeType() {
    return "routing-instance";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlVmiVrf routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlVmiVrf::AgentUtXmlVmiVrf(const string &name, const uuid &id,
                                   const xml_node &node,
                                   AgentUtXmlTestCase *test_case) :
    AgentUtXmlConfig(name, id, node, test_case) {
}

AgentUtXmlVmiVrf::~AgentUtXmlVmiVrf() {
}


bool AgentUtXmlVmiVrf::ReadXml() {
    return AgentUtXmlConfig::ReadXml();
}

bool AgentUtXmlVmiVrf::ToXml(xml_node *parent) {
    xml_node n = AddXmlNodeWithAttr(parent, NodeType().c_str());
    AddXmlNodeWithValue(&n, "name", name());

    xml_node n1 = n.append_child("value");
    AddXmlNodeWithValue(&n1, "direction", "both");
    AddXmlNodeWithValue(&n1, "vlan-tag", "0");

    AddIdPerms(&n);
    return true;
}

void AgentUtXmlVmiVrf::ToString(string *str) {
    AgentUtXmlConfig::ToString(str);
    *str += "\n";
    return;
}

string AgentUtXmlVmiVrf::NodeType() {
    return "virtual-machine-interface-routing-instance";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlAcl routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlAcl::AgentUtXmlAcl(const string &name, const uuid &id,
                             const xml_node &node,
                             AgentUtXmlTestCase *test_case) :
    AgentUtXmlConfig(name, id, node, test_case) {
}

AgentUtXmlAcl::~AgentUtXmlAcl() {
}

bool AgentUtXmlAcl::ReadXml() {
    if (AgentUtXmlConfig::ReadXml() == false)
        return false;

    for (xml_node n = node().first_child(); n; n = n.next_sibling()) {
        if (strcmp(n.name(), "ace") != 0) {
            continue;
        }

        Ace ace;

        GetUintAttribute(n, "src-sg", (uint16_t *)&ace.src_sg_);
        GetUintAttribute(n, "dst-sg", (uint16_t *)&ace.dst_sg_);

        GetStringAttribute(n, "src-vn", &ace.src_vn_);
        GetStringAttribute(n, "dst-vn", &ace.dst_vn_);

        GetStringAttribute(n, "src-ip", &ace.src_ip_);
        GetUintAttribute(n, "src-plen", (uint16_t *)&ace.src_ip_plen_);
        GetStringAttribute(n, "dst-ip", &ace.dst_ip_);
        GetUintAttribute(n, "dst-plen", (uint16_t *)&ace.dst_ip_plen_);

        std::string str;
        GetStringAttribute(n, "sport", &str);
        sscanf(str.c_str(), "%d:%d", &ace.sport_begin_, &ace.sport_end_);

        GetStringAttribute(n, "dport", &str);
        sscanf(str.c_str(), "%d:%d", &ace.dport_begin_, &ace.dport_end_);

        GetStringAttribute(n, "dport", &ace.action_);

        ace_list_.push_back(ace);
    }

    return true;
}

bool AgentUtXmlAcl::ToXml(xml_node *parent) {
    xml_node n = AddXmlNodeWithAttr(parent, NodeType().c_str());
    AddXmlNodeWithValue(&n, "name", name());

    xml_node n1 = n.append_child("access-control-list-entries");
    AddXmlNodeWithValue(&n1, "dynamic", "false");

    for (AceList::iterator it = ace_list_.begin(); it != ace_list_.end();
         it++) {
        n1 = n1.append_child("acl-rule");

        xml_node n2 = n1.append_child("match-condition");
        AddXmlNodeWithValue(&n2, "protocol", "any");

        xml_node n3 = n2.append_child("src-address");

        Ace ace = (*it);
        if (ace.src_sg_) {
            AddXmlNodeWithIntValue(&n3, "security-group", ace.src_sg_);
        }

        if (ace.src_vn_ != "") {
            AddXmlNodeWithValue(&n3, "virtual-network", ace.src_vn_);
        }

        if (ace.src_ip_ != "") {
            xml_node n4 = n3.append_child("subnet");
            AddXmlNodeWithValue(&n4, "ip-prefix", ace.src_ip_);
            AddXmlNodeWithIntValue(&n4, "ip-prefix-len", ace.src_ip_plen_);
        }

        n3 = n2.append_child("dst-address");
        if (ace.dst_sg_) {
            AddXmlNodeWithIntValue(&n3, "security-group", ace.dst_sg_);
        }

        if (ace.dst_vn_ != "") {
            AddXmlNodeWithValue(&n3, "virtual-network", ace.dst_vn_);
        }

        if (ace.dst_ip_ != "") {
            xml_node n4 = n3.append_child("subnet");
            AddXmlNodeWithValue(&n4, "ip-prefix", ace.dst_ip_);
            AddXmlNodeWithIntValue(&n4, "ip-prefix-len", ace.dst_ip_plen_);
        }

        n2 = n1.append_child("action_list");
        AddXmlNodeWithValue(&n2, "simple-action", ace.action_);
    }

    AddIdPerms(&n);
    return true;
}

void AgentUtXmlAcl::ToString(string *str) {
    AgentUtXmlConfig::ToString(str);
    *str += "\n";
    return;
}

string AgentUtXmlAcl::NodeType() {
    return "access-control-list";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlNova routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlNova::AgentUtXmlNova(const string &name, const uuid &id,
                             const xml_node &node,
                             AgentUtXmlTestCase *test_case) :
    AgentUtXmlConfig(name, id, node, false, test_case) {
}

AgentUtXmlNova::~AgentUtXmlNova() {
}

bool AgentUtXmlNova::ReadXml() {
    if (AgentUtXmlNode::ReadXml() == false)
        return false;

    if (GetStringAttribute(node(), "mac", &mac_) == false) {
        cout << "Attribute \"mac\" not specified for nova. Skipping"
            << endl;
        return false;
    }

    uint16_t num;
    if (GetUintAttribute(node(), "vm-uuid", &num) == false) {
        cout << "Attribute \"vm-uuid\" not specified for nova. Skipping"
            << endl;
        return false;
    }
    vm_uuid_ = MakeUuid(num);

    if (GetUintAttribute(node(), "vn-uuid", &num) == false) {
        cout << "Attribute \"vn-uuid\" not specified for nova. Skipping"
            << endl;
        return false;
    }
    vn_uuid_ = MakeUuid(num);

    if (GetStringAttribute(node(), "vm-name", &vm_name_) == false) {
        cout << "Attribute \"vm_name\" not specified for nova. Skipping"
            << endl;
        return false;
    }

    if (GetStringAttribute(node(), "ip", &ip_) == false) {
        cout << "Attribute \"ip\" not specified for nova. Skipping"
            << endl;
        return false;
    }

    return true;
}

bool AgentUtXmlNova::ToXml(xml_node *parent) {
    assert(0);
}

void AgentUtXmlNova::ToString(string *str) {
    AgentUtXmlConfig::ToString(str);
    *str += "\n";
    return;
}

string AgentUtXmlNova::NodeType() {
    return "Nova";
}

bool AgentUtXmlNova::Run() {
    boost::system::error_code ec;
    IpAddress ip = Ip4Address::from_string(ip_, ec);
    NovaIntfAdd(op_delete(), id(), ip, vm_uuid_, vn_uuid_, name(), mac_,
                vm_name_);
    return true;
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlVnValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlVnValidate::AgentUtXmlVnValidate(const string &name,
                                           const uuid &id,
                                           const xml_node &node) :
    AgentUtXmlValidationNode(name, node), id_(id) {
}

AgentUtXmlVnValidate::~AgentUtXmlVnValidate() {
}

bool AgentUtXmlVnValidate::ReadXml() {
    return true;
}

bool AgentUtXmlVnValidate::Validate() {
    if (present()) {
        return VnFind(id());
    } else {
        return !VnFind(id());
    }
}

const string AgentUtXmlVnValidate::ToString() {
    return "virtual-network";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlVmValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlVmValidate::AgentUtXmlVmValidate(const string &name,
                                           const uuid &id,
                                           const xml_node &node) :
    AgentUtXmlValidationNode(name, node), id_(id) {
}

AgentUtXmlVmValidate::~AgentUtXmlVmValidate() {
}

bool AgentUtXmlVmValidate::ReadXml() {
    return true;
}

bool AgentUtXmlVmValidate::Validate() {
    return true;
}

const string AgentUtXmlVmValidate::ToString() {
    return "virtual-network";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlVxlanValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlVxlanValidate::AgentUtXmlVxlanValidate(const string &name,
                                                 const uuid &id,
                                                 const xml_node &node) :
    AgentUtXmlValidationNode(name, node), id_(id) {
}

AgentUtXmlVxlanValidate::~AgentUtXmlVxlanValidate() {
}

bool AgentUtXmlVxlanValidate::ReadXml() {
    GetUintAttribute(node(), "vxlan", &vxlan_id_);
    return true;
}

bool AgentUtXmlVxlanValidate::Validate() {
    if (present()) {
        return VxlanFind(vxlan_id_);
    } else {
        return !VxlanFind(vxlan_id_);
    }
}

const string AgentUtXmlVxlanValidate::ToString() {
    return "vxlan";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlVmInterfaceValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlVmInterfaceValidate::AgentUtXmlVmInterfaceValidate(const string &name,
                                           const uuid &id,
                                           const xml_node &node) :
    AgentUtXmlValidationNode(name, node), id_(id) {
}

AgentUtXmlVmInterfaceValidate::~AgentUtXmlVmInterfaceValidate() {
}

bool AgentUtXmlVmInterfaceValidate::ReadXml() {
    return true;
}

bool AgentUtXmlVmInterfaceValidate::Validate() {
    if (present()) {
        return VmPortFind(id());
    } else {
        return !VmPortFind(id());
    }

    if (present()) {
        return VmPortActive(id());
    } else {
        return !VmPortActive(id());
    }

    return true;
}

const string AgentUtXmlVmInterfaceValidate::ToString() {
    return "virtual-machine-interface";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlEthInterfaceValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlEthInterfaceValidate::AgentUtXmlEthInterfaceValidate(const string &name,
                                           const uuid &id,
                                           const xml_node &node) :
    AgentUtXmlValidationNode(name, node), id_(id) {
}

AgentUtXmlEthInterfaceValidate::~AgentUtXmlEthInterfaceValidate() {
}

bool AgentUtXmlEthInterfaceValidate::ReadXml() {
    return true;
}

bool AgentUtXmlEthInterfaceValidate::Validate() {
    return true;
}

const string AgentUtXmlEthInterfaceValidate::ToString() {
    return "eth-interface";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlVrfValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlVrfValidate::AgentUtXmlVrfValidate(const string &name,
                                             const xml_node &node) :
    AgentUtXmlValidationNode(name, node) {
}

AgentUtXmlVrfValidate::~AgentUtXmlVrfValidate() {
}

bool AgentUtXmlVrfValidate::ReadXml() {
    GetStringAttribute(node(), "vn", &vn_name_);
    return true;
}

bool AgentUtXmlVrfValidate::Validate() {

    VrfEntry *vrf = VrfGet(name().c_str(), true);

    if (present() && (vrf == NULL)) {
        return false;
    } else if (!present() && (vrf != NULL)) {
        return false;
    }

    if (vrf == NULL)
        return true;

    if (delete_marked()) {
        if (vrf->IsDeleted() == false)
            return false;
    }

    if (vn_name_ == "") {
        return true;
    }

    if (vn_name_ == "nil" || vn_name_ == "NIL") {
        return (vrf->vn() == NULL);
    }

    if (vrf->vn() == NULL)
        return false;

    return (vrf->vn()->GetName() == vn_name_);
}

const string AgentUtXmlVrfValidate::ToString() {
    return "vrf";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlAclValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlAclValidate::AgentUtXmlAclValidate(const string &name,
                                             const xml_node &node) :
    AgentUtXmlValidationNode(name, node) {
}

AgentUtXmlAclValidate::~AgentUtXmlAclValidate() {
}

bool AgentUtXmlAclValidate::ReadXml() {
    return true;
}

bool AgentUtXmlAclValidate::Validate() {
    if (present()) {
        return AclFind(id());
    } else {
        return !AclFind(id());
    }
}

const string AgentUtXmlAclValidate::ToString() {
    return "access-control-list";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlFlowValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlFlowValidate::AgentUtXmlFlowValidate(const string &name,
                                               const xml_node &node) :
    AgentUtXmlValidationNode(name, node) {
}

AgentUtXmlFlowValidate::~AgentUtXmlFlowValidate() {
}

bool AgentUtXmlFlowValidate::ReadXml() {
    if (GetUintAttribute(node(), "nh", &nh_id_) == false) {
        cout << "Attribute \"nh\" not specified for Flow. Skipping" << endl;
        return false;
    }

    if (GetStringAttribute(node(), "sip", &sip_) == false) {
        cout << "Attribute \"sip\" not specified for Flow. Skipping" << endl;
        return false;
    }

    if (GetStringAttribute(node(), "dip", &dip_) == false) {
        cout << "Attribute \"dip\" not specified for Flow. Skipping" << endl;
        return false;
    }

    if (GetStringAttribute(node(), "proto", &proto_) == false &&
        GetUintAttribute(node(), "proto", &proto_id_) == false) {
        cout << "Attribute \"proto\" not specified for Flow. Skipping"
            << endl;
        return false;
    }

    if (proto_ == "tcp" || proto_ == "udp") {
        if (proto_ == "tcp")
            proto_id_ = 6;
        else
            proto_id_ = 17;
        if (GetUintAttribute(node(), "sport", &sport_) == false) {
            cout << "Attribute \"sport\" not specified for Flow. Skipping"
                << endl;
            return false;
        }

        if (GetUintAttribute(node(), "dport", &dport_) == false) {
            cout << "Attribute \"dport\" not specified for Flow. Skipping"
                << endl; return false;
        }
    }

    GetStringAttribute(node(), "svn", &svn_);
    GetStringAttribute(node(), "dvn", &dvn_);
    GetStringAttribute(node(), "action", &action_);
    return true;
}

static bool MatchFlowAction(FlowEntry *flow, const string &str) {
    uint64_t action = flow->data().match_p.action_info.action;
    if (str == "pass") {
        return (action & (1 << TrafficAction::PASS));
    }

    if (str == "drop") {
        return ((action & TrafficAction::DROP_FLAGS) != 0);
    }

    return false;
}

bool AgentUtXmlFlowValidate::Validate() {
    FlowEntry *flow = FlowGet(0, sip_, dip_, proto_id_, sport_, dport_,
                               nh_id_);
    if (present() == false)
        return (flow == NULL);

    if (flow == NULL)
        return false;

    if (svn_ != "" && svn_ != flow->data().source_vn)
        return false;

    if (dvn_ != "" && dvn_ != flow->data().dest_vn)
        return false;

    if (MatchFlowAction(flow, action_) == false)
        return false;
    return true;
}

const string AgentUtXmlFlowValidate::ToString() {
    return ("flow <"  + name() + ">");
}

uint32_t AgentUtXmlFlowValidate::wait_count() const {
    if (present()) {
        return 10;
    } else {
        return AgentUtXmlValidationNode::wait_count();
    }
}

/////////////////////////////////////////////////////////////////////////////
// AgentUtXmlL2Route routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlL2Route::AgentUtXmlL2Route(const string &name, const uuid &id,
                                     const xml_node &node,
                                     AgentUtXmlTestCase *test_case) :
    AgentUtXmlNode(name, node, false, test_case) {
}

AgentUtXmlL2Route::~AgentUtXmlL2Route() {
}


bool AgentUtXmlL2Route::ReadXml() {
    if (GetStringAttribute(node(), "mac", &mac_) == false) {
        cout << "Attribute \"mac\" not specified. Skipping" << endl;
        return false;
    }

    if (GetStringAttribute(node(), "vrf", &vrf_) == false) {
        cout << "Attribute \"vrf_\" not specified. Skipping" << endl;
        return false;
    }

    GetStringAttribute(node(), "vn", &vn_);
    GetUintAttribute(node(), "vxlan_id", &vxlan_id_);
    GetStringAttribute(node(), "tunnel-dest", &tunnel_dest_);
    GetStringAttribute(node(), "tunnel-type", &tunnel_type_);

    return true;
}

bool AgentUtXmlL2Route::ToXml(xml_node *parent) {
    assert(0);
    return true;
}

void AgentUtXmlL2Route::ToString(string *str) {
    *str = "L2-Route";
    return;
}

string AgentUtXmlL2Route::NodeType() {
    return "FDB";
}

bool AgentUtXmlL2Route::Run() {
    Layer2AgentRouteTable::AddRemoteVmRouteReq(NULL, vrf_,
                                               MacAddress::FromString(mac_),
                                               Ip4Address::from_string(ip_),
                                               0, NULL);
}

/////////////////////////////////////////////////////////////////////////////
// AgentUtXmlL2RouteValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlL2RouteValidate::AgentUtXmlL2RouteValidate(const string &name,
                                                     const xml_node &node) :
    AgentUtXmlValidationNode(name, node) {
}

AgentUtXmlL2RouteValidate::~AgentUtXmlL2RouteValidate() {
}

bool AgentUtXmlL2RouteValidate::ReadXml() {
    if (GetStringAttribute(node(), "mac", &mac_) == false) {
        cout << "Attribute \"mac\" not specified. Skipping" << endl;
        return false;
    }

    if (GetStringAttribute(node(), "vrf", &vrf_) == false) {
        cout << "Attribute \"vrf_\" not specified. Skipping" << endl;
        return false;
    }

    if (GetStringAttribute(node(), "vn", &vn_) == false) {
        cout << "Attribute \"vn\" not specified. Skipping" << endl;
        return false;
    }

    string addr = "";
    GetStringAttribute(node(), "ip", &addr);
    boost::system::error_code ec;
    ip_ = IpAddress::from_string(addr, ec);

    GetUintAttribute(node(), "vxlan_id", &vxlan_id_);
    GetStringAttribute(node(), "tunnel-dest", &tunnel_dest_);
    GetStringAttribute(node(), "tunnel-type", &tunnel_type_);
    GetUintAttribute(node(), "intf", &intf_uuid_);

    return true;
}

bool AgentUtXmlL2RouteValidate::Validate() {
    Agent *agent = Agent::GetInstance();
    Layer2RouteEntry *rt =
        Layer2AgentRouteTable::FindRoute(agent, vrf_,
                                         MacAddress::FromString(mac_), ip_);
    if (present() == false)
        return (rt == NULL);

    if (rt == NULL)
        return false;

    if (vn_ != "" && vn_ != rt->dest_vn_name())
        return false;

    return true;
}

const string AgentUtXmlL2RouteValidate::ToString() {
    return ("FDB <"  + name() + ">");
}
