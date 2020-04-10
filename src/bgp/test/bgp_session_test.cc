/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_session.h"

#include <vector>

#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_server.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routing_instance.h"
#include "control-node/control_node.h"

using namespace std;
using namespace boost::asio;
using boost::asio::mutable_buffer;

class BgpPeerMock : public BgpPeer {
public:
    BgpPeerMock(BgpServer *server, RoutingInstance *rt,
                const BgpNeighborConfig *config)
        : BgpPeer(server, rt, config) {
    }
    virtual bool ReceiveMsg(BgpSession *session, const u_int8_t *msg,
                            size_t size) {
        BGP_DEBUG_UT("ReceiveMsg: " << size << " bytes");
        sizes.push_back(size);
        return true;
    }
    vector<int>::const_iterator begin() const {
        return sizes.begin();
    }
    vector<int>::const_iterator end() const {
        return sizes.end();
    }
    void TriggerPrefixLimitCheck() const { }

private:
    vector<int> sizes;
};

class BgpSessionTest : public BgpSession {
public:
    BgpSessionTest(BgpSessionManager *session_mgr)
        : BgpSession(session_mgr, NULL), release_count_(0) {
    }

    void Read(Buffer buffer) {
        OnRead(buffer);
    }
    virtual void ReleaseBuffer(Buffer buffer) {
        release_count_++;
    }
    int release_count() const { return release_count_; }

private:
    int release_count_;
};

class BgpSessionUnitTest : public ::testing::Test {
protected:
    BgpSessionUnitTest()
        : server_(&evm_),
          instance_config_(BgpConfigManager::kMasterInstance),
          config_(new BgpNeighborConfig) {
        ConcurrencyScope scope("bgp::Config");
        BgpObjectFactory::Register<BgpPeer>(boost::factory<BgpPeerMock *>());
        RoutingInstance *rti =
                server_.routing_instance_mgr()->CreateRoutingInstance(
                    &instance_config_);
        config_->set_instance_name(BgpConfigManager::kMasterInstance);
        config_->set_name("test-peer");
        peer_ = static_cast<BgpPeerMock *>(
            rti->peer_manager()->PeerLocate(&server_, config_.get()));
        session_.reset(new BgpSessionTest(server_.session_manager()));
        session_->set_peer(peer_);
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        server_.Shutdown();
        task_util::WaitForIdle();
        evm_.Shutdown();
    }

    EventManager evm_;
    BgpServer server_;
    BgpInstanceConfig instance_config_;
    auto_ptr<BgpNeighborConfig> config_;
    BgpPeerMock *peer_;
    auto_ptr<BgpSessionTest> session_;
};

static void CreateFakeMessage(uint8_t *data, size_t length) {
    assert(length > 18);
    memset(data, 0xff, 16);
    put_value(data + 16, 2, length);
    memset(data + 18, 0, length - 18);
}

#define ARRAYLEN(_Array)    sizeof(_Array) / sizeof(_Array[0])

