/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <boost/foreach.hpp>

#include "bgp/bgp_factory.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "io/test/event_manager_test.h"
#include "schema/xmpp_enet_types.h"

using namespace std;

class BgpXmppChannelMock : public BgpXmppChannel {
public:
    BgpXmppChannelMock(XmppChannel *channel, BgpServer *server,
            BgpXmppChannelManager *manager) :
        BgpXmppChannel(channel, server, manager), count_(0) {
            bgp_policy_ = RibExportPolicy(BgpProto::XMPP,
                                          RibExportPolicy::XMPP, -1, 0);
    }

    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        count_ ++;
        BgpXmppChannel::ReceiveUpdate(msg);
    }

    size_t Count() const { return count_; }
    void ResetCount() { count_ = 0; }
    virtual ~BgpXmppChannelMock() { }

private:
    size_t count_;
};

class BgpXmppChannelManagerMock : public BgpXmppChannelManager {
public:
    BgpXmppChannelManagerMock(XmppServer *x, BgpServer *b) :
        BgpXmppChannelManager(x, b) { }

    virtual void XmppHandleChannelEvent(XmppChannel *channel,
                                        xmps::PeerState state) {
         BgpXmppChannelManager::XmppHandleChannelEvent(channel, state);
    }

    virtual BgpXmppChannel *CreateChannel(XmppChannel *channel) {
        BgpXmppChannel *mock_channel =
            new BgpXmppChannelMock(channel, bgp_server_, this);
        return mock_channel;
    }
};

class BgpXmppEvpnMcastTestBase : public ::testing::Test {
public:
    void ValidateShowEvpnTableResponse(Sandesh *sandesh,
        const vector<string> &regular_nves,
        const vector<string> &ar_replicators,
        const vector<string> &ar_leaf_addrs,
        const vector<string> &ar_leaf_replicators) {
        ShowEvpnTableResp *resp = dynamic_cast<ShowEvpnTableResp *>(sandesh);
        EXPECT_TRUE(resp != NULL);
        EXPECT_EQ(1U, resp->get_tables().size());
        const ShowEvpnTable &sevt = resp->get_tables()[0];
        cout << "*****************************************************" << endl;
        cout << sevt.log() << endl;
        cout << "*****************************************************" << endl;
        if (regular_nves != sevt.get_regular_nves())
            return;
        if (ar_replicators != sevt.get_ar_replicators())
            return;
        if (ar_leaf_addrs.size() != sevt.get_ar_leafs().size())
            return;
        if (ar_leaf_replicators.size() != sevt.get_ar_leafs().size())
            return;
        for (size_t idx = 0; idx < sevt.get_ar_leafs().size(); ++idx) {
            if (ar_leaf_addrs[idx] !=
                sevt.get_ar_leafs()[idx].get_address())
                return;
            if (ar_leaf_replicators[idx] !=
                sevt.get_ar_leafs()[idx].get_replicator())
                return;

        }
        validate_done_ = true;
    }

    bool CheckShowEvpnTableResponse(const string &name) {
        ShowEvpnTableReq *show_req = new ShowEvpnTableReq;
        show_req->set_search_string(name);
        show_req->HandleRequest();
        show_req->Release();
        BOOST_FOREACH(test::NetworkAgentMock *tsn, tsns_) {
            validate_done_ |= (tsn->EnetRouteLookup("blue", BroadcastMacXmppId()) == NULL);
        }
        return validate_done_;
    }

protected:
    static const int kAgentCount = 4;
    static const int kMxCount = 4;
    static const int kTsnCount = 1;
    static const int kTorCount = 2;

    typedef set<test::NetworkAgentMock *> VrouterSet;
    typedef map<test::NetworkAgentMock *, string> VrouterToNhAddrMap;

    BgpXmppEvpnMcastTestBase()
        : thread_(&evm_),
          xs_x_(NULL),
          xs_y_(NULL),
          single_server_(false),
          vxlan_(false),
          validate_done_(false),
          tag_(0) {
        EXPECT_TRUE(kAgentCount < 10);
        EXPECT_TRUE(kMxCount < 10);
        EXPECT_TRUE(kTsnCount < 10);
        EXPECT_TRUE(kTorCount < 10);
        mx_params_.edge_replication_not_supported = true;
        tsn_params_.assisted_replication_supported = true;
    }

