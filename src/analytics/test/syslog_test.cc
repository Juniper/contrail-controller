/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"
#include "db_handler_mock.h"

#include "../syslog_collector.cc"
#include "base/task.h"
#include "base/test/task_test_util.h"
#include "io/test/event_manager_test.h"
#include "analytics/parser_util.h"

using ::testing::Field;
using ::testing::StrEq;
using ::testing::Pointee;
using ::testing::_;

TtlMap ttl_map = g_viz_constants.TtlValuesDefault;

class SyslogParserTestHelper : public SyslogParser
{
    public:
        SyslogParserTestHelper() {}
        virtual void MakeSandesh (syslog_m_t v) {
            for (syslog_m_t::iterator i = v.begin(); i != v.end(); ++i) {
                v_.insert(std::pair<std::string, Holder>(i->first,
                    i->second));
            }
            ip_ = GetMapVals(v, "ip", "");
            ts_ = GetMapVal(v, "timestamp", 0);
            module_ = GetModule(v);
            hostname_ = GetMapVals(v, "hostname", ip_);
            severity_ = GetMapVal(v, "severity", 0);
            facility_ = GetFacility(v);
            pid_ = GetPID(v);
            body_ = "<Syslog>" + GetMsgBody (v) + "</Syslog>";
        }

        bool TestParse(std::string s) {
            syslog_m_t v;
            bool r;
            if ((r = parse_syslog (s.begin(), s.end(), v))) {
                v.insert(std::pair<std::string, Holder>("ip",
                      Holder("ip", "10.0.0.42")));
                PostParsing(v);
                MakeSandesh(v);
            }
            return r;
        }

        std::string ip() { return ip_; }
        int64_t     ts() { return ts_; }
        std::string module() { return module_; }
        std::string hostname() { return hostname_; }
        int         severity() { return severity_; }
        std::string facility() { return facility_; }
        int         pid() { return pid_; }
        std::string body() { return body_; }
    private:
        syslog_m_t  v_;
        std::string ip_;
        int64_t     ts_;
        std::string module_;
        std::string hostname_;
        int         severity_;
        std::string facility_;
        int         pid_;
        std::string body_;
};

class SyslogMsgGen : public UdpServer
{
  public:
    explicit SyslogMsgGen(boost::asio::io_service *io_service, int snd_port) :
                UdpServer(io_service) {
        boost::system::error_code ec;
        ep_ = udp::endpoint(boost::asio::ip::address::from_string("127.0.0.1",
            ec), snd_port);
    }

    void Send (std::string snd, udp::endpoint to)
    {
        mutable_buffer send = AllocateBuffer (snd.length());
        char *p = buffer_cast<char *>(send);
        std::copy(snd.begin(), snd.end(), p);
        UDP_UT_LOG_DEBUG( "SyslogMsgGen sending '" << snd << "' to " << to);
        StartSend(to, snd.length(), send);
    }
    void Send (std::string snd, std::string ipaddress, short port)
    {
        boost::system::error_code ec;
        Send (snd, udp::endpoint(boost::asio::ip::address::from_string(
                        ipaddress, ec), port));
    }
    void Send (std::string snd)
    {
        Send (snd, ep_);
    }
    void HandleSend (boost::asio::const_buffer send_buffer,
            udp::endpoint remote_endpoint, std::size_t bytes_transferred,
            const boost::system::error_code& error)
    {
        UDP_UT_LOG_DEBUG("SyslogMsgGen sent " << bytes_transferred << "(" <<
            error << ")\n");
        DeallocateBuffer(send_buffer);
    }
  private:
    udp::endpoint ep_;
};

class SyslogCollectorTest : public ::testing::Test
{
    public:
    void AssertVizMsg(const VizMsg *vmsgp,
        GenDb::GenDbIf::DbAddColumnCb db_cb) {
        EXPECT_STREQ(vmsgp->msg->GetMessageType().c_str(), "Syslog");
    }
    protected:
    virtual void SetUp() {
        evm_.reset(new EventManager());
        Options::Cassandra cassandra_options;
        cassandra_options.cassandra_ips_.push_back("127.0.0.1");
        cassandra_options.cassandra_ports_.push_back(9160);
        cassandra_options.ttlmap_ = ttl_map;
        db_handler_.reset(new DbHandlerMock(evm_.get(), cassandra_options));
        listener_.reset(new SyslogListeners(evm_.get(),
            boost::bind(&SyslogCollectorTest::myTestCb, this, _1, _2, _3),
            db_handler_, 0));
        gen_ = new SyslogMsgGen(evm_.get()->io_service(),
            listener_->GetUdpPort());
        gen_->Initialize(0);
        listener_->Start();
        thread_.reset(new ServerThread(evm_.get()));
        Sandesh::InitGenerator("SyslogTest", "127.0.0.1", "Test", "Test",
                evm_.get(), 0, NULL);
        thread_->Start();
    }

