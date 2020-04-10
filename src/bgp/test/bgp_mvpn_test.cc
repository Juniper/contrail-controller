/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
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
#include "bgp/bgp_mvpn.h"
#include "bgp/bgp_server.h"
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/mvpn/mvpn_table.h"
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
    SG(int ri_index, const MvpnState::SG &sg) : sg(sg) {
        ostringstream os;
        os << ri_index;
        this->ri_index = os.str();
    }
    SG(string ri_index, const MvpnState::SG &sg) : ri_index(ri_index), sg(sg) {}
    bool operator<(const SG &other) const {
        if (ri_index < other.ri_index)
            return true;
        if (ri_index > other.ri_index)
            return false;
        return sg < other.sg;
    }

    string ri_index;
    MvpnState::SG sg;
};


static std::map<SG, const PMSIParams> pmsi_params;
static tbb::mutex pmsi_params_mutex;

class RoutingInstanceTest : public RoutingInstance {
public:
    RoutingInstanceTest(string name, BgpServer *server, RoutingInstanceMgr *mgr,
                        const BgpInstanceConfig *config) :
            RoutingInstance(name, server, mgr, config),
            ri_index_(GetRIIndex(name)) {
        set_mvpn_project_manager_network(
            "default-domain:default-project:ip-fabric:ip-fabric" + ri_index_);
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
    virtual bool IsAs4Supported() const { return false; }
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
            pmsi_params.find(SG(ri_index, MvpnState::SG(source, group)));
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
        MvpnState::SG sg(rt->GetPrefix().source(), rt->GetPrefix().group());
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

typedef std::tr1::tuple<bool, int, int, int> TestParams;
class BgpMvpnTest : public ::testing::TestWithParam<TestParams> {
public:
    void NotifyRoute(ErmVpnRoute **ermvpn_rt, int i, int j) {
        ermvpn_rt[(i-1)*groups_count_+(j-1)]->Notify();
    }

protected:
    BgpMvpnTest() {
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

        if (preconfigure_pm_) {
            os <<
"  <routing-instance name='default-domain:default-project:ip-fabric:ip-fabric'>"
"       <vrf-target>target:127.0.0.1:60000</vrf-target>"
"   </routing-instance>";
        }

        for (size_t i = 1; i <= instances_set_count_; i++) {
            os <<
"   <routing-instance name='red" << i << "'>"
"       <vrf-target>" << getRouteTarget(i, "1") << "</vrf-target>"
"   </routing-instance>"
"   <routing-instance name='blue" << i << "'>"
"       <vrf-target>" << getRouteTarget(i, "2") << "</vrf-target>"
"   </routing-instance>"
"   <routing-instance name='green" << i << "'>"
"       <vrf-target>" << getRouteTarget(i, "3") << "</vrf-target>"
"       <vrf-target>"
"           <import-export>import</import-export>" << getRouteTarget(i, "1") <<
"       </vrf-target>"
"       <vrf-target>"
"           <import-export>import</import-export>" << getRouteTarget(i, "2") <<
"       </vrf-target>"
"   </routing-instance>"
            ;

            if (preconfigure_pm_) {
                os <<
"   <routing-instance name='default-domain:default-project:ip-fabric:ip-fabric"
                    << i << "'>"
"       <vrf-target>target:127.0.0.1:9" << format("%|03|")%i << "</vrf-target>"
"   </routing-instance>";
            }
        }

        os << "</config>";
        return os.str();
    }

    void CreateProjectManagerRoutingInstance() {
        ostringstream os;
        os << "<?xml version='1.0' encoding='utf-8'?><config>";
        os <<
"  <routing-instance name='default-domain:default-project:ip-fabric:ip-fabric'>"
"       <vrf-target>target:127.0.0.1:60000</vrf-target>"
"   </routing-instance>";

        for (size_t i = 1; i <= instances_set_count_; i++) {
            os <<
"   <routing-instance name='default-domain:default-project:ip-fabric:ip-fabric"
                << i << "'>"
"       <vrf-target>target:127.0.0.1:9" << format("%|03|")%i << "</vrf-target>"
"   </routing-instance>";
        }
        os << "</config>";
        server_->Configure(os.str());
        getProjectManagerNetworks(true);
    }

    void DeleteProjectManagerRoutingInstance() {
        ostringstream os;
        os << "<?xml version='1.0' encoding='utf-8'?><delete>";
        os <<
"  <routing-instance name='default-domain:default-project:ip-fabric:ip-fabric'>"
"       <vrf-target>target:127.0.0.1:60000</vrf-target>"
"   </routing-instance>";

        for (size_t i = 1; i <= instances_set_count_; i++) {
            os <<
"   <routing-instance name='default-domain:default-project:ip-fabric:ip-fabric"
                << i << "'>"
"       <vrf-target>target:127.0.0.1:9" << format("%|03|")%i << "</vrf-target>"
"   </routing-instance>";
        }

        os << "</delete>";
        server_->Configure(os.str());
        getProjectManagerNetworks(false);
    }

    void getProjectManagerNetworks(bool create) {
        task_util::WaitForIdle();
        for (size_t i = 1; i <= instances_set_count_; i++) {
            ostringstream os;
            os << "default-domain:default-project:ip-fabric:ip-fabric" << i;
            os << ".ermvpn.0";

            ostringstream os2;
            os2 << "default-domain:default-project:ip-fabric:ip-fabric" << i;
            os2 << ".mvpn.0";

            if (create) {
                TASK_UTIL_EXPECT_NE(static_cast<BgpTable *>(NULL),
                    server_->database()->FindTable(os.str()));
                fabric_ermvpn_[i-1] = dynamic_cast<ErmVpnTable *>(
                    server_->database()->FindTable(os.str()));
                assert(fabric_ermvpn_[i-1]);

                TASK_UTIL_EXPECT_NE(static_cast<BgpTable *>(NULL),
                    server_->database()->FindTable(os2.str()));
                fabric_mvpn_[i-1] = dynamic_cast<MvpnTable *>(
                    server_->database()->FindTable(os2.str()));
                assert(fabric_mvpn_[i-1]);
            } else {
                TASK_UTIL_EXPECT_EQ(static_cast<BgpTable *>(NULL),
                    server_->database()->FindTable(os.str()));
                fabric_ermvpn_[i-1] = NULL;

                TASK_UTIL_EXPECT_EQ(static_cast<BgpTable *>(NULL),
                    server_->database()->FindTable(os2.str()));
                fabric_mvpn_[i-1] = NULL;
            }
        }
    }

    virtual void SetUp() {
        evm_.reset(new EventManager());
        server_.reset(new BgpServerTest(evm_.get(), "local"));
        server_->set_mvpn_ipv4_enable(true);
        thread_.reset(new ServerThread(evm_.get()));
        thread_->Start();
        preconfigure_pm_ = std::tr1::get<0>(GetParam());
        instances_set_count_ = std::tr1::get<1>(GetParam());
        groups_count_ = std::tr1::get<2>(GetParam());
        paths_count_ = std::tr1::get<3>(GetParam());
        fabric_ermvpn_ = new ErmVpnTable *[instances_set_count_];
        fabric_mvpn_ = new MvpnTable *[instances_set_count_];
        red_ = new MvpnTable *[instances_set_count_];
        blue_ = new MvpnTable *[instances_set_count_];
        green_ = new MvpnTable *[instances_set_count_];
        peers_ = new scoped_ptr<BgpPeerMock>[paths_count_];
        for (size_t i = 0; i < paths_count_; i++)
            peers_[i].reset(new BgpPeerMock(i));
        string config = GetConfig();

        server_->Configure(config);
        task_util::WaitForIdle();

        TASK_UTIL_EXPECT_NE(static_cast<BgpTable *>(NULL),
                            server_->database()->FindTable("bgp.mvpn.0"));
        master_ = static_cast<BgpTable *>(
            server_->database()->FindTable("bgp.mvpn.0"));
        TASK_UTIL_EXPECT_NE(static_cast<BgpTable *>(NULL),
                            server_->database()->FindTable("bgp.ermvpn.0"));

        for (size_t i = 1; i <= instances_set_count_; i++) {
            ostringstream r, b, g;
            r << "red" << i << ".mvpn.0";
            b << "blue" << i << ".mvpn.0";
            g << "green" << i << ".mvpn.0";
            TASK_UTIL_EXPECT_NE(static_cast<BgpTable *>(NULL),
                                server_->database()->FindTable(r.str()));
            TASK_UTIL_EXPECT_NE(static_cast<BgpTable *>(NULL),
                                server_->database()->FindTable(b.str()));
            TASK_UTIL_EXPECT_NE(static_cast<BgpTable *>(NULL),
                                server_->database()->FindTable(g.str()));

            red_[i-1] = static_cast<MvpnTable *>(
                server_->database()->FindTable(r.str()));
            blue_[i-1] = static_cast<MvpnTable *>(
                server_->database()->FindTable(b.str()));
            green_[i-1] = static_cast<MvpnTable *>(
                server_->database()->FindTable(g.str()));
        }

        if (preconfigure_pm_)
            getProjectManagerNetworks(true);
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
        delete[] fabric_ermvpn_;
        delete[] fabric_mvpn_;
        delete[] red_;
        delete[] blue_;
        delete[] green_;
        delete[] peers_;
    }

    ErmVpnRoute *AddErmVpnRoute(ErmVpnTable *table, const string &prefix_str,
                                const string &target) {
        ErmVpnPrefix prefix(ErmVpnPrefix::FromString(prefix_str));
        DBRequest add_req;
        add_req.key.reset(new ErmVpnTable::RequestKey(prefix, NULL));
        BgpAttrSpec attr_spec;
        ExtCommunitySpec *commspec(new ExtCommunitySpec());
        RouteTarget tgt = RouteTarget::FromString(target);
        commspec->communities.push_back(tgt.GetExtCommunityValue());
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

    void AddType5MvpnRoute(BgpTable *table, const string &prefix_str,
                           const string &target, const string &source) {
        error_code e;
        IpAddress nh_address = IpAddress::from_string(source, e);
        RoutingInstanceTest *ri = dynamic_cast<RoutingInstanceTest *>(
                table->routing_instance());
        BgpAttrSourceRd source_rd(RouteDistinguisher(
            nh_address.to_v4().to_ulong(), ri->ri_index_i()));
        AddMvpnRoute(table, prefix_str, target, &source_rd);
    }

    void AddMvpnRoute(BgpTable *table, const string &prefix_str,
                      const string &target, BgpAttrSourceRd *source_rd = NULL,
                      bool add_leaf_req = false) {
        for (size_t i = 0; i < paths_count_; i++) {
            MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
            DBRequest add_req;
            add_req.key.reset( new MvpnTable::RequestKey(prefix,
                                                         peers_[i].get()));
            BgpAttrSpec attr_spec;
            ExtCommunitySpec *commspec(new ExtCommunitySpec());
            RouteTarget tgt = RouteTarget::FromString(target);
            commspec->communities.push_back(tgt.GetExtCommunityValue());
            attr_spec.push_back(commspec);
            if (source_rd)
                attr_spec.push_back(source_rd);

            if (add_leaf_req) {
                PmsiTunnelSpec *pmsi_spec(new PmsiTunnelSpec());
                pmsi_spec->tunnel_flags = PmsiTunnelSpec::LeafInfoRequired;
                attr_spec.push_back(pmsi_spec);
            }

            BgpAttrPtr attr = server_->attr_db()->Locate(attr_spec);
            if (source_rd)
                attr_spec.pop_back();
            STLDeleteValues(&attr_spec);
            add_req.data.reset(new MvpnTable::RequestData(attr, 0, 20));
            add_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
            table->Enqueue(&add_req);
        }
        task_util::WaitForIdle();
    }

    void DeleteMvpnRoute(BgpTable *table, const string &prefix_str) {
        for (size_t i = 0; i < paths_count_; i++) {
            DBRequest delete_req;
            MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
            delete_req.key.reset(new MvpnTable::RequestKey(prefix,
                                                           peers_[i].get()));
            delete_req.oper = DBRequest::DB_ENTRY_DELETE;
            table->Enqueue(&delete_req);
        }
        task_util::WaitForIdle();
    }

    MvpnRoute *VerifyLeafADMvpnRoute(MvpnTable *table, const string &prefix,
            const PMSIParams &pmsi) {
        MvpnPrefix type4_prefix =
            MvpnPrefix::FromString("4-" + prefix + ",127.0.0.1");
        TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                            table->FindRoute(type4_prefix));
        MvpnRoute *leaf_ad_rt = table->FindRoute(type4_prefix);
        EXPECT_EQ(type4_prefix, leaf_ad_rt->GetPrefix());
        TASK_UTIL_EXPECT_EQ(type4_prefix.ToString(),
                            leaf_ad_rt->GetPrefix().ToString());
        TASK_UTIL_EXPECT_TRUE(leaf_ad_rt->IsUsable());

        // Verify path attributes.
        const BgpAttr *attr = leaf_ad_rt->BestPath()->GetAttr();
        TASK_UTIL_EXPECT_NE(static_cast<const BgpAttr *>(NULL), attr);
        TASK_UTIL_EXPECT_NE(static_cast<const ExtCommunity *>(NULL),
                  attr->ext_community());

        int tunnel_encap = 0;
        ExtCommunity::ExtCommunityValue tunnel_encap_val;
        BOOST_FOREACH(ExtCommunity::ExtCommunityValue v,
                      attr->ext_community()->communities()) {
            if (ExtCommunity::is_tunnel_encap(v)) {
                tunnel_encap_val = v;
                tunnel_encap++;
            }
        }
        TASK_UTIL_EXPECT_EQ(1, tunnel_encap);
        TASK_UTIL_EXPECT_EQ("encapsulation:" + pmsi.encaps.front(),
                  TunnelEncap(tunnel_encap_val).ToString());
        TASK_UTIL_EXPECT_NE(static_cast<PmsiTunnel *>(NULL),
                            attr->pmsi_tunnel());
        TASK_UTIL_EXPECT_EQ(Ip4Address::from_string(pmsi.address),
                  attr->pmsi_tunnel()->identifier());
        TASK_UTIL_EXPECT_EQ(pmsi.label << 4 | 1, attr->pmsi_tunnel()->label());
        TASK_UTIL_EXPECT_EQ(PmsiTunnelSpec::IngressReplication,
                  attr->pmsi_tunnel()->tunnel_type());
        int rtargets = 0;
        ExtCommunity::ExtCommunityValue rtarget_val;
        BOOST_FOREACH(ExtCommunity::ExtCommunityValue v,
                      attr->ext_community()->communities()) {
            if (ExtCommunity::is_route_target(v)) {
                rtarget_val = v;
                rtargets++;
            }
        }

        // Route target must be solely S-PMSI Sender Originator:0, 192.168.1.1:0
        TASK_UTIL_EXPECT_EQ(1, rtargets);
        TASK_UTIL_EXPECT_EQ("target:192.168.1.1:0",
                            RouteTarget(rtarget_val).ToString());

        return leaf_ad_rt;
    }

    void VerifyWithNoProjectManager(size_t red1c, size_t blue1c, size_t green1c,
            size_t masterc) const {
        // Verify that there is no MvpnManager object created yet.
        for (size_t i = 1; i <= instances_set_count_; i++) {
            TASK_UTIL_EXPECT_EQ(static_cast<MvpnManager *>(NULL),
                                red_[i-1]->manager());
            TASK_UTIL_EXPECT_EQ(static_cast<MvpnManager *>(NULL),
                                blue_[i-1]->manager());
            TASK_UTIL_EXPECT_EQ(static_cast<MvpnManager *>(NULL),
                                green_[i-1]->manager());

            TASK_UTIL_EXPECT_EQ(red1c, red_[i-1]->Size());
            TASK_UTIL_EXPECT_EQ(blue1c, blue_[i-1]->Size());

            // 1 green1 + 1 red1 + 1 blue1
            TASK_UTIL_EXPECT_EQ(green1c, green_[i-1]->Size());
        }
        TASK_UTIL_EXPECT_EQ(masterc, master_->Size());
    }

    void VerifyWithProjectManager(
            size_t red1c = 0, size_t blue1c = 0, size_t green1c = 0,
            size_t masterc = 0) const {
        TASK_UTIL_EXPECT_EQ(masterc + 4*instances_set_count_ + 1,
                            master_->Size());
        for (size_t i = 1; i <= instances_set_count_; i++) {
            TASK_UTIL_EXPECT_EQ(red1c + 1, red_[i-1]->Size());
            TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                                red_[i-1]->FindType1ADRoute());

            TASK_UTIL_EXPECT_EQ(blue1c + 1, blue_[i-1]->Size());
            TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                                blue_[i-1]->FindType1ADRoute());

            // 1 green1+1 red1+1 blue1
            TASK_UTIL_EXPECT_EQ(green1c + 3, green_[i-1]->Size());
            TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                                green_[i-1]->FindType1ADRoute());

            // Verify that only green1 has discovered a neighbor from red1.
            TASK_UTIL_EXPECT_EQ(0U, red_[i - 1]->manager()->neighbors_count());
            TASK_UTIL_EXPECT_EQ(0U, blue_[i - 1]->manager()->neighbors_count());
            TASK_UTIL_EXPECT_EQ(2U,
                                green_[i - 1]->manager()->neighbors_count());

            MvpnNeighbor nbr;
            error_code err;
            EXPECT_TRUE(green_[i-1]->manager()->FindNeighbor(
                        *(red_[i-1]->routing_instance()->GetRD()), &nbr));
            EXPECT_EQ(*(red_[i-1]->routing_instance()->GetRD()), nbr.rd());
            EXPECT_EQ(0U, nbr.source_as());
            EXPECT_EQ(IpAddress::from_string("127.0.0.1", err),
                      nbr.originator());

            EXPECT_TRUE(green_[i-1]->manager()->FindNeighbor(
                        *(blue_[i-1]->routing_instance()->GetRD()), &nbr));
            EXPECT_EQ(*(blue_[i-1]->routing_instance()->GetRD()), nbr.rd());
            EXPECT_EQ(0U, nbr.source_as());
            EXPECT_EQ(IpAddress::from_string("127.0.0.1", err),
                      nbr.originator());
        }
    }

    bool CheckMvpnNeighborRoute(size_t i) const {
        if (red_[i-1]->Size() != 1)
            return false;
        if (!red_[i-1]->FindType1ADRoute())
            return false;

        if (blue_[i-1]->Size() != 1)
            return false;
        if (!blue_[i-1]->FindType1ADRoute())
            return false;

        if (green_[i-1]->Size() != 3) // 1 green1+1 red1+1 blue1
            return false;
        if (!green_[i-1]->FindType1ADRoute())
            return false;

        // Verify that only green1 has discovered a neighbor from red1.
        if (red_[i-1]->manager()->neighbors_count())
            return false;
        if (blue_[i-1]->manager()->neighbors_count())
            return false;
        if (green_[i-1]->manager()->neighbors_count() != 2)
            return false;

        MvpnNeighbor neighbor;
        if (!green_[i-1]->manager()->FindNeighbor(
                *(red_[i-1]->routing_instance()->GetRD()), &neighbor)) {
            return false;
        }
        if (!(*(red_[i-1]->routing_instance()->GetRD()) == neighbor.rd()))
            return false;
        if (neighbor.source_as())
            return false;
        error_code err;
        if (IpAddress::from_string("127.0.0.2", err) != neighbor.originator())
            return false;

        if (!green_[i-1]->manager()->FindNeighbor(
                *(blue_[i-1]->routing_instance()->GetRD()), &neighbor)) {
            return false;
        }
        if (!(*(blue_[i-1]->routing_instance()->GetRD()) == neighbor.rd()))
            return false;
        if (neighbor.source_as())
            return false;
        if (IpAddress::from_string("127.0.0.2", err) != neighbor.originator())
            return false;
        return true;
    }

    void VerifyInitialState(bool pm_configure = true,
            size_t red1c = 0, size_t blue1c = 0, size_t green1c = 0,
            size_t masterc = 0, size_t red1_nopm_c = 0, size_t blue1_nopm_c = 0,
            size_t green1_nopm_c = 0, size_t master_nopm_c = 0) {
        if (!preconfigure_pm_) {
            VerifyWithNoProjectManager(red1_nopm_c, blue1_nopm_c, green1_nopm_c,
                                       master_nopm_c);
            if (!pm_configure)
                return;
            CreateProjectManagerRoutingInstance();
        }

        VerifyWithProjectManager(red1c, blue1c, green1c, masterc);

        // Delete and add ProjectManager a few times and verify.
        for (int i = 0; i < 3; i++) {
            DeleteProjectManagerRoutingInstance();
            VerifyWithNoProjectManager(red1_nopm_c, blue1_nopm_c, green1_nopm_c,
                                       master_nopm_c);
            CreateProjectManagerRoutingInstance();
            VerifyWithProjectManager(red1c, blue1c, green1c, masterc);
        }
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
            boost::bind(&BgpMvpnTest::WalkCallback, this, _1, _2),
            boost::bind(&BgpMvpnTest::WalkDoneCallback, this, _1, _2,
                        &complete));
        std::cout << "Table " << table->name() << " walk start\n";
        table->WalkTable(walk_ref);
        TASK_UTIL_EXPECT_TRUE(complete);
        std::cout << "Table " << table->name() << " walk end\n";
    }

    string prefix1(int index) const {
        ostringstream os;
        os << "1-10.1.1.1:" << index << ",9.8.7.6";
        return os.str();
    }

    string prefix3(int index, int gindex = 1) const {
        ostringstream os;
        os << "3-10.1.1.1:" << index << ",9.8.7.6";
        os << ",224.1.2." << gindex << ",192.168.1.1";
        return os.str();
    }

    string prefix5(int index, int gindex = 1) const {
        ostringstream os;
        os << "5-0.0.0.0:" << index << ",9.8.7.6,224.1.2." << gindex;
        return os.str();
    }

    string prefix7(int index, int gindex = 1) const {
        ostringstream os;
        os << "7-10.1.1.1:" << index << ",1,9.8.7.6,224.1.2." << gindex;
        return os.str();
    }

    string native_prefix7(int gindex = 1) const {
        ostringstream os;
        os << "7-0:0,0,9.8.7.6,224.1.2." << gindex;
        return os.str();
    }
    string ermvpn_prefix(int index, int gindex = 1) const {
        ostringstream os;
        os << "2-10.1.1.1:" << index << "-192.168.1.1,224.1.2." << gindex;
        os << ",9.8.7.6";
        return os.str();
    }

    SG sg(int instance, int group) const {
        error_code e;
        ostringstream os;
        os << "224.1.2." << group;
        return SG(instance, MvpnState::SG(IpAddress::from_string("9.8.7.6", e),
                                          IpAddress::from_string(os.str(), e)));
    }

    SG sg(int instance, const Ip4Address &s, const Ip4Address &g) const {
        return SG(instance, MvpnState::SG(s, g));
    }

    scoped_ptr<EventManager> evm_;
    scoped_ptr<ServerThread> thread_;
    scoped_ptr<BgpServerTest> server_;
    DB db_;
    BgpTable *master_;
    ErmVpnTable **fabric_ermvpn_;
    MvpnTable **fabric_mvpn_;
    MvpnTable **red_;
    MvpnTable **blue_;
    MvpnTable **green_;
    scoped_ptr<BgpPeerMock> *peers_;
    bool preconfigure_pm_;
    size_t instances_set_count_;
    size_t groups_count_;
    size_t paths_count_;
};

static size_t GetInstanceCount() {
    char *env = getenv("BGP_MVPN_TEST_INSTANCE_COUNT");
    size_t count = 4;
    if (!env)
        return count;
    stringToInteger(string(env), count);
    return count;
}

static size_t GetGroupCount() {
    char *env = getenv("BGP_MVPN_TEST_GROUP_COUNT");
    size_t count = 4;
    if (!env)
        return count;
    stringToInteger(string(env), count);
    return count;
}

static size_t GetPathCount() {
    char *env = getenv("BGP_MVPN_TEST_PATH_COUNT");
    size_t count = 4;
    if (!env)
        return count;
    stringToInteger(string(env), count);
    return count;
}

INSTANTIATE_TEST_CASE_P(
    BgpMvpnTestWithParams,
    BgpMvpnTest,
    ::testing::Combine(
        ::testing::Bool(),
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
