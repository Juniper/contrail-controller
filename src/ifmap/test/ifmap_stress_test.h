/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __IFMAP_STRESS_TEST_H__
#define __IFMAP_STRESS_TEST_H__

#include <boost/bind.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/function.hpp>
#include <boost/program_options.hpp>
#include <list>

#include "base/logging.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "io/event_manager.h"
#include "ifmap/ifmap_client.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_util.h"

#include "testing/gunit.h"

class IFMapChannelManager;
class IFMapXmppClientMock;
class IFMapServerParser;
class ServerThread;
class XmppServer;

class IFMapSTOptions {
public:
    static const int kDEFAULT_NUM_EVENTS;
    static const int kDEFAULT_NUM_XMPP_CLIENTS;
    static const int kDEFAULT_PER_CLIENT_NUM_VMS;
    static const int kDEFAULT_NUM_VMIS_PER_VM;
    static const int kDEFAULT_WAIT_FOR_IDLE_TIME;
    static const std::string kDEFAULT_EVENT_WEIGHTS_FILE;

    IFMapSTOptions();
    void Initialize();

    int num_events() const;
    int num_xmpp_clients() const;
    int num_vms() const;
    int num_vmis() const;
    int wait_for_idle_time() const;
    const std::string &events_file() const;
    const std::string &event_weight_file() const;

private:
    int num_events_;
    int num_xmpp_clients_;
    int num_vms_;
    int num_vmis_;
    int wait_for_idle_time_;
    std::string event_weight_file_;
    std::string events_file_;
    boost::program_options::options_description desc_;
};

class IFMapSTEventMgr {
public:
    enum EventType {
        VR_NODE_ADD,
        VR_NODE_DELETE,
        VR_SUB,
        VM_NODE_ADD,
        VM_NODE_DELETE,
        VM_SUB,
        VM_UNSUB,
        OTHER_CONFIG_ADD,
        OTHER_CONFIG_DELETE,
        XMPP_READY,
        XMPP_NOTREADY,
        IROND_CONN_DOWN,
        NUM_EVENT_TYPES,         // 12
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
    int max_events() { return max_events_; }
    int events_executed() { return events_executed_; }
    void LogEvent(IFMapSTEventMgr::EventType event);

private:
    void ReadEventsFile(const IFMapSTOptions &config_options);
    void ReadEventWeightsFile(const std::string &filename);

    static const EventTypeMap event_type_map_;
    static const EventStringMap event_string_map_;

    std::list<std::string> file_events_;
    bool events_from_file_;
    int max_events_;
    int events_executed_;
    std::vector<int> event_weights_;
    int event_weights_sum_;
    EventLog event_log_;
};

struct IFMapSTClientCounters {
    IFMapSTClientCounters() :
        vr_node_adds(0), vr_node_deletes(0), vr_node_deletes_ignored(0),
        vr_subscribes(0), vr_subscribes_ignored(0), vm_node_adds(0),
        vm_node_adds_ignored(0), vm_node_deletes(0), vm_node_deletes_ignored(0),
        other_config_adds(0), other_config_adds_ignored(0),
        other_config_deletes(0), other_config_deletes_ignored(0),
        vm_subscribes(0), vm_subscribes_ignored(0), vm_unsubscribes(0),
        vm_unsubscribes_ignored(0), xmpp_connects(0), xmpp_connects_ignored(0),
        xmpp_disconnects(0), xmpp_disconnects_ignored(0) {
    }
    void incr_vr_node_adds() { ++vr_node_adds; }
    void incr_vr_node_deletes() { ++vr_node_deletes; }
    void incr_vr_node_deletes_ignored() { ++vr_node_deletes_ignored; }
    void incr_vr_subscribes() { ++vr_subscribes; }
    void incr_vr_subscribes_ignored() { ++vr_subscribes_ignored; }
    void incr_vm_node_adds() { ++vm_node_adds; }
    void incr_vm_node_adds_ignored() { ++vm_node_adds_ignored; }
    void incr_vm_node_deletes() { ++vm_node_deletes; }
    void incr_vm_node_deletes_ignored() { ++vm_node_deletes_ignored; }
    void incr_other_config_adds() { ++other_config_adds; }
    void incr_other_config_adds_ignored() { ++other_config_adds_ignored; }
    void incr_other_config_deletes() { ++other_config_deletes; }
    void incr_other_config_deletes_ignored() { ++other_config_deletes_ignored; }
    void incr_vm_subscribes() { ++vm_subscribes; }
    void incr_vm_subscribes_ignored() { ++vm_subscribes_ignored; }
    void incr_vm_unsubscribes() { ++vm_unsubscribes; }
    void incr_vm_unsubscribes_ignored() { ++vm_unsubscribes_ignored; }
    void incr_xmpp_connects() { ++xmpp_connects; }
    void incr_xmpp_connects_ignored() { ++xmpp_connects_ignored; }
    void incr_xmpp_disconnects() { ++xmpp_disconnects; }
    void incr_xmpp_disconnects_ignored() { ++xmpp_disconnects_ignored; }
    uint32_t get_ignored_events() {
        return vr_node_deletes_ignored + vr_subscribes_ignored +
               vm_node_adds_ignored + vm_node_deletes_ignored +
               other_config_adds_ignored + other_config_deletes_ignored +
               vm_subscribes_ignored + vm_unsubscribes_ignored +
               xmpp_connects_ignored + xmpp_disconnects_ignored;
    }
    uint32_t vr_node_adds;
    uint32_t vr_node_deletes;
    uint32_t vr_node_deletes_ignored;
    uint32_t vr_subscribes;
    uint32_t vr_subscribes_ignored;
    uint32_t vm_node_adds;
    uint32_t vm_node_adds_ignored;
    uint32_t vm_node_deletes;
    uint32_t vm_node_deletes_ignored;
    uint32_t other_config_adds;
    uint32_t other_config_adds_ignored;
    uint32_t other_config_deletes;
    uint32_t other_config_deletes_ignored;
    uint32_t vm_subscribes;
    uint32_t vm_subscribes_ignored;
    uint32_t vm_unsubscribes;
    uint32_t vm_unsubscribes_ignored;
    uint32_t xmpp_connects;
    uint32_t xmpp_connects_ignored;
    uint32_t xmpp_disconnects;
    uint32_t xmpp_disconnects_ignored;
};

//class IFMapStressTest : public ::testing::TestWithParam<TestParams> {
class IFMapStressTest : public ::testing::Test {
protected:
    static const std::string kXMPP_CLIENT_PREFIX;
    static const uint64_t kUUID_MSLONG = 1361480977053469917;
    // 1324108391240433664 = 0x12602d9100000000
    static const uint64_t kUUID_LSLONG = 1324108391240433664;
    static const std::string kDefaultXmppServerName;
    static const int kMAX_LOG_NUM_EVENTS = 100000; // events in circular buffer
    static const uint32_t kNUM_EVENTS_BEFORE_SLEEP = 100;
    static const uint32_t kSLEEP_TIME_USEC = 50000; // 50ms

