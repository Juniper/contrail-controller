/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_test_init_h
#define vnsw_agent_test_init_h

#include <sys/socket.h>
#include <net/if.h>
#if defined(__linux__)
#include <linux/netlink.h>
#include <linux/if_tun.h>
#include <linux/if_packet.h>
#include <netinet/ether.h>
#endif
#include <pthread.h>

#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/program_options.hpp>
#include <pugixml/pugixml.hpp>
#include <base/task.h>
#include <base/task_trigger.h>
#include <io/event_manager.h>
#include <base/util.h>

#include "xmpp/xmpp_channel.h"

#include <cmn/agent_cmn.h>
#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <cfg/cfg_listener.h>
#include "cfg/cfg_mirror.h"

#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include "controller/controller_peer.h"
#include <controller/controller_vrf_export.h>
#include <services/services_init.h>
#include <ksync/ksync_init.h>
#include <ksync/vnswif_listener.h>
#include <ifmap/ifmap_agent_parser.h>
#include <ifmap/ifmap_agent_table.h>
#include <cmn/agent_param.h>
#include <oper/vn.h>
#include <oper/multicast.h>
#include <oper/vm.h>
#include <oper/interface_common.h>
#include <oper/route_common.h>
#include <oper/vrf_assign.h>
#include <oper/sg.h>
#include <uve/stats_collector.h>
#include <uve/agent_uve.h>
#include <uve/flow_stats_collector.h>
#include <uve/agent_stats_collector.h>
#include <uve/test/agent_stats_collector_test.h>
#include "pkt_gen.h"
#include "pkt/flow_table.h"
#include "pkt/agent_stats.h"
#include "testing/gunit.h"
#include "kstate/kstate.h"
#include "pkt/pkt_init.h"
#include "kstate/test/test_kstate.h"
#include "sandesh/sandesh_http.h"
#include "xmpp/test/xmpp_test_util.h"
#include <oper/agent_path.h>
#include <controller/controller_route_path.h>

#include "test_agent_init.h"
using namespace std;

#define TUN_INTF_CLONE_DEV "/dev/net/tun"
#define DEFAULT_VNSW_CONFIG_FILE "controller/src/vnsw/agent/test/vnswa_cfg.ini"

#define GETUSERARGS()                           \
    bool ksync_init = false;                    \
    char init_file[1024];                       \
    memset(init_file, '\0', sizeof(init_file)); \
    ::testing::InitGoogleTest(&argc, argv);     \
    namespace opt = boost::program_options;     \
    opt::options_description desc("Options");   \
    opt::variables_map vm;                      \
    desc.add_options()                          \
        ("help", "Print help message")          \
        ("config", opt::value<string>(), "Specify Init config file")  \
        ("kernel", "Run with vrouter")          \
        ("headless", "Run headless vrouter");   \
    opt::store(opt::parse_command_line(argc, argv, desc), vm); \
    opt::notify(vm);                            \
    if (vm.count("help")) {                     \
        cout << "Test Help" << endl << desc << endl; \
        exit(0);                                \
    }                                           \
    if (vm.count("kernel")) {                   \
        ksync_init = true;                      \
    }                                           \
    if (vm.count("config")) {                   \
        strncpy(init_file, vm["config"].as<string>().c_str(), (sizeof(init_file) - 1) ); \
    } else {                                    \
        strcpy(init_file, DEFAULT_VNSW_CONFIG_FILE); \
    }                                           \

#define HEADLESS_MODE vm.count("headless")

struct PortInfo {
    char name[32];
    int intf_id;
    char addr[32];
    char mac[32];
    int vn_id;
    int vm_id;
};

struct FlowIp {
    uint32_t sip;
    uint32_t dip;
    char vrf[32];
};

class FlowFlush : public Task {
public:
    FlowFlush() : Task((TaskScheduler::GetInstance()->GetTaskId("FlowFlush")), 0) {
    }
    virtual bool Run() {
        Agent::GetInstance()->pkt()->flow_table()->DeleteAll();
        return true;
    }
};

class FlowAge : public Task {
public:
    FlowAge() : Task((TaskScheduler::GetInstance()->GetTaskId("FlowAge")), 0) {
    }
    virtual bool Run() {
        Agent::GetInstance()->uve()->flow_stats_collector()->Run();
        return true;
    }
};

struct IpamInfo {
    char ip_prefix[32];
    int plen;
    char gw[32];
    bool dhcp_enable;
};

struct TestIp4Prefix {
    Ip4Address addr_;
    int plen_;
};

