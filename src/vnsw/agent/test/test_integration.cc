/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/random.hpp>

#include <iostream>
#include <pugixml/pugixml.hpp>

#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <ksync/ksync_init.h>
#include "ksync/ksync_index.h"
#include <ksync/interface_ksync.h>
#include <cmn/agent_cmn.h>
#include <base/task.h>
#include <base/patricia.h>
#include <io/event_manager.h>
#include <base/util.h>
#include <ifmap/ifmap_agent_parser.h>
#include <ifmap/ifmap_agent_table.h>
#include <oper/vn.h>
#include <oper/vm.h>

#include "testing/gunit.h"
#include "test_cmn_util.h"
#include "kstate/test/test_kstate_util.h"
#include "vr_types.h"
#include "vnc_cfg_types.h"

using namespace std;
using namespace pugi;
using namespace boost::random;
using namespace autogen;
using namespace Patricia;

#define GETTESTUSERARGS()                       \
    bool ksync_init = false;                    \
    char init_file[1024];                       \
    char test_file[1024];                       \
    memset(init_file, '\0', sizeof(init_file)); \
    memset(test_file, '\0', sizeof(test_file)); \
    ::testing::InitGoogleTest(&argc, argv);     \
    namespace opt = boost::program_options;     \
    opt::options_description desc("Options");   \
    opt::variables_map vm;                      \
    desc.add_options()                          \
        ("help", "Print help message")          \
        ("config", opt::value<string>(), "Specify Init config file")  \
        ("test-config", opt::value<string>(), "Specify test config file")  \
        ("test-seed", opt::value<int>(), "Specify test seed file")  \
        ("dump-config", "Print config")          \
        ("dump-config-file", "Save config to file") \
        ("kernel", "Run with vrouter");         \
    opt::store(opt::parse_command_line(argc, argv, desc), vm); \
    opt::notify(vm);                            \
    if (vm.count("help")) {                     \
        cout << "Test Help" << endl << desc << endl; \
        exit(0);                                \
    }                                           \
    if (vm.count("kernel")) {                   \
        ksync_init = true;                      \
    }                                           \
    if (vm.count("dump-config")) {              \
        dump_config = true;                     \
    }                                           \
    if (vm.count("dump-config-file")) {         \
        dump_config_file = true;                \
    }                                           \
    if (vm.count("test-seed")) {                \
        seed = vm["test-seed"].as<int>(); \
    }                                           \
    if (vm.count("test-config")) {              \
        strncpy(test_file, vm["test-config"].as<string>().c_str(), (sizeof(test_file) - 1) ); \
    } else {                                    \
        strncpy(test_file, "controller/src/vnsw/agent/testdata/vnswad_test.xml", (sizeof(test_file) - 1));\
    }                                           \
    if (vm.count("config")) {                   \
        strncpy(init_file, vm["config"].as<string>().c_str(), (sizeof(init_file) - 1) ); \
    } else {                                    \
        strcpy(init_file, DEFAULT_VNSW_CONFIG_FILE); \
    }                                           \

class GroupEntry {
public:
    GroupEntry() {
        installed_cnt = 0;
        node_cnt = 0;
        parent_ = NULL;
    }

    GroupEntry(GroupEntry *parent) : parent_(parent) {
        installed_cnt = 0;
        node_cnt = 0;
    }

    GroupEntry *parent_;
    vector<xml_node> c_expect;
    size_t installed_cnt;
    size_t node_cnt;
};

class NodeEntry {
public:
    NodeEntry(const char *type, const char *name) : type_(type), name_(name) {
        type_len_ = type_.length();
        name_len_ = name_.length();
        installed_ = false;
    }

    NodeEntry(const char *type, const char *name, xml_node data) : type_(type), name_(name), data_(data) {
        type_len_ = type_.length();
        name_len_ = name_.length();
        installed_ = false;
    }

    class Key {
    public:
        static size_t Length(const NodeEntry *node) {
            return ((node->type_len_ + node->name_len_) << 3);
        }

        static char ByteValue(const NodeEntry *node, size_t i) {
            if (i < node->type_len_) {
                return node->type_.at(i);
            }
            return node->name_.at(i - node->type_len_);
        }
    };

    string type_;
    string name_;
    size_t type_len_;
    size_t name_len_;
    xml_node data_;
    GroupEntry *g_entry;
    bool installed_;
    Node node_;
};

typedef Patricia::Tree<NodeEntry, &NodeEntry::node_, NodeEntry::Key> NodeTree;

void RouterIdDepInit(Agent *agent) {
}

static int seed = 1;

