/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/algorithm/string/predicate.hpp>
#include <boost/format.hpp>

#include "base/regex.h"
#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "db/db_table_walker.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_config_ifmap.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_multicast.h"
#include "bgp/ipeer.h"
#include "bgp/bgp_evpn.h"
#include "bgp/bgp_server.h"
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/evpn/evpn_table.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "io/test/event_manager_test.h"

using boost::format;
using boost::assign::list_of;
using boost::smatch;
using boost::scoped_ptr;
using boost::starts_with;
using boost::system::error_code;
using contrail::regex;
using contrail::regex_match;
using contrail::regex_search;
using std::make_pair;
using std::ostringstream;
using std::string;

struct PMSIParams {
public:
    PMSIParams() { }
    PMSIParams(uint32_t label, const string &address, string encap_s,
               ErmVpnRoute **rt) :
            label(label), address(address), ermvpn_rt(rt) {
        encaps.push_back(encap_s);
    }

    uint32_t label;
    string address;
    std::vector<std::string> encaps;
    ErmVpnRoute **ermvpn_rt;
};

struct SG {
    SG(int ri_index, const EvpnState::SG &sg) : sg(sg) {
        ostringstream os;
        os << ri_index;
        this->ri_index = os.str();
    }
    SG(string ri_index, const EvpnState::SG &sg) : ri_index(ri_index), sg(sg) {}
    bool operator<(const SG &other) const {
        if (ri_index < other.ri_index)
            return true;
        if (ri_index > other.ri_index)
            return false;
        return sg < other.sg;
    }

    string ri_index;
    EvpnState::SG sg;
};


static std::map<SG, const PMSIParams> pmsi_params;
static tbb::mutex pmsi_params_mutex;

class RoutingInstanceTest : public RoutingInstance {
public:
    RoutingInstanceTest(string name, BgpServer *server, RoutingInstanceMgr *mgr,
                        const BgpInstanceConfig *config) :
            RoutingInstance(name, server, mgr, config),
            ri_index_(GetRIIndex(name)) {
    }
    string ri_index() const { return ri_index_; }
    int ri_index_i() const { return atoi(ri_index_.c_str()); }

private:

    string GetRIIndex(const std::string &name) {
        static regex pattern("(\\d+)$");
        smatch match;
        if (regex_search(name, match, pattern))
            return match[1];
        return "";
    }
    string ri_index_;
};

class BgpPeerMock : public IPeer {
public:
    BgpPeerMock(size_t i) : to_str_("test-peer" + integerToString(i)) { }
    virtual ~BgpPeerMock() { }
    virtual const std::string &ToString() const { return to_str_; }
    virtual const std::string &ToUVEKey() const { return to_str_; }
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) { return true; }
    virtual BgpServer *server() { return NULL; }
    virtual BgpServer *server() const { return NULL; }
    virtual IPeerClose *peer_close() { return NULL; }
    virtual IPeerClose *peer_close() const { return NULL; }
    virtual void UpdateCloseRouteStats(Address::Family family,
        const BgpPath *old_path, uint32_t path_flags) const {
    }
    virtual IPeerDebugStats *peer_stats() { return NULL; }
    virtual const IPeerDebugStats *peer_stats() const { return NULL; }
    virtual bool IsReady() const { return true; }
    virtual bool IsXmppPeer() const { return false; }
    virtual void Close(bool graceful) { }
    BgpProto::BgpPeerType PeerType() const { return BgpProto::IBGP; }
    virtual uint32_t bgp_identifier() const { return 0; }
    virtual const std::string GetStateName() const { return "UNKNOWN"; }
    virtual void UpdateTotalPathCount(int count) const { }
    virtual int GetTotalPathCount() const { return 0; }
    virtual bool IsAs4Supported() const { return false; };
    virtual void UpdatePrimaryPathCount(int count,
        Address::Family family) const { }
    virtual int GetPrimaryPathCount() const { return 0; }
    virtual void ProcessPathTunnelEncapsulation(const BgpPath *path,
        BgpAttr *attr, ExtCommunityDB *extcomm_db, const BgpTable *table)
        const {
    }
    virtual const std::vector<std::string> GetDefaultTunnelEncap(
        Address::Family family) const {
        return std::vector<std::string>();
    }
    virtual bool IsRegistrationRequired() const { return true; }
    virtual void MembershipRequestCallback(BgpTable *table) { }
    virtual bool MembershipPathCallback(DBTablePartBase *tpart,
        BgpRoute *route, BgpPath *path) { return false; }
    virtual bool CanUseMembershipManager() const { return true; }
    virtual bool IsInGRTimerWaitState() const { return false; }