    virtual void SetUp() {
        bs_x_.reset(new BgpServerTest(&evm_, "X"));
        xs_x_ = new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);
        bs_x_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created server at port: " <<
            bs_x_->session_manager()->GetPort());
        xs_x_->Initialize(0, false);
        bgp_channel_manager_x_.reset(
            new BgpXmppChannelManagerMock(xs_x_, bs_x_.get()));

        bs_y_.reset(new BgpServerTest(&evm_, "Y"));
        xs_y_ = new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);
        bs_y_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created server at port: " <<
            bs_y_->session_manager()->GetPort());
        xs_y_->Initialize(0, false);
        bgp_channel_manager_y_.reset(
            new BgpXmppChannelManagerMock(xs_y_, bs_y_.get()));

        thread_.Start();
        task_util::WaitForIdle();
    }

    virtual void TearDown() {
        xs_x_->Shutdown();
        task_util::WaitForIdle();
        bs_x_->Shutdown();
        task_util::WaitForIdle();
        bgp_channel_manager_x_.reset();
        TcpServerManager::DeleteServer(xs_x_);
        xs_x_ = NULL;

        xs_y_->Shutdown();
        task_util::WaitForIdle();
        bs_y_->Shutdown();
        task_util::WaitForIdle();
        bgp_channel_manager_y_.reset();
        TcpServerManager::DeleteServer(xs_y_);
        xs_y_ = NULL;

        DeleteAgents();
        DeleteMxs();
        DeleteTors();
        DeleteTsns();
        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
        STLDeleteValues(&agents_);
        STLDeleteValues(&mxs_);
        STLDeleteValues(&tors_);
        STLDeleteValues(&tsns_);
    }

    string BroadcastMac() {
        return integerToString(tag_) + "-" + "ff:ff:ff:ff:ff:ff,0.0.0.0/32";
    }

    string BroadcastMacXmppId() {
        if (tag_ == 0)
            return "ff:ff:ff:ff:ff:ff,0.0.0.0/32";
        return BroadcastMac();
    }

    string TorBroadcastMac(test::NetworkAgentMock *tor) {
        string tor_addr = GetVrouterNexthopAddress(tor);
        return integerToString(tag_) + "-" +
            string("ff:ff:ff:ff:ff:ff,") + tor_addr + "/32";
    }

    string TorBroadcastMacXmppId(test::NetworkAgentMock *tor) {
        string tor_addr = GetVrouterNexthopAddress(tor);
        if (tag_ == 0)
            return string("ff:ff:ff:ff:ff:ff,") + tor_addr + "/32";
        return TorBroadcastMac(tor);
    }

    test::NextHop BuildNextHop(const string &nexthop) {
        if (vxlan_) {
            return test::NextHop(nexthop, tag_, "vxlan");
        } else {
            return test::NextHop(nexthop);
        }
    }

    string GetVrouterNexthopAddress(test::NetworkAgentMock *vrouter) {
        VrouterToNhAddrMap::const_iterator loc = vrouter_map_.find(vrouter);
        assert(loc != vrouter_map_.end());
        return loc->second;
    }

    bool VerifyVrouterInOListCommon(test::NetworkAgentMock *vrouter, int idx,
        const autogen::EnetItemType *rt, bool leaf) {
        const autogen::EnetOlistType &olist =
            leaf ? rt->entry.leaf_olist : rt->entry.olist;
        if (olist.next_hop.size() == 0)
            return false;

        bool found = false;
        string nh_addr = GetVrouterNexthopAddress(vrouter);
        BOOST_FOREACH(const autogen::EnetNextHopType &nh, olist.next_hop) {
            if (nh.address == nh_addr) {
                if (vxlan_) {
                    TASK_UTIL_EXPECT_EQ(tag_, nh.label);
                } else {
                    TASK_UTIL_EXPECT_NE(0, nh.label);
                }
                EXPECT_FALSE(found);
                found = true;
            }
        }
        return found;
    }

    bool VerifyVrouterInOList(test::NetworkAgentMock *vrouter, int idx,
        const autogen::EnetItemType *rt) {
        return VerifyVrouterInOListCommon(vrouter, idx, rt, false);
    }

    bool VerifyVrouterInLeafOList(test::NetworkAgentMock *vrouter, int idx,
        const autogen::EnetItemType *rt) {
        return VerifyVrouterInOListCommon(vrouter, idx, rt, true);
    }

    bool VerifyVrouterNotInOListCommon(test::NetworkAgentMock *vrouter, int idx,
        const autogen::EnetItemType *rt, bool leaf) {
        if (!rt)
            return false;

        const autogen::EnetOlistType &olist =
            leaf ? rt->entry.leaf_olist : rt->entry.olist;
        if (olist.next_hop.size() == 0)
            return false;

        string nh_addr = GetVrouterNexthopAddress(vrouter);
        BOOST_FOREACH(const autogen::EnetNextHopType &nh, olist.next_hop) {
            if (nh.address == nh_addr)
                return false;
        }
        return true;
    }

    bool VerifyVrouterNotInOList(test::NetworkAgentMock *vrouter,
        int idx, const autogen::EnetItemType *rt) {
        return VerifyVrouterNotInOListCommon(vrouter, idx, rt, false);
    }

    bool VerifyVrouterNotInLeafOList(test::NetworkAgentMock *vrouter,
        int idx, const autogen::EnetItemType *rt) {
        return VerifyVrouterNotInOListCommon(vrouter, idx, rt, true);
    }

    bool CheckOListCommon(test::NetworkAgentMock *vrouter,
        bool odd, bool even, bool include_agents,
        bool odd_agents_tors, bool even_agents_tors, bool include_tors) {
        task_util::TaskSchedulerLock lock;

        string key;
        if (IsTor(vrouter)) {
            key = TorBroadcastMacXmppId(vrouter);
        } else {
            key = BroadcastMacXmppId();
        }
        const autogen::EnetItemType *rt = vrouter->EnetRouteLookup("blue", key);
        if (rt == NULL)
            return false;

        size_t count = 0;
        int mx_idx = 0;
        BOOST_FOREACH(test::NetworkAgentMock *mx, mxs_) {
            if (vrouter == mx) {
                mx_idx++;
                continue;
            }
            if ((odd && mx_idx % 2 != 0) || (even && mx_idx % 2 == 0)) {
                if (!rt)
                    return false;
                if (!VerifyVrouterInOList(mx, mx_idx, rt))
                    return false;
                count++;
            } else {
                if (!VerifyVrouterNotInOList(mx, mx_idx, rt))
                    return false;
            }
            mx_idx++;
        }

        int agent_idx = 0;
        if (include_agents) {
            BOOST_FOREACH(test::NetworkAgentMock *agent, agents_) {
                if (!rt)
                    return false;
                if ((odd_agents_tors && agent_idx % 2 != 0) ||
                    (even_agents_tors && agent_idx % 2 == 0)) {
                    if (!VerifyVrouterInOList(agent, agent_idx, rt))
                        return false;
                    count++;
                } else {
                    if (!VerifyVrouterNotInOList(agent, agent_idx, rt))
                        return false;
                }
                agent_idx++;
            }
        }

        const autogen::EnetOlistType &olist = rt->entry.olist;
        if (olist.next_hop.size() != count)
            return false;

        if (include_tors && IsTsn(vrouter)) {
            int tsn_idx = GetTsnIdx(vrouter);
            size_t leaf_count = 0;
            int tor_idx = 0;
            BOOST_FOREACH(test::NetworkAgentMock *tor, tors_) {
                if (tor_replicator_address_list_[tor_idx] ==
                    tsn_address_list_[tsn_idx] &&
                    ((odd_agents_tors && tor_idx % 2 != 0) ||
                    (even_agents_tors && tor_idx % 2 == 0))) {
                    if (!VerifyVrouterInLeafOList(tor, tor_idx, rt))
                        return false;
                    leaf_count++;
                } else {
                    if (!VerifyVrouterNotInLeafOList(tor, tor_idx, rt))
                        return false;
                }
                tor_idx++;
            }

            const autogen::EnetOlistType &olist = rt->entry.leaf_olist;
            if (olist.next_hop.size() != leaf_count)
                return false;
        } else {
            const autogen::EnetOlistType &olist = rt->entry.leaf_olist;
            if (olist.next_hop.size() != 0)
                return false;
        }

        return true;
    }

    void VerifyOListCommon(test::NetworkAgentMock *vrouter,
        bool odd, bool even, bool include_agents,
        bool odd_agents_tors = true, bool even_agents_tors = true,
        bool include_tors = false) {
        TASK_UTIL_EXPECT_TRUE(CheckOListCommon(vrouter, odd, even,
            include_agents, odd_agents_tors, even_agents_tors, include_tors));
    }

    void CreateAgents() {
        for (int idx = 0; idx < kAgentCount; ++idx) {
            string name = string("agent-") + integerToString(idx);
            string local_addr = string("127.0.0.1") + integerToString(idx);
            string remote_addr;
            int port;
            if (single_server_) {
                remote_addr = "127.0.0.1";
                port = xs_x_->GetPort();
            } else {
                remote_addr = (idx % 2 == 0) ? "127.0.0.1" : "127.0.0.2";
                port = (idx % 2 == 0) ? xs_x_->GetPort() : xs_y_->GetPort();
            }
            test::NetworkAgentMock *agent = new test::NetworkAgentMock(
                &evm_, name, port, local_addr, remote_addr);
            agents_.push_back(agent);
            agent_set_.insert(agent);
            string nh_addr = string("192.168.1.1") + integerToString(idx);
            vrouter_map_.insert(make_pair(agent, nh_addr));
            TASK_UTIL_EXPECT_TRUE(agent->IsEstablished());
        }
    }

    void DeleteAgents() {
        BOOST_FOREACH(test::NetworkAgentMock *agent, agents_) {
            agent->Delete();
        }
    }

    bool IsAgent(test::NetworkAgentMock *agent) {
        return (agent_set_.find(agent) != agent_set_.end());
    }

    void SubscribeAgents() {
        BOOST_FOREACH(test::NetworkAgentMock *agent, agents_) {
            agent->EnetSubscribe("blue", 1);
            task_util::WaitForIdle();
        }
    }

    void UnsubscribeAgents() {
        BOOST_FOREACH(test::NetworkAgentMock *agent, agents_) {
            agent->EnetUnsubscribe("blue");
            task_util::WaitForIdle();
        }
    }

    void AddAgentsBroadcastMacRouteCommon(bool odd, bool even) {
        int idx = 0;
        BOOST_FOREACH(test::NetworkAgentMock *agent, agents_) {
            string nh_addr = string("192.168.1.1") + integerToString(idx);
            if ((odd && idx % 2 != 0) || (even && idx % 2 == 0)) {
                agent->AddEnetRoute(
                    "blue", BroadcastMac(), BuildNextHop(nh_addr));
                task_util::WaitForIdle();
            }
            idx++;
        }
    }

    void AddOddAgentsBroadcastMacRoute() {
        AddAgentsBroadcastMacRouteCommon(true, false);
    }

    void AddEvenAgentsBroadcastMacRoute() {
        AddAgentsBroadcastMacRouteCommon(false, true);
    }

    void AddAllAgentsBroadcastMacRoute() {
        AddAgentsBroadcastMacRouteCommon(true, true);
    }

    void DelAgentsBroadcastMacRouteCommon(bool odd, bool even) {
        int idx = 0;
        BOOST_FOREACH(test::NetworkAgentMock *agent, agents_) {
            if ((odd && idx % 2 != 0) || (even && idx % 2 == 0)) {
                agent->DeleteEnetRoute("blue", BroadcastMac());
                task_util::WaitForIdle();
            }
            idx++;
        }
    }

    void DelOddAgentsBroadcastMacRoute() {
        DelAgentsBroadcastMacRouteCommon(true, false);
    }

    void DelEvenAgentsBroadcastMacRoute() {
        DelAgentsBroadcastMacRouteCommon(false, true);
    }
    void DelAllAgentsBroadcastMacRoute() {
        DelAgentsBroadcastMacRouteCommon(true, true);
    }

    void VerifyAllAgentsOddMxsOlist() {
        BOOST_FOREACH(test::NetworkAgentMock *agent, agents_) {
            VerifyOListCommon(agent, true, false, false);
        }
    }

    void VerifyAllAgentsEvenMxsOlist() {
        BOOST_FOREACH(test::NetworkAgentMock *agent, agents_) {
            VerifyOListCommon(agent, false, true, false);
        }
    }

    void VerifyAgentsOlistCommon(bool odd, bool even) {
        int idx = 0;
        BOOST_FOREACH(test::NetworkAgentMock *agent, agents_) {
            if ((odd && idx % 2 != 0) || (even && idx % 2 == 0)) {
                VerifyOListCommon(agent, true, true, false);
            } else {
                TASK_UTIL_EXPECT_TRUE(
                    agent->EnetRouteLookup("blue", BroadcastMacXmppId()) == NULL);
            }
            idx++;
        }
    }

    void VerifyOddAgentsOlist() {
        VerifyAgentsOlistCommon(true, false);
    }

    void VerifyEvenAgentsOlist() {
        VerifyAgentsOlistCommon(false, true);
    }

    void VerifyAllAgentsOlist() {
        VerifyAgentsOlistCommon(true, true);
    }

    void VerifyAllAgentsNoOlist() {
        VerifyAgentsOlistCommon(false, false);
    }

    void CreateMxs() {
        for (int idx = 0; idx < kMxCount; ++idx) {
            string name = string("mx-") + integerToString(idx);
            string local_addr = string("127.0.0.2") + integerToString(idx);
            string remote_addr;
            int port;
            if (single_server_) {
                remote_addr = "127.0.0.1";
                port = xs_x_->GetPort();
            } else {
                remote_addr = (idx % 2 == 0) ? "127.0.0.1" : "127.0.0.2";
                port = (idx % 2 == 0) ? xs_x_->GetPort() : xs_y_->GetPort();
            }
            test::NetworkAgentMock *mx = new test::NetworkAgentMock(
                &evm_, name, port, local_addr, remote_addr);
            mxs_.push_back(mx);
            mx_set_.insert(mx);
            string nh_addr = string("192.168.1.2") + integerToString(idx);
            vrouter_map_.insert(make_pair(mx, nh_addr));
            mx_address_list_.push_back(nh_addr);
            TASK_UTIL_EXPECT_TRUE(mx->IsEstablished());
        }
    }

    void DeleteMxs() {
        BOOST_FOREACH(test::NetworkAgentMock *mx, mxs_) {
            mx->Delete();
        }
    }

    bool IsMx(test::NetworkAgentMock *mx) {
        return (mx_set_.find(mx) != mx_set_.end());
    }

    void SubscribeMxs() {
        BOOST_FOREACH(test::NetworkAgentMock *mx, mxs_) {
            mx->EnetSubscribe("blue", 1);
            task_util::WaitForIdle();
        }
    }

    void UnsubscribeMxs() {
        BOOST_FOREACH(test::NetworkAgentMock *mx, mxs_) {
            mx->EnetUnsubscribe("blue");
            task_util::WaitForIdle();
        }
    }

    void AddMxsBroadcastMacRouteCommon(bool odd, bool even) {
        int idx = 0;
        BOOST_FOREACH(test::NetworkAgentMock *mx, mxs_) {
            string nh_addr = string("192.168.1.2") + integerToString(idx);
            if ((odd && idx % 2 != 0) || (even && idx % 2 == 0)) {
                mx->AddEnetRoute(
                    "blue", BroadcastMac(), BuildNextHop(nh_addr), &mx_params_);
                task_util::WaitForIdle();
            }
            idx++;
        }
    }

    void AddOddMxsBroadcastMacRoute() {
        AddMxsBroadcastMacRouteCommon(true, false);
    }

    void AddEvenMxsBroadcastMacRoute() {
        AddMxsBroadcastMacRouteCommon(false, true);
    }

    void AddAllMxsBroadcastMacRoute() {
        AddMxsBroadcastMacRouteCommon(true, true);
    }

    void DelMxsBroadcastMacRouteCommon(bool odd, bool even) {
        int idx = 0;
        BOOST_FOREACH(test::NetworkAgentMock *mx, mxs_) {
            if ((odd && idx % 2 != 0) || (even && idx % 2 == 0)) {
                mx->DeleteEnetRoute("blue", BroadcastMac());
                task_util::WaitForIdle();
            }
            idx++;
        }
    }

    void DelOddMxsBroadcastMacRoute() {
        DelMxsBroadcastMacRouteCommon(true, false);
    }

    void DelEvenMxsBroadcastMacRoute() {
        DelMxsBroadcastMacRouteCommon(false, true);
    }

    void DelAllMxsBroadcastMacRoute() {
        DelMxsBroadcastMacRouteCommon(true, true);
    }

    void VerifyMxsOlistCommon(bool odd, bool even,
        bool odd_agents, bool even_agents) {
        int idx = 0;
        BOOST_FOREACH(test::NetworkAgentMock *mx, mxs_) {
            if ((odd && idx % 2 != 0) || (even && idx % 2 == 0)) {
                VerifyOListCommon(mx, odd, even, true, odd_agents, even_agents);
            } else {
                TASK_UTIL_EXPECT_TRUE(
                    mx->EnetRouteLookup("blue", BroadcastMacXmppId()) == NULL);
            }
            idx++;
        }
    }

    void VerifyOddMxsOlist(bool odd_agents = true, bool even_agents = true) {
        VerifyMxsOlistCommon(true, false, odd_agents, even_agents);
    }

    void VerifyEvenMxsOlist(bool odd_agents = true, bool even_agents = true) {
        VerifyMxsOlistCommon(false, true, odd_agents, even_agents);
    }

    void VerifyAllMxsOlist(bool odd_agents = true, bool even_agents = true) {
        VerifyMxsOlistCommon(true, true, odd_agents, even_agents);
    }

    void VerifyAllMxsNoOlist() {
        VerifyMxsOlistCommon(false, false, false, false);
    }

    void CreateTors() {
        EXPECT_NE(0U, tsns_.size());
        for (int idx = 0; idx < kTorCount; ++idx) {
            string name = string("tor-") + integerToString(idx);
            string remote_addr;
            int port;
            if (single_server_) {
                remote_addr = "127.0.0.1";
                port = xs_x_->GetPort();
            } else {
                remote_addr = (idx % 2 == 0) ? "127.0.0.1" : "127.0.0.2";
                port = (idx % 2 == 0) ? xs_x_->GetPort() : xs_y_->GetPort();
            }
            test::NetworkAgentMock *tor = new test::NetworkAgentMock(
                &evm_, name, port, "127.0.0.30", remote_addr);
            tors_.push_back(tor);
            tor_set_.insert(tor);
            TASK_UTIL_EXPECT_TRUE(tor->IsEstablished());
            EXPECT_TRUE(kTorCount % kTsnCount == 0);
            int tors_per_tsn = kTorCount / kTsnCount;
            int tsn_idx = idx / tors_per_tsn;
            string tsn_address = tsn_address_list_[tsn_idx];
            tor_replicator_address_list_.push_back(tsn_address);
            string nh_addr = string("192.168.1.3") + integerToString(idx);
            vrouter_map_.insert(make_pair(tor, nh_addr));
            tor_address_list_.push_back(nh_addr);
        }
    }

    void DeleteTors() {
        BOOST_FOREACH(test::NetworkAgentMock *tor, tors_) {
            tor->Delete();
        }
    }

    bool IsTor(test::NetworkAgentMock *tor) {
        return (tor_set_.find(tor) != tor_set_.end());
    }

    void SubscribeTors() {
        BOOST_FOREACH(test::NetworkAgentMock *tor, tors_) {
            tor->EnetSubscribe("blue", 1);
            task_util::WaitForIdle();
        }
    }

    void UnsubscribeTors() {
        BOOST_FOREACH(test::NetworkAgentMock *tor, tors_) {
            tor->EnetUnsubscribe("blue");
            task_util::WaitForIdle();
        }
    }

    void AddTorsBroadcastMacRouteCommon(bool odd, bool even) {
        int idx = 0;
        BOOST_FOREACH(test::NetworkAgentMock *tor, tors_) {
            string nh_addr = string("192.168.1.3") + integerToString(idx);
            test::RouteParams tor_params;
            tor_params.replicator_address = tor_replicator_address_list_[idx];
            if ((odd && idx % 2 != 0) || (even && idx % 2 == 0)) {
                tor->AddEnetRoute("blue",
                    TorBroadcastMac(tor), BuildNextHop(nh_addr), &tor_params);
                task_util::WaitForIdle();
            }
            idx++;
        }
    }

    void AddOddTorsBroadcastMacRoute() {
        AddTorsBroadcastMacRouteCommon(true, false);
    }

    void AddEvenTorsBroadcastMacRoute() {
        AddTorsBroadcastMacRouteCommon(false, true);
    }

    void AddAllTorsBroadcastMacRoute() {
        AddTorsBroadcastMacRouteCommon(true, true);
    }

    void DelTorsBroadcastMacRouteCommon(bool odd, bool even) {
        int idx = 0;
        BOOST_FOREACH(test::NetworkAgentMock *tor, tors_) {
            if ((odd && idx % 2 != 0) || (even && idx % 2 == 0)) {
                tor->DeleteEnetRoute("blue", TorBroadcastMac(tor));
                task_util::WaitForIdle();
            }
            idx++;
        }
    }

    void DelOddTorsBroadcastMacRoute() {
        DelTorsBroadcastMacRouteCommon(true, false);
    }

    void DelEvenTorsBroadcastMacRoute() {
        DelTorsBroadcastMacRouteCommon(false, true);
    }

    void DelAllTorsBroadcastMacRoute() {
        DelTorsBroadcastMacRouteCommon(true, true);
    }

    void CreateTsns() {
        for (int idx = 0; idx < kTsnCount; ++idx) {
            string name = string("tsn-") + integerToString(idx);
            string local_addr = string("127.0.0.4") + integerToString(idx);
            string remote_addr;
            int port;
            if (single_server_) {
                remote_addr = "127.0.0.1";
                port = xs_x_->GetPort();
            } else {
                remote_addr = (idx % 2 == 0) ? "127.0.0.1" : "127.0.0.2";
                port = (idx % 2 == 0) ? xs_x_->GetPort() : xs_y_->GetPort();
            }
            test::NetworkAgentMock *tsn = new test::NetworkAgentMock(
                &evm_, name, port, local_addr, remote_addr);
            tsns_.push_back(tsn);
            tsn_set_.insert(tsn);
            TASK_UTIL_EXPECT_TRUE(tsn->IsEstablished());
            string nh_addr = string("192.168.1.4") + integerToString(idx);
            tsn_address_list_.push_back(nh_addr);
            vrouter_map_.insert(make_pair(tsn, nh_addr));
        }
    }

    void DeleteTsns() {
        BOOST_FOREACH(test::NetworkAgentMock *tsn, tsns_) {
            tsn->Delete();
        }
    }

    bool IsTsn(test::NetworkAgentMock *tsn) {
        return (tsn_set_.find(tsn) != tsn_set_.end());
    }

    int GetTsnIdx(test::NetworkAgentMock *tsn) {
        for (size_t idx = 0; idx < tsns_.size(); ++idx) {
            if (tsns_[idx] == tsn)
                return idx;
        }
        assert(false);
        return -1;
    }

    void SubscribeTsns() {
        BOOST_FOREACH(test::NetworkAgentMock *tsn, tsns_) {
            tsn->EnetSubscribe("blue", 1);
            task_util::WaitForIdle();
        }
    }

    void UnsubscribeTsns() {
        BOOST_FOREACH(test::NetworkAgentMock *tsn, tsns_) {
            tsn->EnetUnsubscribe("blue");
            task_util::WaitForIdle();
        }
    }

    void AddTsnsBroadcastMacRouteCommon(bool odd, bool even) {
        int idx = 0;
        BOOST_FOREACH(test::NetworkAgentMock *tsn, tsns_) {
            string nh_addr = string("192.168.1.4") + integerToString(idx);
            if ((odd && idx % 2 != 0) || (even && idx % 2 == 0)) {
                tsn->AddEnetRoute("blue",
                    BroadcastMac(), BuildNextHop(nh_addr), &tsn_params_);
                task_util::WaitForIdle();
            }
            idx++;
        }
    }

    void AddAllTsnsBroadcastMacRoute() {
        AddTsnsBroadcastMacRouteCommon(true, true);
    }

    void DelTsnsBroadcastMacRouteCommon(bool odd, bool even) {
        int idx = 0;
        BOOST_FOREACH(test::NetworkAgentMock *tsn, tsns_) {
            if ((odd && idx % 2 != 0) || (even && idx % 2 == 0)) {
                tsn->DeleteEnetRoute("blue", BroadcastMac());
                task_util::WaitForIdle();
            }
            idx++;
        }
    }

    void DelAllTsnsBroadcastMacRoute() {
        DelTsnsBroadcastMacRouteCommon(true, true);
    }

    void VerifyTsnsOlistCommon(bool odd_mx, bool even_mx,
        bool odd_tor, bool even_tor, bool include_tors) {
        BOOST_FOREACH(test::NetworkAgentMock *tsn, tsns_) {
            if (odd_mx || even_mx || include_tors) {
                VerifyOListCommon(tsn,
                    odd_mx, even_mx, false, odd_tor, even_tor, include_tors);
            } else {
                TASK_UTIL_EXPECT_TRUE(
                    tsn->EnetRouteLookup("blue", BroadcastMacXmppId()) == NULL);
            }
        }
    }

    void VerifyAllTsnsLeafOlist() {
        VerifyTsnsOlistCommon(false, false, true, true, true);
    }

    void VerifyAllTsnsOddLeafOlist() {
        VerifyTsnsOlistCommon(false, false, true, false, true);
    }

    void VerifyAllTsnsEvenLeafOlist() {
        VerifyTsnsOlistCommon(false, false, false, true, true);
    }

    void VerifyAllTsnsAllOlist() {
        VerifyTsnsOlistCommon(true, true, true, true, true);
    }

    void VerifyAllTsnsNoOlist() {
        VerifyTsnsOlistCommon(false, false, false, false, false);
    }

    EventManager evm_;
    ServerThread thread_;
    BgpServerTestPtr bs_x_;
    BgpServerTestPtr bs_y_;
    XmppServer *xs_x_;
    XmppServer *xs_y_;
    boost::scoped_ptr<BgpXmppChannelManagerMock> bgp_channel_manager_x_;
    boost::scoped_ptr<BgpXmppChannelManagerMock> bgp_channel_manager_y_;
    vector<test::NetworkAgentMock *> agents_;
    vector<test::NetworkAgentMock *> mxs_;
    vector<test::NetworkAgentMock *> tors_;
    vector<test::NetworkAgentMock *> tsns_;
    VrouterSet agent_set_;
    VrouterSet mx_set_;
    VrouterSet tor_set_;
    VrouterSet tsn_set_;
    VrouterToNhAddrMap vrouter_map_;
    vector<string> mx_address_list_;
    vector<string> tor_address_list_;
    vector<string> tor_replicator_address_list_;
    vector<string> tsn_address_list_;
    test::RouteParams mx_params_;
    test::RouteParams tsn_params_;
    bool single_server_;
    bool vxlan_;
    bool validate_done_;
    int tag_;
};