TEST_F(BgpSessionUnitTest, PeerLookupUponAccept) {
#if 0
    BgpPeer           *peer;
    ip::tcp::endpoint  remote_endpoint;
    int                num_peers = 10;
    int                i;
    map<int, BgpPeer *> local_peer_map;
    map<string, BgpPeer *> remote_peer_map;
    int max = 0;

    srand(getpid());
    for (i = 0; i < num_peers; i++) {
        BgpNeighborConfig *config = new BgpNeighborConfig();

        config->peer_key.endpoint.address(
                ip::address::from_string("127.0.0.1"));
        config->peer_key.endpoint.port((rand() % 0xFFff) + 1);
        config->peer_key.peer_id = (rand() % 0xFFff) + 1;
        if (max < config->peer_key.endpoint.port()) {
            max = config->peer_key.endpoint.port();
        }
        peer = rt_.CreatePeer(&server_, config);
        local_peer_map[config->peer_key.peer_id] = peer;
        BGP_DEBUG_UT("Created peer: " << peer->ToString());
    }

    // Add _non_ 127.0.0.1 peers as well.
    for (i = 0; i < num_peers; i++) {
        BgpNeighborConfig *config = new BgpNeighborConfig();

        string addr = "127.0.0." + boost::lexical_cast<std::string>(i + 2);
        config->peer_key.endpoint.address(ip::address::from_string(addr));
        config->peer_key.endpoint.port(179);
        config->peer_key.peer_id = 0;
        peer = rt_.CreatePeer(&server_, config);
        remote_peer_map[addr] = peer;
        BGP_DEBUG_UT("Created peer: " << peer->ToString());
    }

    for(map<string, BgpPeer *>::const_iterator riter = remote_peer_map.begin();
            riter != remote_peer_map.end(); riter++){
        string addr = riter->first;
        remote_endpoint.address(ip::address::from_string(addr));
        remote_endpoint.port(179);
        peer = rt_.LookupPeer(remote_endpoint);
        EXPECT_TRUE(peer == riter->second);

        // Look for a peer with diffent IP. We should not find a right match
        remote_endpoint.address(
            ip::address_v4(remote_endpoint.address().to_v4().to_ulong() + 1));
        remote_endpoint.port(179);
        peer = rt_.LookupPeer(remote_endpoint);
        EXPECT_TRUE(peer != riter->second);
    }


    // Feed remote end-points with what has been peer_ids to LookupPeer()
    // and verify tbat it yields the right peer. Also, verify that if a
    // different port is fed, we still get a peer (due to match on IP address
    // but not really the expected one, unless the port value used is the max
    // port value among all ports configured

    for(map<int, BgpPeer *>::const_iterator iter = local_peer_map.begin();
            iter != local_peer_map.end(); iter++){
        remote_endpoint.address(ip::address::from_string("127.0.0.1"));
        remote_endpoint.port(iter->first);
        peer = rt_.LookupPeer(remote_endpoint);

        EXPECT_TRUE(peer == iter->second);

        // Force the port number not to match by using a different port
        remote_endpoint.port(iter->first + 20);

        // Call the actual routine that does the peer look up (in production
        // code
        peer = rt_.LookupPeer(remote_endpoint);

        if (iter->second->peer_key().endpoint.port() == max) {
            BGP_DEBUG_UT("Max peer should match: " << iter->second->ToString());
            EXPECT_TRUE(peer == iter->second);
        } else {

            // This is the max port peer. Hence the lookup must fail
            EXPECT_TRUE(peer != iter->second);
        }
    }

    // Do some tests with port numbers randomized
    for (i = 0; i < 100; i++) {
        // Look for a peer with a non existing IP address and verify that peer
        // lookup does not succeed
        remote_endpoint.address(ip::address::from_string("128.0.0.1"));
        remote_endpoint.port((rand() % 0xFFff) + 1);
        peer = rt_.LookupPeer(remote_endpoint);
        EXPECT_TRUE(peer == NULL);

        // Look for a peer with configured IP address and verify that peer
        // lookup does succeed yielding peer with the right address.
        remote_endpoint.address(ip::address::from_string("127.0.0.1"));
        remote_endpoint.port((rand() % 0xFFff) + 1);
        peer = rt_.LookupPeer(remote_endpoint);

        EXPECT_TRUE(peer != NULL);
        EXPECT_EQ("127.0.0.1", peer->peer_key().endpoint.address().to_string());
    }
#endif
}

TEST_F(BgpSessionUnitTest, StreamRead) {
    uint8_t stream[4096];
    int sizes[] = { 100, 400, 80, 110, 40, 60 };
    uint8_t *data = stream;
    for (size_t i = 0; i < ARRAYLEN(sizes); i++) {
        CreateFakeMessage(data, sizes[i]);
        data += sizes[i];
    }
    vector<mutable_buffer> buf_list;
    int segments[] = {
        100 + 20,       // complete msg + start (with header)
        200,            // mid part
        180 + 80 + 10,  // end + start full msg + start (no header)
        7,              // still no header
        10,             // header but no end
        83,             // end of message.
        40,
        60
    };
    data = stream;
    for (size_t i = 0; i < ARRAYLEN(segments); i++) {
        buf_list.push_back(mutable_buffer(data, segments[i]));
        data += segments[i];
    }
    for (size_t i = 0; i < buf_list.size(); i++) {
        session_->Read(buf_list[i]);
    }

    size_t i = 0;
    for (vector<int>::const_iterator iter = peer_->begin();
         iter != peer_->end(); ++iter) {
        EXPECT_EQ(sizes[i], *iter);
        i++;
    }
    EXPECT_EQ(ARRAYLEN(sizes), i);
    EXPECT_EQ(buf_list.size(), static_cast<size_t>(session_->release_count()));
}

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
}

static void TearDown() {
    task_util::WaitForIdle();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
