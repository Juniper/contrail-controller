/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include "base/os.h"
#include <iostream>
#include <fstream>
#include <pugixml/pugixml.hpp>
#include <boost/uuid/uuid.hpp>

#include <test/test_cmn_util.h>
#include <pkt/test/test_pkt_util.h>
#include <pkt/flow_mgmt.h>
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

        if (strcmp(n.name(), "flow-export") == 0) {
            val =  new AgentUtXmlFlowExportValidate(n);
        } else if (strcmp(n.name(), "flow-threshold") == 0) {
            val =  new AgentUtXmlFlowThresholdValidate(n);
        } else {
            AgentUtXmlTest::AgentUtXmlTestValidateCreateFn fn =
                test_case()->test()->GetValidateCreateFn(n.name());
            if (CheckValidateNodeWithUuid(n.name(), n, &id, &name) == true) {
                if (fn.empty() == false)
                    val = fn(n.name(), name, id, n);
            }
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
        uint32_t i = 0;
        bool ret = false;
        while (i < node->wait_count()) {
            TestClient::WaitForIdle();
            if (node->Validate() == true) {
                ret = true;
                break;
            }
            usleep(node->sleep_time());
            i++;
        }
        EXPECT_TRUE(ret);
        if (ret == false) {
            cout << "Failed validation of " << node->ToString() << endl;
        }
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
        if (str == "no" || str == "0")
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

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlFlowExportValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlFlowExportValidate::AgentUtXmlFlowExportValidate(
                                                        const xml_node &node) :
    AgentUtXmlValidationNode("flow-export", node), count_(-1) {
}

AgentUtXmlFlowExportValidate::~AgentUtXmlFlowExportValidate() {
}

bool AgentUtXmlFlowExportValidate::ReadXml() {
    GetIntAttribute(node(), "count", &count_);
    return true;
}

bool AgentUtXmlFlowExportValidate::Validate() {
    Agent *agent = Agent::GetInstance();

    TestClient::WaitForIdle();
    uint32_t count = (uint32_t)count_;
    if (agent->flow_stats_manager()->flow_export_count() != count)
        return false;

    return true;
}

const string AgentUtXmlFlowExportValidate::ToString() {
    return "flow-export";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlFlowThresholdValidate routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlFlowThresholdValidate::AgentUtXmlFlowThresholdValidate(
                                                        const xml_node &node) :
    AgentUtXmlValidationNode("flow-threshold", node) {
}

AgentUtXmlFlowThresholdValidate::~AgentUtXmlFlowThresholdValidate() {
}

bool AgentUtXmlFlowThresholdValidate::ReadXml() {
    uint16_t threshold;
    if (GetUintAttribute(node(), "threshold", &threshold) == false) {
        threshold_ = 0;
    } else {
        threshold_ = (uint32_t)threshold;
    }
    return true;
}

bool AgentUtXmlFlowThresholdValidate::Validate() {
    Agent *agent = Agent::GetInstance();

    TestClient::WaitForIdle();
    FlowStatsManager *fsm = agent->flow_stats_manager();
    if (fsm->threshold() != threshold_)
        return false;

    return true;
}

const string AgentUtXmlFlowThresholdValidate::ToString() {
    return "flow-threshold";
}