static const char *config_template = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
                <family>e-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>e-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <virtual-network name='blue'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <routing-instance name='blue'>\
        <virtual-network>blue</virtual-network>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
</config>\
";

// Parameterize single vs. dual servers and the tag value.

typedef std::tr1::tuple<bool, uint32_t> TestParams1;

//
// 2 Control Nodes X and Y.
//
class BgpXmppEvpnMcastTest :
    public BgpXmppEvpnMcastTestBase,
    public ::testing::WithParamInterface<TestParams1> {

protected:
    virtual void SetUp() {
        single_server_ = std::tr1::get<0>(GetParam());
        tag_ = std::tr1::get<1>(GetParam());
        BgpXmppEvpnMcastTestBase::SetUp();
    }

    virtual void TearDown() {
        BgpXmppEvpnMcastTestBase::TearDown();
    }

    void Configure() {
        char config[8192];
        snprintf(config, sizeof(config), config_template,
            bs_x_->session_manager()->GetPort(),
            bs_y_->session_manager()->GetPort());
        bs_x_->Configure(config);
        bs_y_->Configure(config);
        task_util::WaitForIdle();
    }
};

TEST_P(BgpXmppEvpnMcastTest, Noop) {
    Configure();
}

TEST_P(BgpXmppEvpnMcastTest, Basic1) {
    Configure();
    CreateAgents();
    CreateMxs();
    SubscribeAgents();
    SubscribeMxs();

    AddAllAgentsBroadcastMacRoute();
    AddAllMxsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist();

    DelAllAgentsBroadcastMacRoute();
    DelAllMxsBroadcastMacRoute();
    UnsubscribeAgents();
    UnsubscribeMxs();
}