private:
    std::string to_str_;
};

class McastTreeManagerMock : public McastTreeManager {
public:
    McastTreeManagerMock(ErmVpnTable *table) : McastTreeManager(table) {
    }
    ~McastTreeManagerMock() { }
    virtual UpdateInfo *GetUpdateInfo(ErmVpnRoute *route) { return NULL; }

    virtual ErmVpnRoute *GetGlobalTreeRootRoute(const Ip4Address &source,
            const Ip4Address &group) const {
        tbb::mutex::scoped_lock lock(pmsi_params_mutex);
        const RoutingInstanceTest *ri =
            dynamic_cast<const RoutingInstanceTest *>(
                table()->routing_instance());
        string ri_index = ri->ri_index();
        std::map<SG, const PMSIParams>::iterator iter =
            pmsi_params.find(SG(ri_index, EvpnState::SG(source, group)));
        if (iter == pmsi_params.end())
            return NULL;

        ErmVpnRoute **ermvpn_rtp = iter->second.ermvpn_rt;
        lock.release();
        TASK_UTIL_EXPECT_NE(static_cast<ErmVpnRoute *>(NULL), *ermvpn_rtp);

        tbb::mutex::scoped_lock lock2(pmsi_params_mutex);
        assert((*ermvpn_rtp)->GetPrefix().source().to_string() ==
                source.to_string());
        assert((*ermvpn_rtp)->GetPrefix().group().to_string() ==
                group.to_string());
        return *ermvpn_rtp;
    }

    virtual bool GetForestNodePMSI(ErmVpnRoute *rt, uint32_t *label,
            Ip4Address *address, std::vector<std::string> *encap) const {
        tbb::mutex::scoped_lock lock(pmsi_params_mutex);

        if (!rt)
            return false;

        const RoutingInstanceTest *ri =
            dynamic_cast<const RoutingInstanceTest *>(
                table()->routing_instance());
        string ri_index = ri->ri_index();
        EvpnState::SG sg(rt->GetPrefix().source(), rt->GetPrefix().group());
        std::map<SG, const PMSIParams>::iterator iter =
            pmsi_params.find(SG(ri_index, sg));
        if (iter == pmsi_params.end())
            return false;

        *label = iter->second.label;
        error_code e;
        *address = IpAddress::from_string(iter->second.address, e).to_v4();
        BOOST_FOREACH(string encap_str, iter->second.encaps)
            encap->push_back(encap_str);
        return true;
    }

private:
};

typedef std::tr1::tuple<int, int, int> TestParams;
class BgpEvpnTest : public ::testing::TestWithParam<TestParams> {
public:
    void NotifyRoute(ErmVpnRoute **ermvpn_rt, int i, int j) {
        ermvpn_rt[(i-1)*groups_count_+(j-1)]->Notify();
    }

protected:
    BgpEvpnTest() {
    }

    string getRouteTarget (int i, string suffix) const {
        ostringstream os;
        os << "target:127.0.0.1:1" << format("%|03|")%i << suffix;
        return os.str();
    }

    const string GetConfig() const {
        ostringstream os;
            os <<
"<?xml version='1.0' encoding='utf-8'?>"
"<config>"
"   <bgp-router name=\"local\">"
"       <address>127.0.0.1</address>"
"       <autonomous-system>1</autonomous-system>"
"   </bgp-router>"
"";

        for (size_t i = 1; i <= instances_set_count_; i++) {
            os <<
"   <routing-instance name='red" << i << "'>"
"       <vrf-target>" << getRouteTarget(i, "1") << "</vrf-target>"
"   </routing-instance>"
            ;
        }

        os << "</config>";
        return os.str();
    }

