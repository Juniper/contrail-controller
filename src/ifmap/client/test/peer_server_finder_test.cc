/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/client/peer_server_finder.h"

#include "base/logging.h"
#include "base/task.h"

#include <sandesh/sandesh.h>

#include "discovery_client.h"
#include "ifmap/client/ifmap_manager.h"
#include "ifmap/client/ifmap_channel.h"

#include "testing/gunit.h"

class MockIFMapManager : public IFMapManager {
public:
    MockIFMapManager() : IFMapManager(), start_connection_count(0),
                         reset_connection_count(0) {
    }
    virtual void Start(const std::string &host, const std::string &port) {
        ++start_connection_count;
    }
    virtual void ResetConnection(const std::string &host,
                                 const std::string &port) {
        ++reset_connection_count;
    }
    int get_start_connection_count() { return start_connection_count; }
    int get_reset_connection_count() { return reset_connection_count; }

private:
    int start_connection_count;
    int reset_connection_count;
};

class PeerIFMapServerTestFinder : public PeerIFMapServerFinder {
public:
    PeerIFMapServerTestFinder(IFMapManager *ifmap_manager,
                DiscoveryServiceClient *client, std::string url) :
        PeerIFMapServerFinder(ifmap_manager, "test_ds_client", client, url) {
    }

    virtual bool DSExists() {
        if (get_url().size()) {
            return false;
        } else {
            return true;
        }
    }
};

class PeerServerFinderTest : public ::testing::Test {
protected:
    PeerServerFinderTest() : peer_finder_(NULL), ifmap_manager_() {

    }
    void InitFinder(const std::string &url) {
        peer_finder_.reset(
            new PeerIFMapServerTestFinder(&ifmap_manager_, NULL, url));
    }

    void AddResponseEntry(std::vector<DSResponse> &ds_resp,
                          const std::string &host, unsigned short port) {
        DSResponse entry;
        entry.ep.address(boost::asio::ip::address::from_string(host));
        entry.ep.port(port);
        ds_resp.push_back(entry);
    }

    void AddResponseEntryAndProc(std::vector<DSResponse> &ds_resp,
                                 const std::string &host, unsigned short port) {
        AddResponseEntry(ds_resp, host, port);
        peer_finder_->ProcPeerIFMapDSResp(ds_resp);
    }

    void DeleteResponseEntry(std::vector<DSResponse> &ds_resp,
                             const std::string &host, unsigned short port) {
        std::vector<DSResponse>::iterator first = ds_resp.begin();
        for (size_t i = 0; i < ds_resp.size(); ++i) {
            DSResponse entry = ds_resp[i];
            if ((host.compare(entry.ep.address().to_string()) == 0) &&
                (port == entry.ep.port())) {
                ds_resp.erase(first + i);
                break;
            }
        }
    }

    void DeleteResponseEntryAndProc(std::vector<DSResponse> &ds_resp,
                                const std::string &host, unsigned short port) {
        DeleteResponseEntry(ds_resp, host, port);
        peer_finder_->ProcPeerIFMapDSResp(ds_resp);
    }

    std::auto_ptr<PeerIFMapServerTestFinder> peer_finder_;
    MockIFMapManager ifmap_manager_;
};

TEST_F(PeerServerFinderTest, StaticPeer) {
    InitFinder("https://1.1.1.1:1111");
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);

    std::string stat_peer = peer_finder_->get_static_peer();
    std::string curr_peer = peer_finder_->get_current_peer();

    EXPECT_EQ(stat_peer.compare("1.1.1.1:1111"), 0);
    EXPECT_EQ(stat_peer.compare(curr_peer), 0);

    bool valid = peer_finder_->PeerDown();
    EXPECT_EQ(valid, true);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);

    valid = peer_finder_->PeerDown();
    EXPECT_EQ(valid, true);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 2);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);

    valid = peer_finder_->PeerDown();
    EXPECT_EQ(valid, true);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 3);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
}