xml_document                                config_create;
vector<pair<xml_node, GroupEntry *> >       c_all;
vector<pair<xml_node, GroupEntry *> >       d_nodes;
NodeTree                                    c_node_tree;
vector<GroupEntry *>                        c_group_list;
GroupEntry                                  g_group;
bool                                        dump_config = false;
bool                                        dump_config_file = false;

class PugiPredicate {
public:
    bool operator()(xml_attribute attr) const {
        return (strcmp(attr.name(), tmp_.c_str()) == 0);
    }
    bool operator()(xml_node node) const {
        return (strcmp(node.name(), tmp_.c_str()) == 0);
    }
    PugiPredicate(const string &name) : tmp_(name) { }
    string tmp_;
};

xml_node FormNode (xml_node &input, xml_node &output, bool construct_only) {
    xml_node node = output.append_child("node");
    xml_attribute uuid_attr = input.attribute("uuid");

    node.append_attribute("type").set_value(input.name());
    string name(input.child_value());
    boost::trim(name);
    node.append_child("name").append_child(node_pcdata).set_value(name.c_str());

    if (!construct_only) {
        xml_node id_perms = node.append_child("id-perms");
        xml_node uuid_node = id_perms.append_child("uuid");
        char str[50];
        if (uuid_attr) {
            string value(uuid_attr.value());
            boost::trim(value);
            int uuid = strtoul(value.c_str(), NULL, 10);
            sprintf(str, "%016d", 0);
            uuid_node.append_child("uuid-mslong").append_child(node_pcdata).set_value(str);
            sprintf(str, "%016d", uuid);
            uuid_node.append_child("uuid-lslong").append_child(node_pcdata).set_value(str);
        } else {
            static int uuid = 0;
            uuid++;
            sprintf(str, "%016d", 20);
            uuid_node.append_child("uuid-mslong").append_child(node_pcdata).set_value(str);
            sprintf(str, "%016d", uuid);
            uuid_node.append_child("uuid-lslong").append_child(node_pcdata).set_value(str);
        }
        id_perms.append_child("enable").append_child(node_pcdata).set_value("true");
        for (xml_node child = input.first_child(); child; child = child.next_sibling()) {
            if (child.type() == node_pcdata) continue;
            node.append_copy(child);
        }
    }

    return node;
}

xml_node FormLink (xml_node &input, xml_node &output) {
    xml_document xdoc;
    xml_node config = xdoc.append_child("config");
    xml_node node = output.append_child("link");

    for (xml_node child = input.first_child(); child; child = child.next_sibling()) {
        node.append_copy(FormNode(child, config, true));
    }

    return node;
}

static string FileRead(const char *init_file) {
    ifstream ifs(init_file);
    string content ((istreambuf_iterator<char>(ifs) ),
            (istreambuf_iterator<char>() ));
    return content;
}

void FlushConfig() {
    while(c_node_tree.Size() != 0) {
        NodeEntry *node_entry = c_node_tree.GetNext(NULL);
        if (c_node_tree.Remove(node_entry)) {
            delete node_entry;
        }
    }
    vector<GroupEntry *>::iterator it;
    for (it = c_group_list.begin(); it != c_group_list.end(); it++) {
        delete (*it);
    }
    c_group_list.clear();
}

void ReadGroupNode(xml_node &config, xml_node &parent, GroupEntry *g_parent) {
    for (xml_node node = config.first_child(); node; node = node.next_sibling()) {
        xml_node new_config;
        if (strcmp(node.name(), "expect") == 0) {
            new_config = parent.append_copy(node);
            g_parent->c_expect.push_back(new_config);
        } else if (strcmp(node.name(), "group") == 0) {
            new_config = parent.append_child("group");
            GroupEntry *g_entry = new GroupEntry(g_parent);
            c_group_list.push_back(g_entry);
            ReadGroupNode(node, new_config, g_entry);
            g_parent->node_cnt++;
        } else if (strcmp(node.name(), "node") == 0) {
            new_config = parent.append_copy(node);
            NodeEntry *node_entry =
                new NodeEntry(node.attribute("type").value(),
                              node.find_node(PugiPredicate("name")).child_value(),
                              new_config);
            c_node_tree.Insert(node_entry);
            c_all.push_back(make_pair(new_config, g_parent));
            g_parent->node_cnt++;
        } else if (strcmp(node.name(), "link") == 0) {
            if (strcmp(node.first_child().name(), "node") == 0) {
                new_config = parent.append_copy(node);
            } else {
                new_config = FormLink(node, parent);
            }
            c_all.push_back(make_pair(new_config, g_parent));
            g_parent->node_cnt++;
        } else if (strcmp(node.name(), "nova") == 0) {
            new_config = parent.append_copy(node);
            c_all.push_back(make_pair(new_config, g_parent));
            g_parent->node_cnt++;
        } else {
            new_config = FormNode(node, parent, false);
            NodeEntry *node_entry =
                new NodeEntry(new_config.attribute("type").value(),
                              new_config.find_node(PugiPredicate("name")).child_value(),
                              new_config);
            c_node_tree.Insert(node_entry);
            c_all.push_back(make_pair(new_config, g_parent));
            g_parent->node_cnt++;
        }
    }
}