    bool myTestCb(const VizMsg *v, bool b, DbHandler *d) {
        EXPECT_STREQ(v->msg->GetMessageType().c_str(), "Syslog");
        return true;
    }

    void SendLog(std::string s) {
        gen_->Send(s);
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        gen_->Shutdown();
        task_util::WaitForIdle();
        UdpServerManager::DeleteServer(gen_);
        task_util::WaitForIdle();
        listener_->Shutdown();
        task_util::WaitForIdle();
        evm_->Shutdown();
        task_util::WaitForIdle();
        Sandesh::Uninit();
        task_util::WaitForIdle();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
        task_util::WaitForIdle();
    }
    SyslogMsgGen                   *gen_;
    std::auto_ptr<SyslogListeners> listener_;
    boost::shared_ptr<DbHandlerMock> db_handler_;
    std::auto_ptr<EventManager>    evm_;
    std::auto_ptr<ServerThread>    thread_;
};

class SyslogParserTest : public ::testing::Test
{
  public:
    virtual void SetUp() {
        sp_.reset(new SyslogParserTestHelper());
    }
    virtual void TearDown() {
        sp_.reset();
    }
    bool Parse(std::string s) {
        return sp_->TestParse(s);
    }
    std::string ip() { return sp_->ip(); }
    int64_t     ts() { return sp_->ts(); }
    std::string module() { return sp_->module(); }
    std::string hostname() { return sp_->hostname(); }
    int         severity() { return sp_->severity(); }
    std::string facility() { return sp_->facility(); }
    int         pid() { return sp_->pid(); }
    std::string body() { return sp_->body(); }

  private:
    std::auto_ptr<SyslogParserTestHelper> sp_;
};

class LineParserTest : public ::testing::Test
{
  public:
    virtual void SetUp() {
        lp_.reset(new LineParser());
    }
    virtual void TearDown() {
        lp_.reset();
    }
    LineParser::WordListType Parse(std::string s) {
        LineParser::WordListType w;
        lp_->Parse(s, &w);
        return DebugPrint(s, w);
    }
    LineParser::WordListType ParseXML(std::string s, bool a=true) {
        pugi::xml_document doc;
        LineParser::WordListType w;
        if (doc.load(s.c_str(), s.length())) {
            if (a)
                lp_->ParseXML(doc.document_element(), &w);
            else
                lp_->ParseXML(doc.document_element(), &w, a);
        }
        return DebugPrint(s, w);
    }
    unsigned int SearchPattern(std::string exp, std::string text) {
        return lp_->SearchPattern(exp, text);
    }
  private:
    LineParser::WordListType DebugPrint(std::string s,
            LineParser::WordListType w) {
//#define SYSLGDEBUG
#ifdef SYSLGDEBUG
            std::cout << s << "\n  Keywords:\n";
            for (LineParser::WordListType::iterator i = w.begin();
                    i != w.end(); i++)
                std::cout << "      '" << *i << "'\n";
            std::cout << "  Keywords end\n";
#endif
        return w;
    }
    std::auto_ptr<LineParser> lp_;
};

TEST_F(SyslogParserTest, ParseNone)
{
    bool r = Parse("");
    EXPECT_FALSE(r);
}

TEST_F(SyslogParserTest, ParseNoHostOne)
{
    bool r = Parse("<150>Feb 25 13:44:36 haproxy[3535]: 127.0.0.1:43566 [25/Feb/2014:13:44:36.630] contrail-discovery contrail-discovery-backend/10.84.9.45 0/0/0/121/121 200 180 - - ---- 1/1/0/1/0 0/0 \"POST /subscribe HTTP/1.1\"");
    EXPECT_TRUE(r);
    EXPECT_TRUE(0 == strncmp ("10.0.0.42", hostname().c_str(), 9));
}