    virtual void SetUp() {
        evm_.reset(new EventManager());
        server_.reset(new BgpServerTest(evm_.get(), "local"));
        thread_.reset(new ServerThread(evm_.get()));
        thread_->Start();
        instances_set_count_ = std::tr1::get<0>(GetParam());
        groups_count_ = std::tr1::get<1>(GetParam());
        paths_count_ = std::tr1::get<2>(GetParam());
        red_ermvpn_ = new ErmVpnTable *[instances_set_count_];
        red_ = new EvpnTable *[instances_set_count_];
        peers_ = new scoped_ptr<BgpPeerMock>[paths_count_];
        for (size_t i = 0; i < paths_count_; i++)
            peers_[i].reset(new BgpPeerMock(i));
        string config = GetConfig();

        server_->Configure(config);
        task_util::WaitForIdle();

        TASK_UTIL_EXPECT_NE(static_cast<BgpTable *>(NULL),
                            server_->database()->FindTable("bgp.evpn.0"));
        master_ = static_cast<BgpTable *>(
            server_->database()->FindTable("bgp.evpn.0"));
        TASK_UTIL_EXPECT_NE(static_cast<BgpTable *>(NULL),
                            server_->database()->FindTable("bgp.ermvpn.0"));

        for (size_t i = 1; i <= instances_set_count_; i++) {
            ostringstream r, re;
            r << "red" << i << ".evpn.0";
            re << "red" << i << ".ermvpn.0";
            TASK_UTIL_EXPECT_NE(static_cast<BgpTable *>(NULL),
                                server_->database()->FindTable(r.str()));

            red_[i-1] = static_cast<EvpnTable *>(
                server_->database()->FindTable(r.str()));
            red_ermvpn_[i-1] = dynamic_cast<ErmVpnTable *>(
                 server_->database()->FindTable(re.str()));
        }
    }

    void UpdateBgpIdentifier(const string &address) {
        error_code err;
        task_util::TaskFire(boost::bind(&BgpServer::UpdateBgpIdentifier,
            server_.get(), Ip4Address::from_string(address, err)),
            "bgp::Config");
    }

    void TearDown() {
        server_->Shutdown();
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ(0U, TcpServerManager::GetServerCount());
        evm_->Shutdown();
        task_util::WaitForIdle();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
        delete[] red_ermvpn_;
        delete[] red_;
        delete[] peers_;
    }


    ErmVpnRoute *AddErmVpnRoute(ErmVpnTable *table, const string &prefix_str,
                                const string &target) {
        ErmVpnPrefix prefix(ErmVpnPrefix::FromString(prefix_str));
        DBRequest add_req;
        add_req.key.reset(new ErmVpnTable::RequestKey(prefix, NULL));
        BgpAttrSpec attr_spec;
        ExtCommunitySpec *commspec(new ExtCommunitySpec());
        attr_spec.push_back(commspec);

        BgpAttrPtr attr = server_->attr_db()->Locate(attr_spec);
        STLDeleteValues(&attr_spec);
        add_req.data.reset(new ErmVpnTable::RequestData(attr, 0, 20));
        add_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        table->Enqueue(&add_req);
        return FindErmVpnRoute(table, prefix_str);
    }

    void DeleteErmVpnRoute(BgpTable *table, const string &prefix_str) {
        DBRequest delete_req;
        ErmVpnPrefix prefix(ErmVpnPrefix::FromString(prefix_str));
        delete_req.key.reset(new ErmVpnTable::RequestKey(prefix, NULL));
        delete_req.oper = DBRequest::DB_ENTRY_DELETE;
        table->Enqueue(&delete_req);
    }

    ErmVpnRoute *FindErmVpnRoute(ErmVpnTable *table, const string &prefix_str) {
        while (!table->FindRoute(ErmVpnPrefix::FromString(prefix_str)))
            usleep(10);
        return table->FindRoute(ErmVpnPrefix::FromString(prefix_str));
    }

    string ri_index(const BgpTable *table) const {
        const RoutingInstanceTest *ri =
            dynamic_cast<const RoutingInstanceTest *>(
                table->routing_instance());
        return ri->ri_index();
    }

    void AddEvpnRoute(EvpnTable *table, const string &prefix_str) {
        for (size_t i = 0; i < paths_count_; i++) {
            EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str));
            DBRequest add_req;
            add_req.key.reset(new EvpnTable::RequestKey(prefix,
                                                        peers_[i].get()));

