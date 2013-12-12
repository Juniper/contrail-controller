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
#include "cfg/dns_config.h"
#include "cfg/dns_config_parser.h"
#include "testing/gunit.h"
#include "mgr/dns_mgr.h"
#include "bind/named_config.h"

using namespace std;

class NamedConfigTest : public NamedConfig {
public:
    NamedConfigTest(const char *conf_file, const char *zone_dir) : 
                    NamedConfig(conf_file, zone_dir) {}
    static void Init() {
        assert(singleton_ == NULL);
        singleton_ = new NamedConfigTest("./named.conf", "./");
        singleton_->Reset();
    }
    static void Shutdown() {
        delete singleton_;
        singleton_ = NULL;
        remove("./named.conf");
    }
    virtual void UpdateNamedConf(const VirtualDnsConfig *updated_vdns) {
        CreateNamedConf(updated_vdns);
    }
    std::string GetZoneFileName(const std::string &vdns, 
                                const std::string &name) {
        if (name.size() && name.at(name.size() - 1) == '.')
            return (name + "zone");
        else
            return (name + ".zone");
    }
    std::string GetZoneFilePath(const std::string &vdns, 
                                const string &name) {
         return (zone_file_dir_ + GetZoneFileName("", name));
    }
    std::string GetZoneFilePath(const string &name) {
        return GetZoneFilePath("", name);
    }
    std::string GetResolveFile() { return ""; }
};

static bool FileExists(const char *file) {
	ifstream f(file);
    if (f.is_open()) {
	    return true;
	}
	return false;
}

static string FileRead(const string &filename) {
    ifstream file(filename.c_str());
    string content((istreambuf_iterator<char>(file)),
                   istreambuf_iterator<char>());
    return content;
}

static bool FilesEqual(const char *file1, const char *file2) {
    filebuf *pbuf1, *pbuf2;
    ifstream f1(file1), f2(file2);
    long size1, size2;
    char *buf1, *buf2;
    int a = 0;

    // get pointer to associated buffer object
    pbuf1=f1.rdbuf();
    pbuf2=f2.rdbuf();

    if (f1.is_open()) {
            a++;
    }
    if (f2.is_open()) {
            a++;
    }
    // get file size using buffer's members
    size1=pbuf1->pubseekoff(0,ios::end,ios::in);
    size2=pbuf2->pubseekoff(0,ios::end,ios::in);
    if (size1 != size2) {
        f1.close();
        f2.close();
        return false;
    }
    pbuf1->pubseekpos(0,ios::in);
    pbuf2->pubseekpos(0,ios::in);

    // allocate memory to contain file data
    buf1 = new char[size1];
    buf2 = new char[size2];

    // get file data  
    pbuf1->sgetn(buf1, size1);
    pbuf2->sgetn(buf2, size2);

    f1.close();
    f2.close();

    bool ret = false;
    if (memcmp(buf1, buf2, size2) == 0) {
        ret = true;
    }

    delete[] buf1;
    delete[] buf2;
    return ret;
}

class DnsBindTest : public ::testing::Test {
protected:

    DnsBindTest() : parser_(&db_) {
    }
    virtual void SetUp() {
        IFMapLinkTable_Init(&db_, &db_graph_);
        vnc_cfg_Server_ModuleInit(&db_, &db_graph_);
        NamedConfigTest::Init();
        Dns::SetDnsManager(&dns_manager_);
        dns_manager_.config_mgr_.Initialize(&db_, &db_graph_);
        dns_manager_.bind_status_.named_pid_ = 0;
    }
    virtual void TearDown() {
        task_util::WaitForIdle();
        dns_manager_.bind_status_.named_pid_ = -1;
        dns_manager_.Shutdown();
        NamedConfigTest::Shutdown();
        // dns_manager_.GetConfigManager().Terminate();
        task_util::WaitForIdle();
        db_util::Clear(&db_);
    }
    DB db_;
    DBGraph db_graph_;
    DnsManager dns_manager_;
    DnsConfigParser parser_;
};