// +1, +2, -1, -2
TEST_F(PeerServerFinderTest, DSResp) {
    InitFinder("");
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 0);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);

    std::string host1 = "1.1.1.1";
    unsigned short port1 = 1111;
    std::string host1str = "1.1.1.1:1111";
    std::string host2 = "1.1.1.2";
    unsigned short port2 = 1112;
    std::string host2str = "1.1.1.2:1112";

    std::string stat_peer = peer_finder_->get_static_peer();
    EXPECT_EQ(stat_peer.size(), 0);

    // Add host1
    std::vector<DSResponse> ds_resp;
    AddResponseEntryAndProc(ds_resp, host1, port1);
    std::string curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host1, port1));

    // Add host2. Current should remain host1
    AddResponseEntryAndProc(ds_resp, host2, port2);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 2);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host2, port2));

    // Delete host1. host2 should become current.
    DeleteResponseEntryAndProc(ds_resp, host1, port1);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host2str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 1);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host2, port2));

    // Even after removing host2, current should continue to be host2
    DeleteResponseEntryAndProc(ds_resp, host2, port2);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host2str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 0);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 1);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 1);
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host2, port2));

    stat_peer = peer_finder_->get_static_peer();
    EXPECT_EQ(stat_peer.size(), 0);
}

// [+1 +2], [-1 -2]
TEST_F(PeerServerFinderTest, DSResp1) {
    InitFinder("");
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 0);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);

    std::string host1 = "1.1.1.1";
    unsigned short port1 = 1111;
    std::string host1str = "1.1.1.1:1111";
    std::string host2 = "1.1.1.2";
    unsigned short port2 = 1112;
    std::string host2str = "1.1.1.2:1112";

    std::string stat_peer = peer_finder_->get_static_peer();
    EXPECT_EQ(stat_peer.size(), 0);

    // Add host1 and host2
    std::vector<DSResponse> ds_resp;
    AddResponseEntry(ds_resp, host1, port1);
    AddResponseEntryAndProc(ds_resp, host2, port2);
    std::string curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 2);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host2, port2));

    // After deleting host1, host2 should become current
    DeleteResponseEntry(ds_resp, host1, port1);
    DeleteResponseEntryAndProc(ds_resp, host2, port2);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 0);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 1);
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host2, port2));

    stat_peer = peer_finder_->get_static_peer();
    EXPECT_EQ(stat_peer.size(), 0);
}

// +1, +2, -2, -1
TEST_F(PeerServerFinderTest, DSResp2) {
    InitFinder("");
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 0);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);

    std::string host1 = "1.1.1.1";
    unsigned short port1 = 1111;
    std::string host1str = "1.1.1.1:1111";
    std::string host2 = "1.1.1.2";
    unsigned short port2 = 1112;
    std::string host2str = "1.1.1.2:1112";

    std::string stat_peer = peer_finder_->get_static_peer();
    EXPECT_EQ(stat_peer.size(), 0);

    // Add host1
    std::vector<DSResponse> ds_resp;
    AddResponseEntryAndProc(ds_resp, host1, port1);
    std::string curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host2, port2));

    // Add host2. Current should remain host1
    AddResponseEntryAndProc(ds_resp, host2, port2);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 2);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host2, port2));

    // Delete host2. host1 should remain current.
    DeleteResponseEntryAndProc(ds_resp, host2, port2);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host2, port2));

    // Even after removing host1, current should continue to be host1
    DeleteResponseEntryAndProc(ds_resp, host1, port1);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 0);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 1);
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host2, port2));

    stat_peer = peer_finder_->get_static_peer();
    EXPECT_EQ(stat_peer.size(), 0);
}

// [+1 +2], -1, [-2 +1]
TEST_F(PeerServerFinderTest, DSResp3) {
    InitFinder("");
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 0);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);

    std::string host1 = "1.1.1.1";
    unsigned short port1 = 1111;
    std::string host1str = "1.1.1.1:1111";
    std::string host2 = "1.1.1.2";
    unsigned short port2 = 1112;
    std::string host2str = "1.1.1.2:1112";

    std::string stat_peer = peer_finder_->get_static_peer();
    EXPECT_EQ(stat_peer.size(), 0);

    // Add host1 and host2
    std::vector<DSResponse> ds_resp;
    AddResponseEntry(ds_resp, host1, port1);
    AddResponseEntryAndProc(ds_resp, host2, port2);
    std::string curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 2);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host2, port2));

    // After deleting host1, host2 should become current
    DeleteResponseEntryAndProc(ds_resp, host1, port1);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host2str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 1);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host2, port2));

    // After adding host1 and deleting host2, host1 should become current 
    AddResponseEntry(ds_resp, host1, port1);
    DeleteResponseEntryAndProc(ds_resp, host2, port2);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 2);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host2, port2));

    stat_peer = peer_finder_->get_static_peer();
    EXPECT_EQ(stat_peer.size(), 0);
}