            BgpAttrSpec attr_spec;
            uint32_t flags = 0;
            if (prefix.type() == EvpnPrefix::SelectiveMulticastRoute)
                flags |= BgpPath::CheckGlobalErmVpnRoute;
            BgpAttrPtr attr = server_->attr_db()->Locate(attr_spec);
            add_req.data.reset(new EvpnTable::RequestData(attr, flags, 20));
            add_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
            table->Enqueue(&add_req);
        }
        task_util::WaitForIdle();
    }

    void DeleteEvpnRoute(BgpTable *table, const string &prefix_str) {
        for (size_t i = 0; i < paths_count_; i++) {
            DBRequest delete_req;
            EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str));
            delete_req.key.reset(new EvpnTable::RequestKey(prefix,
                                                           peers_[i].get()));
            delete_req.oper = DBRequest::DB_ENTRY_DELETE;
            table->Enqueue(&delete_req);
        }
        task_util::WaitForIdle();
    }

    EvpnRoute *VerifySmetRoute(EvpnTable *table, const string &prefix_str,
            const PMSIParams &pmsi) {
        EvpnPrefix prefix = EvpnPrefix::FromString(prefix_str);
        TASK_UTIL_EXPECT_NE(static_cast<EvpnRoute *>(NULL),
                            table->FindRoute(prefix));
        EvpnRoute *smet_rt = table->FindRoute(prefix);
        EXPECT_EQ(prefix, smet_rt->GetPrefix());
        TASK_UTIL_EXPECT_EQ(prefix.ToString(),
                            smet_rt->GetPrefix().ToString());
        TASK_UTIL_EXPECT_TRUE(smet_rt->IsUsable());

        // Verify path attributes.
        const BgpAttr *attr = smet_rt->BestPath()->GetAttr();
        TASK_UTIL_EXPECT_NE(static_cast<const BgpAttr *>(NULL), attr);
        TASK_UTIL_EXPECT_EQ(Ip4Address::from_string(pmsi.address),
                  attr->nexthop().to_v4());
        return smet_rt;
    }

    bool WalkCallback(DBTablePartBase *tpart, DBEntryBase *db_entry) {
        CHECK_CONCURRENCY("db::DBTable");
        BgpRoute *route = static_cast<BgpRoute *>(db_entry);
        std::cout << route->ToString() << std::endl;
        return true;
    }

    void WalkDoneCallback(DBTable::DBTableWalkRef ref,
                          DBTableBase *table, bool *complete) {
        if (complete)
            *complete = true;
    }

    void WalkTable(BgpTable *table) {
        bool complete = false;
        DBTable::DBTableWalkRef walk_ref = table->AllocWalker(
            boost::bind(&BgpEvpnTest::WalkCallback, this, _1, _2),
            boost::bind(&BgpEvpnTest::WalkDoneCallback, this, _1, _2,
                        &complete));
        std::cout << "Table " << table->name() << " walk start\n";
        table->WalkTable(walk_ref);
        TASK_UTIL_EXPECT_TRUE(complete);
        std::cout << "Table " << table->name() << " walk end\n";
    }

    string prefix2(int index, int gindex = 1) const {
        ostringstream os;
        os << "2-10.1.1.1:65535-" << index << "-11:12:13:14:15:16,0.0.0.0";
        return os.str();
    }

    string prefix6(int index, int gindex) const {
        ostringstream os;
        os << "6-10.1.1.1:65535-" << index << "-10.1.1.1-224.1.1." << gindex;
        os << "-127.0.0.1";
        return os.str();
    }

    string ermvpn_prefix(int index, int gindex = 1) const {
        ostringstream os;
        os << "2-10.1.1.1:" << index << "-192.168.1.1,224.1.1." << gindex;
        os << ",10.1.1.1";
        return os.str();
    }

    SG sg(int instance, int group) const {
        error_code e;
        ostringstream os;
        os << "224.1.1." << group;
        return SG(instance, EvpnState::SG(IpAddress::from_string("10.1.1.1", e),
                                          IpAddress::from_string(os.str(), e)));
    }

    SG sg(int instance, const Ip4Address &s, const Ip4Address &g) const {
        return SG(instance, EvpnState::SG(s, g));
    }

    scoped_ptr<EventManager> evm_;
    scoped_ptr<ServerThread> thread_;
    scoped_ptr<BgpServerTest> server_;
    DB db_;
    BgpTable *master_;
    ErmVpnTable **red_ermvpn_;
    EvpnTable **red_;
    scoped_ptr<BgpPeerMock> *peers_;
    size_t instances_set_count_;
    size_t groups_count_;
    size_t paths_count_;
};

static size_t GetInstanceCount() {
    char *env = getenv("BGP_EVPN_TEST_INSTANCE_COUNT");
    size_t count = 4;
    if (!env)
        return count;
    stringToInteger(string(env), count);
    return count;
}

