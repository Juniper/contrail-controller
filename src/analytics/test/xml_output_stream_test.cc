/*
 * Copyright (c) 2014 Codilime
 */

#include <string>
#include <boost/function.hpp>
#include <boost/scoped_ptr.hpp>
#include <pugixml/pugixml.hpp>
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

using namespace analytics;

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
        }

     private:
        void slice() {
            size_t start = buffer_.find_first_of("<sandesh>");
            size_t end = buffer_.find_first_of("</sandesh>");

            if (start != std::string::npos &&
                end != std::string::npos) {
                std::string message = buffer_.substr(start, end - start);
                buffer_.erase(0, end);
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

     private:
        void OnEvent(TcpSession *session, Event event) {

        }

        virtual void OnRead(Buffer buffer) {
            owner_->slicer_.AddData(BufferData(buffer), BufferSize(buffer));
        }

        XMLOutputStreamTest *owner_;
    };

    class LogServer : public TcpServer {
     public:
        LogServer(EventManager *, XMLOutputStreamTest *owner);

        virtual TcpSession *AllocSession(Socket *socket) {
            return new LogServerSession(this, socket, owner_);
        }

     private:
        XMLOutputStreamTest *owner_;
    };

    void ProcessMessage(std::string);

    int messages_received_;

    EventManager *evm_;
    boost::scoped_ptr<ServerThread> thread_;
    analytics::OutputStreamManager output_stream_mgr_;

    LogServer server_;
    LogServerSession *session_;
    MessageSlicer slicer_;
};

XMLOutputStreamTest::MessageSlicer::MessageSlicer(MessageCallback cb)
: cb_(cb) {}

XMLOutputStreamTest::LogServerSession::LogServerSession(
    LogServer *manager, Socket *socket, XMLOutputStreamTest *owner)
: TcpSession(manager, socket), owner_(owner) {
}

XMLOutputStreamTest::LogServer::LogServer(EventManager *evm,
    XMLOutputStreamTest *owner)
: TcpServer(evm), owner_(owner) {}

XMLOutputStreamTest::XMLOutputStreamTest()
: evm_(new EventManager),
  thread_(new ServerThread(evm_)),
  server_(evm_, this),
  slicer_(boost::bind(&XMLOutputStreamTest::ProcessMessage, this, _1)),
  messages_received_(0) {
    server_.Initialize(50000);
    task_util::WaitForIdle();
    thread_->Start();
}

XMLOutputStreamTest::~XMLOutputStreamTest() {
    server_.Shutdown();
    task_util::WaitForIdle();
    evm_->Shutdown();
    if (thread_.get() != NULL) {
        thread_->Join();
    }
    task_util::WaitForIdle();
}

void XMLOutputStreamTest::ProcessMessage(std::string msg) {
    messages_received_++;
}

TEST_F(XMLOutputStreamTest, INIConfig) {    
    StreamHandlerFactory::set_event_manager(evm_);
    OutputStreamManager::INIConfigReader cfg_reader(&output_stream_mgr_);
    cfg_reader.Configure("config/logstreams/initest");
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