void ReadInputFile(char *input_file) {
    xml_document xdoc;
    if (input_file == NULL || input_file[0] == '\0') {
        LOG(WARN, "No Input file !!!");
        assert(0);
    } else {
        xml_parse_result result = xdoc.load_file(input_file);
        assert(result);
    }

    xml_node config = xdoc.child("config");
    ReadGroupNode(config, config_create, &g_group);
    //xdoc.save_file("config_integration.xml");
    //config_create.save_file("config_integration.xml");
}

void TestReadUuid (xml_node &parent, uuid &id) {
    UuidType    uuid_type;

    if (parent.first_child().type() != node_pcdata) {
        uuid_type.XmlParse(parent);
    } else {
        uuid_type.uuid_mslong = 0;
        string value(parent.child_value());
        boost::trim(value);
        uuid_type.uuid_lslong = strtoul(value.c_str(), NULL, 10);
    }

    CfgUuidSet(uuid_type.uuid_mslong, uuid_type.uuid_lslong, id);
}

void IntfExpectProcess (xml_node &parent, bool expect) {
    TestIntfTable   table;

    for (xml_node node = parent.first_child(); node;
         node = node.next_sibling()) {
         assert(strcmp(node.name(), "intf") == 0);

         TestIntfEntry intf_key(node.find_node(PugiPredicate("name")).child_value());
         TestIntfEntry *intf = table.Find(&intf_key);

         if (expect) {
             EXPECT_TRUE(intf != NULL);
             if (!intf) {
                continue;
             }
         } else {
             EXPECT_TRUE(intf == NULL);
             continue;
         }

         for (xml_node child = node.first_child(); child;
                 child = child.next_sibling()) {
             if (strcmp(child.name(), "mac") == 0) {
                 EXPECT_TRUE(strcasecmp(child.child_value(),
                                        intf->get_mac().c_str()) == 0);
             } else if (strcmp(child.name(), "name") == 0) {
                 EXPECT_TRUE(strcmp(child.child_value(),
                             intf->get_name().c_str()) == 0);
             } else if (strcmp(child.name(), "type") == 0) {
                 EXPECT_TRUE(strcasecmp(child.child_value(), intf->get_type().c_str()) == 0);
             }
         }
    }
}

void RouteNhProcess (KNHInfo *nh, xml_node &parent, bool expect) {
    string nh_type(parent.attribute("type").value());

    EXPECT_TRUE(nh_type == nh->get_type());
}

void RouteExpectProcess (xml_node &parent, bool expect) {
    const char *vrf_name = parent.attribute("vrf").value();
    VrfEntry    *vrf_entry = VrfGet(vrf_name);

    if (!vrf_entry) {
        EXPECT_FALSE(expect);
        return;
    }

    TestRouteTable   table(vrf_entry->vrf_id());
    TestNhTable      nh_table;

    for (xml_node node = parent.first_child(); node;
         node = node.next_sibling()) {
         assert(strcmp(node.name(), "route") == 0);

         int plen;
         {
             string value(node.find_node(PugiPredicate("len")).child_value());
             boost::trim(value);
             plen = strtoul(value.c_str(), NULL, 10);
         }

         TestRouteEntry rt_key(node.find_node(PugiPredicate("prefix")).child_value(),
                               plen);
         TestRouteEntry *rt = table.Find(&rt_key);

         xml_node nh_node = node.find_node(PugiPredicate("nh"));
         if (rt && nh_node) {
            TestNhTable::iterator it = nh_table.find(rt->nh_id);
            EXPECT_TRUE(it != nh_table.end());
            if (it != nh_table.end()) {
                RouteNhProcess(it->second, nh_node, expect);
            }
         }

         if (expect) {
             EXPECT_TRUE(rt != NULL);
         } else {
             EXPECT_TRUE(rt == NULL);
             continue;
         }
    }
}

