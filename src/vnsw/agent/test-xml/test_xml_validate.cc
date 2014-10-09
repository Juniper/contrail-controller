/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include "base/os.h"
#include <iostream>
#include <fstream>
#include <pugixml/pugixml.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/string_generator.hpp>

#include <test/test_cmn_util.h>
#include <pkt/test/test_pkt_util.h>
#include "test_xml.h"
#include "test_xml_validate.h"

using namespace std;
using namespace pugi;
using namespace boost::uuids;

static bool GetStringAttribute(const xml_node &node, const string &name,
                               string *value) {
    xml_attribute attr = node.attribute(name.c_str());
    if (!attr) {
        return false;;
    }

    *value = attr.as_string();

    return true;
}

static bool GetUintAttribute(const xml_node &node, const string &name,
                             uint16_t *value) {
    xml_attribute attr = node.attribute(name.c_str());
    if (!attr) {
        return false;;
    }

    *value = attr.as_uint();

    return true;
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlValidate routines
/////////////////////////////////////////////////////////////////////////////
static bool CheckValidateNode(const string &node_name, const xml_node &node,
                              string *name) {
    if (strcmp(node.name(), node_name.c_str()) != 0) {
        return false;
    }

    xml_attribute attr = node.attribute("name");
    if (!attr) {
        cout << "Attribute \"name\" not found for " << node_name
            << ". Skipping..." << endl;
        return false;;
    }

    *name = attr.as_string();
    if (*name == "") {
        cout << "Invalid \"name\" for " << node_name << " Skipping" << endl;
        return false;
    }

    return true;
}

static bool CheckValidateNodeWithUuid(const string &node_name,
                                      const xml_node &node,
                                      uuid *id,
                                      string *name) {
    if (CheckValidateNode(node_name, node, name) == false)
        return false;

    xml_attribute attr = node.attribute("uuid");
    if (!attr) {
        cout << "Attribute \"uuid\" not found for " << node_name
            << ". Skipping..." << endl;
        return false;;
    }

    int x = attr.as_uint();
    if (x == 0) {
        cout << "Invalid \"uuid\" (0) for " << node_name << " Skipping" << endl;
        return false;
    }

    *id = MakeUuid(x);

    return true;
}

AgentUtXmlValidate::AgentUtXmlValidate(const string &name,
                                       const xml_node &node,
                                       AgentUtXmlTestCase *test_case) :
    AgentUtXmlNode(name, node, false, test_case) {
}

AgentUtXmlValidate::~AgentUtXmlValidate() {
}

void AgentUtXmlValidate::ToString(string *str) {
    AgentUtXmlNode::ToString(str);
    *str += "\n";
    return;
}

string AgentUtXmlValidate::NodeType() {
    return "Validate";
}

bool AgentUtXmlValidate::ReadXml() {
    AgentUtXmlNode::ReadXml();
    for (xml_node n = node().first_child(); n; n = n.next_sibling()) {
        uuid id;
        string name;
        AgentUtXmlValidationNode *val = false;
        if (CheckValidateNodeWithUuid("virtual-network", n, &id, &name)
            == true) {
            val = new AgentUtXmlVnValidate(name, id, n);
        }

        if (CheckValidateNodeWithUuid("virtual-machine", n, &id, &name)
            == true) {
            val = new AgentUtXmlVmValidate(name, id, n);
        }

        if (CheckValidateNodeWithUuid("virtual-machine-interface", n, &id,
                                      &name) == true) {
            val = new AgentUtXmlVmInterfaceValidate(name, id, n);
        }

        if (CheckValidateNodeWithUuid("ethernet-interface", n, &id, &name)
            == true) {
            val = new AgentUtXmlEthInterfaceValidate(name, id, n);
        }

        if (CheckValidateNode("flow", n, &name) == true) {
            val = new AgentUtXmlFlowValidate(name, n);
        }

        if (CheckValidateNode("routing-instance", n, &name) == true) {
            val = new AgentUtXmlVrfValidate(name, n);
        }

        if (CheckValidateNode("acl", n, &name) == true) {
            val = new AgentUtXmlAclValidate(name, n);
        }

        if (val != NULL) {
            val->ReadCmnXml();
            if (val->ReadXml())
                node_list_.push_back(val);
            cout << "Validate node " << val->ToString() << endl;
        } else {
            cout << "Unknown node name <" << n.name() << ">. Ignoring" << endl;
        }
    }

    return true;
}

bool AgentUtXmlValidate::ToXml(xml_node *parent) {
    assert(0);
    return true;
}

bool AgentUtXmlValidate::Run() {
    cout << "Running validation" << endl;
    for (AgentUtXmlValidationList::iterator it = node_list_.begin();
         it != node_list_.end(); it++) {
        TestClient::WaitForIdle();
        cout << "Validating " << (*it)->ToString() << endl;
        WAIT_FOR(100, 1000, ((*it)->Validate() == true));
    }

    return true;
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlValidationNode routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlValidationNode::AgentUtXmlValidationNode(const string &name,
                                                   const xml_node &node) :
    name_(name), node_(node), present_(true), delete_marked_(false) {
}

AgentUtXmlValidationNode::~AgentUtXmlValidationNode() {
}
                                                   
bool AgentUtXmlValidationNode::ReadCmnXml() {
    std::string str;
    if (GetStringAttribute(node_, "present", &str)) {
        present_ = true;
    }

    if (GetStringAttribute(node_, "deleted", &str)) {
       delete_marked_ = true;
    }

    if (GetUintAttribute(node_, "id", &id_) == false)
        GetUintAttribute(node_, "uuid", &id_);

    return true;
}

bool AgentUtXmlValidationNode::ReadXml() {
    return true;
}

bool AgentUtXmlValidationNode::Validate() {
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
    return true;
}

bool AgentUtXmlVrfValidate::Validate() {
    if (present()) {
        return VrfFind(name().c_str());
    } else {
        return !VrfFind(name().c_str());
    }
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
    uint16_t id = 0;
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

    return true;
}

bool AgentUtXmlFlowValidate::Validate() {
    FlowEntry *entry = FlowGet(0, sip_, dip_, proto_id_, sport_, dport_,
                               nh_id_);
    if (present()) {
        return (entry != NULL);
    } else {
        return (entry == NULL);
    }
}

const string AgentUtXmlFlowValidate::ToString() {
    return "flow";
}
