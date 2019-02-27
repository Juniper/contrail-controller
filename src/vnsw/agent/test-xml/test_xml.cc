/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include "base/os.h"
#include <iostream>
#include <fstream>
#include <pugixml/pugixml.hpp>
#include <boost/filesystem.hpp>
#include <boost/uuid/uuid.hpp>

#include <test/test_cmn_util.h>
#include <pkt/test/test_pkt_util.h>
#include <pkt/flow_mgmt.h>
#include <oper/global_vrouter.h>
#include "test_xml.h"
#include "test_xml_validate.h"
#include "test_xml_packet.h"

using namespace std;
using namespace pugi;
using namespace boost::uuids;
using namespace AgentUtXmlUtils;

namespace AgentUtXmlUtils {
bool GetStringAttribute(const xml_node &node, const string &name,
                        string *value) {
    xml_attribute attr = node.attribute(name.c_str());
    if (!attr) {
        return false;;
    }

    *value = attr.as_string();

    return true;
}

bool GetUintAttribute(const xml_node &node, const string &name,
                      uint16_t *value) {
    xml_attribute attr = node.attribute(name.c_str());
    if (!attr) {
        return false;;
    }

    *value = attr.as_uint();

    return true;
}

bool GetIntAttribute(const xml_node &node, const string &name, int *value) {
    xml_attribute attr = node.attribute(name.c_str());
    if (!attr) {
        return false;;
    }

    *value = attr.as_int();

    return true;
}

bool GetBoolAttribute(const xml_node &node, const string &name,
                      bool *value) {
    xml_attribute attr = node.attribute(name.c_str());
    if (!attr) {
        return false;;
    }

    string str = attr.as_string();
    if (str == "true" || str == "yes")
        *value = true;
    else
        *value = false;
    return true;
}

void NovaIntfAdd(bool op_delete, const uuid &id, const Ip4Address &ip,
                 const uuid &vm_uuid, const uuid vn_uuid, const string &name,
                 const string &mac, const string vm_name) {
    if (op_delete) {
        PortUnSubscribe(id);
        cout << "Nova Del Interface Message " << endl;
        return;
    }

    PortSubscribe(name, id, vm_uuid, vm_name, vn_uuid, MakeUuid(1), ip,
                  Ip6Address::v4_compatible(ip), mac);
    cout << "Nova Add Interface Message " << endl;
    return;
}

void LinkXmlNode(xml_node *parent, const string &ltype, const string lname,
                 const string &rtype, const string rname) {
    xml_node n = parent->append_child("link");

    xml_node n1 = n.append_child("node");
    n1.append_attribute("type") = ltype.c_str();

    xml_node n2 = n1.append_child("name");
    n2.append_child(pugi::node_pcdata).set_value(lname.c_str());

    n1 = n.append_child("node");
    n1.append_attribute("type") = rtype.c_str();

    n2 = n1.append_child("name");
    n2.append_child(pugi::node_pcdata).set_value(rname.c_str());

    string mdata = GetMetadata(ltype.c_str(), rtype.c_str());
    xml_node n3 = n.append_child("metadata");
    n3.append_attribute("type") = mdata.c_str();
    return;
}

xml_node AddXmlNodeWithAttr(xml_node *parent, const char *attr) {
    xml_node n = parent->append_child("node");
    n.append_attribute("type") = attr;
    return n;
}

xml_node AddXmlNodeWithValue(xml_node *parent, const char *name,
                             const string &value) {
    xml_node n = parent->append_child(name);
    n.append_child(pugi::node_pcdata).set_value(value.c_str());
    return n;
}

xml_node AddXmlNodeWithIntValue(xml_node *parent, const char *name,
                                int val) {
    stringstream s;
    s << val;
    xml_node n = parent->append_child(name);
    n.append_child(pugi::node_pcdata).set_value(s.str().c_str());
    return n;
}
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlTest routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlTest::AgentUtXmlTest(const std::string &name) : file_name_(name) {
}

AgentUtXmlTest::~AgentUtXmlTest() {

    for (AgentUtXmlTestList::iterator i = test_list_.begin();
         i != test_list_.end(); i++) {
        delete *i;
    }
}

void AgentUtXmlTest::AddConfigEntry(const std::string &name,
                                    AgentUtXmlTestConfigCreateFn fn) {
    config_factory_[name] = fn;
}

void AgentUtXmlTest::AddValidateEntry(const std::string &name,
                                      AgentUtXmlTestValidateCreateFn fn) {
    validate_factory_[name] = fn;
}

AgentUtXmlTest::AgentUtXmlTestConfigCreateFn
AgentUtXmlTest::GetConfigCreateFn(const std::string &name) {
    AgentUtXmlTestConfigFactory::iterator iter = config_factory_.find(name);
    if (iter == config_factory_.end()) {
        return AgentUtXmlTest::AgentUtXmlTestConfigCreateFn();
    }

    return iter->second;
}

AgentUtXmlTest::AgentUtXmlTestValidateCreateFn
AgentUtXmlTest::GetValidateCreateFn(const std::string &name) {
    AgentUtXmlTestValidateFactory::iterator iter = validate_factory_.find(name);
    if (iter == validate_factory_.end()) {
        assert(0);
    }
    return iter->second;
}

bool AgentUtXmlTest::ReadXml() {
    xml_node list = doc_.child("test_suite");
    GetStringAttribute(list, "name", &name_);

    for (xml_node node = list.first_child(); node; node = node.next_sibling()) {
        if (strcmp(node.name(), "test") == 0) {
            xml_attribute attr = node.attribute("name");
            if (!attr) {
                cout << "Missing attribute \"name\". Skipping" << endl;
                continue;
            }
            AgentUtXmlTestCase *test = new AgentUtXmlTestCase(attr.value(),
                                                              node, this);
            attr = node.attribute("verbose");
            bool verbose = false;
            if (!attr) {
                verbose = false;
            } else {
                if (atoi(attr.value()))
                    verbose = true;
            }
            test->set_verbose(verbose);
            test_list_.push_back(test);
            test->ReadXml();
        }
    }

    return true;
}

bool AgentUtXmlTest::Load() {
    boost::system::error_code ec;
    boost::filesystem::path file_path(file_name_);
    uintmax_t file_size = boost::filesystem::file_size(file_path, ec);
    if (ec) {
        cout << "Error <" << ec << "> opening file" << file_name_ << endl;
        return false;
    }

    std::fstream file(file_name_.c_str(), std::ios::binary | std::ios_base::in);
    if (!file) {
        cout << "Error <fstream error> opening file" << file_name_ << endl;
        return false;
    }

    std::vector<char> data(file_size + 1, 0);
    file.read(data.data(), file_size);
    if (!file || file.gcount() < static_cast<std::streamsize>(file_size)) {
        cout << "Error <fstream::read> reading file" << file_name_ << endl;
        return false;
    }

    xml_parse_result result = doc_.load(data.data());
    if (result) {
        cout << "Loaded data file successfully" << endl;
    } else {
        cout << "Error in XML string at offset <: " << result.offset
            << "> (error at [..." << (data.data() + result.offset) << "])" << endl;
        return false;
    }

    return true;
}

void AgentUtXmlTest::ToString(string *str) {
    stringstream s;

    s << "Test Suite : " << name_ << endl;
    *str += s.str();
    for (AgentUtXmlTestList::iterator it = test_list_.begin();
         it != test_list_.end(); it++) {
        (*it)->ToString(str);
    }
    return;
}

bool AgentUtXmlTest::Run() {
    for (AgentUtXmlTestList::iterator it = test_list_.begin();
         it != test_list_.end(); it++) {
        (*it)->Run();
    }

    return true;
}

bool AgentUtXmlTest::Run(std::string test_case) {
    for (AgentUtXmlTestList::iterator it = test_list_.begin();
         it != test_list_.end(); it++) {
        if ((*it)->name().compare(test_case) == 0) {
            (*it)->Run();
            return true;
        }
    }

    return false;
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlTestCase routines
/////////////////////////////////////////////////////////////////////////////
static bool CheckConfigNode(const string &node_name, const xml_node &node,
                            uuid *id, string *name) {
    if (strcmp(node.name(), node_name.c_str()) != 0) {
        return false;
    }

    xml_attribute attr = node.attribute("name");
    if (!attr) {
        cout << "Attribute \"name\" not found for " << node_name
            << ". Skipping..." << endl;
        return false;
    }

    *name = attr.as_string();
    if (*name == "") {
        cout << "Invalid \"name\" for " << node_name << " Skipping" << endl;
        return false;
    }

    if (node_name == "validate")
        return false;

    attr = node.attribute("uuid");
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

AgentUtXmlTestCase::AgentUtXmlTestCase(const std::string &name,
                                       const xml_node &node,
                                       AgentUtXmlTest *test)
    : name_(name), xml_node_(node), test_(test), verbose_(false) {
    cout << "Creating test-case <" << name_ << ">" << endl;
}

AgentUtXmlTestCase::~AgentUtXmlTestCase() {
    for (AgentUtXmlNodeList::iterator i =  node_list_.begin();
         i != node_list_.end(); i++) {
        delete *i;
    }
}

bool AgentUtXmlTestCase::ReadXml() {
    for (xml_node node = xml_node_.first_child(); node;
         node = node.next_sibling()) {

        string str;
        bool op_delete = false;
        if (GetStringAttribute(node, "delete", &str) == true ||
            GetStringAttribute(node, "del", &str) == true) {
            if (str != "false" && str != "0")
                op_delete = true;
        }

        uuid id;
        string name;
        AgentUtXmlNode *cfg = NULL;

        AgentUtXmlTest::AgentUtXmlTestConfigCreateFn fn =
            test_->GetConfigCreateFn(node.name());
        if (CheckConfigNode(node.name(), node, &id, &name) == true) {
            if (fn.empty() == false)
                cfg = fn(node.name(), name, id, node, this);
        }

        if (strcmp(node.name(), "link") == 0) {
            cfg = new AgentUtXmlLink(node, this);
        }

        if (strcmp(node.name(), "packet") == 0) {
            if (GetStringAttribute(node, "name", &name) == false) {
                cout << "Attribute \"name\" not specified for Packet. Skipping"
                    << endl;
                continue;
            }
            cfg = new AgentUtXmlPacket(name, node, this);
        }

        if (strcmp(node.name(), "task") == 0) {
            cfg = new AgentUtXmlTask(node, this);
        }

        if (strcmp(node.name(), "validate") == 0) {
            if (GetStringAttribute(node, "name", &name) == false) {
                cout << "Attribute \"name\" not specified for validate."
                   " Skipping" << endl;
                continue;
            }
            cfg = new AgentUtXmlValidate(name, node, this);
        }

        if (cfg) {
            cfg->set_op_delete(op_delete);
        } else {
            cout << "Unknown node name <" << node.name() << ">. Ignoring"
                << endl;
        }
        if (cfg) {
            bool ret = cfg->ReadXml();
            if (op_delete == false && ret == false) {
                delete cfg;
                cfg = NULL;
            }
        }

        if (cfg) {
            node_list_.push_back(cfg);
        }
    }

    return true;
}

bool AgentUtXmlTestCase::Run() {
    for (AgentUtXmlNodeList::iterator it = node_list_.begin();
         it != node_list_.end(); it++) {
        if ((*it)->gen_xml() == false) {
            (*it)->Run();
            TestClient::WaitForIdle();
            continue;
        }

        xml_document doc;

        xml_node decl = doc.prepend_child(pugi::node_declaration);
        decl.append_attribute("version") = "1.0";

        xml_node n = doc.append_child("config");
        xml_node n1;
        if ((*it)->op_delete()) {
            n1 = n.append_child("delete");
        } else {
            n1 = n.append_child("update");
        }
        (*it)->ToXml(&n1);
        if (verbose_) {
            doc.print(std::cout);
        }
        Agent *agent = Agent::GetInstance();
        agent->ifmap_parser()->ConfigParse(n, 0);
        TestClient::WaitForIdle();
    }

    return true;
}

void AgentUtXmlTestCase::ToString(string *str) {
    stringstream s;

    s << "Test Case : " << name_ << endl;
    *str += s.str();
    for (AgentUtXmlNodeList::iterator it = node_list_.begin();
         it != node_list_.end(); it++) {
        (*it)->ToString(str);
    }
    return;
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlNode routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlNode::AgentUtXmlNode(const string &name, const xml_node &node,
                               AgentUtXmlTestCase *test_case) :
    node_(node), name_(name), op_delete_(false), gen_xml_(true),
    test_case_(test_case) {
}

AgentUtXmlNode::AgentUtXmlNode(const string &name, const xml_node &node,
                               bool gen_xml, AgentUtXmlTestCase *test_case) :
    node_(node), name_(name), op_delete_(false), gen_xml_(gen_xml),
    test_case_(test_case) {
}

AgentUtXmlNode::~AgentUtXmlNode() {
}

bool AgentUtXmlNode::ReadXml() {
    string str;
    op_delete_ = false;
    if (GetStringAttribute(node_, "delete", &str) == true ||
        GetStringAttribute(node_, "del", &str) == true) {
        if (str != "false" && str != "0")
            op_delete_ = true;
    }

    return true;
}

void AgentUtXmlNode::ToString(string *str) {
    stringstream s;

    if (op_delete_)
        s << "Delete ";
    else
        s << "Add ";

    s << NodeType() << " : " << name_ << " ";

    *str += s.str();
    return;
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlTask routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlTask::AgentUtXmlTask(const xml_node &node,
                               AgentUtXmlTestCase *test_case) :
    AgentUtXmlNode("task", node, false, test_case) {
}

AgentUtXmlTask::~AgentUtXmlTask() {
}

bool AgentUtXmlTask::ReadXml() {
    GetStringAttribute(node(), "stop", &stop_);
    return true;
}

bool AgentUtXmlTask::ToXml(xml_node *parent) {
    return true;
}

void AgentUtXmlTask::ToString(string *str) {
    AgentUtXmlNode::ToString(str);
    stringstream s;

    s << "Stop : " << stop_ << endl;
    *str += s.str();
    return;
}

string AgentUtXmlTask::NodeType() {
    return "Task";
}

bool AgentUtXmlTask::Run() {
    if (boost::iequals(stop_, "1") || boost::iequals(stop_, "yes")) {
        TestClient::WaitForIdle();
        TaskScheduler::GetInstance()->Stop();
    } else {
        TaskScheduler::GetInstance()->Start();
        TestClient::WaitForIdle();
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlLink routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlLink::AgentUtXmlLink(const xml_node &node,
                               AgentUtXmlTestCase *test_case) :
    AgentUtXmlNode("link", node, test_case) {
}

AgentUtXmlLink::~AgentUtXmlLink() {
}

bool AgentUtXmlLink::ReadXml() {
    if (GetStringAttribute(node(), "left", &l_node_) == false) {
        cout << "Left node-type not specified for link. Skipping" << endl;
        return false;
    }

    if (GetStringAttribute(node(), "left-name", &l_name_) == false) {
        cout << "Right node-name not specified for link. Skipping" << endl;
        return false;
    }

    if (GetStringAttribute(node(), "right", &r_node_) == false) {
        cout << "Right node-type not specified for link. Skipping" << endl;
        return false;
    }

    if (GetStringAttribute(node(), "right-name", &r_name_) == false) {
        cout << "Right node-name not specified for link. Skipping" << endl;
        return false;
    }

    return true;
}

bool AgentUtXmlLink::ToXml(xml_node *parent) {
    LinkXmlNode(parent, l_node_, l_name_, r_node_, r_name_);
    return true;
}

void AgentUtXmlLink::ToString(string *str) {
    AgentUtXmlNode::ToString(str);
    stringstream s;

    s << "<" << l_node_ << " : " << l_name_ << "> <" << " right-node "
        << r_node_ << " : " << r_name_ << ">" << endl;

    *str += s.str();
    return;
}

string AgentUtXmlLink::NodeType() {
    return "Link";
}

/////////////////////////////////////////////////////////////////////////////
//  AgentUtXmlConfig routines
/////////////////////////////////////////////////////////////////////////////
AgentUtXmlConfig::AgentUtXmlConfig(const string &name, const uuid &id,
                                   const xml_node &node,
                                   AgentUtXmlTestCase *test_case) :
    AgentUtXmlNode(name, node, test_case), id_(id) {
}

AgentUtXmlConfig::AgentUtXmlConfig(const string &name, const uuid &id,
                                   const xml_node &node, bool gen_xml,
                                   AgentUtXmlTestCase *test_case) :
    AgentUtXmlNode(name, node, gen_xml, test_case), id_(id) {
}

AgentUtXmlConfig::~AgentUtXmlConfig() {
}

bool AgentUtXmlConfig::ReadXml() {
    return AgentUtXmlNode::ReadXml();
}

void AgentUtXmlConfig::ToString(std::string *str) {
    AgentUtXmlNode::ToString(str);
    stringstream s;
    s << " UUID : " << id_;
    *str += s.str();
}

static void AddPermissions(xml_node *parent) {
    xml_node n = parent->append_child("permissions");

    AddXmlNodeWithValue(&n, "owner", "cloud-admin");
    AddXmlNodeWithValue(&n, "owner-access", "7");
    AddXmlNodeWithValue(&n, "group", "cloud-admin-group");
    AddXmlNodeWithValue(&n, "group-access", "7");
    AddXmlNodeWithValue(&n, "other-access", "7");
}

static void AddUuid(xml_node *parent, const uuid &id) {
    xml_node n = parent->append_child("uuid");

    std::vector<uint8_t> v1(id.size());
    std::vector<uint64_t> v(id.size());
    std::copy(id.begin(), id.end(), v.begin());

    uint64_t ms_val = v[7] + (v[6] << 8) + (v[5] << 16) + (v[4] << 24) +
                   (v[3] << 32) + (v[2] << 40) + (v[1] << 48) + (v[0] << 56);
    uint64_t ls_val = v[15] + (v[14] << 8) + (v[13] << 16) + (v[12] << 24) +
                   (v[11] << 32) + (v[10] << 40) + (v[9] << 48) + (v[8] << 56);
    stringstream s;
    s << ms_val;
    AddXmlNodeWithValue(&n, "uuid-mslong", s.str());

    stringstream s1;
    s1 << ls_val;
    AddXmlNodeWithValue(&n, "uuid-lslong", s1.str());
}

void AgentUtXmlConfig::AddIdPerms(xml_node *parent) {

    if (op_delete())
        return;

    xml_node n = parent->append_child("id-perms");
    AddPermissions(&n);
    AddUuid(&n, id());
    AddXmlNodeWithValue(&n, "enable", "true");
}