void MplsExpectProcess (xml_node &parent, bool expect) {
    TestMplsTable   table;

    for (xml_node node = parent.first_child(); node;
         node = node.next_sibling()) {
         assert(strcmp(node.name(), "mpls") == 0);

         int label;
         {
             string value(node.find_node(PugiPredicate("label")).child_value());
             boost::trim(value);
             label = strtoul(value.c_str(), NULL, 10);
         }

         KMplsInfo *mpls = table.at(label);

         if (expect) {
             EXPECT_TRUE(mpls != NULL);
         } else {
             EXPECT_TRUE(mpls == NULL);
             continue;
         }
    }
}

void ExpectMsgProcess (xml_node &node, bool expect) {
    if (strcmp(node.attribute("type").value(), "interface-table") == 0) {
        IntfExpectProcess(node, expect);
    } else if (strcmp(node.attribute("type").value(), "route-table") == 0) {
        RouteExpectProcess(node, expect);
    } else if (strcmp(node.attribute("type").value(), "mpls-table") == 0) {
        MplsExpectProcess(node, expect);
    } else {
        assert(0);
    }
}

void GroupAddInstalled (GroupEntry *g_parent) {
    g_parent->installed_cnt++;
    if (g_parent->installed_cnt != g_parent->node_cnt) {
        assert(g_parent->installed_cnt < g_parent->node_cnt);
        return;
    }

    usleep(10000);
    client->WaitForIdle();
    usleep(10000);

    for (unsigned int i = 0; i < g_parent->c_expect.size(); i++) {
        ExpectMsgProcess(g_parent->c_expect[i], true);
    }

    if (g_parent->parent_) {
        GroupAddInstalled(g_parent->parent_);
    }
}

void GroupDelInstalled (GroupEntry *g_parent) {
    assert(g_parent->installed_cnt);
    g_parent->installed_cnt--;
    if (g_parent->installed_cnt) {
        return;
    }

    usleep(10000);
    client->WaitForIdle();
    usleep(10000);

    for (unsigned int i = 0; i < g_parent->c_expect.size(); i++) {
        ExpectMsgProcess(g_parent->c_expect[i], false);
    }

    if (g_parent->parent_) {
        GroupDelInstalled(g_parent->parent_);
    }
}

void DumpXmlNode (pugi::xml_node &node) {
    if (dump_config) {
        ostringstream oss;
        node.print(oss);
        string xml = oss.str();
        LOG(ERROR, "Parsing Config :\n" << xml);
    }
}

void NovaMsgProcess (xml_document &xdoc, pair<xml_node, GroupEntry *> node, bool create) {
    uuid    port_id;
    uuid    vn_id;
    uuid    vm_id;
    uuid    project_id = MakeUuid(kProjectUuid);
    const char   *ipaddr = NULL;
    const char   *mac = NULL;
    const char   *tap_intf = NULL;
    xml_node  parent = node.first;
    GroupEntry *g_parent = node.second;

    for (xml_node node = parent.first_child(); node;
         node = node.next_sibling()) {
         if (strcmp(node.name(), "uuid") == 0) {
            TestReadUuid(node, port_id);
            if (!create) break;
         } else if (strcmp(node.name(), "vn-uuid") == 0) {
            TestReadUuid(node, vn_id);
         } else if (strcmp(node.name(), "vm-uuid") == 0) {
            TestReadUuid(node, vm_id);
         } else if (strcmp(node.name(), "tap-interface") == 0) {
            tap_intf = node.child_value();
         } else if (strcmp(node.name(), "ip") == 0) {
            ipaddr = node.child_value();
         } else if (strcmp(node.name(), "mac") == 0) {
            mac = node.child_value();
         }
    }

    DumpXmlNode(parent);

    CfgIntKey *key = new CfgIntKey(port_id);

    DBRequest req;
    if (create) {
        CfgIntData *data = new CfgIntData();
        boost::system::error_code ec;
        IpAddress ip = Ip4Address::from_string(ipaddr, ec);
        data->Init(vm_id, vn_id, project_id, tap_intf, ip, mac, "",
                   VmInterface::kInvalidVlanId,
                   CfgIntEntry::CfgIntVMPort, 0);

        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        req.key.reset(key);
        req.data.reset(data);
    } else {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        req.key.reset(key);
        req.data.reset(NULL);
    }
    Agent::GetInstance()->interface_config_table()->Enqueue(&req);
    xdoc.append_copy(parent);

    if (create) {
        GroupAddInstalled(g_parent);
    } else {
        GroupDelInstalled(g_parent);
    }
}

void IntegrationTestAddConfig(xml_document &xdoc, xml_node &node) {
    xml_node config = xdoc.append_child("config");
    xml_node update = config.append_child("update");
    update.append_copy(node);
    DumpXmlNode(config);
    Agent::GetInstance()->ifmap_parser()->ConfigParse(config, 0);
}

