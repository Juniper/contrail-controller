/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/test/ifmap_stress_test.h"

#include "base/logging.h"
#include "base/string_util.h"
#include "base/task.h"
#include "base/test/task_test_util.h"
#include "base/util.h"
#include "control-node/control_node.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/ifmap_server_table.h"
#include "ifmap/ifmap_xmpp.h"
#include "ifmap/test/ifmap_test_util.h"
#include "ifmap/test/ifmap_xmpp_client_mock.h"
#include "io/test/event_manager_test.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/sandesh_types.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "xmpp/xmpp_server.h"

#include <iostream>
#include <fstream>

using namespace std;
namespace opt = boost::program_options;

// Globals
static const char **gargv;
static int gargc;
static string GetUserName();

#define IFMAP_STRESS_TEST_LOG(str)                               \
do {                                                             \
    log4cplus::Logger logger = log4cplus::Logger::getRoot();     \
    LOG4CPLUS_DEBUG(logger, "IFMAP_STRESS_TEST_LOG: "            \
                    << __FILE__  << ":"  << __FUNCTION__ << "()" \
                    << ":"  << __LINE__ << " " << str);          \
} while (false)

const int IFMapSTOptions::kDEFAULT_NUM_EVENTS = 50;
const int IFMapSTOptions::kDEFAULT_NUM_XMPP_CLIENTS = 5;
const int IFMapSTOptions::kDEFAULT_PER_CLIENT_NUM_VMS = 24;
const int IFMapSTOptions::kDEFAULT_NUM_VMIS_PER_VM = 8;
const int IFMapSTOptions::kDEFAULT_WAIT_FOR_IDLE_TIME = 30;
const string IFMapSTOptions::kDEFAULT_EVENT_WEIGHTS_FILE=
        "controller/src/ifmap/testdata/ifmap_event_weights.txt";

const string IFMapStressTest::kXMPP_CLIENT_PREFIX = "XmppClient";
const string IFMapStressTest::kDefaultXmppServerName = "bgp.contrail.com";

// **** Start IFMapSTOptions routines.

IFMapSTOptions::IFMapSTOptions() :
        num_events_(kDEFAULT_NUM_EVENTS),
        num_xmpp_clients_(kDEFAULT_NUM_XMPP_CLIENTS),
        num_vms_(kDEFAULT_PER_CLIENT_NUM_VMS),
        num_vmis_(kDEFAULT_NUM_VMIS_PER_VM),
        wait_for_idle_time_(kDEFAULT_WAIT_FOR_IDLE_TIME),
        event_weight_file_(kDEFAULT_EVENT_WEIGHTS_FILE),
        desc_("Configuration options") {
    Initialize();
}

void IFMapSTOptions::Initialize() {
    string nevents_msg = "Number of events (default "
        + integerToString(IFMapSTOptions::kDEFAULT_NUM_EVENTS) + ")";
    string ncli_xmpp_msg = "Number of XMPP clients (default "
        + integerToString(IFMapSTOptions::kDEFAULT_NUM_XMPP_CLIENTS) + ")";
    string num_vms_msg = "Number of virtual machines per client (default "
        + integerToString(IFMapSTOptions::kDEFAULT_PER_CLIENT_NUM_VMS) + ")";
    string num_vmis_msg = "Number of interfaces per VM (default "
        + integerToString(IFMapSTOptions::kDEFAULT_NUM_VMIS_PER_VM) + ")";
    string idle_time_msg = "Number of seconds to wait for idle cpu (default "
        + integerToString(IFMapSTOptions::kDEFAULT_WAIT_FOR_IDLE_TIME) + ")";
    desc_.add_options()
        ("Help", "produce help message")
        ("nclients-xmpp", opt::value<int>(), ncli_xmpp_msg.c_str())
        ("nevents", opt::value<int>(), nevents_msg.c_str())
        ("num-vms", opt::value<int>(), num_vms_msg.c_str())
        ("num-vmis", opt::value<int>(), num_vmis_msg.c_str())
        ("wait-for-idle-time", opt::value<int>(), idle_time_msg.c_str())
        ("events-file", opt::value<string>(), "Events filename")
        ("event-weight-file", opt::value<string>(), "Event weights filename")
        ;

    opt::variables_map var_map;
    opt::store(opt::parse_command_line(gargc, gargv, desc_), var_map);
    opt::notify(var_map);

    if (var_map.count("Help")) {
        cout << desc_ << endl;
        exit(0);
    }
    if (var_map.count("nclients-xmpp")) {
        num_xmpp_clients_ = var_map["nclients-xmpp"].as<int>();
    }
    if (var_map.count("nevents")) {
        num_events_ = var_map["nevents"].as<int>();
    }
    if (var_map.count("num-vms")) {
        num_vms_ = var_map["num-vms"].as<int>();
    }
    if (var_map.count("num-vmis")) {
        num_vmis_ = var_map["num-vmis"].as<int>();
    }
    if (var_map.count("wait-for-idle-time")) {
        wait_for_idle_time_ = var_map["wait-for-idle-time"].as<int>();
    }
    if (var_map.count("events-file")) {
        events_file_ = var_map["events-file"].as<string>();
    }
    if (var_map.count("event-weight-file")) {
        event_weight_file_ = var_map["event-weight-file"].as<string>();
    }
}

const string &IFMapSTOptions::events_file() const {
    return events_file_;
}

const string &IFMapSTOptions::event_weight_file() const {
    return event_weight_file_;
}

int IFMapSTOptions::num_events() const {
    return num_events_;
}

int IFMapSTOptions::num_xmpp_clients() const {
    return num_xmpp_clients_;
}

int IFMapSTOptions::num_vms() const {
    return num_vms_;
}

int IFMapSTOptions::num_vmis() const {
    return num_vmis_;
}

int IFMapSTOptions::wait_for_idle_time() const {
    return wait_for_idle_time_;
}

// **** Start IFMapSTEventMgr routines.

IFMapSTEventMgr::IFMapSTEventMgr() :
        events_from_file_(false), max_events_(0), events_executed_(0),
        event_weights_sum_(0) {
}

inline bool IFMapSTEventMgr::events_from_file() const {
    return events_from_file_;
}

void IFMapSTEventMgr::ReadEventsFile(const IFMapSTOptions &config_options) {
    string filename = config_options.events_file();
    if (filename.empty()) {
        max_events_ = config_options.num_events();
        return;
    }
    ifstream file(filename.c_str());
    string event;

    getline(file, event);
    while (file.good()) {
        // Check that its a valid event.
        assert((event_string_map_.find(event) != event_string_map_.end()) &&
               "Invalid event in Events file");
        file_events_.push_back(event);
        getline(file, event);
    }

    // Stop if an empty file was provided.
    assert(!file_events_.empty() && "Event filename is empty");
    file.close();
    cout << file_events_.size() << " events read from file" << endl;
    events_from_file_ = true;
    max_events_ = file_events_.size();
}

