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
#include "db/db.h"
#include "io/event_manager.h"

#include "sandesh/common/vns_types.h"
#include "sandesh/sandesh_types.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"

#include <iostream>
#include <fstream>

using namespace std;
namespace opt = boost::program_options;

// Globals
static const char **gargv;
static int gargc;

#define IFMAP_STRESS_TEST_LOG(str)                               \
do {                                                             \
    log4cplus::Logger logger = log4cplus::Logger::getRoot();     \
    LOG4CPLUS_DEBUG(logger, "IFMAP_STRESS_TEST_LOG: "            \
                    << __FILE__  << ":"  << __FUNCTION__ << "()" \
                    << ":"  << __LINE__ << " " << str);          \
} while (false)

const int IFMapSTOptions::kDEFAULT_NUM_EVENTS = 50;
const int IFMapSTOptions::kDEFAULT_NUM_XMPP_CLIENTS = 5;
const std::string IFMapSTOptions::kDEFAULT_EVENT_WEIGHTS_FILE=
        "controller/src/ifmap/testdata/ifmap_event_weights.txt";

const std::string IFMapStressTest::kXMPP_CLIENT_PREFIX = "XmppClient";

IFMapSTOptions::IFMapSTOptions() :
        num_events_(kDEFAULT_NUM_EVENTS),
        num_xmpp_clients_(kDEFAULT_NUM_XMPP_CLIENTS),
        event_weight_file_(kDEFAULT_EVENT_WEIGHTS_FILE),
        desc_("Configuration options") {
    Initialize();
}

void IFMapSTOptions::Initialize() {
    string ncli_xmpp_msg = "Number of XMPP clients (default "
        + integerToString(IFMapSTOptions::kDEFAULT_NUM_XMPP_CLIENTS) + ")";
    string nevents_msg = "Number of events (default "
        + integerToString(IFMapSTOptions::kDEFAULT_NUM_EVENTS) + ")";
    desc_.add_options()
        ("Help", "produce help message")
        ("nclients-xmpp", opt::value<int>(), ncli_xmpp_msg.c_str())
        ("nevents", opt::value<int>(), nevents_msg.c_str())
        ("events-file", opt::value<string>(), "Events filename")
        ("event-weight-file", opt::value<string>(), "Event weights filename")
        ;

    opt::variables_map var_map;
    opt::store(opt::parse_command_line(gargc, gargv, desc_), var_map);
    opt::notify(var_map);

    if (var_map.count("Help")) {
        std::cout << desc_ << std::endl;
        exit(0);
    }
    if (var_map.count("nclients-xmpp")) {
        num_xmpp_clients_ = var_map["nclients-xmpp"].as<int>();
    }
    if (var_map.count("nevents")) {
        num_events_ = var_map["nevents"].as<int>();
    }
    if (var_map.count("events-file")) {
        events_file_ = var_map["events-file"].as<string>();
    }
    if (var_map.count("event-weight-file")) {
        event_weight_file_ = var_map["event-weight-file"].as<string>();
    }
}

const std::string &IFMapSTOptions::events_file() const {
    return events_file_;
}

const std::string &IFMapSTOptions::event_weight_file() const {
    return event_weight_file_;
}

int IFMapSTOptions::num_events() const {
    return num_events_;
}

int IFMapSTOptions::num_xmpp_clients() const {
    return num_xmpp_clients_;
}

IFMapSTEventMgr::IFMapSTEventMgr() :
        events_from_file_(false), max_events_(0), events_processed_(0),
        event_weights_sum_(0) {
}

inline bool IFMapSTEventMgr::events_from_file() const {
    return events_from_file_;
}

void IFMapSTEventMgr::ReadEventsFile(const IFMapSTOptions &config_options) {
    std::string filename = config_options.events_file();
    if (filename.empty()) {
        max_events_ = config_options.num_events();
        return;
    }
    ifstream file(filename.c_str());
    std::string event;

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
    std::cout << file_events_.size() << " events read from file" << std::endl;
    events_from_file_ = true;
    max_events_ = file_events_.size();
}