class TestClient {
public:
    TestClient(Agent *agent) : agent_(agent), param_(agent) {
        vn_notify_ = 0;
        vm_notify_ = 0;
        port_notify_ = 0;
        port_del_notify_ = 0;
        vm_del_notify_ = 0;
        vn_del_notify_ = 0;
        vrf_del_notify_ = 0;
        cfg_notify_ = 0;
        acl_notify_ = 0;
        vrf_notify_ = 0;
        mpls_notify_ = 0;
        nh_notify_ = 0;
        mpls_del_notify_ = 0;
        nh_del_notify_ = 0;
        shutdown_done_ = false;
        comp_nh_list_.clear();
    };
    virtual ~TestClient() { };

    void VrfNotify(DBTablePartBase *partition, DBEntryBase *e) {
        vrf_notify_++;
        if (e->IsDeleted()) {
            vrf_del_notify_++;
        }
    };

    void AclNotify(DBTablePartBase *partition, DBEntryBase *e) {
        acl_notify_++;
    };

    void VnNotify(DBTablePartBase *partition, DBEntryBase *e) {
        vn_notify_++;
        if (e->IsDeleted()) {
            vn_del_notify_++;
        }
    };

    void VmNotify(DBTablePartBase *partition, DBEntryBase *e) {
        vm_notify_++;
        if (e->IsDeleted()) {
            vm_del_notify_++;
        }
    };

    void PortNotify(DBTablePartBase *partition, DBEntryBase *e) {
        const Interface *intf = static_cast<const Interface *>(e);
        port_notify_++;
        if (intf->IsDeleted()) {
            port_del_notify_++;
        }
    };

    void CompositeNHNotify(DBTablePartBase *partition, DBEntryBase *e) {
        const NextHop *nh = static_cast<const NextHop *>(e);
        if (nh->GetType() != NextHop::COMPOSITE) 
            return;
        nh_notify_++;
        std::vector<const NextHop *>::iterator it = 
            std::find(comp_nh_list_.begin(), comp_nh_list_.end(), nh);
        if (e->IsDeleted()) {
            nh_del_notify_++;
            if (it != comp_nh_list_.end())
                comp_nh_list_.erase(it);
        } else { 
            if (it == comp_nh_list_.end()) {
                comp_nh_list_.push_back(nh);
            }
        }
    };

    void MplsNotify(DBTablePartBase *partition, DBEntryBase *e) {
        mpls_notify_++;
        if (e->IsDeleted()) {
            mpls_del_notify_++;
        }
    };

    void CfgNotify(DBTablePartBase *partition, DBEntryBase *e) {
        cfg_notify_++;
    };

    static void WaitForIdle(int wait_seconds = 1) {
        static const int kTimeout = 1000;
        int i;
        int count = ((wait_seconds * 1000000) / kTimeout);
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        for (i = 0; i < count; i++) {
            if (scheduler->IsEmpty()) {
                break;
            }
            usleep(kTimeout);
        }
        EXPECT_GT(count, i);
    }

    void VrfReset() {vrf_notify_ = 0;};
    void AclReset() {acl_notify_ = 0;};
    void VnReset() {vn_notify_ = 0;};
    void VmReset() {vm_notify_ = 0;};
    void PortReset() {port_notify_ = port_del_notify_ = 0;};
    void NextHopReset() {nh_notify_ = nh_del_notify_ = 0;};
    void CompositeNHReset() {comp_nh_list_.clear();};
    void MplsReset() {mpls_notify_ = mpls_del_notify_ = 0;};
    void Reset() {vrf_notify_ = acl_notify_ = port_notify_ = vn_notify_ = 
        vm_notify_ = cfg_notify_ = port_del_notify_ =  
        vm_del_notify_ = vn_del_notify_ = vrf_del_notify_ = 0;};
    uint32_t acl_notify() { return acl_notify_;}

    void NotifyCfgWait(int cfg_count) {
        int i = 0;

        while (cfg_notify_ != cfg_count) {
            if (i++ < 25) {
                usleep(1000);
            } else {
                break;
            }
        }
    };

    void IfStatsTimerWait(int count) {
        int i = 0;

        AgentStatsCollectorTest *collector = static_cast<AgentStatsCollectorTest *>
            (Agent::GetInstance()->uve()->agent_stats_collector());
        while (collector->interface_stats_responses_ < count) {
            if (i++ < 1000) {
                usleep(1000);
            } else {
                break;
            }
        }
        EXPECT_TRUE(collector->interface_stats_responses_ >= count);
        WaitForIdle(2);
    }

