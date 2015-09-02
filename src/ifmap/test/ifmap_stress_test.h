/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __IFMAP_STRESS_TEST_H__
#define __IFMAP_STRESS_TEST_H__

#include <boost/program_options.hpp>
#include "testing/gunit.h"
#include <list>

class IFMapSTOptions {
public:
    static const int kDEFAULT_NUM_EVENTS;
    static const int kDEFAULT_NUM_XMPP_CLIENTS;
    static const std::string kDEFAULT_EVENT_WEIGHTS_FILE;

    IFMapSTOptions();
    void Initialize();

    int num_events() const;
    int num_xmpp_clients() const;
    const std::string &events_file() const;
    const std::string &event_weight_file() const;

private:
    int num_events_;
    int num_xmpp_clients_;
    std::string event_weight_file_;
    std::string events_file_;
    boost::program_options::options_description desc_;
};

class IFMapSTEventMgr {
public:
    enum EventType {
        VR_NODE_ADD,
        VR_NODE_DELETE,
        VR_ADD_CLIENT_READY,
        CLIENT_READY_VR_ADD,
        VM_NODE_ADD,
        VM_NODE_DELETE,
        VM_SUB,
        VM_UNSUB,
        VM_ADD_VM_SUB,
        VM_SUB_VM_ADD,
        VM_DEL_VM_UNSUB,
        VM_UNSUB_VM_DEL,
        XMPP_READY,
        XMPP_NOTREADY,
        IROND_CONN_DOWN,
        NUM_EVENT_TYPES,         // 15
    };
    typedef std::map<EventType, std::string> EventTypeMap;
    typedef std::map<std::string, EventType> EventStringMap;
    typedef std::vector<std::string> EventLog;
    typedef EventLog::size_type EvLogSz_t;

    IFMapSTEventMgr();
    void Initialize(const IFMapSTOptions &config_options);
    static EventTypeMap InitEventTypeMap();
    static EventStringMap InitEventStringMap();
    bool events_from_file() const;
    std::string EventToString(EventType event) const;
    EventType StringToEvent(std::string event) const;
    EventType GetNextEvent();
    bool EventAvailable() const;
    EvLogSz_t GetEventLogSize() const { return event_log_.size(); }

private:
    void ReadEventsFile(const IFMapSTOptions &config_options);
    void ReadEventWeightsFile(const std::string &filename);

    static const EventTypeMap event_type_map_;
    static const EventStringMap event_string_map_;

    std::list<std::string> file_events_;
    bool events_from_file_;
    int max_events_;
    int events_processed_;
    std::vector<int> event_weights_;
    int event_weights_sum_;
    EventLog event_log_;
};

//class IFMapStressTest : public ::testing::TestWithParam<TestParams> {
class IFMapStressTest : public ::testing::Test {
protected:
    static const std::string kXMPP_CLIENT_PREFIX;
    typedef IFMapSTEventMgr::EventType EventType;
    void SetUp();

    IFMapSTOptions config_options_;
    IFMapSTEventMgr event_generator_;
    std::vector<std::string> xmpp_client_names_;

    void CreateXmppClientNames();
};

#endif // #define __IFMAP_STRESS_TEST_H__