TEST_P(BgpXmppEvpnMcastTest, Basic2) {
    Configure();
    CreateAgents();
    CreateMxs();
    SubscribeAgents();
    SubscribeMxs();

    AddAllMxsBroadcastMacRoute();
    AddAllAgentsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist();

    DelAllMxsBroadcastMacRoute();
    DelAllAgentsBroadcastMacRoute();
    UnsubscribeAgents();
    UnsubscribeMxs();
}

TEST_P(BgpXmppEvpnMcastTest, Basic3) {
    Configure();
    CreateAgents();
    SubscribeAgents();
    AddAllAgentsBroadcastMacRoute();

    CreateMxs();
    SubscribeMxs();
    AddAllMxsBroadcastMacRoute();

    VerifyAllAgentsOlist();
    VerifyAllMxsOlist();

    DelAllAgentsBroadcastMacRoute();
    DelAllMxsBroadcastMacRoute();
    UnsubscribeAgents();
    UnsubscribeMxs();
}

TEST_P(BgpXmppEvpnMcastTest, Basic4) {
    Configure();

    CreateMxs();
    SubscribeMxs();
    AddAllMxsBroadcastMacRoute();

    CreateAgents();
    SubscribeAgents();
    AddAllAgentsBroadcastMacRoute();

    VerifyAllAgentsOlist();
    VerifyAllMxsOlist();

    DelAllMxsBroadcastMacRoute();
    DelAllAgentsBroadcastMacRoute();
    UnsubscribeAgents();
    UnsubscribeMxs();
}