void IFMapSTEventMgr::ReadEventWeightsFile(const string &filename) {
    ifstream file(filename.c_str());
    string line;
    int weight = 0;
    bool valid = false;

    getline(file, line);
    while (file.good()) {
        valid = stringToInteger(line, weight);
        assert(valid && "Weight in weights file is not an integer");
        event_weights_.push_back(weight);
        event_weights_sum_ += weight;
        getline(file, line);
    }

    assert((event_weights_.size() == NUM_EVENT_TYPES) &&
           "Event weights file does not have a weight for each event type");
    file.close();
    cout << "event_weights_sum_ " << event_weights_sum_ << endl;
}

void IFMapSTEventMgr::Initialize(const IFMapSTOptions &config_options) {
    ReadEventsFile(config_options);
    ReadEventWeightsFile(config_options.event_weight_file());
}

// Type to String
IFMapSTEventMgr::EventTypeMap IFMapSTEventMgr::InitEventTypeMap() {
    IFMapSTEventMgr::EventTypeMap evmap;
    evmap[VR_NODE_ADD] = "VR_NODE_ADD";
    evmap[VR_NODE_DELETE] = "VR_NODE_DELETE";
    evmap[VR_SUB] = "VR_SUB";
    evmap[VM_NODE_ADD] = "VM_NODE_ADD";
    evmap[VM_NODE_DELETE] = "VM_NODE_DELETE";
    evmap[VM_SUB] = "VM_SUB";
    evmap[VM_UNSUB] = "VM_UNSUB";
    evmap[OTHER_CONFIG_ADD] = "OTHER_CONFIG_ADD";
    evmap[OTHER_CONFIG_DELETE] = "OTHER_CONFIG_DELETE";
    evmap[XMPP_READY] = "XMPP_READY";
    evmap[XMPP_NOTREADY] = "XMPP_NOTREADY";
    evmap[IROND_CONN_DOWN] = "IROND_CONN_DOWN";
    assert((evmap.size() == NUM_EVENT_TYPES) &&
           "event_type_map_ not initialized properly");
    return evmap;
}

const IFMapSTEventMgr::EventTypeMap IFMapSTEventMgr::event_type_map_ =
    IFMapSTEventMgr::InitEventTypeMap();

string
IFMapSTEventMgr::EventToString(IFMapSTEventMgr::EventType event) const {
    return event_type_map_.at(event);
}

// String to Type
IFMapSTEventMgr::EventStringMap IFMapSTEventMgr::InitEventStringMap() {
    IFMapSTEventMgr::EventStringMap evmap;
    evmap["VR_NODE_ADD"] = VR_NODE_ADD;
    evmap["VR_NODE_DELETE"] = VR_NODE_DELETE;
    evmap["VR_SUB"] = VR_SUB;
    evmap["VM_NODE_ADD"] = VM_NODE_ADD;
    evmap["VM_NODE_DELETE"] = VM_NODE_DELETE;
    evmap["VM_SUB"] = VM_SUB;
    evmap["VM_UNSUB"] = VM_UNSUB;
    evmap["OTHER_CONFIG_ADD"] = OTHER_CONFIG_ADD;
    evmap["OTHER_CONFIG_DELETE"] = OTHER_CONFIG_DELETE;
    evmap["XMPP_READY"] = XMPP_READY;
    evmap["XMPP_NOTREADY"] = XMPP_NOTREADY;
    evmap["VM_SUB"] = VM_SUB;
    evmap["VM_UNSUB"] = VM_UNSUB;
    evmap["IROND_CONN_DOWN"] = IROND_CONN_DOWN;
    assert((evmap.size() == NUM_EVENT_TYPES) &&
           "event_string_map_ not initialized properly");
    return evmap;
}

const IFMapSTEventMgr::EventStringMap IFMapSTEventMgr::event_string_map_ =
    IFMapSTEventMgr::InitEventStringMap();

IFMapSTEventMgr::EventType
IFMapSTEventMgr::StringToEvent(string event) const {
    return event_string_map_.at(event);
}

bool IFMapSTEventMgr::EventAvailable() const {
    return (events_executed_ < max_events_ ? true : false);
}

void IFMapSTEventMgr::LogEvent(IFMapSTEventMgr::EventType event) {
    string event_log = "Event " + EventToString(event);
    event_log_.push_back(event_log);
}

IFMapSTEventMgr::EventType IFMapSTEventMgr::GetNextEvent() {
    EventType event;
    assert(EventAvailable());

    if (events_from_file()) {
        assert(!file_events_.empty());
        event = StringToEvent(file_events_.front());
        file_events_.pop_front();
    } else {
        int random_number;
        random_number = (std::rand() % event_weights_sum_);
        int event_type;
        for (event_type = 0; event_type < NUM_EVENT_TYPES; ++event_type) {
            if (random_number < event_weights_[event_type]) {
                break;
            }
            random_number -= event_weights_[event_type];
        }
        event = static_cast<EventType>(event_type);
    }
    ++events_executed_;

    LogEvent(event);

    return event;
}

// **** Start IFMapStressTest routines.

