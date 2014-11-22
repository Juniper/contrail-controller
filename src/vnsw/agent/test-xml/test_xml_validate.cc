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
using namespace AgentUtXmlUtils;

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
    for (AgentUtXmlValidationList::iterator i =  node_list_.begin();
         i != node_list_.end(); i++) {
        delete *i;
    }
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
        AgentUtXmlValidationNode *val = NULL;

        AgentUtXmlTest::AgentUtXmlTestValidateCreateFn fn =
            test_case()->test()->GetValidateCreateFn(n.name());
        if (CheckValidateNodeWithUuid(n.name(), n, &id, &name) == true) {
            if (fn.empty() == false)
                val = fn(n.name(), name, id, n);
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
    cout << "Running validation" << " <" << name() << ">" << endl;
    for (AgentUtXmlValidationList::iterator it = node_list_.begin();
         it != node_list_.end(); it++) {
        TestClient::WaitForIdle();
        AgentUtXmlValidationNode *node = *it;
        cout << "Validating " << node->ToString() << endl;
        WAIT_FOR(1000, 1000, (node->Validate() == true));
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
        if (str == "no")
            present_ = false;
        else
            present_ = true;
    }

    if (GetStringAttribute(node_, "deleted", &str)) {
       delete_marked_ = true;
    }

    if (GetStringAttribute(node_, "del", &str)) {
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
