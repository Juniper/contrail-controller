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

class SyslogMsgTCPGen;

class SyslogMsgTCPGenSession : public TcpSession
{
  public:
    SyslogMsgTCPGenSession(SyslogMsgTCPGen *server,
        boost::asio::ip::tcp::socket *socket, std::string msg);
    virtual ~SyslogMsgTCPGenSession() { }
    virtual void OnRead(boost::asio::const_buffer b) { }
  private:
    void EventHandler(TcpSession *s, Event e);
    SyslogMsgTCPGen *server_;
    std::string msg_;
};

class SyslogMsgTCPGen : public TcpServer
{
  public:
    explicit SyslogMsgTCPGen(EventManager *evm, int port) :
            TcpServer(evm), to_(port), done_(false) {
    }

    ~SyslogMsgTCPGen() { }

    virtual TcpSession *AllocSession(Socket *socket) {
        return new SyslogMsgTCPGenSession(this, socket, m_);
    }

    void SessionReset(TcpSession *s) {
        if (s) {
            set_done(); // per session.. may be l8r
            s->Close();
            DeleteSession(s);
        }
    }

    bool done() { return done_; }
    void set_done() { done_ = true; }


    void SetPort(int p) { to_ = p; }

    void Send(std::string ip, int port, std::string msg, TcpSession *s) {
        assert(s);
        boost::system::error_code e;
        boost::asio::ip::tcp::endpoint endpoint(
                boost::asio::ip::address::from_string(ip, e), port);
        if (!e.value()) {
            TcpServer::Connect(s, endpoint);
        }
    }

    void Send(std::string ip, int port, std::string msg) {
        m_ = msg;
        return Send(ip, port, msg, CreateSession());
    }

    void Send(int port, std::string msg) {
        return Send("127.0.0.1", port, msg);
    }

    void Send(std::string msg) {
        return Send(to_, msg);
    }

  private:
    int    to_;
    bool   done_;
    std::string m_;
};

SyslogMsgTCPGenSession::SyslogMsgTCPGenSession(SyslogMsgTCPGen *server,
        boost::asio::ip::tcp::socket *socket, std::string msg) :
            TcpSession(server, socket), server_(server), msg_(msg)
{
    set_observer(boost::bind(&SyslogMsgTCPGenSession::EventHandler,
            this, _1, _2));
}

void
SyslogMsgTCPGenSession::EventHandler(TcpSession *s, Event e) {
    switch (e) {
        case CONNECT_COMPLETE:
            SetSocketOptions(); //non-blocking
            Send((const u_int8_t*)msg_.c_str(), msg_.length(), NULL);
            server_->SessionReset(this);
            break;
        default:
            break;
    }
}

class SyslogMsgUDPGen : public UDPServer
{
  public:
    explicit SyslogMsgUDPGen(boost::asio::io_service& io_service, int snd_port) :
                UDPServer(io_service) {
        boost::system::error_code ec;
        ep_ = udp::endpoint(boost::asio::ip::address::from_string("127.0.0.1",
            ec), snd_port);
    }

    void Send (std::string snd, udp::endpoint to)
    {
        mutable_buffer send = AllocateBuffer (snd.length());
        char *p = buffer_cast<char *>(send);
        std::copy(snd.begin(), snd.end(), p);
        UDP_UT_LOG_DEBUG( "SyslogMsgUDPGen sending '" << snd << "' to " << to);
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
        UDP_UT_LOG_DEBUG("SyslogMsgUDPGen sent " << bytes_transferred << "(" <<
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
        listener_ = new SyslogListeners(evm_.get(),
            boost::bind(&SyslogCollectorTest::myTestCb, this, _1, _2, _3),
            db_handler_.get(), 0);
        listener_->Start();
        thread_.reset(new ServerThread(evm_.get()));
        thread_->Start();

        udp_gen_ = new SyslogMsgUDPGen(*evm_.get()->io_service(),
            listener_->GetUdpPort());
        udp_gen_->Initialize(0);

        tcp_gen_ = new SyslogMsgTCPGen(evm_.get(), listener_->GetTcpPort());
        tcp_gen_->Initialize(0);

        Sandesh::InitGenerator("SyslogTest", "127.0.0.1", "Test", "Test",
                evm_.get(), 8080, NULL);
    }

    bool myTestCb(const VizMsg *v, bool b, DbHandler *d) {
        EXPECT_STREQ(v->msg->GetMessageType().c_str(), "Syslog");
        return true;
    }

    void SendUDPLog(std::string s) {
        udp_gen_->Send(s);
    }

    void SendTCPLog(std::string s) {
        tcp_gen_->Send(s);
        task_util::WaitForIdle();
        TASK_UTIL_ASSERT_TRUE(tcp_gen_->done());
    }

    virtual void TearDown() {
        evm_->Shutdown();
        task_util::WaitForIdle();
        listener_->Shutdown();
        task_util::WaitForIdle();
        TcpServerManager::DeleteServer(listener_);
        TcpServerManager::DeleteServer(tcp_gen_);
        task_util::WaitForIdle();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
        Sandesh::Uninit();
    }
    SyslogMsgUDPGen               *udp_gen_;
    SyslogMsgTCPGen               *tcp_gen_;
    SyslogListeners               *listener_;
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
    SendUDPLog("<84>Feb 25 13:44:21 a3s45 sudo: pam_limits(sudo:session): invalid line 'cassandra - memlock unlimited' - skipped]");
    SendTCPLog("<85>Feb 25 13:44:21 a3s45 sudo:     root : TTY=pts/1 ; PWD=/root/tghose/github ; USER=root ; COMMAND=/bin/echo ... :D ...");
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