TEST_P(BgpXmppEvpnMcastTest, AddMxs) {
    Configure();
    CreateAgents();
    CreateMxs();
    SubscribeAgents();
    SubscribeMxs();

    AddAllAgentsBroadcastMacRoute();
    AddOddMxsBroadcastMacRoute();
    VerifyAllAgentsOddMxsOlist();
    VerifyOddMxsOlist();

    AddEvenMxsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist();

    DelAllAgentsBroadcastMacRoute();
    DelAllMxsBroadcastMacRoute();
    VerifyAllAgentsNoOlist();
    VerifyAllMxsNoOlist();

    UnsubscribeAgents();
    UnsubscribeMxs();
}

TEST_P(BgpXmppEvpnMcastTest, DelMxs) {
    Configure();
    CreateAgents();
    CreateMxs();
    SubscribeAgents();
    SubscribeMxs();

    AddAllAgentsBroadcastMacRoute();
    AddAllMxsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist();

    DelOddMxsBroadcastMacRoute();
    VerifyAllAgentsEvenMxsOlist();
    VerifyEvenMxsOlist();

    DelEvenMxsBroadcastMacRoute();
    VerifyAllAgentsNoOlist();
    VerifyAllMxsNoOlist();

    DelAllAgentsBroadcastMacRoute();
    UnsubscribeAgents();
    UnsubscribeMxs();
}