TEST_F(SyslogParserTest, ParseWithHostOne)
{
    bool r = Parse("<84>Feb 25 13:44:21 a3s45 sudo: pam_limits(sudo:session): invalid line 'cassandra - nofile 100000' - skipped]");
    EXPECT_TRUE(r);
    EXPECT_TRUE(0 == strncmp ("a3s45", hostname().c_str(), 5));
}

TEST_F(SyslogCollectorTest, End2End)
{
    EXPECT_CALL(*db_handler_.get(), MessageTableInsert(_,_))
            .WillRepeatedly(Invoke(this, &SyslogCollectorTest::AssertVizMsg));
    SendLog("<84>Feb 25 13:44:21 a3s45 sudo: pam_limits(sudo:session): invalid line 'cassandra - memlock unlimited' - skipped]");
    sleep(1);
    task_util::WaitForIdle();
}

TEST_F(LineParserTest, SimpleWords)
{
    EXPECT_THAT(Parse("fox box cox"), testing::Contains("fox"));
}

TEST_F(LineParserTest, QuotedWords)
{
    EXPECT_THAT(Parse("fox \"Steph Curry\" cox"), testing::Contains(
                "steph curry"));
}

TEST_F(LineParserTest, SingleQuotedWords)
{
    EXPECT_THAT(Parse("fox 'Steph Curry' cox"), testing::Contains(
                "steph curry"));
}

TEST_F(LineParserTest, IpAddr)
{
    EXPECT_THAT(Parse("my server has 10.84.5.22 address"), testing::Contains(
                "10.84.5.22"));
}

TEST_F(LineParserTest, IpAddrMask)
{
    EXPECT_THAT(Parse("my server has 10.84.5.22/23 address"), testing::Contains(
                "10.84.5.22/23"));
}

TEST_F(LineParserTest, StopWords)
{
    EXPECT_THAT(Parse("my server has that address of the or via"),
            testing::ElementsAre("address", "has", "my", "server"));
}

TEST_F(LineParserTest, UuidTest)
{
    EXPECT_THAT(Parse("my server is d52eea70-e419-4246-98b9-292ae98d4d04"),
            testing::Contains("d52eea70-e419-4246-98b9-292ae98d4d04"));
}

TEST_F(LineParserTest, WeiredString)
{
    EXPECT_THAT(Parse("<discClientLog type=\"sandesh\"><log_msg type=\"string\" identifier=\"1\">query resp =&gt; {\"AlarmGenerator\": [{\"ip-address\": \"10.84.9.45\", \"@publisher-id\": \"a3s45\", \"redis-port\": \"6379\", \"instance-id\": \"0\", \"partitions\": \"{\\\"0\\\": 1456357027958320, \\\"1\\\": 1456357027960085, \\\"2\\\": 1456357027962225, \\\"3\\\": 1456357027965660, \\\"4\\\": 1456357027972614, \\\"5\\\": 1456357027972692, \\\"6\\\": 1456357027976379, \\\"7\\\": 1456357027980294, \\\"8\\\": 1456357027983837, \\\"9\\\": 1456357027986552, \\\"10\\\": 1456357027988989, \\\"11\\\": 1456357027991162, \\\"12\\\": 1456357027992856, \\\"13\\\": 1456357027995411, \\\"14\\\": 1456357027998922}\"}], \"ttl\": 1199} </log_msg></discClientLog>"), testing::Contains("resp"));
}

TEST_F(LineParserTest, CommaInString)
{
    EXPECT_THAT(Parse("my server has 10.84.5.22, 23 & 24 address"),
            testing::Contains("10.84.5.22"));
}

TEST_F(LineParserTest, AmpersandInString)
{
    EXPECT_THAT(Parse("my server has 10.84.5.22, 0x2b, 23 & 5.24 and address"),
            testing::ElementsAre(
                "10.84.5.22", "address", "has", "my", "server"));
}

TEST_F(LineParserTest, SystestVerify)
{
    EXPECT_THAT(Parse("pizza pasta babaghanoush"),
            testing::ElementsAre("babaghanoush", "pasta", "pizza"));
}

TEST_F(LineParserTest, HexnOctInString)
{
    EXPECT_THAT(Parse("my server has 10.84.5.22, 0x2b, 23 & 5.24 023 address"),
            testing::ElementsAre(
                "10.84.5.22", "address", "has", "my", "server"));
}

