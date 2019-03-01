/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <sys/socket.h>

#include <net/if.h>

#ifdef __linux__
#include <linux/netlink.h>
#include <linux/if_tun.h>
#include <linux/if_packet.h>
#endif

#ifdef __FreeBSD__
#include <sys/sockio.h>
#include <ifaddrs.h>
#endif

#include "testing/gunit.h"

#include <base/logging.h>
#include <io/event_manager.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "cfg/cfg_init.h"
#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "vrouter/ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "oper/vrouter.h"
#include "filter/acl.h"
#include "test_cmn_util.h"
#include "vr_types.h"
#include <controller/controller_export.h>
#include <ksync/ksync_sock_user.h>
#include <boost/assign/list_of.hpp>

struct AddressRangeEntry {
    std::string start;
    std::string end;
    AddressRangeEntry(const string &first, const string &last) :
        start(first), end(last) {
    }
};

struct SubnetEntry {
    std::string addr;
    int plen;
    SubnetEntry(const string &ip, int len) :
        addr(ip), plen(len) {
    }
};

class VrouterTest : public ::testing::Test {
public:
    VrouterTest() {
        agent = Agent::GetInstance();
    }
    virtual ~VrouterTest() { }

    virtual void SetUp() {
        bgp_peer = CreateBgpPeer(Ip4Address(1), "BGP Peer1");
    }

    virtual void TearDown() {
        DeleteBgpPeer(bgp_peer);
    }


    void AddVrouter(const char *name, int id) {
        AddNode("virtual-router", name, id, "");
    }

    void AddIpam(const char *name, int id, const char *subnet, int plen) {
        ostringstream str;
        char buff[8192];

        sprintf(buff,
                "<network-ipam-mgmt>\n "
                      "<ipam-dns-method>default-dns-server</ipam-dns-method>\n "
                "</network-ipam-mgmt>\n "
                "<ipam-subnet-method>flat-subnet</ipam-subnet-method>\n "
                "<ipam-subnets>\n "
                      "<subnets>\n "
                            "<subnet>\n "
                                  "<ip-prefix>%s</ip-prefix>\n "
                                  "<ip-prefix-len>%d</ip-prefix-len>\n "
                            "</subnet>\n "
                            "<default-gateway>12.1.0.1</default-gateway>\n "
                            "<dns-server-address>12.1.0.2</dns-server-address>\n "
                            "<enable-dhcp>true</enable-dhcp>\n "
                      "</subnets>\n "
                "</ipam-subnets>\n ", subnet, plen);
        AddNode("network-ipam", name, id, buff);
    }

    void AddVrouterIpam(const char *vr_name, const char *ipam,
                        std::vector<AddressRangeEntry> rlist,
                        std::vector<SubnetEntry> slist) {
        char buff[10240];
        int len = 0;

        AddXmlHdr(buff, len);

        char node_name[256];
        sprintf(node_name, "%s,%s", ipam, vr_name);

        std::stringstream str;
        str << "       <node type=\"virtual-router-network-ipam\">\n";
        str << "           <name>" << node_name << "</name>\n";
        str << "           <value>\n";
        std::vector<AddressRangeEntry>::iterator it = rlist.begin();
        while (it != rlist.end()) {
            str << "           <allocation-pools>\n";
            str << "               <start>" << it->start  << "</start>\n";
            str << "               <end>" << it->end << "</end>\n";
            str << "           </allocation-pools>\n";
            ++it;
        }
        std::vector<SubnetEntry>::iterator sit = slist.begin();
        while (sit != slist.end()) {
            str << "          <subnet>\n";
            str << "              <ip-prefix>" << sit->addr << "</ip-prefix>\n";
            str << "              <ip-prefix-len>" << sit->plen << "</ip-prefix-len>\n";
            str << "          </subnet>\n";
            ++sit;
        }
        str << "           </value>\n";
        str << "       </node>\n";
        std::string final_str = str.str();
        memcpy(buff + len, final_str.data(), final_str.size());
        len += final_str.size();

        AddXmlTail(buff, len);
        ApplyXmlString(buff);
    }