static size_t GetGroupCount() {
    char *env = getenv("BGP_EVPN_TEST_GROUP_COUNT");
    size_t count = 4;
    if (!env)
        return count;
    stringToInteger(string(env), count);
    return count;
}

static size_t GetPathCount() {
    char *env = getenv("BGP_EVPN_TEST_PATH_COUNT");
    size_t count = 4;
    if (!env)
        return count;
    stringToInteger(string(env), count);
    return count;
}

INSTANTIATE_TEST_CASE_P(
    BgpEvpnTestWithParams,
    BgpEvpnTest,
    ::testing::Combine(
        ::testing::Values(1, 2, static_cast<int>(GetInstanceCount())),
        ::testing::Values(1, 2, static_cast<int>(GetGroupCount())),
        ::testing::Values(1, 2, static_cast<int>(GetPathCount()))));

static void SetUp() {
    bgp_log_test::init();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<McastTreeManager>(
        boost::factory<McastTreeManagerMock *>());
    BgpObjectFactory::Register<RoutingInstance>(
        boost::factory<RoutingInstanceTest *>());
}

static void TearDown() {
    task_util::WaitForIdle();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}

// Add Smet route and make sure that it does not propagate to master table
// until ermvpn route is also created
TEST_P(BgpEvpnTest, Smet_With_ErmVpnRoute) {
    // Inject smet route from a mock peer into red.mvpn.0 table
    ErmVpnRoute *ermvpn_rt[instances_set_count_*groups_count_];
    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            tbb::mutex::scoped_lock lock(pmsi_params_mutex);
            ermvpn_rt[(i-1)*groups_count_+(j-1)] = NULL;
            PMSIParams pmsi(PMSIParams(10, "1.2.3.4", "gre",
                            &ermvpn_rt[(i-1)*groups_count_+(j-1)]));
            pmsi_params.insert(make_pair(sg(i, j), pmsi));
            lock.release();
            AddEvpnRoute(red_[i-1], prefix6(i, j));
        }
    }

    TASK_UTIL_EXPECT_EQ(0U, master_->Size());

    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            ErmVpnRoute *rt =
                AddErmVpnRoute(red_ermvpn_[i-1], ermvpn_prefix(i, j),
                               "target:127.0.0.1:1100");
            tbb::mutex::scoped_lock lock(pmsi_params_mutex);
            ermvpn_rt[(i-1)*groups_count_+(j-1)] = rt;
        }
    }

    // smet routes should now be valid and propagate
    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(groups_count_, red_[i-1]->Size());
    }

    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            DeleteEvpnRoute(red_[i-1], prefix6(i, j));
            {
                tbb::mutex::scoped_lock lock(pmsi_params_mutex);
                pmsi_params.erase(sg(i, j));
            }
            DeleteErmVpnRoute(red_ermvpn_[i-1], ermvpn_prefix(i, j));
        }
    }

    TASK_UTIL_EXPECT_EQ(0U, master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(0U, red_[i - 1]->Size());
    }
}

// Verify that if ermvpn route is deleted, then any smet route if originated
// already is withdrawn.
TEST_P(BgpEvpnTest, Smet_With_ErmVpnRoute_2) {
    // Inject Type6 route from a mock peer into evpn.0 table with red1 route
    // target. This route should go into red1 table.
    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++) {
            AddEvpnRoute(red_[i-1], prefix6(i, j));
        }

    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(groups_count_, red_[i-1]->Size());
    }
    TASK_UTIL_EXPECT_EQ(0U, master_->Size());

    // Make ermvpn route available now and verifiy that leaf-ad is originated.
    // Add a ermvpn route into the table.
    ErmVpnRoute *ermvpn_rt[instances_set_count_*groups_count_];
    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            tbb::mutex::scoped_lock lock(pmsi_params_mutex);
            ermvpn_rt[(i-1)*groups_count_+(j-1)] = NULL;
            PMSIParams pmsi(PMSIParams(10, "1.2.3.4", "gre",
                            &ermvpn_rt[(i-1)*groups_count_+(j-1)]));
            pmsi_params.insert(make_pair(sg(i, j), pmsi));
        }
    }

    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            ErmVpnRoute *rt =
                AddErmVpnRoute(red_ermvpn_[i-1], ermvpn_prefix(i, j),
                               "target:127.0.0.1:1100");
            tbb::mutex::scoped_lock lock(pmsi_params_mutex);
            ermvpn_rt[(i-1)*groups_count_+(j-1)] = rt;
        }
    }

    TASK_UTIL_EXPECT_EQ(instances_set_count_*groups_count_, master_->Size());

    // Delete the ermvpn route and verify that leaf-ad route is also deleted.
    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            {
                tbb::mutex::scoped_lock lock(pmsi_params_mutex);
                pmsi_params.erase(sg(i, j));
            }
            DeleteErmVpnRoute(red_ermvpn_[i-1], ermvpn_prefix(i, j));
        }
    }

    TASK_UTIL_EXPECT_EQ(0U, master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++)
        TASK_UTIL_EXPECT_EQ(0U, red_ermvpn_[i - 1]->Size());

    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++) {
            DeleteEvpnRoute(red_[i-1], prefix6(i, j));
        }
}

