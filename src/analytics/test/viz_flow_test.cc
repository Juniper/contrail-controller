/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/random/linear_congruential.hpp>
#include <boost/random/uniform_int.hpp>
#include <boost/generator_iterator.hpp>
#include <boost/lexical_cast.hpp>

#include "base/util.h"
#include "base/parse_object.h"
#include "io/test/event_manager_test.h"

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh_constants.h"
#include "sandesh/sandesh.h"
#include "sandesh/common/flow_types.h"

#include "vizd_test_types.h"
#include "../viz_collector.h"

using namespace boost::asio::ip;
using namespace std;

boost::asio::io_service io_service;

enum { max_length = 1024 };

boost::posix_time::millisec *timeslice;
boost::asio::deadline_timer *timer;
boost::random::minstd_rand rng;         // produces randomness out of thin air
                                    // see pseudo-random number generators
boost::random::mt19937 gen;
boost::random::uniform_int_distribution<> hundred(1,100);
boost::random::uniform_int_distribution<> direction(0,1);

string collector_server = "127.0.0.1";
short collector_port = ContrailPorts::CollectorPort();

class VizCollectorTest : public ::testing::Test {
public:
    virtual void SetUp() {
        evm_.reset(new EventManager());
        thread_.reset(new ServerThread(evm_.get()));
        Sandesh::InitGenerator("VizdTest", "127.0.0.1", "Test", "Test", evm_.get(),
                               0, NULL);
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
        const int num_vns = 2;
        const int num_vms_pervn = 2;
        const int num_ints_pervm = 1;
        std::vector<UveVirtualNetworkAgent> vna(num_vns);
        std::vector<UveVirtualNetworkConfig> vnc(num_vns);
            std::vector<UveInterVnStats> in_stats(10);
            std::vector<UveInterVnStats> out_stats(10);
        std::vector<UveVirtualMachineConfig> vmc(num_vns * num_vms_pervn);
        std::vector<UveVirtualMachineAgent> vma(num_vns * num_vms_pervn);
                std::vector<VmInterfaceAgent> ainterface_list(num_ints_pervm);

        std::vector<UveVirtualNetworkAgent>::iterator vna_it;
        std::vector<UveVirtualNetworkConfig>::iterator vnc_it;
        int i;
        int count = 1;
        while (count-- > 0) {
        for (i = 0, vna_it = vna.begin(), vnc_it = vnc.begin();
                i < num_vns; i++, vna_it++, vnc_it++) {
            std::string stri = static_cast<ostringstream*>( &(ostringstream() << i) )->str();
            vnc_it->set_name("default-domain:admin:vn" + stri);

            std::vector<std::string> connected_networks;
            std::vector<std::string> partially_connected_networks;
            for (int j = 0; j < num_vns; j++) {
                if ((i % 2) == (j % 2)) {
                    std::string strj = static_cast<ostringstream*>( &(ostringstream() << j) )->str();
                    std::string peervn = "default-domain:admin:vn" + strj;
                    connected_networks.push_back(peervn);
                } else {
                    std::string strj = static_cast<ostringstream*>( &(ostringstream() << j) )->str();
                    std::string peervn = "default-domain:admin:vn" + strj;
                    partially_connected_networks.push_back(peervn);
                }
            }
            vnc_it->set_connected_networks(connected_networks);
            vnc_it->set_partially_connected_networks(partially_connected_networks);
            vnc_it->set_total_virtual_machines(num_vms_pervn);
            vnc_it->set_total_interfaces(num_vms_pervn * num_ints_pervm);
            vnc_it->set_total_acl_rules(i);

            vna_it->set_name("default-domain:admin:vn" + stri);
            vna_it->set_total_acl_rules(i);
            vna_it->set_in_tpkts(vna_it->get_in_tpkts()+(i+1)*100);
            vna_it->set_in_bytes(vna_it->get_in_bytes()+(i+1)*1000);
            vna_it->set_out_tpkts(vna_it->get_out_tpkts()+(i+1)*200);
            vna_it->set_out_bytes(vna_it->get_out_bytes()+(i+1)*2000);
            for (int j = 0; j < num_vns; j++) {
                std::string strj = static_cast<ostringstream*>( &(ostringstream() << j) )->str();
                in_stats[j].set_other_vn("default-domain:admin:vn" + strj);
                in_stats[j].set_tpkts(in_stats[j].get_tpkts()+(i+1)*10);
                in_stats[j].set_bytes(in_stats[j].get_bytes()+(i+1)*100);
                out_stats[j].set_other_vn("default-domain:admin:vn" + strj);
                out_stats[j].set_tpkts(out_stats[j].get_tpkts()+(i+1)*20);
                out_stats[j].set_bytes(out_stats[j].get_bytes()+(i+1)*200);
            }
            vna_it->set_in_stats(in_stats);
            vna_it->set_out_stats(out_stats);
            std::vector<std::string> virtualmachine_list;
            std::vector<std::string> interface_list;
            for (int j = 0; j < num_vms_pervn; j++) {
                std::string strj = static_cast<ostringstream*>( &(ostringstream() << j) )->str();
                std::string virtual_machine("default-domain:admin:vn" + stri + "vm" + strj);
                virtualmachine_list.push_back(virtual_machine);

                for (int k = 0; k < num_ints_pervm; k++) {
                    std::string strk = static_cast<ostringstream*>( &(ostringstream() << k) )->str();
                    std::string interface("default-domain:admin:vn" + stri + "vm" + strj + "int" + strk);
                    interface_list.push_back(interface);
                }
            }
            vna_it->set_virtualmachine_list(virtualmachine_list);
            vna_it->set_interface_list(interface_list);

            UveVirtualNetworkConfigTrace::Send(*vnc_it);
            UveVirtualNetworkAgentTrace::Send(*vna_it);
        }

        std::vector<UveVirtualMachineConfig>::iterator vmc_it;
        std::vector<UveVirtualMachineAgent>::iterator vma_it;
        for (i = 0, vmc_it = vmc.begin(), vma_it = vma.begin();
                i < num_vns * num_vms_pervn;
                i++, vmc_it++, vma_it++) {
                std::string stri = static_cast<ostringstream*>( &(ostringstream() << i/num_vms_pervn) )->str();
                std::string strj = static_cast<ostringstream*>( &(ostringstream() << i%2) )->str();
                vmc_it->set_name("default-domain:admin:vn" + stri + "vm" + strj);
                std::vector<VmInterfaceConfig> interface_list(num_ints_pervm);
                std::vector<VmInterfaceConfig>::iterator il_it;
                int k;
                for (k = 0, il_it = interface_list.begin();
                        k < num_ints_pervm;
                        k++, il_it++) {
                    std::string strk = static_cast<ostringstream*>( &(ostringstream() << k) )->str();
                    il_it->set_name("default-domain:admin:vn" + stri + "vm" + strj + "int" + strk);
                }
                vmc_it->set_interface_list(interface_list);

                vma_it->set_name("default-domain:admin:vn" + stri + "vm" + strj);
                std::vector<VmInterfaceAgent>::iterator ail_it;
                for (k = 0, ail_it = ainterface_list.begin();
                        k < num_ints_pervm;
                        k++, ail_it++) {
                    std::string strk = static_cast<ostringstream*>( &(ostringstream() << k) )->str();
                    ail_it->set_name("default-domain:admin:vn" + stri + "vm" + strj + "int" + strk);

                    ail_it->set_ip_address("25"+stri+".1."+ strj + ".1" + strk);
                    ail_it->set_virtual_network("default-domain:admin:vn" + stri);
                    ail_it->set_in_pkts(ail_it->get_in_pkts()+10);
                    ail_it->set_in_bytes(ail_it->get_in_bytes()+100);
                    ail_it->set_out_pkts(ail_it->get_out_pkts()+20);
                    ail_it->set_out_bytes(ail_it->get_out_bytes()+200);
                }
                vma_it->set_interface_list(ainterface_list);

                UveVirtualMachineConfigTrace::Send(*vmc_it);
                UveVirtualMachineAgentTrace::Send(*vma_it);
        }
        usleep(2*1000*1000);
        }

        const int base_src_port = 32768;
        const int dst_ports[] = {80, 443, 22, 23, 20};
        std::string source_vn;
        std::string dest_vn;
        const int repeat_flows = 10;
        FlowDataIpv4 f1[repeat_flows];
        int src_index, dest_index;
        // Send flow setup
        count = 1;
        while (count-- > 0) {
        for (int ix = 0; ix < repeat_flows; ix++) {
            boost::uuids::uuid unm = boost::uuids::random_generator()();
            std::string flowuuid = boost::lexical_cast<std::string>(unm);
            f1[ix].set_flowuuid(flowuuid);
            //f1[ix].set_direction_ing(ix % 2);
            f1[ix].set_direction_ing(direction(gen));
            src_index = (ix/(num_vns*num_vms_pervn*num_ints_pervm)) % (num_vns*num_vms_pervn*num_ints_pervm);
            dest_index = ix % (num_vns*num_vms_pervn*num_ints_pervm);
            source_vn = vma[src_index/num_ints_pervm].get_interface_list()[src_index%num_ints_pervm].get_virtual_network();
            f1[ix].set_sourcevn(source_vn);
            boost::asio::ip::address_v4 source_ip(boost::asio::ip::address_v4::from_string(vma[src_index/num_ints_pervm].get_interface_list()[src_index%num_ints_pervm].get_ip_address()));
            f1[ix].set_sourceip(source_ip.to_ulong());
            dest_vn = vma[dest_index/num_ints_pervm].get_interface_list()[dest_index%num_ints_pervm].get_virtual_network();
            f1[ix].set_destvn(dest_vn);
            boost::asio::ip::address_v4 dest_ip(boost::asio::ip::address_v4::from_string(vma[dest_index/num_ints_pervm].get_interface_list()[dest_index%num_ints_pervm].get_ip_address()));
            f1[ix].set_destip(dest_ip.to_ulong());
            f1[ix].set_protocol(0x11);
            f1[ix].set_sport(base_src_port + ix);
            f1[ix].set_dport(dst_ports[ix % (sizeof(dst_ports)/sizeof(dst_ports[0]))]);
            f1[ix].set_setup_time(UTCTimestampUsec());
            FLOW_DATA_IPV4_OBJECT_SEND(f1[ix]);
            LOG(DEBUG, "Long flowuuid[" << ix << "]: " << unm);
            usleep(50000);
        }
        
        // Send intermediate flow statistics
        boost::random::mt19937 gen;
        boost::random::uniform_int_distribution<> dist(1, 1000);

        const int repeat_flow_stats = 10;
        for (int ix = 0; ix < repeat_flow_stats; ix++) {
            for (int jx = 0; jx < repeat_flows; jx++) {
                // Start with random packet count
                int64_t pkts;
                int64_t bytes;
                if (ix == 0) {
                    f1[jx].__isset.setup_time = false;
                    pkts = dist(gen);
                    bytes = pkts*576;
                    f1[jx].set_bytes(bytes);
                    f1[jx].set_packets(pkts);
                } else {
                    pkts = f1[jx].get_packets();
                    pkts += 100;
                    f1[jx].set_packets(pkts);
                    bytes = pkts*576;
                    f1[jx].set_bytes(bytes);
                }
                FLOW_DATA_IPV4_OBJECT_SEND(f1[jx]);
                usleep(50000);
            }
        }

        // Send flow teardown
        for (int ix = 0; ix < repeat_flows; ix++) {
            int64_t pkts;
            int64_t bytes;
            pkts = f1[ix].get_packets();
            pkts += 100;
            f1[ix].set_packets(pkts);
            bytes = pkts*576;
            f1[ix].set_bytes(bytes);
            f1[ix].set_teardown_time(UTCTimestampUsec());
            FLOW_DATA_IPV4_OBJECT_SEND(f1[ix]);
            usleep(50000);
        }
        usleep(5*1000*1000);
        }

#if 0 
        const int num_src_vns = 10;
        const int num_dst_vns = 10;
        const int base_src_ip = 0x0a010101;
        const int base_dst_ip = 0x14010101;
        const int base_src_port = 32768;
        const int dst_ports[] = {80, 443, 22, 23, 20};
        std::string source_vn;
        std::string dest_vn;
        const int repeat_flows = 100;
        FlowDataIpv4 f1[repeat_flows];

        LOG(DEBUG, "Sending " << repeat_flows << " long lived flows");

        // Send flow setup
        for (int ix = 0; ix < repeat_flows; ix++) {
            boost::uuids::uuid unm = boost::uuids::random_generator()();
            std::string flowuuid = boost::lexical_cast<std::string>(unm);
            f1[ix].set_flowuuid(flowuuid);
            f1[ix].set_direction_ing(ix % 1);
            source_vn = "srcvn" + boost::lexical_cast<std::string>(ix % num_src_vns);
            f1[ix].set_sourcevn(source_vn);
            f1[ix].set_sourceip(base_src_ip + ix);
            dest_vn = "dstvn" + boost::lexical_cast<std::string>(ix % num_dst_vns);
            f1[ix].set_destvn(dest_vn);
            f1[ix].set_destip(base_dst_ip + ix);
            f1[ix].set_protocol(0x11);
            f1[ix].set_sport(base_src_port + ix);
            f1[ix].set_dport(dst_ports[ix % sizeof(dst_ports)]);
            f1[ix].set_setup_time(UTCTimestampUsec());
            FLOW_DATA_IPV4_OBJECT_SEND(f1[ix]);
            LOG(DEBUG, "Long flowuuid[" << ix << "]: " << unm);
            usleep(5000);
        }

        // Send intermediate flow statistics
        boost::random::mt19937 gen;
        boost::random::uniform_int_distribution<> dist(1, 1000);

        const int repeat_flow_stats = 10;
        for (int ix = 0; ix < repeat_flow_stats; ix++) {
            for (int jx = 0; jx < repeat_flows; jx++) {
                // Start with random packet count
                int64_t pkts;
                int64_t bytes;
                if (ix == 0) {
                    pkts = dist(gen);
                    bytes = pkts*576;
                    f1[jx].set_bytes(bytes);
                    f1[jx].set_packets(pkts);
                } else {
                    pkts = f1[jx].get_packets();
                    pkts += 100;
                    f1[jx].set_packets(pkts);
                    bytes = pkts*576;
                    f1[jx].set_bytes(bytes);
                }
                FLOW_DATA_IPV4_OBJECT_SEND(f1[jx]);
                usleep(5000);
            }
            usleep(1000 * 1000);
        }

        // Send flow teardown
        for (int ix = 0; ix < repeat_flows; ix++) {
            int64_t pkts;
            int64_t bytes;
            pkts = f1[ix].get_packets();
            pkts += 100;
            f1[ix].set_packets(pkts);
            bytes = pkts*576;
            f1[ix].set_bytes(bytes);
            f1[ix].set_teardown_time(UTCTimestampUsec());
            FLOW_DATA_IPV4_OBJECT_SEND(f1[ix]);
            usleep(5000);
        }

        LOG(DEBUG, "Sending " << repeat_flows << " short lived flows");
        // Send flow setup, bytes, packets, and teardown
        for (int ix = 0; ix < repeat_flows; ix++) {
            boost::uuids::uuid unm = boost::uuids::random_generator()();
            std::string flowuuid = boost::lexical_cast<std::string>(unm);
            f1[ix].set_flowuuid(flowuuid);
            f1[ix].set_direction_ing(ix % 1);
            source_vn = "short-srcvn" + boost::lexical_cast<std::string>(ix % num_src_vns);
            f1[ix].set_sourcevn(source_vn);
            f1[ix].set_sourceip(base_src_ip + ix);
            dest_vn = "short-dstvn" + boost::lexical_cast<std::string>(ix % num_dst_vns);
            f1[ix].set_destvn(dest_vn);
            f1[ix].set_destip(base_dst_ip + ix);
            f1[ix].set_protocol(0x11);
            f1[ix].set_sport(base_src_port + ix);
            f1[ix].set_dport(dst_ports[ix % sizeof(dst_ports)]);
            f1[ix].set_setup_time(UTCTimestampUsec() - 3 * 1000 * 1000);
            f1[ix].set_teardown_time(UTCTimestampUsec());
            int pkts = dist(gen);
            int bytes = pkts*576;
            f1[ix].set_bytes(bytes);
            f1[ix].set_packets(pkts);
            FLOW_DATA_IPV4_OBJECT_SEND(f1[ix]);
            LOG(DEBUG, "Short flowuuid[" << ix << "]: " << unm);
            usleep(5000);
        }

#endif
        usleep(5000000);
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
    SendMessage();
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