TEST_P(BgpXmppEvpnMcastTest, AddDelMxs) {
    Configure();
    CreateAgents();
    CreateMxs();
    SubscribeAgents();
    SubscribeMxs();
    AddAllAgentsBroadcastMacRoute();

    AddOddMxsBroadcastMacRoute();
    VerifyAllAgentsOddMxsOlist();
    VerifyOddMxsOlist();

    AddEvenMxsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist();

    DelOddMxsBroadcastMacRoute();
    VerifyAllAgentsEvenMxsOlist();
    VerifyEvenMxsOlist();

    AddOddMxsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist();

    DelEvenMxsBroadcastMacRoute();
    VerifyAllAgentsOddMxsOlist();
    VerifyOddMxsOlist();

    AddEvenMxsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist();

    DelAllAgentsBroadcastMacRoute();
    DelAllMxsBroadcastMacRoute();
    VerifyAllAgentsNoOlist();
    VerifyAllMxsNoOlist();

    UnsubscribeAgents();
    UnsubscribeMxs();
}

TEST_P(BgpXmppEvpnMcastTest, AddAgents) {
    Configure();
    CreateAgents();
    CreateMxs();
    SubscribeAgents();
    SubscribeMxs();

    AddOddAgentsBroadcastMacRoute();
    AddAllMxsBroadcastMacRoute();
    VerifyOddAgentsOlist();
    VerifyAllMxsOlist(true, false);

    AddEvenAgentsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist(true, true);

    DelAllAgentsBroadcastMacRoute();
    DelAllMxsBroadcastMacRoute();
    VerifyAllAgentsNoOlist();
    VerifyAllMxsNoOlist();

    UnsubscribeAgents();
    UnsubscribeMxs();
}

TEST_P(BgpXmppEvpnMcastTest, DelAgents) {
    Configure();
    CreateAgents();
    CreateMxs();
    SubscribeAgents();
    SubscribeMxs();

    AddAllAgentsBroadcastMacRoute();
    AddAllMxsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist(true, true);

    DelOddAgentsBroadcastMacRoute();
    VerifyEvenAgentsOlist();
    VerifyAllMxsOlist(false, true);

    DelEvenAgentsBroadcastMacRoute();
    VerifyAllAgentsNoOlist();
    VerifyAllMxsOlist(false, false);

    DelAllMxsBroadcastMacRoute();
    VerifyAllAgentsNoOlist();
    VerifyAllMxsNoOlist();

    UnsubscribeAgents();
    UnsubscribeMxs();
}