IFMapStressTest::IFMapStressTest()
        : db_(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable")),
          ifmap_server_(&db_, &db_graph_, evm_.io_service()), parser_(NULL),
          xmpp_server_(NULL), log_buffer_(kMAX_LOG_NUM_EVENTS),
          events_ignored_(0) {
}

void IFMapStressTest::Log(string log_string) {
    IFMAP_STRESS_TEST_LOG(log_string);
    log_buffer_.push_back(log_string);
}

void IFMapStressTest::WaitForIdle(int wait_seconds) {
    if (wait_seconds) {
        task_util::WaitForIdle(wait_seconds);
    } else if (config_options_.wait_for_idle_time()) {
        usleep(10);
        task_util::WaitForIdle(config_options_.wait_for_idle_time());
    }
}

void IFMapStressTest::SetUp() {
    xmpp_server_ = new XmppServer(&evm_, kDefaultXmppServerName);
    thread_.reset(new ServerThread(&evm_));
    xmpp_server_->Initialize(0);

    IFMapLinkTable_Init(&db_, &db_graph_);
    parser_ = IFMapServerParser::GetInstance("vnc_cfg");
    vnc_cfg_ParserInit(parser_);
    vnc_cfg_Server_ModuleInit(&db_, &db_graph_);
    bgp_schema_ParserInit(parser_);
    bgp_schema_Server_ModuleInit(&db_, &db_graph_);
    ifmap_server_.Initialize();

    ifmap_channel_mgr_.reset(new IFMapChannelManager(xmpp_server_,
                                                     &ifmap_server_));
    ifmap_server_.set_ifmap_channel_manager(ifmap_channel_mgr_.get());
    thread_->Start();

    event_generator_.Initialize(config_options_);
    CreateXmppClients();
    XmppClientInits();
    SetupEventCallbacks();
}

void IFMapStressTest::TearDown() {
    WaitForIdle();
    VerifyConfig();
    DeleteXmppClients();

    WaitForIdle();
    ifmap_server_.Shutdown();

    WaitForIdle();
    IFMapLinkTable_Clear(&db_);
    IFMapTable::ClearTables(&db_);

    WaitForIdle();
    db_.Clear();
    DB::ClearFactoryRegistry();
    parser_->MetadataClear("vnc_cfg");

    xmpp_server_->Shutdown();
    WaitForIdle();
    TcpServerManager::DeleteServer(xmpp_server_);
    evm_.Shutdown();
    if (thread_.get() != NULL) {
        thread_->Join();
    }
    PrintTestInfo();
}

// Pick a random element from the set and return its value.
template<typename SetType>
SetType IFMapStressTest::PickRandomElement(const std::set<SetType> &client_set){
    assert(client_set.size() != 0);
    int random_id = (std::rand() % client_set.size());
    typename std::set<SetType>::const_iterator iter = client_set.begin();
    std::advance(iter, random_id);
    return *iter;
}

string IFMapStressTest::XmppClientNameCreate(int id) {
    return kXMPP_CLIENT_PREFIX + integerToString(id);
}

void IFMapStressTest::XmppClientInits() {
    assert((int)xmpp_clients_.size() == config_options_.num_xmpp_clients());

    vm_to_add_ids_.reserve(config_options_.num_xmpp_clients());
    vm_to_delete_ids_.reserve(config_options_.num_xmpp_clients());
    vm_sub_pending_ids_.reserve(config_options_.num_xmpp_clients());
    vm_unsub_pending_ids_.reserve(config_options_.num_xmpp_clients());
    client_counters_.reserve(config_options_.num_xmpp_clients());

    VmIdSet vm_set;
    IFMapSTClientCounters counter;
    for (int i = 0; i < config_options_.num_xmpp_clients(); ++i) {
        // To begin with, all the clients are disconnected.
        xmpp_disconnected_.insert(i);

        // Add empty sets for each client.
        vm_to_add_ids_.push_back(vm_set);
        vm_to_delete_ids_.push_back(vm_set);
        vm_sub_pending_ids_.push_back(vm_set);
        vm_unsub_pending_ids_.push_back(vm_set);

        // To begin with, all the VM ids are available for vm-subs and
        // vm-node-creates.
        for (int j = 0; j < config_options_.num_vms(); ++j) {
            vm_to_add_ids_.at(i).insert(j);
            vm_sub_pending_ids_.at(i).insert(j);
        }
        client_counters_.push_back(counter);
    }
}

void IFMapStressTest::CreateXmppClients() {
    assert(xmpp_clients_.empty());
    for (int i = 0; i < config_options_.num_xmpp_clients(); ++i) {
        string client_name(XmppClientNameCreate(i));
        string filename("/tmp/" + GetUserName() + "_" + client_name);
        IFMapXmppClientMock *client =
            new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(),
                                    client_name, filename);
        TASK_UTIL_EXPECT_EQ(true, client->IsEstablished());
        xmpp_clients_.push_back(client);
    }
    Log("Created " + integerToString(xmpp_clients_.size()) + " xmpp clients");
}

void IFMapStressTest::DeleteXmppClients() {
    assert((int)xmpp_clients_.size() == config_options_.num_xmpp_clients());
    for (int i = 0; i < config_options_.num_xmpp_clients(); ++i) {
        xmpp_clients_.at(i)->UnRegisterWithXmpp();
        xmpp_clients_.at(i)->Shutdown();
        task_util::WaitForIdle();
        TcpServerManager::DeleteServer(xmpp_clients_.at(i));
    }
    xmpp_clients_.clear();
}

void IFMapStressTest::SetupEventCallbacks() {
    callbacks_[IFMapSTEventMgr::VR_NODE_ADD] =
        boost::bind(&IFMapStressTest::VirtualRouterNodeAdd, this);
    callbacks_[IFMapSTEventMgr::VR_NODE_DELETE] =
        boost::bind(&IFMapStressTest::VirtualRouterNodeDelete, this);
    callbacks_[IFMapSTEventMgr::VR_SUB] =
        boost::bind(&IFMapStressTest::VirtualRouterSubscribe, this);
    callbacks_[IFMapSTEventMgr::VM_NODE_ADD] =
        boost::bind(&IFMapStressTest::VirtualMachineNodeAdd, this);
    callbacks_[IFMapSTEventMgr::VM_NODE_DELETE] =
        boost::bind(&IFMapStressTest::VirtualMachineNodeDelete, this);
    callbacks_[IFMapSTEventMgr::VM_SUB] =
        boost::bind(&IFMapStressTest::VirtualMachineSubscribe, this);
    callbacks_[IFMapSTEventMgr::VM_UNSUB] =
        boost::bind(&IFMapStressTest::VirtualMachineUnsubscribe, this);
    callbacks_[IFMapSTEventMgr::OTHER_CONFIG_ADD] =
        boost::bind(&IFMapStressTest::OtherConfigAdd, this);
    callbacks_[IFMapSTEventMgr::OTHER_CONFIG_DELETE] =
        boost::bind(&IFMapStressTest::OtherConfigDelete, this);
    callbacks_[IFMapSTEventMgr::XMPP_READY] =
        boost::bind(&IFMapStressTest::XmppConnect, this);
    callbacks_[IFMapSTEventMgr::XMPP_NOTREADY] =
        boost::bind(&IFMapStressTest::XmppDisconnect, this);
}

IFMapStressTest::EvCb
IFMapStressTest::GetCallback(IFMapStressTest::EventType event) {
    EvCbMap::const_iterator iter = callbacks_.find(event);
    if (iter == callbacks_.end()) {
        return NULL;
    }
    return iter->second;
}