// +1, +2, +3, -1, -2, -3
TEST_F(PeerServerFinderTest, DSResp4) {
    InitFinder("");
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 0);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);

    std::string host1 = "1.1.1.1";
    unsigned short port1 = 1111;
    std::string host1str = "1.1.1.1:1111";
    std::string host2 = "1.1.1.2";
    unsigned short port2 = 1112;
    std::string host2str = "1.1.1.2:1112";
    std::string host3 = "1.1.1.3";
    unsigned short port3 = 1113;
    std::string host3str = "1.1.1.3:1113";

    std::string stat_peer = peer_finder_->get_static_peer();
    EXPECT_EQ(stat_peer.size(), 0);

    // Add host1. host1 should be current
    std::vector<DSResponse> ds_resp;
    AddResponseEntryAndProc(ds_resp, host1, port1);
    std::string curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host2, port2));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host3, port3));

    // Add host2. host1 should be current
    AddResponseEntryAndProc(ds_resp, host2, port2);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 2);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host2, port2));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host3, port3));

    // Add host3. host1 should be current
    AddResponseEntryAndProc(ds_resp, host3, port3);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 3);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host2, port2));
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host3, port3));

    // After deleting host1, host2 should become current
    DeleteResponseEntryAndProc(ds_resp, host1, port1);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host2str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 2);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 1);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host2, port2));
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host3, port3));

    // After deleting host2, host3 should become current
    DeleteResponseEntryAndProc(ds_resp, host2, port2);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host3str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 2);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host2, port2));
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host3, port3));

    // After deleting host3, host3 should remain current
    DeleteResponseEntryAndProc(ds_resp, host3, port3);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host3str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 0);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 2);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 1);
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host2, port2));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host3, port3));

    stat_peer = peer_finder_->get_static_peer();
    EXPECT_EQ(stat_peer.size(), 0);
}

// +1, +2, +3, [-1 -3], +1, -1, -2
TEST_F(PeerServerFinderTest, DSResp5) {
    InitFinder("");
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 0);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);

    std::string host1 = "1.1.1.1";
    unsigned short port1 = 1111;
    std::string host1str = "1.1.1.1:1111";
    std::string host2 = "1.1.1.2";
    unsigned short port2 = 1112;
    std::string host2str = "1.1.1.2:1112";
    std::string host3 = "1.1.1.3";
    unsigned short port3 = 1113;
    std::string host3str = "1.1.1.3:1113";

    std::string stat_peer = peer_finder_->get_static_peer();
    EXPECT_EQ(stat_peer.size(), 0);

    // Add host1. host1 should be current
    std::vector<DSResponse> ds_resp;
    AddResponseEntryAndProc(ds_resp, host1, port1);
    std::string curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host2, port2));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host3, port3));

    // Add host2. host1 should be current
    AddResponseEntryAndProc(ds_resp, host2, port2);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 2);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host2, port2));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host3, port3));

    // Add host3. host1 should be current
    AddResponseEntryAndProc(ds_resp, host3, port3);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 3);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host2, port2));
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host3, port3));

    // After deleting host1 and host3, host2 should become current
    DeleteResponseEntry(ds_resp, host1, port1);
    DeleteResponseEntryAndProc(ds_resp, host3, port3);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host2str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 1);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host2, port2));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host3, port3));

    // After adding host1, host2 should remain current 
    AddResponseEntryAndProc(ds_resp, host1, port1);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host2str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 2);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 1);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host2, port2));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host3, port3));

    // After deleting host1, host2 should remain current
    DeleteResponseEntryAndProc(ds_resp, host1, port1);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host2str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 1);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host2, port2));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host3, port3));

    // After deleting host2, host2 should continue to remain current
    DeleteResponseEntryAndProc(ds_resp, host2, port2);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host2str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 0);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 1);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 1);
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host2, port2));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host3, port3));

    stat_peer = peer_finder_->get_static_peer();
    EXPECT_EQ(stat_peer.size(), 0);
}

