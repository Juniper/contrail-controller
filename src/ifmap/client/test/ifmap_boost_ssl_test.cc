/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/client/test/boost_ssl_client.h"
#include "ifmap/client/test/boost_ssl_server.h"

#include "base/logging.h"
#include "base/task.h"
#include "base/task_annotations.h"

#include <pthread.h>
#include <sandesh/sandesh.h>

#include "testing/gunit.h"

#define PASSWORD        "test"

class BoostSslTest : public ::testing::Test {
protected:
    BoostSslTest()
        : client_(new BoostSslClient(io_service_)),
          server_(new BoostSslServer(io_service_, port, PASSWORD)),
          thread_id_(pthread_self()) {
    }

    virtual void SetUp() {
        Start();
    }

    virtual void TearDown() {
        Join();
    }

    static void *ServerStart(void *serverp) {
        BoostSslServer *server = reinterpret_cast<BoostSslServer *>(serverp);
        server->Start();
        return NULL;
    }

    void Start() {
        int res = pthread_create(&thread_id_, NULL, &ServerStart,
                                 server_.get());
        assert(res == 0);
    }

    void Join() {
        int res = pthread_join(thread_id_, NULL);
        assert(res == 0);
    }

    boost::asio::io_service io_service_;
    std::auto_ptr<BoostSslClient> client_;
    std::auto_ptr<BoostSslServer> server_;
    pthread_t thread_id_;
    static const unsigned short port = 23232;
};

TEST_F(BoostSslTest, Connection) {

    client_->Start("test", "test", "127.0.0.1", "23232");
    sleep(5);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    bool success = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return success;
}