void IFMapStressTest::VerifyConfig() {
    for (ClientIdSet::const_iterator xc_iter = xmpp_connected_.begin();
            xc_iter != xmpp_connected_.end(); ++xc_iter) {
        int client_id = *xc_iter;
        TASK_UTIL_EXPECT_TRUE(
            ConnectedToXmppServer(xmpp_clients_.at(client_id)->name()));
    }
    for (ClientIdSet::const_iterator vrs_iter = vr_subscribed_.begin();
            vrs_iter != vr_subscribed_.end(); ++vrs_iter) {
        int client_id = *vrs_iter;
        string vr_name = xmpp_clients_.at(client_id)->name();
        TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(vr_name) != NULL);
    }
    IFMapTable *vm_table = static_cast<IFMapTable *>(
        db_.FindTable("__ifmap__.virtual_machine.0"));
    assert(vm_table);
    for (int client_id = 0; client_id < config_options_.num_xmpp_clients();
         ++client_id) {
        VmIdSet vtai_set = vm_to_add_ids_.at(client_id);
        for (VmIdSet::const_iterator iter = vtai_set.begin();
             iter != vtai_set.end(); ++iter) {
            string vm_name = VirtualMachineNameCreate(client_id, *iter);
            TASK_UTIL_EXPECT_FALSE(vm_table->FindNode(vm_name) != NULL);
        }
        VmIdSet vtdi_set = vm_to_delete_ids_.at(client_id);
        for (VmIdSet::const_iterator iter = vtdi_set.begin();
             iter != vtdi_set.end(); ++iter) {
            string vm_name = VirtualMachineNameCreate(client_id, *iter);
            TASK_UTIL_EXPECT_TRUE(vm_table->FindNode(vm_name) != NULL);
        }
    }
    for (int client_id = 0; client_id < config_options_.num_xmpp_clients();
         ++client_id) {
        IFMapClient *client =
            ifmap_server_.FindClient(xmpp_clients_.at(client_id)->name());
        if (!client) {
            continue;
        }
        VmIdSet vspi_set = vm_sub_pending_ids_.at(client_id);
        for (VmIdSet::const_iterator iter = vspi_set.begin();
             iter != vspi_set.end(); ++iter) {
            string vm_name = VirtualMachineNameCreate(client_id, *iter);
            TASK_UTIL_EXPECT_FALSE(client->HasAddedVm(vm_name));
        }
        VmIdSet vupi_set = vm_unsub_pending_ids_.at(client_id);
        for (VmIdSet::const_iterator iter = vupi_set.begin();
             iter != vupi_set.end(); ++iter) {
            string vm_name = VirtualMachineNameCreate(client_id, *iter);
            TASK_UTIL_EXPECT_TRUE(client->HasAddedVm(vm_name));
        }
    }
    VerifyNodes();
}

void IFMapStressTest::VerifyNodes() {
    IFMapTable *vr_table = static_cast<IFMapTable *>(
        db_.FindTable("__ifmap__.virtual_router.0"));
    assert(vr_table);
    for (VrNameSet::const_iterator vr_iter = vr_nodes_created_.begin();
            vr_iter != vr_nodes_created_.end(); ++vr_iter) {
        TASK_UTIL_EXPECT_TRUE(vr_table->FindNode(*vr_iter) != NULL);
    }

    IFMapTable *vm_table = static_cast<IFMapTable *>(
        db_.FindTable("__ifmap__.virtual_machine.0"));
    assert(vm_table);
    for (VmNameSet::const_iterator vm_iter = vm_nodes_created_.begin();
            vm_iter != vm_nodes_created_.end(); ++vm_iter) {
        TASK_UTIL_EXPECT_TRUE(vm_table->FindNode(*vm_iter) != NULL);
    }

    IFMapTable *vmi_table = static_cast<IFMapTable *>(
        db_.FindTable("__ifmap__.virtual_machine_interface.0"));
    assert(vmi_table);
    IFMapTable *vn_table = static_cast<IFMapTable *>(
        db_.FindTable("__ifmap__.virtual_network.0"));
    assert(vn_table);
    int client_id = -1, vm_id = -1;
    for (VmNameSet::const_iterator vm_iter = vm_configs_added_names_.begin();
            vm_iter != vm_configs_added_names_.end(); ++vm_iter) {
        VmNameToIds(*vm_iter, &client_id, &vm_id);
        assert(client_id != -1);
        assert(vm_id != -1);
        for (int vmi_id = 0; vmi_id < config_options_.num_vmis(); ++vmi_id) {
            string vmi_name = VMINameCreate(client_id, vm_id, vmi_id);
            TASK_UTIL_EXPECT_TRUE(vmi_table->FindNode(vmi_name) != NULL);

            string vn_name = VirtualNetworkNameCreate(client_id, vm_id, vmi_id);
            TASK_UTIL_EXPECT_TRUE(vn_table->FindNode(vn_name) != NULL);
        }
    }
}

void IFMapStressTest::PrintTestInfo() {
    uint32_t total_client_ignored_events = 0;
    for (int i = 0; i < config_options_.num_xmpp_clients(); ++i) {
        IFMapSTClientCounters counters = client_counters_.at(i);
        cout << "Client " << i << " counters" << endl;
        cout << "\t" << setw(30) << left << "vr_node_adds" << setw(9)
             << right << counters.vr_node_adds << endl;
        cout << "\t" << setw(30) << left << "vr_node_deletes" << setw(9)
             << right << counters.vr_node_deletes << endl;
        cout << "\t" << setw(30) << left << "vr_subscribes" << setw(9)
             << right << counters.vr_subscribes << endl;
        cout << "\t" << setw(30) << left << "vm_node_adds" << setw(9)
             << right << counters.vm_node_adds << endl;
        cout << "\t" << setw(30) << left << "vm_node_deletes" << setw(9)
             << right << counters.vm_node_deletes << endl;
        cout << "\t" << setw(30) << left << "other_cfg_adds" << setw(9)
             << right << counters.other_config_adds << endl;
        cout << "\t" << setw(30) << left << "other_cfg_dels" << setw(9)
             << right << counters.other_config_deletes << endl;
        cout << "\t" << setw(30) << left << "vm_subscribes" << setw(9)
             << right << counters.vm_subscribes << endl;
        cout << "\t" << setw(30) << left << "vm_unsubscribes" << setw(9)
             << right << counters.vm_unsubscribes << endl;
        cout << "\t" << setw(30) << left << "xmpp_connects" << setw(9)
             << right << counters.xmpp_connects << endl;
        cout << "\t" << setw(30) << left << "xmpp_disconnects" << setw(9)
             << right << counters.xmpp_disconnects << endl;
        cout << "\t" << setw(35) << left << "vr_node_deletes_ignored" << setw(9)
             << right << counters.vr_node_deletes_ignored << endl;
        cout << "\t" << setw(35) << left << "vr_subscribes_ignored" << setw(9)
             << right << counters.vr_subscribes_ignored << endl;
        cout << "\t" << setw(35) << left << "vm_node_adds_ignored" << setw(9)
             << right << counters.vm_node_adds_ignored << endl;
        cout << "\t" << setw(35) << left << "vm_node_deletes_ignored" << setw(9)
             << right << counters.vm_node_deletes_ignored << endl;
        cout << "\t" << setw(35) << left << "other_cfg_adds_ignored" << setw(9)
             << right << counters.other_config_adds_ignored << endl;
        cout << "\t" << setw(35) << left << "other_cfg_dels_ignored" << setw(9)
             << right << counters.other_config_deletes_ignored << endl;
        cout << "\t" << setw(35) << left << "vm_subscribes_ignored" << setw(9)
             << right << counters.vm_subscribes_ignored << endl;
        cout << "\t" << setw(35) << left << "vm_unsubscribes_ignored" << setw(9)
             << right << counters.vm_unsubscribes_ignored << endl;
        cout << "\t" << setw(35) << left << "xmpp_connects_ignored" << setw(9)
             << right << counters.xmpp_connects_ignored << endl;
        cout << "\t" << setw(35) << left << "xmpp_disconns_ignored" << setw(9)
             << right << counters.xmpp_disconnects_ignored << endl;
        cout << "\t" << setw(35) << left << "Total ignored events" << setw(9)
             << right << counters.get_ignored_events() << endl;
        total_client_ignored_events += counters.get_ignored_events();
    }
    cout << endl << setw(40) << left << "Events executed"
         << event_generator_.events_executed() << endl;
    cout << setw(40) << left << "Events ignored (client + other)"
         << total_client_ignored_events << " + " << events_ignored_ << " = "
         << (total_client_ignored_events + events_ignored_)
         << endl;
    float events_processed = event_generator_.events_executed() -
                             events_ignored_ - total_client_ignored_events;
    float percentage_processed =
        (events_processed/float(event_generator_.events_executed())) * 100;
    cout << setw(40) << "% processed" << percentage_processed << "%" << endl;
}

