#include <boost/asio.hpp>
#include <pthread.h>

#include "testing/gunit.h"
#include "base/task.h"
#include "base/test/task_test_util.h"
#include "io/event_manager.h"
#include "io/usock_server.h"
#include "io/test/event_manager_test.h"
#include "io/io_log.h"

namespace {

class UsockServer: public UnixDomainSocketServer {
public:
    UsockServer(boost::asio::io_service *io_service, const char *path)
      : UnixDomainSocketServer(io_service, path) {
      set_observer(boost::bind(&UsockServer::EventHandler, this, _1, _2, _3));
    }

    void EventHandler (UnixDomainSocketServer *server,
                       UnixDomainSocketSession *session, Event event)
    {
        if (event == NEW_SESSION) {
            LOG(DEBUG, "Session created");
            char msg[] = "Test Message";
            session->Send((uint8_t *)msg, sizeof(msg));
        } else if (event == DELETE_SESSION) {
            LOG (DEBUG, "Session closed");
        }
    }
};

class UsockClient {
public:
    explicit UsockClient(const std::string &path) :
        sock_path_(path),
        socket_(-1) {
    }

    ~UsockClient() {
        if (socket_ != -1) {
            close(socket_);
        }
    }

    bool Connect() {
        socket_ = socket(AF_UNIX, SOCK_STREAM, 0);
        assert(socket_ != -1);
        struct sockaddr_un remote;
        memset(&remote, 0, sizeof(remote));
        remote.sun_family = AF_UNIX;
        strncpy(remote.sun_path, sock_path_.c_str(), sizeof(remote.sun_path));
        int len = strlen(remote.sun_path) + sizeof(remote.sun_family);
        int res = connect(socket_, (sockaddr *) &remote, len);
        return (res != -1);
    }

    int Send(const u_int8_t *data, size_t len) {
        return send(socket_, data, len, 0);
    }

    int Recv(u_int8_t *buffer, size_t len) {
        return recv(socket_, buffer, len, 0);
    }

    void Close() {
        int res = shutdown(socket_, SHUT_RDWR);
        assert(res == 0);
    }

private:
    std::string sock_path_;
    int socket_;
};

class UsockTest : public ::testing::Test {
protected:
    UsockTest() :
        evm_(new EventManager()) {
    }
    virtual void SetUp() {
        snprintf(socket_path_, 512, "/tmp/contrail-iotest-%u.sock", getpid());
        std::remove(socket_path_);
        server_.reset(new UsockServer(evm_->io_service(), socket_path_));
        thread_.reset(new ServerThread(evm_.get()));
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        evm_->Shutdown();
        task_util::WaitForIdle();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
        task_util::WaitForIdle();
    }

    std::auto_ptr<ServerThread> thread_;
    std::auto_ptr<EventManager> evm_;
    std::auto_ptr<UsockServer> server_;
    char socket_path_[512];
};

TEST_F(UsockTest, Basic) {
    task_util::WaitForIdle();
    thread_->Start();
    char path[512];
    snprintf(path, 512, "/tmp/contrail-iotest-%u.sock", getpid());
    UsockClient client(path);
    TASK_UTIL_EXPECT_TRUE(client.Connect());
    const char exp_msg[] = "Test Message";
    char rcv_msg[256];
    int len = client.Recv((u_int8_t *) rcv_msg, sizeof(rcv_msg));
    TASK_UTIL_EXPECT_EQ(0, memcmp(rcv_msg, exp_msg, len));
    client.Close();
    task_util::WaitForIdle();
}

}  // namespace

int main(int argc, char **argv) {
    LoggingInit();
    Sandesh::SetLoggingParams(true, "", SandeshLevel::UT_DEBUG);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