namespace {

#if 0
TEST_F(DnsBindTest, Config) {
    string content = FileRead("controller/src/dns/testdata/config_test_1.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    NamedConfigTest *cfg = static_cast<NamedConfigTest *>(NamedConfig::GetNamedConfigObject());

    string dns_domains[] = {
        "contrail.juniper.net",
        "test.example.com",
        "contrail.juniper.com",
        "2.1.1.in-addr.arpa",
        "3.2.1.in-addr.arpa",
        "6.5.4.in-addr.arpa",
        "3.2.2.in-addr.arpa",
    };

    EXPECT_TRUE(FilesEqual(cfg->GetConfFilePath().c_str(),
                "controller/src/dns/testdata/named.conf.1"));
    string s1 = cfg->GetZoneFilePath(dns_domains[0]);
    EXPECT_TRUE(FilesEqual(s1.c_str(), 
                "controller/src/dns/testdata/contrail.juniper.net.zone.1"));
    string s2 = cfg->GetZoneFilePath(dns_domains[1]);
    EXPECT_TRUE(FilesEqual(s2.c_str(), 
                "controller/src/dns/testdata/test.example.com.zone.1"));
    s1 = cfg->GetZoneFilePath(dns_domains[3]);
    EXPECT_TRUE(FilesEqual(s1.c_str(), 
                "controller/src/dns/testdata/2.1.1.in-addr.arpa.zone"));
    for (int i = 0; i < 3; i++) {
        s1 = cfg->GetZoneFilePath(dns_domains[i+4]);
        EXPECT_TRUE(FileExists(s1.c_str()));
    }

    const char config_change[] = "\
<config>\
    <virtual-DNS name='test-DNS' domain='default-domain'>\
        <domain-name>contrail.juniper.com</domain-name>\
        <dynamic-records-from-client>true</dynamic-records-from-client>\
        <record-order>fixed</record-order>\
        <default-ttl-seconds>120</default-ttl-seconds>\
        <next-virtual-DNS>juniper.com</next-virtual-DNS>\
    </virtual-DNS>\
</config>\
";
    EXPECT_TRUE(parser_.Parse(config_change));
    task_util::WaitForIdle();

    EXPECT_TRUE(FilesEqual(cfg->GetConfFilePath().c_str(),
                "controller/src/dns/testdata/named.conf.2"));
    s1 = cfg->GetZoneFilePath(dns_domains[2]);
    EXPECT_TRUE(FilesEqual(s1.c_str(), 
                "controller/src/dns/testdata/contrail.juniper.com.zone.1"));
    s2 = cfg->GetZoneFilePath(dns_domains[1]);
    EXPECT_TRUE(FilesEqual(s2.c_str(), 
                "controller/src/dns/testdata/test.example.com.zone.1"));

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    EXPECT_TRUE(FilesEqual(cfg->GetConfFilePath().c_str(),
                "controller/src/dns/testdata/named.conf.3"));
    for (int i = 0; i < 4; i++) {
        s1 = cfg->GetZoneFilePath(dns_domains[i]);
        EXPECT_FALSE(FileExists(s1.c_str()));
    }
}
#endif

TEST_F(DnsBindTest, Reordered) {
    string content = FileRead("controller/src/dns/testdata/config_test_2.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
    NamedConfigTest *cfg = static_cast<NamedConfigTest *>(NamedConfig::GetNamedConfigObject());

    string dns_domains[] = {
        "contrail.juniper.net",
        "test.example.com",
        "test.juniper.net",
        "test1.juniper.net",
        "192.1.1.in-addr.arpa",
        "193.1.1.in-addr.arpa",
        "13.2.12.in-addr.arpa",
        "0.3.13.in-addr.arpa",
        "1.3.13.in-addr.arpa",
        "2.3.13.in-addr.arpa",
        "3.3.13.in-addr.arpa",
        "3.2.1.in-addr.arpa",
        "6.5.4.in-addr.arpa",
        "64.3.2.2.in-addr.arpa",
        "65.3.2.2.in-addr.arpa",
        "66.3.2.2.in-addr.arpa",
        "67.3.2.2.in-addr.arpa",
    };

    EXPECT_TRUE(FilesEqual(cfg->GetConfFilePath().c_str(),
                "controller/src/dns/testdata/named.conf.4"));
    for (int i = 0; i < 17; i++) {
        string s1 = cfg->GetZoneFilePath(dns_domains[i]);
        EXPECT_TRUE(FileExists(s1.c_str()));
    }

    const char config_change[] = "\
<config>\
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
    for (int i = 0; i < 17; i++) {
        string s1 = cfg->GetZoneFilePath(dns_domains[i]);
        EXPECT_TRUE(FileExists(s1.c_str()));
    }
    string zone = "3.2.25.in-addr.arpa";
    string s1 = cfg->GetZoneFilePath(zone);
    EXPECT_TRUE(FileExists(s1.c_str()));
    EXPECT_TRUE(FilesEqual(cfg->GetConfFilePath().c_str(),
                "controller/src/dns/testdata/named.conf.5"));

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
    <virtual-network-network-ipam ipam='ipam4' vn='vn4'> \
        <ipam-subnets> \
            <subnet> \
                <ip-prefix>13.3.0.0</ip-prefix> \
                <ip-prefix-len>22</ip-prefix-len> \
            </subnet> \
            <default-gateway>13.3.13.254</default-gateway> \
        </ipam-subnets> \
    </virtual-network-network-ipam> \
</config>\
";

    string new_dns_domains[] = {
        "3.2.129.in-addr.arpa",
        "3.2.130.in-addr.arpa",
        "3.2.25.in-addr.arpa",
    };

    EXPECT_TRUE(parser_.Parse(config_change_1));
    task_util::WaitForIdle();
    EXPECT_TRUE(FilesEqual(cfg->GetConfFilePath().c_str(),
                "controller/src/dns/testdata/named.conf.6"));
    for (int i = 0; i < 12; i++) {
        string s1 = cfg->GetZoneFilePath(dns_domains[i]);
        EXPECT_TRUE(FileExists(s1.c_str()));
    }
    for (int i = 0; i < 3; i++) {
        string s1 = cfg->GetZoneFilePath(new_dns_domains[i]);
        EXPECT_TRUE(FileExists(s1.c_str()));
    }
    for (int i = 12; i < 17; i++) {
        string s1 = cfg->GetZoneFilePath(dns_domains[i]);
        EXPECT_FALSE(FileExists(s1.c_str()));
    }

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
                <ip-prefix-len>24</ip-prefix-len> \
            </subnet> \
            <default-gateway>12.2.13.254</default-gateway> \
        </ipam-subnets> \
        <ipam-subnets> \
            <subnet> \
                <ip-prefix>13.3.0.0</ip-prefix> \
                <ip-prefix-len>22</ip-prefix-len> \
            </subnet> \
            <default-gateway>13.3.13.254</default-gateway> \
        </ipam-subnets> \
    </virtual-network-network-ipam> \
</delete>\
";

    EXPECT_TRUE(parser_.Parse(config_change_2));
    task_util::WaitForIdle();
    EXPECT_TRUE(FilesEqual(cfg->GetConfFilePath().c_str(),
                "controller/src/dns/testdata/named.conf.7"));

    const char config_change_3[] = "\
<delete>\
    <virtual-network-network-ipam ipam='ipam4' vn='vn4'> \
        <ipam-subnets> \
            <subnet> \
                <ip-prefix>13.3.0.0</ip-prefix> \
                <ip-prefix-len>22</ip-prefix-len> \
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

    string deleted_domains[] = {
        "3.2.129.in-addr.arpa",
        "3.2.130.in-addr.arpa",
        "13.2.12.in-addr.arpa",
        "0.3.13.in-addr.arpa",
        "1.3.13.in-addr.arpa",
        "2.3.13.in-addr.arpa",
        "3.3.13.in-addr.arpa",
    };

    string remaining_domains[] = {
        "3.2.25.in-addr.arpa",
        "192.1.1.in-addr.arpa",
        "193.1.1.in-addr.arpa",
        "3.2.1.in-addr.arpa",
    };

    EXPECT_TRUE(parser_.Parse(config_change_3));
    task_util::WaitForIdle();
    EXPECT_TRUE(FilesEqual(cfg->GetConfFilePath().c_str(),
                "controller/src/dns/testdata/named.conf.8"));
    for (int i = 0; i < 7; i++) {
        string s1 = cfg->GetZoneFilePath(deleted_domains[i]);
        EXPECT_FALSE(FileExists(s1.c_str()));
    }
    for (int i = 0; i < 4; i++) {
        string s1 = cfg->GetZoneFilePath(remaining_domains[i]);
        EXPECT_TRUE(FileExists(s1.c_str()));
    }

    const char config_change_4[] = "\
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
    EXPECT_TRUE(parser_.Parse(config_change_4));
    task_util::WaitForIdle();

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    for (int i = 0; i < 17; i++) {
        string s1 = cfg->GetZoneFilePath(dns_domains[i]);
        EXPECT_FALSE(FileExists(s1.c_str()));
    }
    for (int i = 0; i < 3; i++) {
        string s1 = cfg->GetZoneFilePath(new_dns_domains[i]);
        EXPECT_FALSE(FileExists(s1.c_str()));
    }
}

}  // namespace

int main(int argc, char **argv) {
    Dns::Init();
    ::testing::InitGoogleTest(&argc, argv);
    int error = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return error;
}