string IFMapStressTest::VirtualRouterNameCreate(int id) {
    return "VR_"+ kXMPP_CLIENT_PREFIX + integerToString(id);
}

void IFMapStressTest::VirtualRouterNodeAdd() {
    int client_id = (std::rand() % config_options_.num_xmpp_clients());
    assert(client_id < config_options_.num_xmpp_clients());
    string vr_name = VirtualRouterNameCreate(client_id);

    // Add 2 properties to the VR: id-perms and display-name.
    autogen::IdPermsType *prop1 = new autogen::IdPermsType();
    ifmap_test_util::IFMapMsgNodeAdd(&db_, "virtual-router", vr_name, 0,
                                     "id-perms", prop1);
    autogen::VirtualRouter::StringProperty *prop2 =
        new autogen::VirtualRouter::StringProperty();
    prop2->data = vr_name;
    // If the node has already been added, it will look like a 'change'.
    ifmap_test_util::IFMapMsgNodeAdd(&db_, "virtual-router", vr_name, 0,
                                     "display-name", prop2);

    vr_nodes_created_.insert(vr_name);
    client_counters_.at(client_id).incr_vr_node_adds();
    Log("VR-node-add " + vr_name + ", client id " + integerToString(client_id));
}

void IFMapStressTest::VirtualRouterNodeDelete() {
    int client_id = (std::rand() % config_options_.num_xmpp_clients());
    assert(client_id < config_options_.num_xmpp_clients());
    string vr_name = VirtualRouterNameCreate(client_id);

    // If the node is not created, we are done.
    if (vr_nodes_created_.find(vr_name) == vr_nodes_created_.end()) {
        client_counters_.at(client_id).incr_vr_node_deletes_ignored();
        return;
    }

    // Remove all the properties that were added during node add.
    autogen::IdPermsType *prop1 = new autogen::IdPermsType();
    ifmap_test_util::IFMapMsgNodeDelete(&db_, "virtual-router", vr_name, 0,
                                        "id-perms", prop1);
    autogen::VirtualRouter::StringProperty *prop2 =
        new autogen::VirtualRouter::StringProperty();
    prop2->data = vr_name;
    ifmap_test_util::IFMapMsgNodeDelete(&db_, "virtual-router", vr_name, 0,
                                        "display-name", prop2);

    vr_nodes_created_.erase(vr_name);
    // xxx remove assert
    assert(vr_nodes_created_.find(vr_name) == vr_nodes_created_.end());
    client_counters_.at(client_id).incr_vr_node_deletes();
    Log("VR-node-delete " + vr_name + ", client id "
        + integerToString(client_id));
}

void IFMapStressTest::VirtualRouterSubscribe() {
    // If none of the clients are connected to the xmpp-server, we are done.
    if (xmpp_connected_.empty()) {
        incr_events_ignored();
        return;
    }
    // From the group of connected clients, pick one to send the VR-subscribe.
    int client_id = PickRandomElement(xmpp_connected_);
    xmpp_clients_.at(client_id)->SendConfigSubscribe();
    string vr_name = xmpp_clients_.at(client_id)->name();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(vr_name) != NULL);
    if (ifmap_server_.FindClient(vr_name) != NULL) {
        vr_subscribed_.insert(client_id);
        client_counters_.at(client_id).incr_vr_subscribes();
        Log("VR-sub " + vr_name + ", client id " + integerToString(client_id));
    }
}

// EG: "VM_XmppClient0_VM23"
string IFMapStressTest::VirtualMachineNameCreate(int client_id, int vm_id) {
    return "VM_"+ kXMPP_CLIENT_PREFIX + integerToString(client_id) + "_"
           + "VM" + integerToString(vm_id);
}

// EG: "VM_XmppClient4_VM23". Return 4.
int IFMapStressTest::VmNameToClientId(const string &vm_name) {
    size_t pos = vm_name.find(kXMPP_CLIENT_PREFIX);
    assert(pos != string::npos);
    size_t pos1 = pos + kXMPP_CLIENT_PREFIX.size();
    size_t pos2 = vm_name.find("_", pos1);
    assert(pos2 != string::npos);

    string client_str = vm_name.substr(pos1, pos2 - pos1);
    int client_id = -1;
    bool retb = stringToInteger(client_str, client_id);
    assert(retb);
    assert(client_id != -1);
    return client_id;
}

// EG: "VM_XmppClient0_VM23". Return 23.
int IFMapStressTest::VmNameToVmId(const string &vm_name) {
    string vm_prefix = string("_VM");
    size_t pos1 = vm_name.find(vm_prefix);
    string vm_str = vm_name.substr(pos1 + vm_prefix.size());
    int vm_id = -1;
    bool retb = stringToInteger(vm_str, vm_id);
    assert(retb);
    assert(vm_id != -1);
    return vm_id;
}

// EG: "VM_XmppClient0_VM23". Return 0 and 23.
void IFMapStressTest::VmNameToIds(const string &vm_name, int *client_id,
                                  int *vm_id) {
    *client_id = VmNameToClientId(vm_name);
    *vm_id = VmNameToVmId(vm_name);
}