// Add smet route and verify that it gets copied to master table 
// but only after ermvpn route becomes available.
TEST_P(BgpEvpnTest, Smet_With_ErmVpnRoute_3) {
    // Inject smet route from a mock peer into evpn.0 table with red1 route
    // target. This route should go into red1 table.
    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++) {
            AddEvpnRoute(red_[i-1], prefix6(i, j));
        }

    TASK_UTIL_EXPECT_EQ(0U, master_->Size());
    // Make ermvpn route available now and verifiy that smet route gets copied.
    // to master table. Add a ermvpn route into the table.
    ErmVpnRoute *ermvpn_rt[instances_set_count_*groups_count_];
    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            tbb::mutex::scoped_lock lock(pmsi_params_mutex);
            ermvpn_rt[(i-1)*groups_count_+(j-1)] = NULL;
            PMSIParams pmsi(PMSIParams(10, "1.2.3.4", "gre",
                            &ermvpn_rt[(i-1)*groups_count_+(j-1)]));
            pmsi_params.insert(make_pair(sg(i, j), pmsi));
        }
    }

    TASK_UTIL_EXPECT_EQ(0U, master_->Size());

    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            ErmVpnRoute *rt =
                AddErmVpnRoute(red_ermvpn_[i-1], ermvpn_prefix(i, j),
                               "target:127.0.0.1:1100");
            tbb::mutex::scoped_lock lock(pmsi_params_mutex);
            ermvpn_rt[(i-1)*groups_count_+(j-1)] = rt;
        }

        TASK_UTIL_EXPECT_EQ(i*groups_count_, master_->Size());

        for (size_t j = 1; j <= groups_count_; j++) {
            // Lookup the actual smet route and verify its attributes.
            EvpnRoute *smet_rt = VerifySmetRoute(red_[i-1], prefix6(i, j),
                                                     pmsi_params[sg(i, j)]);
            const BgpPath *red_path = smet_rt->BestPath();
            const BgpAttr *red_attr = red_path->GetAttr();

            // Notify ermvpn route without any change.
            task_util::TaskFire(boost::bind(&BgpEvpnTest::NotifyRoute, this,
                         ermvpn_rt, i, j), "bgpConfig");

            // Verify that smet path or its attributes did not change.
            std::map<SG, const PMSIParams>::iterator iter =
                pmsi_params.find(sg(i, j));
            assert(iter != pmsi_params.end());
            TASK_UTIL_EXPECT_EQ(smet_rt,
                VerifySmetRoute(red_[i-1], prefix6(i, j), iter->second));
            TASK_UTIL_EXPECT_EQ(red_path, smet_rt->BestPath());
            TASK_UTIL_EXPECT_EQ(red_attr, smet_rt->BestPath()->GetAttr());
        }
    }

    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            DeleteEvpnRoute(red_[i-1], prefix6(i, j));
            {
                tbb::mutex::scoped_lock lock(pmsi_params_mutex);
                assert(pmsi_params.erase(sg(i, j)) == 1);
            }
            DeleteErmVpnRoute(red_ermvpn_[i-1], ermvpn_prefix(i, j));
        }
    }

    TASK_UTIL_EXPECT_EQ(0U, master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(0U, red_[i - 1]->Size());
        if (red_ermvpn_[i-1]->Size() > 0)
            WalkTable(red_ermvpn_[i-1]);
        TASK_UTIL_EXPECT_EQ(0U, red_ermvpn_[i - 1]->Size());
    }
}
