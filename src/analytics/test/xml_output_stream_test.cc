/*
 * Copyright (c) 2014 Codilime
 */

#include <string>
#include <fstream>
#include <streambuf>
#include <boost/function.hpp>
#include <boost/scoped_ptr.hpp>
#include <pugixml/pugixml.hpp>
#include <sandesh/sandesh_message_builder.h>
#include "testing/gunit.h"
#include "base/task.h"
#include "base/test/task_test_util.h"
#include "io/event_manager.h"
#include "io/test/event_manager_test.h"
#include "io/tcp_server.h"
#include "io/tcp_session.h"
#include "analytics/stream_manager.h"
#include "analytics/xml_output_stream.h"
#include "analytics/stream_handler_factory.h"

#define WAIT_FOR(Cond, milliseconds)    \
do {                                    \
        for (int i = 0; i < milliseconds; i++) { \
            if (Cond) break;            \
            usleep(1000);               \
        }                               \
        EXPECT_TRUE(Cond);              \
} while (0)

using namespace analytics;

static void load_file(const char *path, std::string& out) {
    std::ifstream t(path);

    t.seekg(0, std::ios::end);
    out.reserve(t.tellg());
    t.seekg(0, std::ios::beg);

    out.assign((std::istreambuf_iterator<char>(t)),
                std::istreambuf_iterator<char>());
}

class XMLOutputStreamTest : public ::testing::Test {
 public:
    XMLOutputStreamTest();
    ~XMLOutputStreamTest();

    class MessageSlicer {
     public:
        typedef boost::function<void(std::string)> MessageCallback;

        MessageSlicer(MessageCallback cb);

        void AddData(const uint8_t* data, size_t size) {
            buffer_ += std::string((const char *)(data), size);
            slice();
        }

     private:
        void slice() {
            size_t start = buffer_.find("<sandesh>");
            size_t end = buffer_.find("</sandesh>");

            if (start != std::string::npos &&
                end != std::string::npos) {
                std::string message =
                    buffer_.substr(start, end - start + sizeof("</sandesh>"));
                buffer_.erase(0, end + sizeof("</sandesh>"));
                cb_(message);
            }
        }

        std::string buffer_;
        MessageCallback cb_;
    };

    class LogServer;
    class LogServerSession : public TcpSession {
     public:
        LogServerSession(LogServer *manager, Socket *socket,
            XMLOutputStreamTest *owner);

        void OnEvent(TcpSession *session, Event event) {
        }

        virtual void OnRead(Buffer buffer) {
            onread_called_++;
            owner_->slicer_.AddData(BufferData(buffer), BufferSize(buffer));
        }

        XMLOutputStreamTest *owner_;

        int onread_called_;
    };

    class LogServer : public TcpServer {
     public:
        LogServer(EventManager *, XMLOutputStreamTest *owner);

        virtual TcpSession *AllocSession(Socket *socket) {
            return owner_->session_ =
                new LogServerSession(this, socket, owner_);
        }

     private:
        XMLOutputStreamTest *owner_;
    };

    class OutputStreamManagerTest : public analytics::OutputStreamManager {
     public:
        int num_handlers() {
            return output_handlers_.size();
        }
    };

    class SandeshXMLMessageTest : public SandeshXMLMessage {
    public:
        SandeshXMLMessageTest() {}
        virtual ~SandeshXMLMessageTest() {}


        virtual bool Parse(const uint8_t *xml_msg, size_t size) {
            xdoc_.load_buffer(xml_msg, size, pugi::parse_default &
                ~pugi::parse_escapes);
            message_node_ = xdoc_.first_child();
            message_type_ = message_node_.name();
            size_ = size;
            return true;
        }

        void SetHeader(const SandeshHeader &header) { header_ = header; }
    };

    void ProcessMessage(std::string);

    int messages_received_;
    bool data_ok_;

    EventManager *evm_;
    boost::scoped_ptr<ServerThread> thread_;
    OutputStreamManagerTest output_stream_mgr_;

    LogServer *server_;
    LogServerSession *session_;
    MessageSlicer slicer_;
};

XMLOutputStreamTest::MessageSlicer::MessageSlicer(MessageCallback cb)
: cb_(cb) {}

XMLOutputStreamTest::LogServerSession::LogServerSession(
    LogServer *manager, Socket *socket, XMLOutputStreamTest *owner)
: TcpSession(manager, socket), owner_(owner), onread_called_(0) {
}

XMLOutputStreamTest::LogServer::LogServer(EventManager *evm,
    XMLOutputStreamTest *owner)
: TcpServer(evm), owner_(owner) {}

XMLOutputStreamTest::XMLOutputStreamTest()
: messages_received_(0),
  data_ok_(false),
  evm_(new EventManager),
  thread_(new ServerThread(evm_)),
  slicer_(boost::bind(&XMLOutputStreamTest::ProcessMessage, this, _1)) {
    server_ = new LogServer(evm_, this);
    server_->Initialize(50000);
    task_util::WaitForIdle();
    thread_->Start();
}

XMLOutputStreamTest::~XMLOutputStreamTest() {
//    server_->Shutdown();
    task_util::WaitForIdle();
    evm_->Shutdown();
    if (thread_.get() != NULL) {
        thread_->Join();
    }
    task_util::WaitForIdle();
}

void XMLOutputStreamTest::ProcessMessage(std::string test_xml) {
    messages_received_++;

    pugi::xml_document doc;
    doc.load(test_xml.c_str());
    if (!doc.child("Sandesh").child("VNSwitchErrorMsg").empty())
        data_ok_ = true;
}

TEST_F(XMLOutputStreamTest, INIConfigWReconnect) {
    StreamHandlerFactory::set_event_manager(evm_);
    OutputStreamManager::INIConfigReader cfg_reader(&output_stream_mgr_);
    cfg_reader.Configure("controller/src/analytics/config/logstreams/initest");
    EXPECT_EQ(output_stream_mgr_.output_handlers()->size(), 1);

    SandeshXMLMessageTest *test_message = new SandeshXMLMessageTest;
    {
        std::string test_xml;
        load_file("controller/src/analytics/data/"
            "logstream_test_sandeshmsg1.xml", test_xml);
        test_message->Parse((const uint8_t *)(test_xml.c_str()),
            test_xml.size());
    }

    output_stream_mgr_.append(test_message);

    {
        const analytics::TCPOutputStream &os =
            static_cast<const analytics::TCPOutputStream &>(
                (*output_stream_mgr_.output_handlers())[0]);
        const struct analytics::TCPOutputStream::Stats *stats =
            os.stats();
        WAIT_FOR(stats->messages_sent, 1000);
    }

    task_util::WaitForIdle();
    WAIT_FOR(session_->onread_called_, 1000);
    WAIT_FOR(messages_received_ == 1, 1000);
    EXPECT_EQ(data_ok_, true);

    // hang up
    session_->Close();
    server_->DeleteSession(session_);

    // send some more and expect it to reconnect
    output_stream_mgr_.append(test_message);
    output_stream_mgr_.append(test_message);

    WAIT_FOR(messages_received_ == 3, 2000);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