int IFMapStressTest::GetVmIdToAddNode(int client_id) {
    assert(!vm_to_add_ids_.at(client_id).empty());
    int vm_id = PickRandomElement(vm_to_add_ids_.at(client_id));
    vm_to_add_ids_.at(client_id).erase(vm_id);
    vm_to_delete_ids_.at(client_id).insert(vm_id);
    assert((int)(vm_to_add_ids_.at(client_id).size() +
        vm_to_delete_ids_.at(client_id).size()) == config_options_.num_vms());
    return vm_id;
}

int IFMapStressTest::GetVmIdToDeleteNode(int client_id) {
    assert(!vm_to_delete_ids_.at(client_id).empty());
    int vm_id = PickRandomElement(vm_to_delete_ids_.at(client_id));
    vm_to_delete_ids_.at(client_id).erase(vm_id);
    vm_to_add_ids_.at(client_id).insert(vm_id);
    assert((int)(vm_to_add_ids_.at(client_id).size() +
        vm_to_delete_ids_.at(client_id).size()) == config_options_.num_vms());
    return vm_id;
}

// Take 0x12602d9100000000 and insert client-id in bits 16-32 and vm_id in bits
// 0-31.
uint64_t IFMapStressTest::GetUuidLsLong(int client_id, int vm_id) {
    return kUUID_LSLONG | (client_id << 16) | vm_id;
}

void IFMapStressTest::VirtualMachineNodeAdd() {
    int client_id = (std::rand() % config_options_.num_xmpp_clients());
    assert(client_id < config_options_.num_xmpp_clients());
    // If all the VM nodes are already created, we are done.
    if (vm_to_add_ids_.at(client_id).empty()) {
        client_counters_.at(client_id).incr_vm_node_adds_ignored();
        return;
    }
    // Get a VM id from the to-add group.
    int vm_id = GetVmIdToAddNode(client_id);
    string vm_name = VirtualMachineNameCreate(client_id, vm_id);

    // Add 2 properties to the VM: uuid and display-name.
    autogen::IdPermsType *prop1 = new autogen::IdPermsType();
    prop1->uuid.uuid_mslong = kUUID_MSLONG;
    prop1->uuid.uuid_lslong = GetUuidLsLong(client_id, vm_id);
    ifmap_test_util::IFMapMsgNodeAdd(&db_, "virtual-machine", vm_name, 0,
                                     "id-perms", prop1);
    autogen::VirtualMachine::StringProperty *prop2 =
        new autogen::VirtualMachine::StringProperty();
    prop2->data = vm_name;
    ifmap_test_util::IFMapMsgNodeAdd(&db_, "virtual-machine", vm_name, 0,
                                     "display-name", prop2);

    vm_nodes_created_.insert(vm_name);
    client_counters_.at(client_id).incr_vm_node_adds();
    Log("VM-node-add " + vm_name + ", client id " +
        integerToString(client_id));
}

void IFMapStressTest::VirtualMachineNodeDelete() {
    int client_id = (std::rand() % config_options_.num_xmpp_clients());
    assert(client_id < config_options_.num_xmpp_clients());
    // If there are no VM nodes to delete, we are done.
    if (vm_to_delete_ids_.at(client_id).empty()) {
        client_counters_.at(client_id).incr_vm_node_deletes_ignored();
        return;
    }
    // Get a VM id from the to-delete group. If this VM id has not been created
    // yet, we are done.
    int vm_id = GetVmIdToDeleteNode(client_id);
    string vm_name = VirtualMachineNameCreate(client_id, vm_id);
    if (vm_nodes_created_.find(vm_name) == vm_nodes_created_.end()) {
        client_counters_.at(client_id).incr_vm_node_deletes_ignored();
        return;
    }

    // If other config were added for this vm, delete it.
    if (vm_configs_added_names_.find(vm_name) != vm_configs_added_names_.end()){
        OtherConfigDeleteInternal(vm_name);
    }

    // Remove all the properties that were added during node add.
    autogen::IdPermsType *prop1 = new autogen::IdPermsType();
    prop1->uuid.uuid_mslong = kUUID_MSLONG;
    prop1->uuid.uuid_lslong = GetUuidLsLong(client_id, vm_id);
    ifmap_test_util::IFMapMsgNodeDelete(&db_, "virtual-machine", vm_name, 0,
                                        "id-perms", prop1);
    autogen::VirtualMachine::StringProperty *prop2 =
        new autogen::VirtualMachine::StringProperty();
    prop2->data = vm_name;
    ifmap_test_util::IFMapMsgNodeDelete(&db_, "virtual-machine", vm_name, 0,
                                        "display-name", prop2);

    vm_nodes_created_.erase(vm_name);
    client_counters_.at(client_id).incr_vm_node_deletes();
    assert(vm_nodes_created_.find(vm_name) == vm_nodes_created_.end());
    Log("VM-node-delete " + vm_name + ", client id " +
        integerToString(client_id));
}

int IFMapStressTest::GetVmIdToAddOtherConfig(int client_id) {
    assert(!vm_to_delete_ids_.at(client_id).empty());
    int vm_id = PickRandomElement(vm_to_delete_ids_.at(client_id));
    assert((int)(vm_to_add_ids_.at(client_id).size() +
        vm_to_delete_ids_.at(client_id).size()) == config_options_.num_vms());
    return vm_id;
}

string IFMapStressTest::VMINameCreate(int client_id, int vm_id, int vmi_id) {
    return "VMI_"+ kXMPP_CLIENT_PREFIX + integerToString(client_id) + "_"
           + "VM" + integerToString(vm_id) + "_"
           + "VMI" + integerToString(vmi_id);
}

string IFMapStressTest::VirtualNetworkNameCreate(int client_id, int vm_id,
                                                 int vmi_id) {
    return "VN_"+ kXMPP_CLIENT_PREFIX + integerToString(client_id) + "_"
           + "VM" + integerToString(vm_id) + "_"
           + "VMI" + integerToString(vmi_id);
}

