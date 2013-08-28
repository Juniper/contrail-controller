/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <boost/algorithm/string/replace.hpp>
#include <boost/bind.hpp>

#include "base/logging.h"
#include "base/task.h"
#include "base/test/task_test_util.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_server_table.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/test/ifmap_test_util.h"
#include "io/event_manager.h"
#include "schema/vnc_cfg_types.h"
#include "cmn/dns.h"
#include "bind/bind_util.h"
#include "cfg/dns_config.h"
#include "cfg/dns_config_parser.h"
#include <sandesh/common/vns_types.h>
#include <sandesh/common/vns_constants.h>
#include "testing/gunit.h"

using namespace std;

static string FileRead(const string &filename) {
    ifstream file(filename.c_str());
    string content((istreambuf_iterator<char>(file)),
                   istreambuf_iterator<char>());
    return content;
}

class DnsConfigManagerTest : public ::testing::Test {
protected:
    enum {
        DNS_NETWORK_IPAM,
        DNS_VIRT_DOMAIN,
        DNS_VIRT_DOMAIN_RECORD,
    } DnsNodeType;
#define MAX_NUM_TYPES 3
#define MAX_NUM_OPS 4

    DnsConfigManagerTest()
        : parser_(&db_) {
    }
    virtual void SetUp() {
        IFMapLinkTable_Init(&db_, &db_graph_);
        vnc_cfg_Server_ModuleInit(&db_, &db_graph_);
        config_manager_.Initialize(&db_, &db_graph_);
        Register();
    }
    virtual void TearDown() {
        task_util::WaitForIdle();
        config_manager_.Terminate();
        task_util::WaitForIdle();
        db_util::Clear(&db_);
    }
    void DnsIpamCallback(const Subnet &subnet, const VirtualDnsConfig *c,  
                           DnsConfigManager::EventType ev) {
        counts_[DNS_NETWORK_IPAM][ev]++;
    }
    void VDnsCallback(const VirtualDnsConfig *c, 
                      DnsConfigManager::EventType ev) {
        counts_[DNS_VIRT_DOMAIN][ev]++;
        const VirtualDnsConfig::IpamList &list = c->GetIpamList();
        for (VirtualDnsConfig::IpamList::iterator iter = list.begin();
             iter != list.end(); ++iter) {
            const IpamConfig::VnniList &vnni_list = (*iter)->GetVnniList();
            for (IpamConfig::VnniList::iterator vnni_it = vnni_list.begin();
                 vnni_it != vnni_list.end(); ++vnni_it) {
                Subnets &subnets = (*vnni_it)->GetSubnets();
                counts_[DNS_NETWORK_IPAM][ev] += subnets.size();
            }
        }   
        if (ev == DnsConfigManager::CFG_DELETE)
            counts_[DNS_VIRT_DOMAIN_RECORD][ev] += 
                c->virtual_dns_records_.size();
    }
    void VDnsRecCallback(const VirtualDnsRecordConfig *c, 
                         DnsConfigManager::EventType ev) {
        counts_[DNS_VIRT_DOMAIN_RECORD][ev]++;
    }
    int GetCount(int type, DnsConfigManager::EventType ev) {
        return counts_[type][ev];
    }
    void ClearCounts() {
        for (int i = 0; i < MAX_NUM_TYPES; ++i)
            for (int j = 0; j < MAX_NUM_OPS; ++j)
                counts_[i][j] = 0;
    }
    void Register() {
        DnsConfigManager::Observers obs;
        obs.subnet =
            boost::bind(&DnsConfigManagerTest::DnsIpamCallback, this, _1, _2, _3);
        obs.virtual_dns =
            boost::bind(&DnsConfigManagerTest::VDnsCallback, this, _1, _2);
        obs.virtual_dns_record =
            boost::bind(&DnsConfigManagerTest::VDnsRecCallback, this, _1, _2);
        config_manager_.RegisterObservers(obs);
    }
    DB db_;
    DBGraph db_graph_;
    DnsConfigManager config_manager_;
    DnsConfigParser parser_;
    int counts_[MAX_NUM_TYPES][MAX_NUM_OPS];
};