    void AddVrouterIpamLink(const char *vr_name, const char *ipam) {
        char buff[10240];
        int len = 0;

        char node_name[256];
        sprintf(node_name, "%s,%s", ipam, vr_name);

        AddXmlHdr(buff, len);
        LinkString(buff, len, "virtual-router", vr_name,
                      "virtual-router-network-ipam", node_name);

        AddXmlTail(buff, len);
        ApplyXmlString(buff);
    }

    void DeleteVrouterIpamLink(const char *vr_name, const char *ipam) {
        char node_name[256];
        sprintf(node_name, "%s,%s", ipam, vr_name);
        DelLink("virtual-router", vr_name, "virtual-router-network-ipam",
                node_name);
    }

    void DeleteVrouterIpam(const char *vr_name, const char *ipam) {
        char node_name[256];
        sprintf(node_name, "%s,%s", ipam, vr_name);
        DelNode("virtual-router-network-ipam", node_name);
    }

    Agent *agent;
    BgpPeer *bgp_peer;
};

/* Verify that VRouter oper object is updated with subnet information configured
 * in virtual-router-network-ipam object which is linked to virtual-router
 * object */
TEST_F(VrouterTest, vrouter_pool1) {
    std::vector<AddressRangeEntry> rlist;
    AddressRangeEntry aentry("12.0.1.10", "12.0.1.20");
    rlist.push_back(aentry);

    std::vector<SubnetEntry> slist;
    SubnetEntry sentry("12.0.1.0", 24);
    slist.push_back(sentry);

    AddVrouter("vr1", 1);
    AddIpam("ipam1", 11, "12.0.0.0", 16);
    AddVrouterIpam("vr1", "ipam1", rlist, slist);
    AddVrouterIpamLink("vr1", "ipam1");
    client->WaitForIdle();

    VRouter *vr = agent->oper_db()->vrouter();
    WAIT_FOR(100, 1000, (vr->SubnetCount() == 1));

    DeleteVrouterIpamLink("vr1", "ipam1");
    client->WaitForIdle();

    WAIT_FOR(100, 1000, (vr->SubnetCount() == 0));
    client->WaitForIdle();

    DeleteVrouterIpam("vr1", "ipam1");
    DelNode("network-ipam", "ipam1");
    DelNode("virtual-router", "vr1");
    client->WaitForIdle();
}

/* Verify that subnet routes are added and removed from fabric_vrf as and when
 * subnets get added and removed from VRouter oper object */
TEST_F(VrouterTest, vrouter_pool2) {
    std::vector<AddressRangeEntry> rlist;
    AddressRangeEntry aentry("12.0.1.10", "12.0.1.20");
    rlist.push_back(aentry);

    std::vector<SubnetEntry> slist;
    SubnetEntry sentry("12.0.1.0", 24);
    slist.push_back(sentry);

    AddVrouter("vr1", 1);
    AddIpam("ipam1", 11, "12.0.0.0", 16);
    AddVrouterIpam("vr1", "ipam1", rlist, slist);
    AddVrouterIpamLink("vr1", "ipam1");
    client->WaitForIdle();

    VRouter *vr = agent->oper_db()->vrouter();
    WAIT_FOR(100, 1000, (vr->SubnetCount() == 1));

    Ip4Address addr = Ip4Address::from_string("12.0.1.0");
    WAIT_FOR(1000, 1000, (RouteFind(agent->fabric_vrf_name().c_str(),
                                    addr, 24) == true));

    DeleteVrouterIpamLink("vr1", "ipam1");
    client->WaitForIdle();

    WAIT_FOR(100, 1000, (vr->SubnetCount() == 0));
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (RouteFind(agent->fabric_vrf_name().c_str(),
                                    addr, 24) == false));

    DeleteVrouterIpam("vr1", "ipam1");
    DelNode("network-ipam", "ipam1");
    DelNode("virtual-router", "vr1");
    client->WaitForIdle();
}

int main(int argc, char **argv) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init, false, false, false);

    int ret = RUN_ALL_TESTS();

    usleep(10000);
    client->WaitForIdle();
    usleep(100000);
    TestShutdown();
    delete client;

    return ret;
}