    void VrfStatsTimerWait(int count) {
        int i = 0;

        AgentStatsCollectorTest *collector = static_cast<AgentStatsCollectorTest *>
            (Agent::GetInstance()->uve()->agent_stats_collector());
        while (collector->vrf_stats_responses_ <= count) {
            if (i++ < 1000) {
                usleep(1000);
            } else {
                break;
            }
        }
        EXPECT_TRUE(collector->vrf_stats_responses_ >= count);
        WaitForIdle(2);
    }

    void DropStatsTimerWait(int count) {
        int i = 0;

        AgentStatsCollectorTest *collector = static_cast<AgentStatsCollectorTest *>
            (Agent::GetInstance()->uve()->agent_stats_collector());
        while(collector->drop_stats_responses_ <= count) {
            if (i++ < 1000) {
                usleep(1000);
            } else {
                break;
            }
        }
        EXPECT_TRUE(collector->drop_stats_responses_ >= count);
        WaitForIdle(2);
    }

    void KStateResponseWait(int count) {
        int i = 0;

        while (TestKStateBase::handler_count_ < count) {
            if (i++ < 100) {
                usleep(1000);
            } else {
                break;
            }
        }
        EXPECT_TRUE(TestKStateBase::handler_count_ >= count);
        WaitForIdle();
    }

    bool AclNotifyWait(int count) {
        int i = 0;

        while (acl_notify_ != count) {
            if (i++ < 25) {
                usleep(1000);
            } else {
                break;
            }
        }

        WaitForIdle();
        EXPECT_EQ(count, acl_notify_);
        return (acl_notify_ == count);
    }

    bool VrfNotifyWait(int count) {
        int i = 0;

        while (vrf_notify_ != count) {
            if (i++ < 25) {
                usleep(1000);
            } else {
                break;
            }
        }

        WaitForIdle();
        EXPECT_EQ(count, vrf_notify_);
        return (vrf_notify_ == count);
    }

    bool VnNotifyWait(int vn_count) {
        int i = 0;

        while (vn_notify_ != vn_count) {
            if (i++ < 25) {
                usleep(1000);
            } else {
                break;
            }
        }

        WaitForIdle();
        EXPECT_EQ(vn_count, vn_notify_);
        return (vn_notify_ == vn_count);
    }

    bool VmNotifyWait(int vm_count) {
        int i = 0;

        while (vm_notify_ != vm_count) {
            if (i++ < 25) {
                usleep(1000);
            } else {
                break;
            }
        }

        WaitForIdle();
        EXPECT_EQ(vm_count, vm_notify_);
        return (vm_notify_ == vm_count);
    }

    bool PortNotifyWait(int port_count) {
        int i = 0;

        while (port_notify_ < port_count) {
            if (i++ < 25) {
                usleep(10000);
            } else {
                break;
            }
        }

        WaitForIdle();
        EXPECT_GE(port_notify_, port_count);
        return (port_notify_ >= port_count);
    }

    bool PortDelNotifyWait(int port_count) {
        int i = 0;

        while (port_del_notify_ != port_count) {
            if (i++ < 100) {
                usleep(10000);
            } else {
                break;
            }
        }

        WaitForIdle(2);
        EXPECT_EQ(port_count, port_del_notify_);
        return (port_del_notify_ == port_count);
    }
    
    bool VmDelNotifyWait(int count) {
        int i = 0;

        while (vm_del_notify_ != count) {
            if (i++ < 100) {
                usleep(10000);
            } else {
                break;
            }
        }

        WaitForIdle();
        EXPECT_EQ(count, vm_del_notify_);
        return (vm_del_notify_ == count);
    }

    bool VnDelNotifyWait(int count) {
        int i = 0;

        while (vn_del_notify_ != count) {
            if (i++ < 100) {
                usleep(10000);
            } else {
                break;
            }
        }

        WaitForIdle();
        EXPECT_EQ(count, vn_del_notify_);
        return (vn_del_notify_ == count);
    }

    bool VrfDelNotifyWait(int count) {
        int i = 0;

        while (vrf_del_notify_ != count) {
            if (i++ < 100) {
                usleep(10000);
            } else {
                break;
            }
        }

        WaitForIdle();
        EXPECT_EQ(count, vrf_del_notify_);
        return (vrf_del_notify_ == count);
    }

    size_t CompositeNHCount() {return comp_nh_list_.size();};
    bool CompositeNHDelWait(int nh_count) {
        WAIT_FOR(100, 10000, (nh_del_notify_ >= nh_count));
        return (nh_del_notify_ >= nh_count);
    }

    bool CompositeNHWait(int nh_count) {
        WAIT_FOR(100, 10000, (nh_notify_ >= nh_count));
        return (nh_notify_ >= nh_count);
    }

