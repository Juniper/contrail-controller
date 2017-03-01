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
    NamedConfigTest(const std::string &conf_dir, const std::string &conf_file) :
                    NamedConfig(conf_dir, conf_file, "/var/log/named/bind.log",
                                "rndc.conf", "xvysmOR8lnUQRBcunkC6vg==", "100M") {}
    static void Init() {
        assert(singleton_ == NULL);
        singleton_ = new NamedConfigTest(".", "named.conf");
        singleton_->Reset();
    }
    static void Shutdown() {
        delete singleton_;
        singleton_ = NULL;
        remove("./named.conf");
        remove("./rndc.conf");
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
         return (named_config_dir_ + GetZoneFileName("", name));
    }
    std::string GetZoneFilePath(const string &name) {
        return GetZoneFilePath("", name);
    }
    std::string GetResolveFile() { return ""; }
};

static string FileRead(const string &filename) {
    ifstream file(filename.c_str());
    string content((istreambuf_iterator<char>(file)),
                   istreambuf_iterator<char>());
    return content;
}

class DnsManagerTest : public ::testing::Test {
protected:

    DnsManagerTest() : parser_(&db_) {
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

// TODO: to be updated

namespace {
TEST_F(DnsManagerTest, Update) {
    string content = FileRead("controller/src/dns/testdata/config_test_1.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

    const char config_change1[] = "\
<config>\
    <virtual-DNS-record name='record3' dns='test-DNS'>\
        <record-name>host3</record-name>\
        <record-type>A</record-type>\
        <record-class>IN</record-class>\
        <record-data>1.2.8.9</record-data>\
        <record-ttl-seconds>50</record-ttl-seconds>\
    </virtual-DNS-record>\
</config>\
";
    EXPECT_TRUE(parser_.Parse(config_change1));
    task_util::WaitForIdle();

    const char config_change2[] = "\
<config>\
    <virtual-DNS-record name='record3' dns='test-DNS'>\
        <record-name>host3</record-name>\
        <record-type>A</record-type>\
        <record-class>IN</record-class>\
        <record-data>1.2.3.6</record-data>\
        <record-ttl-seconds>100</record-ttl-seconds>\
    </virtual-DNS-record>\
</config>\
";
    EXPECT_TRUE(parser_.Parse(config_change2));
    task_util::WaitForIdle();

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
}

TEST_F(DnsManagerTest, Reordered) {
    string content = FileRead("controller/src/dns/testdata/config_test_2.xml");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();

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
    <domain name='last-domain'>\
        <project-limit>20</project-limit>\
        <virtual-network-limit>30</virtual-network-limit>\
        <security-group-limit>200</security-group-limit>\
    </domain>\
</config>\
";
    EXPECT_TRUE(parser_.Parse(config_change));
    task_util::WaitForIdle();

    boost::replace_all(content, "<config>", "<delete>");
    boost::replace_all(content, "</config>", "</delete>");
    EXPECT_TRUE(parser_.Parse(content));
    task_util::WaitForIdle();
}

}  // namespace

int main(int argc, char **argv) {
    Dns::Init();
    ::testing::InitGoogleTest(&argc, argv);
    int error = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return error;
}