void IFMapSTEventMgr::ReadEventWeightsFile(const std::string &filename) {
    ifstream file(filename.c_str());
    std::string line;
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
    std::cout << "event_weights_sum_ " << event_weights_sum_ << std::endl;
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
    evmap[VR_ADD_CLIENT_READY] = "VR_ADD_CLIENT_READY";
    evmap[CLIENT_READY_VR_ADD] = "CLIENT_READY_VR_ADD";
    evmap[VM_NODE_ADD] = "VM_NODE_ADD";
    evmap[VM_NODE_DELETE] = "VM_NODE_DELETE";
    evmap[VM_SUB] = "VM_SUB";
    evmap[VM_UNSUB] = "VM_UNSUB";
    evmap[VM_ADD_VM_SUB] = "VM_ADD_VM_SUB";
    evmap[VM_SUB_VM_ADD] = "VM_SUB_VM_ADD";
    evmap[VM_DEL_VM_UNSUB] = "VM_DEL_VM_UNSUB";
    evmap[VM_UNSUB_VM_DEL] = "VM_UNSUB_VM_DEL";
    evmap[XMPP_READY] = "XMPP_READY";
    evmap[XMPP_NOTREADY] = "XMPP_NOTREADY";
    evmap[IROND_CONN_DOWN] = "IROND_CONN_DOWN";
    assert((evmap.size() == NUM_EVENT_TYPES) &&
           "event_type_map_ not initialized properly");
    return evmap;
}

const IFMapSTEventMgr::EventTypeMap IFMapSTEventMgr::event_type_map_ =
    IFMapSTEventMgr::InitEventTypeMap();

std::string
IFMapSTEventMgr::EventToString(IFMapSTEventMgr::EventType event) const {
    return event_type_map_.at(event);
}

// String to Type
IFMapSTEventMgr::EventStringMap IFMapSTEventMgr::InitEventStringMap() {
    IFMapSTEventMgr::EventStringMap evmap;
    evmap["VR_NODE_ADD"] = VR_NODE_ADD;
    evmap["VR_NODE_DELETE"] = VR_NODE_DELETE;
    evmap["VR_ADD_CLIENT_READY"] = VR_ADD_CLIENT_READY;
    evmap["CLIENT_READY_VR_ADD"] = CLIENT_READY_VR_ADD;
    evmap["VM_NODE_ADD"] = VM_NODE_ADD;
    evmap["VM_NODE_DELETE"] = VM_NODE_DELETE;
    evmap["VM_SUB"] = VM_SUB;
    evmap["VM_UNSUB"] = VM_UNSUB;
    evmap["VM_ADD_VM_SUB"] = VM_ADD_VM_SUB;
    evmap["VM_SUB_VM_ADD"] = VM_SUB_VM_ADD;
    evmap["VM_DEL_VM_UNSUB"] = VM_DEL_VM_UNSUB;
    evmap["VM_UNSUB_VM_DEL"] = VM_UNSUB_VM_DEL;
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
IFMapSTEventMgr::StringToEvent(std::string event) const {
    return event_string_map_.at(event);
}

bool IFMapSTEventMgr::EventAvailable() const {
    return (events_processed_ < max_events_ ? true : false);
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
    ++events_processed_;

    std::string event_log = "Event " + EventToString(event);
    event_log_.push_back(event_log);

    return event;
}

void IFMapStressTest::CreateXmppClientNames() {
    for (int i = 0; i < config_options_.num_xmpp_clients(); ++i) {
        xmpp_client_names_.push_back(kXMPP_CLIENT_PREFIX + integerToString(0));
    }
}

void IFMapStressTest::SetUp() {
    event_generator_.Initialize(config_options_);
    CreateXmppClientNames();
}

TEST_F(IFMapStressTest, Noop) {
}

TEST_F(IFMapStressTest, TestEventCreate) {
    IFMapSTEventMgr::EventType event;
    std::cout << "List of events:" << std::endl;
    size_t count = 0;
    while (event_generator_.EventAvailable()) {
        event = event_generator_.GetNextEvent();
        std::cout << event_generator_.EventToString(event) << ", ";
        ++count;
        if (count % 5 == 0) {
            std::cout << std::endl;
        }
    }
    if (count % 5) {
        std::cout << std::endl;
    }
    TASK_UTIL_EXPECT_EQ(event_generator_.GetEventLogSize(), count);
}

static void SetUp() {
    std::srand(std::time(0));
    LoggingInit();
    ControlNode::SetDefaultSchedulingPolicy();
}

static void TearDown() {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    gargc = argc;
    gargv = const_cast<const char **>(argv);
    for (int i = 0; i < gargc; ++i) {
        std::cout << "main:Arg " << i << " is " << gargv[i] << std::endl;
    }
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}