    bool MplsDelWait(int mpls_count) {
        WAIT_FOR(100, 10000, (mpls_del_notify_ >= mpls_count));
        return (mpls_del_notify_ >= mpls_count);
    }

    bool MplsWait(int mpls_count) {
        WAIT_FOR(100, 10000, (mpls_notify_ >= mpls_count));
        return (mpls_notify_ >= mpls_count);
    }
    
    bool NotifyWait(int port_count, int vn_count, int vm_count) {
        bool vn_ret = VnNotifyWait(vn_count);
        bool vm_ret = VmNotifyWait(vm_count);
        bool port_ret = PortNotifyWait(port_count);

        return (vn_ret && vm_ret && port_ret);
    };

    void SetFlowFlushExclusionPolicy() {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        TaskPolicy policy;
        policy.push_back(TaskExclusion(scheduler->GetTaskId("Agent::StatsCollector")));
        policy.push_back(TaskExclusion(scheduler->GetTaskId("Agent::FlowHandler")));
        policy.push_back(TaskExclusion(scheduler->GetTaskId("Agent::KSync")));
        scheduler->SetPolicy(scheduler->GetTaskId("FlowFlush"), policy);
    }

    void EnqueueFlowFlush() {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        FlowFlush *task = new FlowFlush();
        scheduler->Enqueue(task);
    }

    void SetFlowAgeExclusionPolicy() {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        TaskPolicy policy;
        policy.push_back(TaskExclusion(scheduler->GetTaskId("Agent::StatsCollector")));
        policy.push_back(TaskExclusion(scheduler->GetTaskId("Agent::FlowHandler")));
        policy.push_back(TaskExclusion(scheduler->GetTaskId("Agent::KSync")));
        scheduler->SetPolicy(scheduler->GetTaskId("FlowAge"), policy);
    }

    void EnqueueFlowAge() {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        FlowAge *task = new FlowAge();
        scheduler->Enqueue(task);
    }

    void Init() {
        Agent::GetInstance()->interface_config_table()->Register(boost::bind(&TestClient::CfgNotify, 
                                                   this, _1, _2));
        Agent::GetInstance()->vn_table()->Register(boost::bind(&TestClient::VnNotify, 
                                                   this, _1, _2));
        Agent::GetInstance()->vm_table()->Register(boost::bind(&TestClient::VmNotify, 
                                                   this, _1, _2));
        Agent::GetInstance()->interface_table()->Register(boost::bind(&TestClient::PortNotify, 
                                                   this, _1, _2));
        Agent::GetInstance()->acl_table()->Register(boost::bind(&TestClient::AclNotify, 
                                                   this, _1, _2));
        Agent::GetInstance()->vrf_table()->Register(boost::bind(&TestClient::VrfNotify, 
                                                   this, _1, _2));
        Agent::GetInstance()->nexthop_table()->Register(boost::bind(&TestClient::CompositeNHNotify,
                                                   this, _1, _2));
        Agent::GetInstance()->mpls_table()->Register(boost::bind(&TestClient::MplsNotify, 
                                                   this, _1, _2));
    };
    TestAgentInit *agent_init() { return &agent_init_; }
    AgentParam *param() { return &param_; }
    Agent *agent() { return agent_; }
    void set_agent(Agent *agent) { agent_ = agent; }
    void delete_agent() {
        delete agent_;
        agent_ = NULL;
    }

    void Shutdown();

    bool shutdown_done_;
    int vn_notify_;
    int vm_notify_;
    int port_notify_;
    int port_del_notify_;
    int vm_del_notify_;
    int vn_del_notify_;
    int vrf_del_notify_;
    int cfg_notify_;
    int acl_notify_;
    int vrf_notify_;
    int nh_del_notify_;
    int mpls_del_notify_;
    int nh_notify_;
    int mpls_notify_;
    std::vector<const NextHop *> comp_nh_list_;
    Agent *agent_;
    TestAgentInit agent_init_;
    AgentParam param_;
};

TestClient *TestInit(const char *init_file = NULL, bool ksync_init = false, 
                     bool pkt_init = true, bool services_init = true,
                     bool uve_init = true,
                     int agent_stats_interval = AgentParam::AgentStatsInterval,
                     int flow_stats_interval = FlowStatsCollector::FlowStatsInterval,
                     bool asio = true, bool ksync_sync_mode = true);

TestClient *VGwInit(const string &init_file, bool ksync_init);
void TestShutdown();
TestClient *StatsTestInit();

extern TestClient *client;

#endif // vnsw_agent_test_init_h