TEST_F(LineParserTest, XmlString)
{
    EXPECT_THAT(ParseXML("<discClientLog type=\"sandesh\"><log_msg type=\"string\" identifier=\"1\">query resp =&gt; {\"AlarmGenerator\": [{\"ip-address\": \"10.84.9.45\", \"@publisher-id\": \"a3s45\", \"redis-port\": \"6379\", \"instance-id\": \"0\", \"partitions\": \"{\\\"0\\\": 1456357027958320, \\\"1\\\": 1456357027960085, \\\"2\\\": 1456357027962225, \\\"3\\\": 1456357027965660, \\\"4\\\": 1456357027972614, \\\"5\\\": 1456357027972692, \\\"6\\\": 1456357027976379, \\\"7\\\": 1456357027980294, \\\"8\\\": 1456357027983837, \\\"9\\\": 1456357027986552, \\\"10\\\": 1456357027988989, \\\"11\\\": 1456357027991162, \\\"12\\\": 1456357027992856, \\\"13\\\": 1456357027995411, \\\"14\\\": 1456357027998922}\"}], \"ttl\": 1199} </log_msg></discClientLog>"), testing::Contains("resp"));
}

TEST_F(LineParserTest, BlockString)
{
    EXPECT_THAT(ParseXML("<boo>aa bb { foo toy, 12 { bar 5 ball box } }</boo>"),
            testing::Contains("box"));
}

TEST_F(LineParserTest, AttrString)
{
    EXPECT_THAT(ParseXML(
                "<boo f=\"bad\">aa bb {foo toy, 12{bar 5 ball box}}</boo>"),
            testing::Contains("bad"));
}

TEST_F(LineParserTest, NoAttrString)
{
    EXPECT_THAT(ParseXML(
                "<boo f=\"bad\">my server has address: 10.84.5.22</boo>",
                false), testing::ElementsAre(
                "10.84.5.22", "address", "has", "my", "server"));
}

TEST_F(LineParserTest, RegexpTextSearch)
{
    EXPECT_EQ(1, SearchPattern("box", "Its in the box of gems"));
}

TEST_F(LineParserTest, RegexpTextCaseSearch)
{
    EXPECT_EQ(1, SearchPattern("bOx", "Its in the Box of gems"));
}

TEST_F(LineParserTest, RegexpWildcardSearch)
{
    EXPECT_EQ(1, SearchPattern("bo.*f", "Its in the box of gems"));
}

TEST_F(LineParserTest, RegexpWildcardSearchMultiple)
{
    EXPECT_EQ(2, SearchPattern("bo.", "Some bones of the body"));
}

TEST_F(LineParserTest, SearchJson)
{
    EXPECT_EQ(1, SearchPattern("\"type\": \"int\"", "{\"box\": {\"count\": 2," \
                "\"ball\": \"golf\", \"type\": \"int\"}, \"foo\": \"bar\"}"));
}

TEST_F(LineParserTest, SearchMultilineJsonLike)
{
    EXPECT_EQ(1, SearchPattern("used_sys_mem: 37335184", "\n{   \n    ComputeCpuState: \n    {\n        name: a7s30\n        cpu_info:  \n        {\n            VrouterCpuInfo:\n            {\n                mem_virt: 1039252\n                cpu_share: 1.05\n                used_sys_mem: 37335184\n                one_min_cpuload: 0.14\n                mem_res: 265460\n            }\n            tags: .mem_virt,.cpu_share,.mem_res\n        }\n    }\n}\n"));
}

TEST_F(LineParserTest, WCSearchMultilineJsonLike)
{
    EXPECT_EQ(1, SearchPattern("name: a7s[0-9]+", "\n{   \n    ComputeCpuState: \n    {\n        name: a7s30\n        cpu_info:  \n        {\n            VrouterCpuInfo:\n            {\n                mem_virt: 1039252\n                cpu_share: 1.05\n                used_sys_mem: 37335184\n                one_min_cpuload: 0.14\n                mem_res: 265460\n            }\n            tags: .mem_virt,.cpu_share,.mem_res\n        }\n    }\n}\n"));
}

int
main(int argc, char **argv)
{
    LoggingInit();
    Sandesh::SetLoggingParams(true, "", SandeshLevel::UT_DEBUG);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}