void IFMapStressTest::OtherConfigAdd() {
    int client_id = (std::rand() % config_options_.num_xmpp_clients());
    assert(client_id < config_options_.num_xmpp_clients());
    // If no VM nodes have been added yet, we are done.
    if (vm_to_delete_ids_.at(client_id).empty()) {
        client_counters_.at(client_id).incr_other_config_adds_ignored();
        return;
    }
    int vm_id = GetVmIdToAddOtherConfig(client_id);
    string vm_name = VirtualMachineNameCreate(client_id, vm_id);
    for (int vmi_id = 0; vmi_id < config_options_.num_vmis(); ++vmi_id) {
        // Create the VMI node with uuid and display-name.
        string vmi_name = VMINameCreate(client_id, vm_id, vmi_id);
        autogen::IdPermsType *prop1 = new autogen::IdPermsType();
        ifmap_test_util::IFMapMsgNodeAdd(&db_, "virtual-machine-interface",
                                         vmi_name, 0, "id-perms", prop1);
        autogen::VirtualMachine::StringProperty *prop2 =
            new autogen::VirtualMachine::StringProperty();
        prop2->data = vmi_name;
        ifmap_test_util::IFMapMsgNodeAdd(&db_, "virtual-machine-interface",
                                         vmi_name, 0, "display-name", prop2);
        ifmap_test_util::IFMapMsgLink(&db_, "virtual-machine", vm_name,
            "virtual-machine-interface", vmi_name,
            "virtual-machine-virtual-machine-interface");

        // Create the virtual network node with uuid and display-name.
        string vn_name = VirtualNetworkNameCreate(client_id, vm_id, vmi_id);
        prop1 = new autogen::IdPermsType();
        ifmap_test_util::IFMapMsgNodeAdd(&db_, "virtual-network",
                                         vn_name, 0, "id-perms", prop1);
        prop2 = new autogen::VirtualMachine::StringProperty();
        prop2->data = vn_name;
        ifmap_test_util::IFMapMsgNodeAdd(&db_, "virtual-network",
                                         vn_name, 0, "display-name", prop2);
        ifmap_test_util::IFMapMsgLink(&db_, "virtual-machine-interface",
            vmi_name, "virtual-network", vn_name,
            "virtual-machine-interface-virtual-network");
    }
    vm_configs_added_names_.insert(vm_name);

    client_counters_.at(client_id).incr_other_config_adds();
    Log("Other-cfg-adds, client id " + integerToString(client_id) + ", VM " +
        vm_name);
}

void IFMapStressTest::OtherConfigDelete() {
    if (vm_configs_added_names_.empty()) {
        incr_events_ignored();
        return;
    }

    string vm_name = PickRandomElement(vm_configs_added_names_);
    OtherConfigDeleteInternal(vm_name);
    int client_id = VmNameToClientId(vm_name);
    client_counters_.at(client_id).incr_other_config_deletes();
    Log("Other-cfg-deletes, client id " + integerToString(client_id) + ", VM "
        + vm_name);
}

void IFMapStressTest::OtherConfigDeleteInternal(const string &vm_name) {
    int client_id = -1, vm_id = -1;
    VmNameToIds(vm_name, &client_id, &vm_id);
    assert(client_id != -1);
    assert(vm_id != -1);

    for (int vmi_id = 0; vmi_id < config_options_.num_vmis(); ++vmi_id) {
        string vmi_name = VMINameCreate(client_id, vm_id, vmi_id);

        ifmap_test_util::IFMapMsgUnlink(&db_, "virtual-machine", vm_name,
            "virtual-machine-interface", vmi_name,
            "virtual-machine-virtual-machine-interface");
        autogen::IdPermsType *prop1 = new autogen::IdPermsType();
        ifmap_test_util::IFMapMsgNodeDelete(&db_, "virtual-machine-interface",
                                            vmi_name, 0, "id-perms", prop1);
        autogen::VirtualMachine::StringProperty *prop2 =
            new autogen::VirtualMachine::StringProperty();
        prop2->data = vmi_name;
        ifmap_test_util::IFMapMsgNodeDelete(&db_, "virtual-machine-interface",
                                            vmi_name, 0, "display-name", prop2);

        string vn_name = VirtualNetworkNameCreate(client_id, vm_id, vmi_id);
        ifmap_test_util::IFMapMsgUnlink(&db_, "virtual-machine-interface",
            vmi_name, "virtual-network", vn_name,
            "virtual-machine-interface-virtual-network");
        prop1 = new autogen::IdPermsType();
        ifmap_test_util::IFMapMsgNodeDelete(&db_, "virtual-network",
                                            vn_name, 0, "id-perms", prop1);
        prop2 = new autogen::VirtualMachine::StringProperty();
        prop2->data = vn_name;
        ifmap_test_util::IFMapMsgNodeDelete(&db_, "virtual-network",
                                            vn_name, 0, "display-name", prop2);
    }
    vm_configs_added_names_.erase(vm_name);
}

int IFMapStressTest::GetVmIdToSubscribe(int client_id) {
    assert(!vm_sub_pending_ids_.at(client_id).empty());
    int vm_id = PickRandomElement(vm_sub_pending_ids_.at(client_id));
    vm_sub_pending_ids_.at(client_id).erase(vm_id);
    vm_unsub_pending_ids_.at(client_id).insert(vm_id);
    assert((int)(vm_sub_pending_ids_.at(client_id).size() +
      vm_unsub_pending_ids_.at(client_id).size()) == config_options_.num_vms());
    return vm_id;
}

int IFMapStressTest::GetVmIdToUnsubscribe(int client_id) {
    assert(!vm_unsub_pending_ids_.at(client_id).empty());
    int vm_id = PickRandomElement(vm_unsub_pending_ids_.at(client_id));
    vm_unsub_pending_ids_.at(client_id).erase(vm_id);
    vm_sub_pending_ids_.at(client_id).insert(vm_id);
    assert((int)(vm_sub_pending_ids_.at(client_id).size() +
      vm_unsub_pending_ids_.at(client_id).size()) == config_options_.num_vms());
    return vm_id;
}

void IFMapStressTest::VirtualMachineSubscribe() {
    // If none of the clients have VR-subscribed, we cant VM-subscribe.
    if (vr_subscribed_.empty()) {
        incr_events_ignored();
        return;
    }

    int client_id = PickRandomElement(vr_subscribed_);
    assert(client_id < config_options_.num_xmpp_clients());
    // If all the VMs are already subscribed, we are done.
    if (vm_sub_pending_ids_.at(client_id).empty()) {
        client_counters_.at(client_id).incr_vm_subscribes_ignored();
        return;
    }
    TASK_UTIL_EXPECT_TRUE(
        ifmap_server_.FindClient(xmpp_clients_.at(client_id)->name()) != NULL);
    IFMapClient *client =
        ifmap_server_.FindClient(xmpp_clients_.at(client_id)->name());

    int vm_id = GetVmIdToSubscribe(client_id);
    string vm_name = VirtualMachineNameCreate(client_id, vm_id);
    xmpp_clients_.at(client_id)->SendVmConfigSubscribe(vm_name);
    client_counters_.at(client_id).incr_vm_subscribes();
    Log("VM-sub " + vm_name + ", client id " + integerToString(client_id)
        + ", vm_count " + integerToString(client->VmCount()));
}