void IntegrationTestDelConfig(xml_document &xdoc, xml_node &node) {
    xml_node config = xdoc.append_child("config");
    xml_node update = config.append_child("delete");
    update.append_copy(node);
    DumpXmlNode(config);
    Agent::GetInstance()->ifmap_parser()->ConfigParse(config, 0);
}

void AddNodeConfig (xml_document &xdoc, xml_node &node, GroupEntry *g_parent) {
    NodeEntry key(node.attribute("type").value(),
            node.find_node(PugiPredicate("name")).child_value());
    NodeEntry *node_entry = c_node_tree.Find(&key);
    if (node_entry->installed_) {
        return;
    }

    node_entry->installed_ = true;
    IntegrationTestAddConfig(xdoc, node_entry->data_);
    d_nodes.push_back(make_pair(node_entry->data_, g_parent));

    GroupAddInstalled(g_parent);
}

void DelNodeConfig (xml_document &xdoc, xml_node &node, GroupEntry *g_parent) {
    NodeEntry key(node.attribute("type").value(),
            node.find_node(PugiPredicate("name")).child_value());
    NodeEntry *node_entry = c_node_tree.Find(&key);
    if (!node_entry->installed_) {
        return;
    }
    node_entry->installed_ = false;
    IntegrationTestDelConfig(xdoc, node_entry->data_);

    GroupDelInstalled(g_parent);
}

void AddLinkConfig (xml_document &xdoc, xml_node &node, GroupEntry *g_parent) {
    xml_node child = node.first_child();
    assert(strcmp(child.name(), "node") == 0);
    AddNodeConfig(xdoc, child, g_parent);
    child = child.next_sibling();
    assert(strcmp(child.name(), "node") == 0);
    AddNodeConfig(xdoc, child, g_parent);
    IntegrationTestAddConfig(xdoc, node);
    d_nodes.push_back(make_pair(node, g_parent));

    GroupAddInstalled(g_parent);
}

class IntegrationTest : public ::testing::Test {
};

struct RandomNumGen : unary_function<unsigned, unsigned> {
    mt19937 &state_;
    unsigned operator() (unsigned i) {
        boost::uniform_int<> rng(0, i-1);
        return rng(state_);
    }
    RandomNumGen(mt19937 &state) : state_(state) {}
};

TEST_F(IntegrationTest, LoadCfgFile) {
    static int i = 1;
    LOG(ERROR, "Iteration : " << i << "   Random Seed : " << seed);
    xml_document xdoc;
    xml_document d_xdoc;
    mt19937 mt_gen(seed);
    char file_name[1024];
    vector<pair<xml_node, GroupEntry *> > config_all = c_all;
    RandomNumGen    rand_num(mt_gen);
    const char *node_name;

    random_shuffle(config_all.begin(), config_all.end(), rand_num);

    for (unsigned int i = 0; i < config_all.size(); i++) {
        node_name = config_all[i].first.name();
        if (strcmp(node_name, "node") == 0) {
            AddNodeConfig(xdoc, config_all[i].first, config_all[i].second);
        } else if (strcmp(node_name, "link") == 0) {
            AddLinkConfig(xdoc, config_all[i].first, config_all[i].second);
        } else if (strcmp(node_name, "nova") == 0) {
            NovaMsgProcess(xdoc, config_all[i], true);
            d_nodes.push_back(config_all[i]);
        } else {
            assert(0);
        }
    }

    while (!d_nodes.empty()) {
        xml_node d_node = d_nodes.back().first;
        GroupEntry *g_parent = d_nodes.back().second;
        node_name = d_node.name();
        if (strcmp(node_name, "node") == 0) {
            DelNodeConfig(d_xdoc, d_node, g_parent);
        } else if (strcmp(node_name, "link") == 0) {
            IntegrationTestDelConfig(d_xdoc, d_node);
            GroupDelInstalled(g_parent);
        } else if (strcmp(node_name, "nova") == 0) {
            NovaMsgProcess(d_xdoc, d_nodes.back(), false);
        }
        d_nodes.pop_back();
    }

    if (dump_config_file) {
        sprintf(file_name, "Create_integration_%d.xml", i);
        xdoc.save_file(file_name);

        sprintf(file_name, "Delete_integration_%d.xml", i);
        d_xdoc.save_file(file_name);
    }
    i++;
    seed++;
}

int main(int argc, char **argv) {

    GETTESTUSERARGS();

    client = TestInit(init_file, ksync_init);

    ReadInputFile(test_file);
    int ret = RUN_ALL_TESTS();
    FlushConfig();
    TestShutdown();
    delete client;
    return ret;
}
