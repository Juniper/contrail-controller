/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"
#include "db_handler_mock.h"

#include "../syslog_collector.cc"
#include "base/task.h"
#include "base/test/task_test_util.h"
#include "io/test/event_manager_test.h"

using ::testing::Field;
using ::testing::StrEq;
using ::testing::Pointee;
using ::testing::_;

class SyslogParserTestHelper : public SyslogParser
{
    public:
        SyslogParserTestHelper() {}
        virtual void MakeSandesh (syslog_m_t v) {
            for (syslog_m_t::iterator i = v.begin(); i != v.end(); ++i) {
                v_.insert(std::pair<std::string, Holder>(i->first,
                    i->second));
            }
            ip_ = GetMapVals(v, "ip");
            ts_ = GetMapVal(v, "timestamp");
            module_ = GetModule(v);
            hostname_ = GetMapVals(v, "hostname", ip_);
            severity_ = GetMapVal(v, "severity");
            facility_ = GetFacility(v);
            pid_ = GetPID(v);
            body_ = "<Syslog>" + GetMsgBody (v) + "</Syslog>";
        }

        bool TestParse(std::string s) {
            syslog_m_t v;
            bool r = parse_syslog (s.begin(), s.end(), v);
            v.insert(std::pair<std::string, Holder>("ip",
                  Holder("ip", "10.0.0.42")));
            PostParsing (v);

            MakeSandesh (v);
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
    void AssertVizMsg(const VizMsg *vmsgp) {
        EXPECT_STREQ(vmsgp->msg->GetMessageType().c_str(), "Syslog");
    }
    protected:
    virtual void SetUp() {
        evm_.reset(new EventManager());
        db_handler_.reset(new DbHandlerMock(evm_.get()));
        listener_.reset(new SyslogListeners(evm_.get(),
            boost::bind(&SyslogCollectorTest::myTestCb, this, _1, _2, _3),
            db_handler_.get(), 0));
        gen_ = new SyslogMsgGen(evm_.get()->io_service(),
            listener_->GetUdpPort());
        gen_->Initialize(0);
        listener_->Start();
        thread_.reset(new ServerThread(evm_.get()));
        Sandesh::InitGenerator("SyslogTest", "127.0.0.1", "Test", "Test",
                evm_.get(), 8080, NULL);
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
        if (thread_.get() != NULL) {
            thread_->Join();
        }
        Sandesh::Uninit();
        task_util::WaitForIdle();
    }
    SyslogMsgGen                   *gen_;
    std::auto_ptr<SyslogListeners> listener_;
    std::auto_ptr<DbHandlerMock>   db_handler_;
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
    EXPECT_CALL(*db_handler_.get(), MessageTableInsert(_))
            .WillRepeatedly(Invoke(this, &SyslogCollectorTest::AssertVizMsg));
    SendLog("<84>Feb 25 13:44:21 a3s45 sudo: pam_limits(sudo:session): invalid line 'cassandra - memlock unlimited' - skipped]");
    sleep(1);
    task_util::WaitForIdle();
}

int
main(int argc, char **argv)
{
    LoggingInit();
    Sandesh::SetLoggingParams(true, "", SandeshLevel::UT_DEBUG);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}