TEST_P(BgpXmppEvpnMcastTest, AddDelAgents) {
    Configure();
    CreateAgents();
    CreateMxs();
    SubscribeAgents();
    SubscribeMxs();
    AddAllMxsBroadcastMacRoute();

    AddOddAgentsBroadcastMacRoute();
    VerifyOddAgentsOlist();
    VerifyAllMxsOlist(true, false);

    AddEvenAgentsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist(true, true);

    DelOddAgentsBroadcastMacRoute();
    VerifyEvenAgentsOlist();
    VerifyAllMxsOlist(false, true);

    AddOddAgentsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist(true, true);

    DelEvenAgentsBroadcastMacRoute();
    VerifyOddAgentsOlist();
    VerifyAllMxsOlist(true, false);

    AddEvenAgentsBroadcastMacRoute();
    VerifyAllAgentsOlist();
    VerifyAllMxsOlist(true, true);

    DelAllAgentsBroadcastMacRoute();
    DelAllMxsBroadcastMacRoute();
    VerifyAllAgentsNoOlist();
    VerifyAllMxsNoOlist();

    UnsubscribeAgents();
    UnsubscribeMxs();
}

#if 0
// Parameterize single vs. dual servers and the tag value.

typedef std::tr1::tuple<bool, uint32_t> TestParams2;

//
// 2 Control Nodes X and Y.
//
class BgpXmppEvpnArMcastTest :
    public BgpXmppEvpnMcastTestBase,
    public ::testing::WithParamInterface<TestParams2> {

protected:
    virtual void SetUp() {
        single_server_ = std::tr1::get<0>(GetParam());
        tag_ = std::tr1::get<1>(GetParam());
        vxlan_ = true;
        BgpXmppEvpnMcastTestBase::SetUp();
    }

    virtual void TearDown() {
        BgpXmppEvpnMcastTestBase::TearDown();
    }

    void Configure() {
        char config[8192];
        snprintf(config, sizeof(config), config_template,
            bs_x_->session_manager()->GetPort(),
            bs_y_->session_manager()->GetPort());
        bs_x_->Configure(config);
        bs_y_->Configure(config);
        task_util::WaitForIdle();
    }
};

TEST_P(BgpXmppEvpnArMcastTest, ArBasic1) {
    Configure();
    CreateTsns();
    CreateTors();
    SubscribeTsns();
    VerifyAllTsnsNoOlist();
    SubscribeTors();

    AddAllTsnsBroadcastMacRoute();
    AddAllTorsBroadcastMacRoute();
    VerifyAllTsnsLeafOlist();

    DelAllTorsBroadcastMacRoute();

    VerifyAllTsnsNoOlist();
    DelAllTsnsBroadcastMacRoute();
    UnsubscribeTsns();
    UnsubscribeTors();
}

#if 0
TEST_P(BgpXmppEvpnArMcastTest, ArBasic2) {
    Configure();
    CreateTsns();
    CreateTors();
    SubscribeTsns();
    VerifyAllTsnsNoOlist();
    SubscribeTors();

    AddAllTorsBroadcastMacRoute();
    AddAllTsnsBroadcastMacRoute();
    VerifyAllTsnsLeafOlist();

    DelAllTorsBroadcastMacRoute();
    VerifyAllTsnsNoOlist();
    DelAllTsnsBroadcastMacRoute();
    UnsubscribeTsns();
    UnsubscribeTors();
}

TEST_P(BgpXmppEvpnArMcastTest, ArBasic3) {
    Configure();
    CreateTsns();
    CreateTors();

    SubscribeTsns();
    AddAllTsnsBroadcastMacRoute();
    VerifyAllTsnsNoOlist();

    SubscribeTors();
    AddAllTorsBroadcastMacRoute();
    VerifyAllTsnsLeafOlist();

    DelAllTorsBroadcastMacRoute();
    VerifyAllTsnsNoOlist();
    DelAllTsnsBroadcastMacRoute();
    UnsubscribeTsns();
    UnsubscribeTors();
}

TEST_P(BgpXmppEvpnArMcastTest, ArBasic4) {
    Configure();
    CreateTsns();
    CreateTors();

    SubscribeTors();
    AddAllTorsBroadcastMacRoute();
    VerifyAllTsnsNoOlist();

    SubscribeTsns();
    AddAllTsnsBroadcastMacRoute();
    VerifyAllTsnsLeafOlist();

    DelAllTorsBroadcastMacRoute();
    VerifyAllTsnsNoOlist();
    DelAllTsnsBroadcastMacRoute();
    UnsubscribeTsns();
    UnsubscribeTors();
}

TEST_P(BgpXmppEvpnArMcastTest, ArAddTors) {
    Configure();
    CreateTsns();
    CreateTors();
    SubscribeTsns();
    VerifyAllTsnsNoOlist();
    SubscribeTors();

    AddAllTsnsBroadcastMacRoute();
    AddOddTorsBroadcastMacRoute();
    VerifyAllTsnsOddLeafOlist();

    AddEvenTorsBroadcastMacRoute();
    VerifyAllTsnsLeafOlist();

    DelAllTorsBroadcastMacRoute();
    VerifyAllTsnsNoOlist();
    DelAllTsnsBroadcastMacRoute();
    UnsubscribeTsns();
    UnsubscribeTors();
}

TEST_P(BgpXmppEvpnArMcastTest, ArDelTors) {
    Configure();
    CreateTsns();
    CreateTors();
    SubscribeTsns();
    VerifyAllTsnsNoOlist();
    SubscribeTors();

    AddAllTsnsBroadcastMacRoute();
    AddAllTorsBroadcastMacRoute();
    VerifyAllTsnsLeafOlist();

    DelOddTorsBroadcastMacRoute();
    VerifyAllTsnsEvenLeafOlist();

    DelEvenTorsBroadcastMacRoute();
    VerifyAllTsnsNoOlist();
    DelAllTsnsBroadcastMacRoute();
    UnsubscribeTsns();
    UnsubscribeTors();
}

TEST_P(BgpXmppEvpnArMcastTest, ArAddDelTors) {
    Configure();
    CreateTsns();
    CreateTors();
    SubscribeTsns();
    VerifyAllTsnsNoOlist();
    SubscribeTors();
    AddAllTsnsBroadcastMacRoute();

    AddOddTorsBroadcastMacRoute();
    VerifyAllTsnsOddLeafOlist();

    AddEvenTorsBroadcastMacRoute();
    VerifyAllTsnsLeafOlist();

    DelOddTorsBroadcastMacRoute();
    VerifyAllTsnsEvenLeafOlist();

    AddOddTorsBroadcastMacRoute();
    VerifyAllTsnsLeafOlist();

    DelEvenTorsBroadcastMacRoute();
    VerifyAllTsnsOddLeafOlist();

    AddEvenTorsBroadcastMacRoute();
    VerifyAllTsnsLeafOlist();

    DelAllTorsBroadcastMacRoute();
    VerifyAllTsnsNoOlist();
    DelAllTsnsBroadcastMacRoute();
    UnsubscribeTsns();
    UnsubscribeTors();
}