    typedef IFMapSTEventMgr::EventType EventType;
    typedef boost::function<void(void)> EvCb;
    typedef std::map<EventType, EvCb> EvCbMap;
    typedef std::set<std::string> VrNameSet;
    typedef std::set<std::string> VmNameSet;
    typedef std::set<int> IntSet;
    typedef IntSet ClientIdSet;
    typedef IntSet VmIdSet;
    typedef std::vector<VmIdSet> PerClientVmIds;
    typedef std::vector<IFMapSTClientCounters> ClientCounters;

    IFMapStressTest();
    void SetUp();
    void TearDown();
    void WaitForIdle(int wait_seconds = 0);
    std::string XmppClientNameCreate(int id);
    void CreateXmppClients();
    void DeleteXmppClients();
    void XmppClientInits();
    std::string VirtualRouterNameCreate(int id);
    void VirtualRouterNodeAdd();
    void VirtualRouterNodeDelete();
    void VirtualRouterSubscribe();
    std::string VirtualMachineNameCreate(int client_id, int vm_id);
    void VirtualMachineNodeAdd();
    void VirtualMachineNodeDelete();
    void VirtualMachineSubscribe();
    void VirtualMachineUnsubscribe();
    int GetXmppDisconnectedClientId();
    int GetXmppConnectedClientId();
    void XmppConnect();
    void XmppDisconnect();
    void SetupEventCallbacks();
    EvCb GetCallback(EventType event);
    std::string EventToString(EventType event) const;
    void VerifyConfig();
    void VerifyNodes();
    bool ConnectedToXmppServer(const std::string &client_name);
    uint64_t GetUuidLsLong(int client_id, int vm_id);

    template<typename SetType>
    SetType PickRandomElement(const std::set<SetType> &client_set);

    int GetVmIdToAddNode(int client_id);
    int GetVmIdToDeleteNode(int client_id);
    int GetVmIdToSubscribe(int client_id);
    int GetVmIdToUnsubscribe(int client_id);
    int GetVmIdToAddOtherConfig(int client_id);
    void Log(std::string log_string);
    void PrintTestInfo();
    std::string VMINameCreate(int client_id, int vm_id, int vmi_id);
    void OtherConfigAdd();
    void OtherConfigDelete();
    void OtherConfigDeleteInternal(const std::string &in_vmname);
    std::string VirtualNetworkNameCreate(int client_id, int vm_id, int vmi_id);
    int VmNameToClientId(const std::string &vm_name);
    int VmNameToVmId(const std::string &vm_name);
    void VmNameToIds(const std::string &vm_name, int *client_id, int *vm_id);
    void incr_events_ignored() { ++events_ignored_; }
    void TimeToSleep(uint32_t count, uint32_t total = kNUM_EVENTS_BEFORE_SLEEP,
                     uint32_t usec_time = kSLEEP_TIME_USEC);

    DB db_;
    DBGraph db_graph_;
    EventManager evm_;
    IFMapServer ifmap_server_;
    IFMapServerParser *parser_;
    std::auto_ptr<ServerThread> thread_;
    XmppServer *xmpp_server_;
    std::auto_ptr<IFMapChannelManager> ifmap_channel_mgr_;
    IFMapSTOptions config_options_;
    IFMapSTEventMgr event_generator_;
    std::vector<IFMapXmppClientMock *> xmpp_clients_;
    VrNameSet vr_nodes_created_; // VR names that have a config node
    VmNameSet vm_nodes_created_; // VM names that have a config node
    EvCbMap callbacks_;
    ClientIdSet xmpp_connected_; // xmpp connected client ids
    ClientIdSet xmpp_disconnected_; // xmpp disconnected client ids
    ClientIdSet vr_subscribed_; // VR subscribed client ids
    PerClientVmIds vm_to_add_ids_; // VM ids that dont have a config node
    PerClientVmIds vm_to_delete_ids_; // VM ids that have a config node
    PerClientVmIds vm_sub_pending_ids_; // VM ids that have not subscribed
    PerClientVmIds vm_unsub_pending_ids_; // VM ids that have subscribed
    VmNameSet vm_configs_added_names_; // VMs that have other config
    boost::circular_buffer<std::string> log_buffer_;
    ClientCounters client_counters_;
    uint32_t events_ignored_;
};

#endif // #define __IFMAP_STRESS_TEST_H__
