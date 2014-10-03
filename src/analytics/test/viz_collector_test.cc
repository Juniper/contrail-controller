/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/random/linear_congruential.hpp>
#include <boost/random/uniform_int.hpp>
#include <boost/generator_iterator.hpp>

#include "base/util.h"
#include "base/parse_object.h"
#include "base/contrail_ports.h"
#include "io/test/event_manager_test.h"

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>

#include "../viz_collector.h"
#include "viz_collector_test_types.h"
#include "vizd_test_types.h"

using namespace boost::asio::ip;
using namespace std;

boost::asio::io_service io_service;

enum { max_length = 1024 };

boost::posix_time::millisec *timeslice;
boost::asio::deadline_timer *timer;
boost::random::minstd_rand rng;         // produces randomness out of thin air
                                    // see pseudo-random number generators
boost::random::uniform_int_distribution<> hundred(1,100);

string sourcehost = "127.0.0.1";

string collector_server = "127.0.0.1";
short collector_port = ContrailPorts::CollectorPort();

class VizCollectorTest : public ::testing::Test {
public:
    virtual void SetUp() {
        evm_.reset(new EventManager());
        thread_.reset(new ServerThread(evm_.get()));
        Sandesh::InitGenerator("VizdTest", sourcehost, "Test", "Test", evm_.get(),
                               8080, NULL);
        Sandesh::ConnectToCollector(collector_server, collector_port);
    }

    virtual void TearDown() {
        Sandesh::Uninit();
        evm_->Shutdown();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
    }

    void SendMessage()
      {

        LOG(DEBUG, "Inside SendMessage");

        SANDESH_ASYNC_TEST1_SEND(100, "sat1string100");
        SANDESH_ASYNC_TEST1_SEND(101, "sat1string101");
        SANDESH_ASYNC_TEST1_SEND(102, "sat1string102");

        SAT2_struct s1;
        s1.f1 = "sat2string100";
        s1.f2 = 100;
        SANDESH_ASYNC_TEST2_SEND(s1, 100);
        s1.f1 = "sat2string101";
        s1.f2 = 101;
        SANDESH_ASYNC_TEST2_SEND(s1, 101);

        SAT3_struct s3;
        s3.f1 = "sat3string1";
        s3.f2 = 1;
        SANDESH_ASYNC_TEST3_SEND(s3, 1);
        s3.f1 = "sat3string2";
        s3.f2 = 2;
        SANDESH_ASYNC_TEST3_SEND(s3, 2);

        LOG(DEBUG, "Ending SendMessage");
      }

    void SendVNTable1() {
        LOG(DEBUG, "Inside SendVNTable");
        UveVirtualNetworkConfig uvevn;
        uvevn.set_name("abc-corp:vn02");
        uvevn.set_total_interfaces(10);
        uvevn.set_total_virtual_machines(5);
        uvevn.set_total_acl_rules(60);

        vector<string> vcn;
//        vcn.push_back("map-reduce-02");
        uvevn.set_connected_networks(vcn);
        UveVirtualNetworkConfigTrace::Send(uvevn);

        UveVirtualNetworkAgent uvena;
        uvena.set_name("abc-corp:vn02");
        uvena.set_in_tpkts(40);
        uvena.set_total_acl_rules(55);

        std::vector<UveInterVnStats> vvn;
        UveInterVnStats vnstat;
        vnstat.set_other_vn("abc-corp:map-reduce-02");
        vnstat.set_tpkts(10);
        vnstat.set_bytes(1200);
        vvn.push_back(vnstat);
//        vnstat.set_other_vn("abc-corp:map-reduce-03");
//        vnstat.set_tpkts(30);
//        vnstat.set_bytes(5000);
//        vvn.push_back(vnstat);

        uvena.set_in_stats(vvn);
        UveVirtualNetworkAgentTrace::Send(uvena);
        
        UveVirtualNetworkAgent uvena2;
        uvena2.set_name("abc-corp:vn02");
        uvena2.set_deleted(true);
        UveVirtualNetworkAgentTrace::Send(uvena2);
        while (true) { usleep(5000); }
        LOG(DEBUG, "Ending SendVNTable1");
    }

    void SendVNTable2() {
        LOG(DEBUG, "Inside SendVNTable");
        UveVirtualNetworkConfig uvevn;
        uvevn.set_name("abc-corp:vn02");
        uvevn.set_total_interfaces(15);
        uvevn.set_total_virtual_machines(8);
        uvevn.set_total_acl_rules(60);

        vector<string> vcn;
        vcn.push_back("map-reduce-02");
        vcn.push_back("map-reduce-04");
        uvevn.set_connected_networks(vcn);

        UveVirtualNetworkConfigTrace::Send(uvevn);

        UveVirtualNetworkAgent uvena;
        uvena.set_name("abc-corp:vn02");
        uvena.set_in_tpkts(30);
        uvena.set_total_acl_rules(60);

        std::vector<UveInterVnStats> vvn;
        UveInterVnStats vnstat;
        vnstat.set_other_vn("abc-corp:map-reduce-02");
        vnstat.set_tpkts(20);
        vnstat.set_bytes(1800);
        vvn.push_back(vnstat);
//        vnstat.set_other_vn("abc-corp:map-reduce-03");
//        vnstat.set_tpkts(30);
//        vnstat.set_bytes(5000);
//        vvn.push_back(vnstat);

        uvena.set_in_stats(vvn);
        UveVirtualNetworkAgentTrace::Send(uvena);

        LOG(DEBUG, "Ending SendVNTable2");
    }

    std::auto_ptr<ServerThread> thread_;
    std::auto_ptr<EventManager> evm_;
};

TEST_F(VizCollectorTest, Test1) {
    thread_->Start();
#if 0
    int loop_count = 0;
    while (!Sandesh::SessionConnected()) {
        if (loop_count++ > 2000) {
            LOG(DEBUG, "Couldn't connect to the Collector...");
            break;
        }
        usleep(5000);
    }
#endif
    usleep(1000000);
    SendMessage();
    SendVNTable1();
    usleep(500000);
    sourcehost = "10.1.1.1";
}

TEST_F(VizCollectorTest, Test2) {
    thread_->Start();
    Sandesh::SetLoggingParams(true, "Test2", SandeshLevel::SYS_INFO, true);
    usleep(1000000);
    SendVNTable2();
    usleep(500000);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