TEST_P(BgpXmppEvpnArMcastTest, ArWithIngressReplication1) {
    Configure();
    CreateTsns();
    CreateTors();
    CreateMxs();
    SubscribeTsns();
    SubscribeTors();
    SubscribeMxs();

    AddAllTsnsBroadcastMacRoute();
    AddAllTorsBroadcastMacRoute();
    AddAllMxsBroadcastMacRoute();
    VerifyAllTsnsAllOlist();

    DelAllTsnsBroadcastMacRoute();
    DelAllTorsBroadcastMacRoute();
    DelAllMxsBroadcastMacRoute();
    UnsubscribeTsns();
    UnsubscribeTors();
    UnsubscribeMxs();
}

TEST_P(BgpXmppEvpnArMcastTest, ArWithIngressReplication2) {
    Configure();
    CreateTsns();
    CreateMxs();
    CreateTors();
    SubscribeTsns();
    SubscribeMxs();
    SubscribeTors();

    AddAllTsnsBroadcastMacRoute();
    AddAllMxsBroadcastMacRoute();
    AddAllTorsBroadcastMacRoute();
    VerifyAllTsnsAllOlist();

    DelAllTsnsBroadcastMacRoute();
    DelAllMxsBroadcastMacRoute();
    DelAllTorsBroadcastMacRoute();
    UnsubscribeTsns();
    UnsubscribeMxs();
    UnsubscribeTors();
}

TEST_P(BgpXmppEvpnArMcastTest, ArWithIngressReplication3) {
    Configure();
    CreateMxs();
    CreateTsns();
    CreateTors();
    SubscribeMxs();
    SubscribeTsns();
    SubscribeTors();

    AddAllMxsBroadcastMacRoute();
    AddAllTsnsBroadcastMacRoute();
    AddAllTorsBroadcastMacRoute();
    VerifyAllTsnsAllOlist();

    DelAllMxsBroadcastMacRoute();
    DelAllTsnsBroadcastMacRoute();
    DelAllTorsBroadcastMacRoute();
    UnsubscribeMxs();
    UnsubscribeTsns();
    UnsubscribeTors();
}

TEST_P(BgpXmppEvpnArMcastTest, ArWithIngressReplication4) {
    Configure();
    CreateMxs();
    CreateTsns();
    CreateTors();
    SubscribeMxs();
    SubscribeTors();
    SubscribeTsns();

    AddAllMxsBroadcastMacRoute();
    AddAllTorsBroadcastMacRoute();
    AddAllTsnsBroadcastMacRoute();
    VerifyAllTsnsAllOlist();

    DelAllMxsBroadcastMacRoute();
    DelAllTorsBroadcastMacRoute();
    DelAllTsnsBroadcastMacRoute();
    UnsubscribeMxs();
    UnsubscribeTors();
    UnsubscribeTsns();
}

TEST_P(BgpXmppEvpnArMcastTest, ArWithIngressReplication5) {
    Configure();
    CreateTsns();
    CreateTors();
    CreateMxs();
    SubscribeTors();
    SubscribeMxs();
    SubscribeTsns();

    AddAllTorsBroadcastMacRoute();
    AddAllMxsBroadcastMacRoute();
    AddAllTsnsBroadcastMacRoute();
    VerifyAllTsnsAllOlist();

    DelAllTorsBroadcastMacRoute();
    DelAllMxsBroadcastMacRoute();
    DelAllTsnsBroadcastMacRoute();
    UnsubscribeTors();
    UnsubscribeMxs();
    UnsubscribeTsns();
}

TEST_P(BgpXmppEvpnArMcastTest, ArWithIngressReplication6) {
    Configure();
    CreateTsns();
    CreateTors();
    CreateMxs();
    SubscribeTors();
    SubscribeTsns();
    SubscribeMxs();

    AddAllTorsBroadcastMacRoute();
    AddAllTsnsBroadcastMacRoute();
    AddAllMxsBroadcastMacRoute();
    VerifyAllTsnsAllOlist();

    DelAllTorsBroadcastMacRoute();
    DelAllTsnsBroadcastMacRoute();
    DelAllMxsBroadcastMacRoute();
    UnsubscribeTors();
    UnsubscribeMxs();
    UnsubscribeTsns();
}
#endif

TEST_P(BgpXmppEvpnArMcastTest, ArIntrospect) {
    Configure();
    CreateTsns();
    CreateTors();
    CreateMxs();
    SubscribeTsns();
    SubscribeTors();
    SubscribeMxs();

    AddAllTsnsBroadcastMacRoute();
    AddAllTorsBroadcastMacRoute();
    AddAllMxsBroadcastMacRoute();
    VerifyAllTsnsAllOlist();
    DelAllTorsBroadcastMacRoute();

    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = bs_x_.get();
    sandesh_context.xmpp_peer_manager = bgp_channel_manager_x_.get();
    Sandesh::set_client_context(&sandesh_context);
    vector<string> regular_nves = mx_address_list_;
    vector<string> ar_replicators = tsn_address_list_;
    vector<string> ar_leaf_addrs = tor_address_list_;
    vector<string> ar_leaf_replicators = tor_replicator_address_list_;
    Sandesh::set_response_callback(boost::bind(
        &BgpXmppEvpnMcastTestBase::ValidateShowEvpnTableResponse,
        this, _1, regular_nves, ar_replicators,
        ar_leaf_addrs, ar_leaf_replicators));
    TASK_UTIL_EXPECT_TRUE(CheckShowEvpnTableResponse("blue.evpn.0"));

    DelAllTsnsBroadcastMacRoute();
    DelAllMxsBroadcastMacRoute();
    UnsubscribeTsns();
    UnsubscribeTors();
    UnsubscribeMxs();
}

INSTANTIATE_TEST_CASE_P(Default, BgpXmppEvpnArMcastTest,
    ::testing::Combine(::testing::Bool(), ::testing::Values(512U, 65536u)));
#endif
INSTANTIATE_TEST_CASE_P(Default,
                        BgpXmppEvpnMcastTest,
                        ::testing::Combine(::testing::Bool(),
                                           ::testing::Values(0U, 4093u)));

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
    virtual void SetUp() {
    }
    virtual void TearDown() {
    }
};

static void SetUp() {
    BgpServer::Initialize();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<StateMachine>(
        boost::factory<StateMachineTest *>());
    BgpObjectFactory::Register<BgpXmppMessageBuilder>(
        boost::factory<BgpXmppMessageBuilder *>());
}
static void TearDown() {
    BgpServer::Terminate();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();

    return result;
}