void IFMapStressTest::VirtualMachineUnsubscribe() {
    if (vr_subscribed_.empty()) {
        incr_events_ignored();
        return;
    }
    int client_id = PickRandomElement(vr_subscribed_);
    assert(client_id < config_options_.num_xmpp_clients());
    // If all the VMs are already unsubscribed, we are done.
    if (vm_unsub_pending_ids_.at(client_id).empty()) {
        client_counters_.at(client_id).incr_vm_unsubscribes_ignored();
        return;
    }
    TASK_UTIL_EXPECT_TRUE(
        ifmap_server_.FindClient(xmpp_clients_.at(client_id)->name()) != NULL);
    IFMapClient *client =
        ifmap_server_.FindClient(xmpp_clients_.at(client_id)->name());

    int vm_id = GetVmIdToUnsubscribe(client_id);
    string vm_name = VirtualMachineNameCreate(client_id, vm_id);
    xmpp_clients_.at(client_id)->SendVmConfigUnsubscribe(vm_name);
    client_counters_.at(client_id).incr_vm_unsubscribes();
    Log("VM-unsub " + vm_name + ", client id " + integerToString(client_id)
        + ", vm_count " + integerToString(client->VmCount()));
}

int IFMapStressTest::GetXmppDisconnectedClientId() {
    assert(!xmpp_disconnected_.empty());
    int client_id = PickRandomElement(xmpp_disconnected_);
    xmpp_disconnected_.erase(client_id);
    xmpp_connected_.insert(client_id);
    assert((int)(xmpp_connected_.size() + xmpp_disconnected_.size()) ==
           config_options_.num_xmpp_clients());
    return client_id;
}

int IFMapStressTest::GetXmppConnectedClientId() {
    assert(!xmpp_connected_.empty());
    int client_id = PickRandomElement(xmpp_connected_);
    xmpp_connected_.erase(client_id);
    xmpp_disconnected_.insert(client_id);
    assert((int)(xmpp_connected_.size() + xmpp_disconnected_.size()) ==
           config_options_.num_xmpp_clients());
    return client_id;
}

void IFMapStressTest::XmppConnect() {
    // If all the clients are already connected, we are done.
    if (xmpp_disconnected_.empty()) {
        incr_events_ignored();
        return;
    }
    int client_id = GetXmppDisconnectedClientId();
    xmpp_clients_.at(client_id)->RegisterWithXmpp();
    TASK_UTIL_EXPECT_TRUE(
        ConnectedToXmppServer(xmpp_clients_.at(client_id)->name()));
    client_counters_.at(client_id).incr_xmpp_connects();
    Log("Xmpp-connect for client " + integerToString(client_id) +
        ", connected-count " + integerToString(xmpp_connected_.size()) +
        ", disconnected-count " + integerToString(xmpp_disconnected_.size()));
}

void IFMapStressTest::XmppDisconnect() {
    // If all the clients are already disconnected, we are done.
    if (xmpp_connected_.empty()) {
        incr_events_ignored();
        return;
    }
    int client_id = GetXmppConnectedClientId();
    EXPECT_TRUE(ConnectedToXmppServer(xmpp_clients_.at(client_id)->name()));
    xmpp_clients_.at(client_id)->UnRegisterWithXmpp();
    client_counters_.at(client_id).incr_xmpp_disconnects();
    Log("Xmpp-disconnect for client " + integerToString(client_id) +
        ", connected-count " + integerToString(xmpp_connected_.size()) +
        ", disconnected-count " + integerToString(xmpp_disconnected_.size()));
}

bool IFMapStressTest::ConnectedToXmppServer(const string &client_name) {
    XmppConnection *connection = xmpp_server_->FindConnection(client_name);
    if (connection == NULL) {
        return false;
    }
    return (connection->GetStateMcState() == xmsm::ESTABLISHED);
}

void IFMapStressTest::TimeToSleep(uint32_t count, uint32_t total,
                                  uint32_t usec_time) {
    if ((count % total) == 0) {
        usleep(usec_time);
    }
}

string IFMapStressTest::EventToString(IFMapStressTest::EventType event) const {
    return event_generator_.EventToString(event);
}

TEST_F(IFMapStressTest, Noop) {
}

TEST_F(IFMapStressTest, TestEventCreate) {
    IFMapSTEventMgr::EventType event;
    cout << "List of events:" << endl;
    size_t count = 0;
    while (event_generator_.EventAvailable()) {
        event = event_generator_.GetNextEvent();
        cout << EventToString(event) << ", ";
        ++count;
        if (count % 5 == 0) {
            cout << endl;
        }
    }
    if (count % 5) {
        cout << endl;
    }
    TASK_UTIL_EXPECT_EQ(event_generator_.GetEventLogSize(), count);
}

TEST_F(IFMapStressTest, EventsWithSleep) {
    IFMapSTEventMgr::EventType event;
    cout << "List of events:" << endl;
    uint32_t event_count = 0;
    while (event_generator_.EventAvailable()) {
        event = event_generator_.GetNextEvent();
        EvCb callback = GetCallback(event);
        if (callback) {
            cout << "Calling cb for event " << EventToString(event) << endl;
            callback();
        } else {
            cout << "No routine for event " << EventToString(event) << endl;
        }
        TimeToSleep(++event_count);
    }
    TASK_UTIL_EXPECT_EQ((int)event_generator_.GetEventLogSize(),
                        event_generator_.max_events());
    Log("Processed " + integerToString(event_generator_.GetEventLogSize())
        + " events");
}

// Trigger a vm-sub followed by a vm-unsub with no vm-node-add.
TEST_F(IFMapStressTest, VmSubUnsubWithoutNode) {
    uint32_t max_events = event_generator_.max_events();
    uint32_t event_count = 0;
    uint32_t rounds = 0;
    while (event_count < max_events) {
        EvCb callback = GetCallback(IFMapSTEventMgr::XMPP_READY);
        callback();
        event_generator_.LogEvent(IFMapSTEventMgr::XMPP_READY);
        callback = GetCallback(IFMapSTEventMgr::VR_SUB);
        callback();
        event_generator_.LogEvent(IFMapSTEventMgr::VR_SUB);
        callback = GetCallback(IFMapSTEventMgr::VM_SUB);
        callback();
        event_generator_.LogEvent(IFMapSTEventMgr::VM_SUB);
        callback = GetCallback(IFMapSTEventMgr::VM_UNSUB);
        callback();
        event_generator_.LogEvent(IFMapSTEventMgr::VM_UNSUB);
        TimeToSleep(++rounds, 25, 100000);
        event_count += 4;
    }
    WaitForIdle(300);
    TASK_UTIL_EXPECT_EQ(event_generator_.GetEventLogSize(), event_count);
    Log("Processed " + integerToString(event_generator_.GetEventLogSize())
        + " events");
}

static string GetUserName() {
    return string(getenv("LOGNAME"));
}

static void SetUp() {
    std::srand(std::time(0));
    LoggingInit();
    ControlNode::SetDefaultSchedulingPolicy();
}

static void TearDown() {
    task_util::WaitForIdle();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    gargc = argc;
    gargv = const_cast<const char **>(argv);
    for (int i = 0; i < gargc; ++i) {
        cout << "main:Arg " << i << " is " << gargv[i] << endl;
    }
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}