// +1, -1, +1, -1, +2, -2, +2, -2
TEST_F(PeerServerFinderTest, DSResp6) {
    InitFinder("");
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 0);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);

    std::string host1 = "1.1.1.1";
    unsigned short port1 = 1111;
    std::string host1str = "1.1.1.1:1111";

    std::string stat_peer = peer_finder_->get_static_peer();
    EXPECT_EQ(stat_peer.size(), 0);

    // Add host1. host1 should be current
    std::vector<DSResponse> ds_resp;
    AddResponseEntryAndProc(ds_resp, host1, port1);
    std::string curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host1, port1));

    // Delete host1. host1 should remain current
    DeleteResponseEntryAndProc(ds_resp, host1, port1);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 0);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 1);
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host1, port1));

    // Add host1. host1 should remain current
    AddResponseEntryAndProc(ds_resp, host1, port1);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 1);
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host1, port1));

    // Delete host1. host1 should remain current
    DeleteResponseEntryAndProc(ds_resp, host1, port1);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 0);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 2);
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host1, port1));

    std::string host2 = "2.2.2.2";
    unsigned short port2 = 2222;
    std::string host2str = "2.2.2.2:2222";

    // Add host2. host2 should become current
    AddResponseEntryAndProc(ds_resp, host2, port2);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host2str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 1);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 2);
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host2, port2));

    // Delete host2. host2 should remain current
    DeleteResponseEntryAndProc(ds_resp, host2, port2);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host2str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 0);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 1);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 3);
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host2, port2));

    // Add host2. host2 should remain current
    AddResponseEntryAndProc(ds_resp, host2, port2);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host2str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 1);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 3);
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host2, port2));

    // Delete host2. host2 should remain current
    DeleteResponseEntryAndProc(ds_resp, host2, port2);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host2str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 0);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 1);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 4);
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host2, port2));

    stat_peer = peer_finder_->get_static_peer();
    EXPECT_EQ(stat_peer.size(), 0);
}

// +1, -1, +2, -2, +1, -1, +2, -2
TEST_F(PeerServerFinderTest, DSResp7) {
    InitFinder("");
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 0);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);

    std::string host1 = "1.1.1.1";
    unsigned short port1 = 1111;
    std::string host1str = "1.1.1.1:1111";
    std::string host2 = "2.2.2.2";
    unsigned short port2 = 2222;
    std::string host2str = "2.2.2.2:2222";

    std::string stat_peer = peer_finder_->get_static_peer();
    EXPECT_EQ(stat_peer.size(), 0);

    // Add host1. host1 should be current
    std::vector<DSResponse> ds_resp;
    AddResponseEntryAndProc(ds_resp, host1, port1);
    std::string curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 0);
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host2, port2));

    // Delete host1. host1 should remain current
    DeleteResponseEntryAndProc(ds_resp, host1, port1);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 0);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 0);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 1);
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host2, port2));

    // Add host2. host2 should become current
    AddResponseEntryAndProc(ds_resp, host2, port2);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host2str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 1);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 1);
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host2, port2));

    // Delete host2. host2 should remain current
    DeleteResponseEntryAndProc(ds_resp, host2, port2);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host2str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 0);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 1);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 2);
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host2, port2));

    // Add host1. host1 should become current
    AddResponseEntryAndProc(ds_resp, host1, port1);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 2);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 2);
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host2, port2));

    // Delete host1. host1 should remain current
    DeleteResponseEntryAndProc(ds_resp, host1, port1);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host1str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 0);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 2);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 3);
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host2, port2));

    // Add host2. host2 should become current
    AddResponseEntryAndProc(ds_resp, host2, port2);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host2str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 3);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 3);
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_TRUE(peer_finder_->HostPortInDSDb(host2, port2));

    // Delete host2. host2 should remain current
    DeleteResponseEntryAndProc(ds_resp, host2, port2);
    curr_peer = peer_finder_->get_current_peer();
    EXPECT_EQ(curr_peer.compare(host2str), 0);
    EXPECT_EQ(peer_finder_->get_peer_ifmap_servers_count(), 0);
    EXPECT_EQ(ifmap_manager_.get_start_connection_count(), 1);
    EXPECT_EQ(ifmap_manager_.get_reset_connection_count(), 3);
    EXPECT_EQ(peer_finder_->get_using_non_ds_peer_count(), 4);
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host1, port1));
    EXPECT_FALSE(peer_finder_->HostPortInDSDb(host2, port2));

    stat_peer = peer_finder_->get_static_peer();
    EXPECT_EQ(stat_peer.size(), 0);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    bool success = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return success;
}