namespace {

//
// Config parsing tests
//

TEST_F(DnsConfigManagerTest, Config) {
    ClearCounts();
    string content = FileRead("src/dns/testdata/config_test_1.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    EXPECT_EQ(4, GetCount(DNS_NETWORK_IPAM, DnsConfigManager::CFG_ADD));
    EXPECT_EQ(2, GetCount(DNS_VIRT_DOMAIN, DnsConfigManager::CFG_ADD));
    EXPECT_EQ(5, GetCount(DNS_VIRT_DOMAIN_RECORD, DnsConfigManager::CFG_ADD));

    const char config_change[] = "\
<config>\
    <virtual-DNS-record name='record3' dns='test-DNS'>\
        <record-name>host3</record-name>\
        <record-type>A</record-type>\
        <record-class>IN</record-class>\
        <record-data>1.2.8.9</record-data>\
        <record-ttl-seconds>50</record-ttl-seconds>\
    </virtual-DNS-record>\
    <virtual-DNS name='test-DNS' domain='default-domain'>\
        <domain-name>contrail.juniper.com</domain-name>\
        <dynamic-records-from-client>1</dynamic-records-from-client>\
        <record-order>fixed</record-order>\
        <default-ttl-seconds>120</default-ttl-seconds>\
        <next-virtual-DNS>juniper.com</next-virtual-DNS>\
    </virtual-DNS>\
</config>\
";
    EXPECT_TRUE(parser_.Parse(config_change));
    task_util::WaitForIdle();
    EXPECT_EQ(6, GetCount(DNS_VIRT_DOMAIN_RECORD, DnsConfigManager::CFG_ADD));
    EXPECT_EQ(1, GetCount(DNS_VIRT_DOMAIN_RECORD, DnsConfigManager::CFG_DELETE));
    EXPECT_EQ(1, GetCount(DNS_VIRT_DOMAIN, DnsConfigManager::CFG_CHANGE));

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    EXPECT_EQ(6, GetCount(DNS_VIRT_DOMAIN_RECORD, DnsConfigManager::CFG_DELETE));
    EXPECT_EQ(4, GetCount(DNS_NETWORK_IPAM, DnsConfigManager::CFG_DELETE));
    EXPECT_EQ(2, GetCount(DNS_VIRT_DOMAIN, DnsConfigManager::CFG_DELETE));
}

TEST_F(DnsConfigManagerTest, Reordered) {
    ClearCounts();
    string content = FileRead("src/dns/testdata/config_test_2.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    EXPECT_EQ(6, GetCount(DNS_NETWORK_IPAM, DnsConfigManager::CFG_ADD));
    EXPECT_EQ(4, GetCount(DNS_VIRT_DOMAIN, DnsConfigManager::CFG_ADD));
    EXPECT_EQ(8, GetCount(DNS_VIRT_DOMAIN_RECORD, DnsConfigManager::CFG_ADD));

    const char config_change[] = "\
<config>\
    <virtual-DNS-record name='record3' dns='test-DNS'>\
        <record-name>host3</record-name>\
        <record-type>A</record-type>\
        <record-class>IN</record-class>\
        <record-data>1.2.8.9</record-data>\
        <record-ttl-seconds>50</record-ttl-seconds>\
    </virtual-DNS-record>\
    <virtual-DNS name='test-DNS' domain='default-domain'>\
        <domain-name>contrail.juniper.com</domain-name>\
        <dynamic-records-from-client>1</dynamic-records-from-client>\
        <record-order>fixed</record-order>\
        <default-ttl-seconds>120</default-ttl-seconds>\
        <next-virtual-DNS>juniper.com</next-virtual-DNS>\
    </virtual-DNS>\
    <virtual-DNS-record name='last-rec1' dns='last-DNS'>\
        <record-name>host1</record-name>\
        <record-type>A</record-type>\
        <record-class>IN</record-class>\
        <record-data>9.9.9.9</record-data>\
        <record-ttl-seconds>60</record-ttl-seconds>\
    </virtual-DNS-record>\
    <virtual-network-network-ipam ipam='ipam2' vn='vn3'> \
        <ipam-subnets> \
            <subnet> \
                <ip-prefix>2.2.3.64</ip-prefix> \
                <ip-prefix-len>30</ip-prefix-len> \
            </subnet> \
            <default-gateway>2.2.3.254</default-gateway> \
        </ipam-subnets> \
        <ipam-subnets> \
            <subnet> \
                <ip-prefix>25.2.3.0</ip-prefix> \
                <ip-prefix-len>24</ip-prefix-len> \
            </subnet> \
            <default-gateway>25.2.3.254</default-gateway> \
        </ipam-subnets> \
    </virtual-network-network-ipam> \
</config>\
";
    EXPECT_TRUE(parser_.Parse(config_change));
    task_util::WaitForIdle();
    EXPECT_EQ(10, GetCount(DNS_VIRT_DOMAIN_RECORD, DnsConfigManager::CFG_ADD));
    EXPECT_EQ(2, GetCount(DNS_VIRT_DOMAIN_RECORD, DnsConfigManager::CFG_DELETE));
    EXPECT_EQ(1, GetCount(DNS_VIRT_DOMAIN, DnsConfigManager::CFG_CHANGE));
    EXPECT_EQ(7, GetCount(DNS_NETWORK_IPAM, DnsConfigManager::CFG_ADD));
    EXPECT_EQ(0, GetCount(DNS_NETWORK_IPAM, DnsConfigManager::CFG_CHANGE));

    const char config_change_1[] = "\
<config>\
    <virtual-network-network-ipam ipam='ipam1' vn='vn1'> \
        <ipam-subnets> \
            <subnet> \
                <ip-prefix>1.2.3.0</ip-prefix> \
                <ip-prefix-len>24</ip-prefix-len> \
            </subnet> \
            <default-gateway>1.2.3.254</default-gateway> \
        </ipam-subnets> \
    </virtual-network-network-ipam> \
    <virtual-network-network-ipam ipam='ipam4' vn='vn8'> \
        <ipam-subnets> \
            <subnet> \
                <ip-prefix>129.2.3.0</ip-prefix> \
                <ip-prefix-len>24</ip-prefix-len> \
            </subnet> \
            <default-gateway>129.2.3.254</default-gateway> \
        </ipam-subnets> \
        <ipam-subnets> \
            <subnet> \
                <ip-prefix>130.2.3.0</ip-prefix> \
                <ip-prefix-len>24</ip-prefix-len> \
            </subnet> \
            <default-gateway>130.2.3.254</default-gateway> \
        </ipam-subnets> \
    </virtual-network-network-ipam> \
    <virtual-network-network-ipam ipam='ipam2' vn='vn3'> \
        <ipam-subnets> \
            <subnet> \
                <ip-prefix>25.2.3.0</ip-prefix> \
                <ip-prefix-len>24</ip-prefix-len> \
            </subnet> \
            <default-gateway>25.2.3.254</default-gateway> \
        </ipam-subnets> \
    </virtual-network-network-ipam> \
</config>\
";
    EXPECT_TRUE(parser_.Parse(config_change_1));
    task_util::WaitForIdle();
    EXPECT_EQ(9, GetCount(DNS_NETWORK_IPAM, DnsConfigManager::CFG_ADD));
    EXPECT_EQ(2, GetCount(DNS_NETWORK_IPAM, DnsConfigManager::CFG_DELETE));

    const char config_change_2[] = "\
<delete>\
    <virtual-network-network-ipam ipam='ipam4' vn='vn8'> \
        <ipam-subnets> \
            <subnet> \
                <ip-prefix>129.2.3.0</ip-prefix> \
                <ip-prefix-len>24</ip-prefix-len> \
            </subnet> \
            <default-gateway>129.2.3.254</default-gateway> \
        </ipam-subnets> \
        <ipam-subnets> \
            <subnet> \
                <ip-prefix>130.2.3.0</ip-prefix> \
                <ip-prefix-len>24</ip-prefix-len> \
            </subnet> \
            <default-gateway>130.2.3.254</default-gateway> \
        </ipam-subnets> \
    </virtual-network-network-ipam> \
    <virtual-network-network-ipam ipam='ipam4' vn='vn7'> \
        <ipam-subnets> \
            <subnet> \
                <ip-prefix>12.2.13.0</ip-prefix> \
                <ip-prefix-len>28</ip-prefix-len> \
            </subnet> \
            <default-gateway>12.2.13.254</default-gateway> \
        </ipam-subnets> \
        <ipam-subnets> \
            <subnet> \
                <ip-prefix>13.3.13.0</ip-prefix> \
                <ip-prefix-len>28</ip-prefix-len> \
            </subnet> \
            <default-gateway>13.3.13.254</default-gateway> \
        </ipam-subnets> \
    </virtual-network-network-ipam> \
    <network-ipam name='ipam4'> \
        <ipam-method>dhcp</ipam-method> \
        <ipam-dns-method>virtual-dns-server</ipam-dns-method> \
        <ipam-dns-server> \
            <virtual-dns-server-name>last-DNS1</virtual-dns-server-name> \
        </ipam-dns-server> \
        <dhcp-option-list></dhcp-option-list> \
    </network-ipam> \
</delete>\
";
    EXPECT_TRUE(parser_.Parse(config_change_2));
    task_util::WaitForIdle();
    EXPECT_EQ(9, GetCount(DNS_NETWORK_IPAM, DnsConfigManager::CFG_ADD));
    EXPECT_EQ(6, GetCount(DNS_NETWORK_IPAM, DnsConfigManager::CFG_DELETE));

    const char config_change_3[] = "\
<config>\
    <network-ipam name='ipam1'> \
        <ipam-method>dhcp</ipam-method> \
        <ipam-dns-method>virtual-dns-server</ipam-dns-method> \
        <ipam-dns-server> \
            <virtual-dns-server-name>test-DNS</virtual-dns-server-name> \
        </ipam-dns-server> \
        <dhcp-option-list></dhcp-option-list> \
    </network-ipam> \
</config>\
";
    EXPECT_TRUE(parser_.Parse(config_change_3));
    task_util::WaitForIdle();
    EXPECT_EQ(11, GetCount(DNS_NETWORK_IPAM, DnsConfigManager::CFG_ADD));
    EXPECT_EQ(8, GetCount(DNS_NETWORK_IPAM, DnsConfigManager::CFG_DELETE));

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    EXPECT_EQ(11, GetCount(DNS_NETWORK_IPAM, DnsConfigManager::CFG_DELETE));
    EXPECT_EQ(4, GetCount(DNS_VIRT_DOMAIN, DnsConfigManager::CFG_DELETE));
    EXPECT_EQ(10, GetCount(DNS_VIRT_DOMAIN_RECORD, DnsConfigManager::CFG_DELETE));
    ClearCounts();
}

}  // namespace

int main(int argc, char **argv) {
    Dns::Init();
    ::testing::InitGoogleTest(&argc, argv);
    int error = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return error;
}
