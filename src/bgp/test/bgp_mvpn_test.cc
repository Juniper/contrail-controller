/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/algorithm/string/predicate.hpp>
#include <boost/format.hpp>
#include <boost/regex.hpp>

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
using boost::regex;
using boost::regex_search;
using boost::scoped_ptr;
using boost::starts_with;
using boost::system::error_code;
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

typedef std::tr1::tuple<bool, int, int> TestParams;
class BgpMvpnTest : public ::testing::TestWithParam<TestParams> {
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
        fabric_ermvpn_ = new ErmVpnTable *[instances_set_count_];
        fabric_mvpn_ = new MvpnTable *[instances_set_count_];
        red_ = new MvpnTable *[instances_set_count_];
        blue_ = new MvpnTable *[instances_set_count_];
        green_ = new MvpnTable *[instances_set_count_];
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
        TASK_UTIL_EXPECT_EQ(0, TcpServerManager::GetServerCount());
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
        MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
        DBRequest add_req;
        add_req.key.reset(new MvpnTable::RequestKey(prefix, NULL));

        BgpAttrSpec attr_spec;
        ExtCommunitySpec *commspec(new ExtCommunitySpec());
        RouteTarget tgt = RouteTarget::FromString(target);
        commspec->communities.push_back(tgt.GetExtCommunityValue());
        attr_spec.push_back(commspec);
        attr_spec.push_back(&source_rd);

        BgpAttrPtr attr = server_->attr_db()->Locate(attr_spec);
        attr_spec.pop_back();
        STLDeleteValues(&attr_spec);
        add_req.data.reset(new MvpnTable::RequestData(attr, 0, 20));
        add_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        table->Enqueue(&add_req);
        task_util::WaitForIdle();
    }

    void AddMvpnRoute(BgpTable *table, const string &prefix_str,
                      const string &target) {
        MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
        DBRequest add_req;
        add_req.key.reset(new MvpnTable::RequestKey(prefix, NULL));

        BgpAttrSpec attr_spec;
        ExtCommunitySpec *commspec(new ExtCommunitySpec());
        RouteTarget tgt = RouteTarget::FromString(target);
        commspec->communities.push_back(tgt.GetExtCommunityValue());
        attr_spec.push_back(commspec);

        BgpAttrPtr attr = server_->attr_db()->Locate(attr_spec);
        STLDeleteValues(&attr_spec);
        add_req.data.reset(new MvpnTable::RequestData(attr, 0, 20));
        add_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        table->Enqueue(&add_req);
        task_util::WaitForIdle();
    }

    void DeleteMvpnRoute(BgpTable *table, const string &prefix_str) {
        DBRequest delete_req;
        MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
        delete_req.key.reset(new MvpnTable::RequestKey(prefix, NULL));
        delete_req.oper = DBRequest::DB_ENTRY_DELETE;
        table->Enqueue(&delete_req);
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
        TASK_UTIL_EXPECT_EQ(pmsi.label, attr->pmsi_tunnel()->label());
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
            TASK_UTIL_EXPECT_EQ(0, red_[i-1]->manager()->neighbors_count());
            TASK_UTIL_EXPECT_EQ(0, blue_[i-1]->manager()->neighbors_count());
            TASK_UTIL_EXPECT_EQ(2, green_[i-1]->manager()->neighbors_count());

            MvpnNeighbor nbr;
            error_code err;
            EXPECT_TRUE(green_[i-1]->manager()->FindNeighbor(
                        *(red_[i-1]->routing_instance()->GetRD()), &nbr));
            EXPECT_EQ(*(red_[i-1]->routing_instance()->GetRD()), nbr.rd());
            EXPECT_EQ(0, nbr.source_as());
            EXPECT_EQ(IpAddress::from_string("127.0.0.1", err),
                      nbr.originator());

            EXPECT_TRUE(green_[i-1]->manager()->FindNeighbor(
                        *(blue_[i-1]->routing_instance()->GetRD()), &nbr));
            EXPECT_EQ(*(blue_[i-1]->routing_instance()->GetRD()), nbr.rd());
            EXPECT_EQ(0, nbr.source_as());
            EXPECT_EQ(IpAddress::from_string("127.0.0.1", err),
                      nbr.originator());
        }
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
        os << "5-0.0.0.0:" << index << ",224.1.2." << gindex << ",9.8.7.6";
        return os.str();
    }

    string prefix7(int index, int gindex = 1) const {
        ostringstream os;
        os << "7-10.1.1.1:" << index << ",1,224.1.2." << gindex << ",9.8.7.6";
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
    bool preconfigure_pm_;
    size_t instances_set_count_;
    size_t groups_count_;
};

// Ensure that Type1 AD routes are created inside the mvpn table.
TEST_P(BgpMvpnTest, Type1ADLocal) {
    if (!preconfigure_pm_) {
        for (size_t i = 1; i <= instances_set_count_; i++) {
            TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                                red_[i-1]->FindType1ADRoute());
            TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                                blue_[i-1]->FindType1ADRoute());
            TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                                green_[i-1]->FindType1ADRoute());
        }
    }
    VerifyInitialState();
}

// Change Identifier and ensure that routes have updated originator id.
TEST_P(BgpMvpnTest, Type1ADLocalWithIdentifierChanged) {
    if (!preconfigure_pm_) {
        for (size_t i = 1; i <= instances_set_count_; i++) {
            TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                                red_[i-1]->FindType1ADRoute());
            TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                                blue_[i-1]->FindType1ADRoute());
            TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                                green_[i-1]->FindType1ADRoute());
        }
    }
    VerifyInitialState();
    error_code err;
    UpdateBgpIdentifier("127.0.0.2");
    TASK_UTIL_EXPECT_EQ(4*instances_set_count_ + 1, master_->Size());

    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(1, red_[i-1]->Size());
        TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                            red_[i-1]->FindType1ADRoute());

        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size());
        TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                            blue_[i-1]->FindType1ADRoute());

        TASK_UTIL_EXPECT_EQ(3, green_[i-1]->Size()); // 1 green1+1 red1+1 blue1
        TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                            green_[i-1]->FindType1ADRoute());

        // Verify that only green1 has discovered a neighbor from red1.
        TASK_UTIL_EXPECT_EQ(0, red_[i-1]->manager()->neighbors_count());
        TASK_UTIL_EXPECT_EQ(0, blue_[i-1]->manager()->neighbors_count());
        TASK_UTIL_EXPECT_EQ(2, green_[i-1]->manager()->neighbors_count());

        MvpnNeighbor neighbor;
        EXPECT_TRUE(green_[i-1]->manager()->FindNeighbor(
                        *(red_[i-1]->routing_instance()->GetRD()), &neighbor));
        EXPECT_EQ(*(red_[i-1]->routing_instance()->GetRD()), neighbor.rd());
        EXPECT_EQ(0, neighbor.source_as());
        EXPECT_EQ(IpAddress::from_string("127.0.0.2", err),
                  neighbor.originator());

        EXPECT_TRUE(green_[i-1]->manager()->FindNeighbor(
                        *(blue_[i-1]->routing_instance()->GetRD()), &neighbor));
        EXPECT_EQ(*(blue_[i-1]->routing_instance()->GetRD()), neighbor.rd());
        EXPECT_EQ(0, neighbor.source_as());
        EXPECT_EQ(IpAddress::from_string("127.0.0.2", err),
                  neighbor.originator());
    }
}

// Reset BGP Identifier and ensure that Type1 route is no longer generated.
TEST_P(BgpMvpnTest, Type1ADLocalWithIdentifierRemoved) {
    if (!preconfigure_pm_) {
        for (size_t i = 1; i <= instances_set_count_; i++) {
            TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                                red_[i-1]->FindType1ADRoute());
            TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                                blue_[i-1]->FindType1ADRoute());
            TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                                green_[i-1]->FindType1ADRoute());
        }
    }
    VerifyInitialState();
    error_code err;
    UpdateBgpIdentifier("0.0.0.0");
    TASK_UTIL_EXPECT_EQ(0, master_->Size());

    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(0, red_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                            red_[i-1]->FindType1ADRoute());

        TASK_UTIL_EXPECT_EQ(0, blue_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                            blue_[i-1]->FindType1ADRoute());

        TASK_UTIL_EXPECT_EQ(0, green_[i-1]->Size()); // 1 green1+1 red1+1 blue1
        TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                            green_[i-1]->FindType1ADRoute());

        // Verify that only green1 has discovered a neighbor from red1.
        TASK_UTIL_EXPECT_EQ(0, red_[i-1]->manager()->neighbors_count());
        TASK_UTIL_EXPECT_EQ(0, blue_[i-1]->manager()->neighbors_count());
        TASK_UTIL_EXPECT_EQ(0, green_[i-1]->manager()->neighbors_count());
    }
}

// Add Type1AD route from a mock bgp peer into bgp.mvpn.0 table.
TEST_P(BgpMvpnTest, Type1AD_Remote) {
    if (!preconfigure_pm_) {
        for (size_t i = 1; i <= instances_set_count_; i++) {
            TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                                red_[i-1]->FindType1ADRoute());
            TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                                blue_[i-1]->FindType1ADRoute());
            TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                                green_[i-1]->FindType1ADRoute());
        }
    }
    VerifyInitialState();

    // Inject a Type1 route from a mock peer into bgp.mvpn.0 table with red1
    // route-target.

    for (size_t i = 1; i <= instances_set_count_; i++) {
        // Verify that only green1 has discovered a neighbor from red1.
        TASK_UTIL_EXPECT_EQ(0, red_[i-1]->manager()->neighbors_count());
        TASK_UTIL_EXPECT_EQ(0, blue_[i-1]->manager()->neighbors_count());
        TASK_UTIL_EXPECT_EQ(2, green_[i-1]->manager()->neighbors_count());

        AddMvpnRoute(master_, prefix1(i), getRouteTarget(i, "1"));

        TASK_UTIL_EXPECT_EQ(2, red_[i-1]->Size()); // 1 local + 1 remote(red1)
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        TASK_UTIL_EXPECT_EQ(4, green_[i-1]->Size()); // 1 local + 1 remote(red1)

        // Verify that neighbor is detected.
        TASK_UTIL_EXPECT_EQ(1, red_[i-1]->manager()->neighbors_count());
        TASK_UTIL_EXPECT_EQ(0, blue_[i-1]->manager()->neighbors_count());
        TASK_UTIL_EXPECT_EQ(3, green_[i-1]->manager()->neighbors_count());

        MvpnNeighbor nbr;
        error_code err;
        ostringstream os;
        os << i;
        string is = os.str();

        EXPECT_TRUE(red_[i-1]->manager()->FindNeighbor(
                        RouteDistinguisher::FromString("10.1.1.1:"+is, &err),
                        &nbr));
        EXPECT_EQ(0, nbr.source_as());
        EXPECT_EQ(IpAddress::from_string("9.8.7.6", err), nbr.originator());

        EXPECT_TRUE(green_[i-1]->manager()->FindNeighbor(
                        RouteDistinguisher::FromString("10.1.1.1:"+is, &err),
                        &nbr));
        EXPECT_EQ(0, nbr.source_as());
        EXPECT_EQ(IpAddress::from_string("9.8.7.6", err), nbr.originator());
    }

    TASK_UTIL_EXPECT_EQ(5*instances_set_count_ + 1, master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++)
        DeleteMvpnRoute(master_, prefix1(i));

    // Verify that neighbor is deleted.
    TASK_UTIL_EXPECT_EQ(4*instances_set_count_ + 1, master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(1, red_[i-1]->Size()); // 1 local
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        TASK_UTIL_EXPECT_EQ(3, green_[i-1]->Size()); // 1 local+1 red1+1 blue1
        TASK_UTIL_EXPECT_EQ(0, red_[i-1]->manager()->neighbors_count());
        TASK_UTIL_EXPECT_EQ(0, blue_[i-1]->manager()->neighbors_count());
        TASK_UTIL_EXPECT_EQ(2, green_[i-1]->manager()->neighbors_count());
    }
}

// Add Type3 S-PMSI route and verify that Type4 Leaf-AD is not originated if
// PMSI information is not available for forwarding.
TEST_P(BgpMvpnTest, Type3_SPMSI_Without_ErmVpnRoute) {
    VerifyInitialState(preconfigure_pm_);

    // Inject Type3 route from a mock peer into bgp.mvpn.0 table with red1
    // route target. This route should go into red1 and green1 table.
    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            AddMvpnRoute(master_, prefix3(i, j), getRouteTarget(i, "1"));

    if (!preconfigure_pm_) {
        TASK_UTIL_EXPECT_EQ(instances_set_count_*groups_count_,
                            master_->Size());
        for (size_t i = 1; i <= instances_set_count_; i++) {
            TASK_UTIL_EXPECT_EQ(groups_count_, red_[i-1]->Size());
            TASK_UTIL_EXPECT_EQ(0, blue_[i-1]->Size());
            TASK_UTIL_EXPECT_EQ(groups_count_, green_[i-1]->Size());
        }
        VerifyInitialState(true, groups_count_, 0, groups_count_,
                           instances_set_count_*groups_count_, groups_count_, 0,
                           groups_count_, instances_set_count_*groups_count_);
    }

    TASK_UTIL_EXPECT_EQ((4 + groups_count_)*instances_set_count_ + 1,
                        master_->Size());

    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local + groups_count_ remote(red1)
        TASK_UTIL_EXPECT_EQ(groups_count_+1, red_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local + 2 remote(red1) + 1 remote(green1)
        TASK_UTIL_EXPECT_EQ(3 + 1*groups_count_, green_[i-1]->Size());
    }

    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            DeleteMvpnRoute(master_, prefix3(i, j));

    TASK_UTIL_EXPECT_EQ(4*instances_set_count_+1, master_->Size());

    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(1, red_[i-1]->Size()); // 1 local
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local + 1 remote(red1) + 1 remote(blue1)
        TASK_UTIL_EXPECT_EQ(3, green_[i-1]->Size());
    }
}

// Add Type3 S-PMSI route and verify that Type4 Leaf-AD gets originated with the
// right set of path attributes.
TEST_P(BgpMvpnTest, Type3_SPMSI_With_ErmVpnRoute) {
    VerifyInitialState(preconfigure_pm_);
    // Inject Type3 route from a mock peer into bgp.mvpn.0 table with red1 route
    // target. This route should go into red1 and green1 table.
    ErmVpnRoute *ermvpn_rt[instances_set_count_*groups_count_];
    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            tbb::mutex::scoped_lock lock(pmsi_params_mutex);
            ermvpn_rt[(i-1)*groups_count_+(j-1)] = NULL;
            PMSIParams pmsi(PMSIParams(10, "1.2.3.4", "gre",
                            &ermvpn_rt[(i-1)*groups_count_+(j-1)]));
            pmsi_params.insert(make_pair(sg(i, j), pmsi));
            lock.release();
            AddMvpnRoute(master_, prefix3(i,j), getRouteTarget(i, "1"));
        }
    }

    if (!preconfigure_pm_) {
        TASK_UTIL_EXPECT_EQ(instances_set_count_*groups_count_,
                            master_->Size());
        for (size_t i = 1; i <= instances_set_count_; i++) {
            TASK_UTIL_EXPECT_EQ(groups_count_, red_[i-1]->Size());
            TASK_UTIL_EXPECT_EQ(0, blue_[i-1]->Size());
            TASK_UTIL_EXPECT_EQ(groups_count_, green_[i-1]->Size());
        }
        VerifyInitialState(true, groups_count_, 0, groups_count_,
                           instances_set_count_*groups_count_, groups_count_, 0,
                           groups_count_, instances_set_count_*groups_count_);
    }

    TASK_UTIL_EXPECT_EQ((4 + groups_count_)*instances_set_count_ + 1,
                        master_->Size());

    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            ErmVpnRoute *rt =
                AddErmVpnRoute(fabric_ermvpn_[i-1], ermvpn_prefix(i, j),
                               "target:127.0.0.1:1100");
            tbb::mutex::scoped_lock lock(pmsi_params_mutex);
            ermvpn_rt[(i-1)*groups_count_+(j-1)] = rt;
        }
    }

    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local+1 remote(red1)+1 leaf-ad
        TASK_UTIL_EXPECT_EQ(1 + 2*groups_count_, red_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local + 1 remote(red1) + 1 remote(green1) + // AD
        // 1 red-spmsi + 1 red-leafad
        TASK_UTIL_EXPECT_EQ(3 + 2*groups_count_, green_[i-1]->Size());
        for (size_t j = 1; j <= groups_count_; j++) {
            // Lookup the actual leaf-ad route and verify its attributes.
            VerifyLeafADMvpnRoute(red_[i-1], prefix3(i, j),
                                  pmsi_params[sg(i, j)]);
            VerifyLeafADMvpnRoute(green_[i-1], prefix3(i, j),
                                  pmsi_params[sg(i, j)]);
        }
    }

    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            // Setup ermvpn route before type 3 spmsi route is added.
            DeleteMvpnRoute(master_, prefix3(i, j));
            {
                tbb::mutex::scoped_lock lock(pmsi_params_mutex);
                pmsi_params.erase(sg(i, j));
            }
            DeleteErmVpnRoute(fabric_ermvpn_[i-1], ermvpn_prefix(i, j));
        }
    }

    TASK_UTIL_EXPECT_EQ(4*instances_set_count_ + 1, master_->Size()); // 3 local
    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(1, red_[i-1]->Size()); // 1 local
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local + 1 remote(red1) + 1 remote(blue1)
        TASK_UTIL_EXPECT_EQ(3, green_[i-1]->Size());
    }
}

// Add Type3 S-PMSI route and verify that Type4 Leaf-AD gets originated with the
// right set of path attributes, but only after ermvpn route becomes available.
TEST_P(BgpMvpnTest, Type3_SPMSI_With_ErmVpnRoute_2) {
    VerifyInitialState(preconfigure_pm_);
    // Inject Type3 route from a mock peer into bgp.mvpn.0 table with red1 route
    // target. This route should go into red1 and green1 table.
    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            AddMvpnRoute(master_, prefix3(i,j), getRouteTarget(i, "1"));

    if (!preconfigure_pm_) {
        VerifyInitialState(false, groups_count_, 0, groups_count_,
                           instances_set_count_*groups_count_, groups_count_,
                           0, groups_count_,
                           instances_set_count_*groups_count_);
    } else {
        TASK_UTIL_EXPECT_EQ((4 + groups_count_)*instances_set_count_ + 1,
                            master_->Size());
        for (size_t i = 1; i <= instances_set_count_; i++) {
            // 1 local + 1 remote(red1)
            TASK_UTIL_EXPECT_EQ(groups_count_ + 1, red_[i-1]->Size());
            TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
            // 1 local + 2 remote(red1) + 1 remote(green1)
            TASK_UTIL_EXPECT_EQ(groups_count_+3, green_[i-1]->Size());
        }
    }

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

    if (!preconfigure_pm_) {
        VerifyInitialState(true, groups_count_, 0, groups_count_,
                           instances_set_count_*groups_count_, groups_count_,
                           0, groups_count_,
                           instances_set_count_*groups_count_);
    }

    TASK_UTIL_EXPECT_EQ((4 + groups_count_)*instances_set_count_ + 1,
                        master_->Size());

    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            ErmVpnRoute *rt =
                AddErmVpnRoute(fabric_ermvpn_[i-1], ermvpn_prefix(i, j),
                               "target:127.0.0.1:1100");
            tbb::mutex::scoped_lock lock(pmsi_params_mutex);
            ermvpn_rt[(i-1)*groups_count_+(j-1)] = rt;
        }
    }

    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local+1 remote(red1)+1 leaf-ad
        TASK_UTIL_EXPECT_EQ(1 + 2*groups_count_, red_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local + 1 remote(red1) + 1 remote(green1) + // AD
        // 1 red-spmsi + 1 red-leafad
        TASK_UTIL_EXPECT_EQ(3 + 2*groups_count_, green_[i-1]->Size());
        for (size_t j = 1; j <= groups_count_; j++) {
            // Lookup the actual leaf-ad route and verify its attributes.
            VerifyLeafADMvpnRoute(red_[i-1], prefix3(i, j),
                                  pmsi_params[sg(i, j)]);
            VerifyLeafADMvpnRoute(green_[i-1], prefix3(i, j),
                                  pmsi_params[sg(i, j)]);
        }
    }

    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            // Setup ermvpn route before type 3 spmsi route is added.
            DeleteMvpnRoute(master_, prefix3(i, j));
            {
                tbb::mutex::scoped_lock lock(pmsi_params_mutex);
                pmsi_params.erase(sg(i, j));
            }
            DeleteErmVpnRoute(fabric_ermvpn_[i-1], ermvpn_prefix(i, j));
        }
    }

    TASK_UTIL_EXPECT_EQ(4*instances_set_count_ + 1, master_->Size()); // 3 local
    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(1, red_[i-1]->Size()); // 1 local
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local + 1 remote(red1) + 1 remote(blue1)
        TASK_UTIL_EXPECT_EQ(3, green_[i-1]->Size());
    }
}

// Verify that if ermvpn route is deleted, then any type 4 route if originated
// already is withdrawn.
TEST_P(BgpMvpnTest, Type3_SPMSI_With_ErmVpnRoute_3) {
    VerifyInitialState(preconfigure_pm_);
    // Inject Type3 route from a mock peer into bgp.mvpn.0 table with red1 route
    // target. This route should go into red1 and green1 table.
    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            AddMvpnRoute(master_, prefix3(i,j), getRouteTarget(i, "1"));

    if (!preconfigure_pm_) {
        VerifyInitialState(false, groups_count_, 0, groups_count_,
                           instances_set_count_*groups_count_, groups_count_,
                           0, groups_count_,
                           instances_set_count_*groups_count_);
    } else {
        TASK_UTIL_EXPECT_EQ((4 + groups_count_)*instances_set_count_ + 1,
                            master_->Size());
        for (size_t i = 1; i <= instances_set_count_; i++) {
            // 1 local + 1 remote(red1)
            TASK_UTIL_EXPECT_EQ(groups_count_ + 1, red_[i-1]->Size());
            TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
            // 1 local + 2 remote(red1) + 1 remote(green1)
            TASK_UTIL_EXPECT_EQ(groups_count_+3, green_[i-1]->Size());
        }
    }

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

    if (!preconfigure_pm_) {
        VerifyInitialState(true, groups_count_, 0, groups_count_,
                           instances_set_count_*groups_count_, groups_count_,
                           0, groups_count_,
                           instances_set_count_*groups_count_);
    }

    TASK_UTIL_EXPECT_EQ((4 + groups_count_)*instances_set_count_ + 1,
                        master_->Size());

    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            ErmVpnRoute *rt =
                AddErmVpnRoute(fabric_ermvpn_[i-1], ermvpn_prefix(i, j),
                               "target:127.0.0.1:1100");
            tbb::mutex::scoped_lock lock(pmsi_params_mutex);
            ermvpn_rt[(i-1)*groups_count_+(j-1)] = rt;
        }
    }

    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local+1 remote(red1)+1 leaf-ad
        TASK_UTIL_EXPECT_EQ(1 + 2*groups_count_, red_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local + 1 remote(red1) + 1 remote(green1) + // AD
        // 1 red-spmsi + 1 red-leafad
        TASK_UTIL_EXPECT_EQ(3 + 2*groups_count_, green_[i-1]->Size());
        for (size_t j = 1; j <= groups_count_; j++) {
            // Lookup the actual leaf-ad route and verify its attributes.
            VerifyLeafADMvpnRoute(red_[i-1], prefix3(i, j),
                                  pmsi_params[sg(i, j)]);
            VerifyLeafADMvpnRoute(green_[i-1], prefix3(i, j),
                                  pmsi_params[sg(i, j)]);
        }
    }

    // Delete the ermvpn route and verify that leaf-ad route is also deleted.
    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            {
                tbb::mutex::scoped_lock lock(pmsi_params_mutex);
                pmsi_params.erase(sg(i, j));
            }
            DeleteErmVpnRoute(fabric_ermvpn_[i-1], ermvpn_prefix(i, j));
        }
    }

    TASK_UTIL_EXPECT_EQ((4 + groups_count_)*instances_set_count_ + 1,
                        master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(groups_count_ + 1, red_[i-1]->Size()); // 1 local
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local + 1 remote(red1) + 1 remote(blue1)
        TASK_UTIL_EXPECT_EQ(groups_count_+3, green_[i-1]->Size());
    }

    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            DeleteMvpnRoute(master_, prefix3(i, j));

    TASK_UTIL_EXPECT_EQ(4*instances_set_count_ + 1, master_->Size()); // 3 local
    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(1, red_[i-1]->Size()); // 1 local
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local + 1 remote(red1) + 1 remote(blue1)
        TASK_UTIL_EXPECT_EQ(3, green_[i-1]->Size());
    }
}

// Add Type3 S-PMSI route and verify that Type4 Leaf-AD gets originated with the
// right set of path attributes, but only after ermvpn route becomes available.
// Add spurious notify on ermvpn route and ensure that type-4 leafad path and
// its attributes do not change.
TEST_P(BgpMvpnTest, Type3_SPMSI_With_ErmVpnRoute_4) {
    VerifyInitialState(preconfigure_pm_);
    // Inject Type3 route from a mock peer into bgp.mvpn.0 table with red1 route
    // target. This route should go into red1 and green1 table.
    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            AddMvpnRoute(master_, prefix3(i,j), getRouteTarget(i, "1"));

    if (!preconfigure_pm_) {
        VerifyInitialState(false, groups_count_, 0, groups_count_,
                           instances_set_count_*groups_count_, groups_count_,
                           0, groups_count_,
                           instances_set_count_*groups_count_);
    } else {
        TASK_UTIL_EXPECT_EQ((4 + groups_count_)*instances_set_count_ + 1,
                            master_->Size());
        for (size_t i = 1; i <= instances_set_count_; i++) {
            // 1 local + 1 remote(red1)
            TASK_UTIL_EXPECT_EQ(groups_count_ + 1, red_[i-1]->Size());
            TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
            // 1 local + 2 remote(red1) + 1 remote(green1)
            TASK_UTIL_EXPECT_EQ(groups_count_+3, green_[i-1]->Size());
        }
    }

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

    if (!preconfigure_pm_) {
        VerifyInitialState(true, groups_count_, 0, groups_count_,
                           instances_set_count_*groups_count_, groups_count_,
                           0, groups_count_,
                           instances_set_count_*groups_count_);
    }

    TASK_UTIL_EXPECT_EQ((4 + groups_count_)*instances_set_count_ + 1,
                        master_->Size());

    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            ErmVpnRoute *rt =
                AddErmVpnRoute(fabric_ermvpn_[i-1], ermvpn_prefix(i, j),
                               "target:127.0.0.1:1100");
            tbb::mutex::scoped_lock lock(pmsi_params_mutex);
            ermvpn_rt[(i-1)*groups_count_+(j-1)] = rt;
        }

        // 1 local+1 remote(red1)+1 leaf-ad
        TASK_UTIL_EXPECT_EQ(1 + 2*groups_count_, red_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local + 1 remote(red1) + 1 remote(green1) + // AD
        // 1 red-spmsi + 1 red-leafad
        TASK_UTIL_EXPECT_EQ(3 + 2*groups_count_, green_[i-1]->Size());

        for (size_t j = 1; j <= groups_count_; j++) {
            // Lookup the actual leaf-ad route and verify its attributes.
            MvpnRoute *leafad_red_rt =
                VerifyLeafADMvpnRoute(red_[i-1], prefix3(i, j),
                                      pmsi_params[sg(i, j)]);
            MvpnRoute *leafad_green_rt =
                VerifyLeafADMvpnRoute(green_[i-1], prefix3(i, j),
                                      pmsi_params[sg(i, j)]);
            const BgpPath *red_path = leafad_red_rt->BestPath();
            const BgpAttr *red_attr = red_path->GetAttr();
            const BgpPath *green_path = leafad_green_rt->BestPath();
            const BgpAttr *green_attr = green_path->GetAttr();

            // Notify ermvpn route without any change.
            ermvpn_rt[(i-1)*groups_count_+(j-1)]->Notify();

            // Verify that leafad path or its attributes did not change.
            std::map<SG, const PMSIParams>::iterator iter =
                pmsi_params.find(sg(i, j));
            assert(iter != pmsi_params.end());
            TASK_UTIL_EXPECT_EQ(leafad_red_rt,
                VerifyLeafADMvpnRoute(red_[i-1], prefix3(i,j), iter->second));
            TASK_UTIL_EXPECT_EQ(leafad_green_rt,
                VerifyLeafADMvpnRoute(green_[i-1], prefix3(i,j), iter->second));
            TASK_UTIL_EXPECT_EQ(red_path, leafad_red_rt->BestPath());
            TASK_UTIL_EXPECT_EQ(green_path, leafad_green_rt->BestPath());
            TASK_UTIL_EXPECT_EQ(red_attr, leafad_red_rt->BestPath()->GetAttr());
            TASK_UTIL_EXPECT_EQ(green_attr,
                                leafad_green_rt->BestPath()->GetAttr());
        }
    }

    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            // Setup ermvpn route before type 3 spmsi route is added.
            DeleteMvpnRoute(master_, prefix3(i, j));
            {
                tbb::mutex::scoped_lock lock(pmsi_params_mutex);
                pmsi_params.erase(sg(i, j));
            }
            DeleteErmVpnRoute(fabric_ermvpn_[i-1], ermvpn_prefix(i, j));
        }
    }

    TASK_UTIL_EXPECT_EQ(4*instances_set_count_ + 1, master_->Size()); // 3 local
    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(1, red_[i-1]->Size()); // 1 local
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local + 1 remote(red1) + 1 remote(blue1)
        TASK_UTIL_EXPECT_EQ(3, green_[i-1]->Size());
    }
}

// Similar to previous test, but this time, do change some of the PMSI attrs.
// Leaf4 ad path already generated must be updated with the PMSI information.
TEST_P(BgpMvpnTest, Type3_SPMSI_With_ErmVpnRoute_5) {
    VerifyInitialState(preconfigure_pm_);
    // Inject Type3 route from a mock peer into bgp.mvpn.0 table with red1 route
    // target. This route should go into red1 and green1 table.
    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            AddMvpnRoute(master_, prefix3(i,j), getRouteTarget(i, "1"));

    if (!preconfigure_pm_) {
        VerifyInitialState(false, groups_count_, 0, groups_count_,
                           instances_set_count_*groups_count_, groups_count_,
                           0, groups_count_,
                           instances_set_count_*groups_count_);
    } else {
        TASK_UTIL_EXPECT_EQ((4 + groups_count_)*instances_set_count_ + 1,
                            master_->Size());
        for (size_t i = 1; i <= instances_set_count_; i++) {
            // 1 local + 1 remote(red1)
            TASK_UTIL_EXPECT_EQ(groups_count_ + 1, red_[i-1]->Size());
            TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
            // 1 local + 2 remote(red1) + 1 remote(green1)
            TASK_UTIL_EXPECT_EQ(groups_count_+3, green_[i-1]->Size());
        }
    }

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

    if (!preconfigure_pm_) {
        VerifyInitialState(true, groups_count_, 0, groups_count_,
                           instances_set_count_*groups_count_, groups_count_,
                           0, groups_count_,
                           instances_set_count_*groups_count_);
    }

    TASK_UTIL_EXPECT_EQ((4 + groups_count_)*instances_set_count_ + 1,
                        master_->Size());

    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            ErmVpnRoute *rt =
                AddErmVpnRoute(fabric_ermvpn_[i-1], ermvpn_prefix(i, j),
                               "target:127.0.0.1:1100");
            tbb::mutex::scoped_lock lock(pmsi_params_mutex);
            ermvpn_rt[(i-1)*groups_count_+(j-1)] = rt;
        }

        // 1 local+1 remote(red1)+1 leaf-ad
        TASK_UTIL_EXPECT_EQ(1 + 2*groups_count_, red_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local + 1 remote(red1) + 1 remote(green1) + // AD
        // 1 red-spmsi + 1 red-leafad
        TASK_UTIL_EXPECT_EQ(3 + 2*groups_count_, green_[i-1]->Size());

        for (size_t j = 1; j <= groups_count_; j++) {
            // Lookup the actual leaf-ad route and verify its attributes.
            MvpnRoute *leafad_red_rt =
                VerifyLeafADMvpnRoute(red_[i-1], prefix3(i, j),
                                      pmsi_params[sg(i, j)]);
            MvpnRoute *leafad_green_rt =
                VerifyLeafADMvpnRoute(green_[i-1], prefix3(i, j),
                                      pmsi_params[sg(i, j)]);
            const BgpPath *red_path = leafad_red_rt->BestPath();
            const BgpAttr *red_attr = red_path->GetAttr();
            const BgpPath *green_path = leafad_green_rt->BestPath();
            const BgpAttr *green_attr = green_path->GetAttr();

            // Update PMSI.
            {
                tbb::mutex::scoped_lock lock(pmsi_params_mutex);
                pmsi_params.erase(sg(i, j));
            }

            PMSIParams pmsi(PMSIParams(20, "1.2.3.5", "udp",
                                       &ermvpn_rt[(i-1)*groups_count_+(j-1)]));
            {
                tbb::mutex::scoped_lock lock(pmsi_params_mutex);
                assert(pmsi_params.insert(make_pair(sg(i, j), pmsi)).second);
            }
            TASK_UTIL_EXPECT_EQ(ermvpn_rt[(i-1)*groups_count_+(j-1)],
                AddErmVpnRoute(fabric_ermvpn_[i-1], ermvpn_prefix(i, j),
                               "target:127.0.0.1:1101"));
            tbb::mutex::scoped_lock lock(pmsi_params_mutex);
            // Verify that leafad path or its attributes did not change.
            std::map<SG, const PMSIParams>::iterator iter =
                pmsi_params.find(sg(i, j));
            lock.release();
            assert(iter != pmsi_params.end());
            TASK_UTIL_EXPECT_NE(red_path, leafad_red_rt->BestPath());
            TASK_UTIL_EXPECT_NE(green_path, leafad_green_rt->BestPath());
            TASK_UTIL_EXPECT_NE(static_cast<BgpPath *>(NULL),
                                leafad_red_rt->BestPath());
            TASK_UTIL_EXPECT_NE(static_cast<BgpPath *>(NULL),
                                leafad_green_rt->BestPath());
            TASK_UTIL_EXPECT_NE(red_attr, leafad_red_rt->BestPath()->GetAttr());
            TASK_UTIL_EXPECT_NE(green_attr,
                                leafad_green_rt->BestPath()->GetAttr());
            TASK_UTIL_EXPECT_EQ(leafad_red_rt,
                VerifyLeafADMvpnRoute(red_[i-1], prefix3(i,j), iter->second));
            TASK_UTIL_EXPECT_EQ(leafad_green_rt,
                VerifyLeafADMvpnRoute(green_[i-1], prefix3(i,j), iter->second));
        }
    }

    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            // Setup ermvpn route before type 3 spmsi route is added.
            DeleteMvpnRoute(master_, prefix3(i, j));
            {
                tbb::mutex::scoped_lock lock(pmsi_params_mutex);
                pmsi_params.erase(sg(i, j));
            }
            DeleteErmVpnRoute(fabric_ermvpn_[i-1], ermvpn_prefix(i, j));
        }
    }

    TASK_UTIL_EXPECT_EQ(4*instances_set_count_ + 1, master_->Size()); // 3 local
    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(1, red_[i-1]->Size()); // 1 local
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local + 1 remote(red1) + 1 remote(blue1)
        TASK_UTIL_EXPECT_EQ(3, green_[i-1]->Size());
    }
}

// Receive Type-7 join route and ensure that Type-3 S-PMSI is generated.
// Type-5 route comes in first followed by Type-7 join
TEST_P(BgpMvpnTest, Type3_SPMSI_1) {
    VerifyInitialState(preconfigure_pm_);
    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            AddType5MvpnRoute(red_[i-1], prefix5(i, j), getRouteTarget(i, "1"),
                              "10.1.1.1");
        }
    }

    if (!preconfigure_pm_) {
        VerifyInitialState(false, groups_count_, 0, groups_count_,
                           instances_set_count_*groups_count_, groups_count_,
                           0, groups_count_,
                           instances_set_count_*groups_count_);
    } else {
        TASK_UTIL_EXPECT_EQ((4 + groups_count_)*instances_set_count_ + 1,
                            master_->Size());
        for (size_t i = 1; i <= instances_set_count_; i++) {
            // 1 local + 1 remote(red1)
            TASK_UTIL_EXPECT_EQ(groups_count_ + 1, red_[i-1]->Size());
            TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
            // 1 local + 2 remote(red1) + 1 remote(green1)
            TASK_UTIL_EXPECT_EQ(groups_count_+3, green_[i-1]->Size());
        }
    }

    // Inject type-7 receiver route with red1 RI vit.
    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            AddMvpnRoute(master_, prefix7(i,j), "target:127.0.0.1:" +
                integerToString(red_[i-1]->routing_instance()->index()));
        }
    }

    if (!preconfigure_pm_) {
        VerifyInitialState(false, 2*groups_count_, 0, groups_count_,
                           2*instances_set_count_*groups_count_,
                           2*groups_count_, 0, groups_count_,
                           2*instances_set_count_*groups_count_);
        VerifyInitialState(true, 3*groups_count_, 0, 2*groups_count_,
                           3*instances_set_count_*groups_count_,
                           2*groups_count_, 0, groups_count_,
                           2*instances_set_count_*groups_count_);
    }

    // Route should go only into red_ which has the source-active route. This
    // should cause a Type3 S-PMSI route to be originated. This route will get
    // imported into green1 but no type-4 will get generated as there is no
    // active receiver agent joined yet.

    // 4 local-ad + 1 remote-sa + 1 remote-join + 1 local-spmsi
    TASK_UTIL_EXPECT_EQ((4 + 3*groups_count_)*instances_set_count_ + 1,
        master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local-ad + 1 remote-sa(red1)+1 remote-join + 1 spmsi
        TASK_UTIL_EXPECT_EQ(1 + 3*groups_count_, red_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local-ad + 1 remote-ad(red1) + 1 remote-ad(blue1) +
        // 1 remote-sa(red) + 1 spmsi(red1)
        TASK_UTIL_EXPECT_EQ(3 + 2*groups_count_, green_[i-1]->Size());
    }

    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            DeleteMvpnRoute(red_[i-1], prefix5(i, j));

    // 4 local-ad + 1 join
    TASK_UTIL_EXPECT_EQ((4+groups_count_)*instances_set_count_ + 1,
                         master_->Size());

    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local+ 1 join
        TASK_UTIL_EXPECT_EQ(1+groups_count_, red_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local-ad + 1 remote-ad(red1) + 1 remote-ad(blue1)
        TASK_UTIL_EXPECT_EQ(3, green_[i-1]->Size());
    }

    // Remove join route.
    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            DeleteMvpnRoute(master_, prefix7(i, j));

    // 4 local-ad
    TASK_UTIL_EXPECT_EQ(4*instances_set_count_ + 1, master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(1, red_[i-1]->Size()); // 1 local+ 1 join
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local + 1 remote(red1) + 1 remote(blue1)
        TASK_UTIL_EXPECT_EQ(3, green_[i-1]->Size());
    }
}

// Receive Type-7 join route and ensure that Type-3 S-PMSI is generated.
// Type-7 join comes in first followed by Type-5 source-active
TEST_P(BgpMvpnTest, Type3_SPMSI_2) {
    VerifyInitialState(preconfigure_pm_);
    // Inject type-7 receiver route with red1 RI vit. There is no source-active
    // route yet, hence no type-3 s-pmsi should be generated.
    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            AddMvpnRoute(master_, prefix7(i,j), "target:127.0.0.1:" +
                integerToString(red_[i-1]->routing_instance()->index()));
        }
    }

    if (!preconfigure_pm_) {
        VerifyInitialState(false, groups_count_, 0, 1,
                           instances_set_count_*groups_count_,
                           groups_count_, 0, 0,
                           instances_set_count_*groups_count_);
    } else {
        // 4 local-ad + 1 remote-join
        TASK_UTIL_EXPECT_EQ((4+groups_count_)*instances_set_count_ + 1,
                            master_->Size());
        for (size_t i = 1; i <= instances_set_count_; i++) {
            // 1 local-ad + 1 remote-sa(red1)
            TASK_UTIL_EXPECT_EQ(1+groups_count_, red_[i-1]->Size());
            TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
            // 1 local-ad + 1 remote-ad(red1) + 1 remote-ad(blue1)
            TASK_UTIL_EXPECT_EQ(3, green_[i-1]->Size());
        }
    }

    // Now inject a remote type5.
    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            AddType5MvpnRoute(red_[i-1], prefix5(i, j), getRouteTarget(i, "1"),
                              "10.1.1.1");
        }
    }

    if (!preconfigure_pm_) {
        VerifyInitialState(false, 2*groups_count_, 0, groups_count_,
                           2*instances_set_count_*groups_count_,
                           2*groups_count_, 0, groups_count_,
                           2*instances_set_count_*groups_count_);
        VerifyInitialState(true, 3*groups_count_, 0, 2*groups_count_,
                           3*instances_set_count_*groups_count_,
                           2*groups_count_, 0, groups_count_,
                           2*instances_set_count_*groups_count_);
    }

    // Route should go only into red_ which has the source-active route. This
    // should cause a Type3 S-PMSI route to be originated. This route will get
    // imported into green1 but no type-4 will get generated as there is no
    // active receiver agent joined yet.

    // 4 local-ad + 1 remote-sa + 1 remote-join + 1 local-spmsi
    TASK_UTIL_EXPECT_EQ((4 + 3*groups_count_)*instances_set_count_ + 1,
        master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local-ad + 1 remote-sa(red1)+1 remote-join + 1 spmsi
        TASK_UTIL_EXPECT_EQ(1 + 3*groups_count_, red_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local-ad + 1 remote-ad(red1) + 1 remote-ad(blue1) +
        // 1 remote-sa(red) + 1 spmsi(red1)
        TASK_UTIL_EXPECT_EQ(3 + 2*groups_count_, green_[i-1]->Size());
    }

    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            DeleteMvpnRoute(red_[i-1], prefix5(i, j));

    // 4 local-ad + 1 join
    TASK_UTIL_EXPECT_EQ((4+groups_count_)*instances_set_count_ + 1,
                         master_->Size());

    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local+ 1 join
        TASK_UTIL_EXPECT_EQ(1+groups_count_, red_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local-ad + 1 remote-ad(red1) + 1 remote-ad(blue1)
        TASK_UTIL_EXPECT_EQ(3, green_[i-1]->Size());
    }

    // Remove join route.
    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            DeleteMvpnRoute(master_, prefix7(i, j));

    // 4 local-ad
    TASK_UTIL_EXPECT_EQ(4*instances_set_count_ + 1, master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(1, red_[i-1]->Size()); // 1 local+ 1 join
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local + 1 remote(red1) + 1 remote(blue1)
        TASK_UTIL_EXPECT_EQ(3, green_[i-1]->Size());
    }
}

// Receive Type-5 source-active followed by type-7 join.
// Type-7 join route gets deleted first, afterwards.
TEST_P(BgpMvpnTest, Type3_SPMSI_3) {
    VerifyInitialState(preconfigure_pm_);
    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            AddType5MvpnRoute(red_[i-1], prefix5(i, j), getRouteTarget(i, "1"),
                              "10.1.1.1");
        }
    }

    if (!preconfigure_pm_) {
        VerifyInitialState(false, groups_count_, 0, groups_count_,
                           instances_set_count_*groups_count_, groups_count_,
                           0, groups_count_,
                           instances_set_count_*groups_count_);
    } else {
        TASK_UTIL_EXPECT_EQ((4 + groups_count_)*instances_set_count_ + 1,
                            master_->Size());
        for (size_t i = 1; i <= instances_set_count_; i++) {
            // 1 local + 1 remote(red1)
            TASK_UTIL_EXPECT_EQ(groups_count_ + 1, red_[i-1]->Size());
            TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
            // 1 local + 2 remote(red1) + 1 remote(green1)
            TASK_UTIL_EXPECT_EQ(groups_count_+3, green_[i-1]->Size());
        }
    }

    // Inject type-7 receiver route with red1 RI vit.
    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            AddMvpnRoute(master_, prefix7(i,j), "target:127.0.0.1:" +
                integerToString(red_[i-1]->routing_instance()->index()));
        }
    }

    if (!preconfigure_pm_) {
        VerifyInitialState(false, 2*groups_count_, 0, groups_count_,
                           2*instances_set_count_*groups_count_,
                           2*groups_count_, 0, groups_count_,
                           2*instances_set_count_*groups_count_);
        VerifyInitialState(true, 3*groups_count_, 0, 2*groups_count_,
                           3*instances_set_count_*groups_count_,
                           2*groups_count_, 0, groups_count_,
                           2*instances_set_count_*groups_count_);
    }

    // Route should go only into red_ which has the source-active route. This
    // should cause a Type3 S-PMSI route to be originated. This route will get
    // imported into green1 but no type-4 will get generated as there is no
    // active receiver agent joined yet.

    // 4 local-ad + 1 remote-sa + 1 remote-join + 1 local-spmsi
    TASK_UTIL_EXPECT_EQ((4 + 3*groups_count_)*instances_set_count_ + 1,
        master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local-ad + 1 remote-sa(red1)+1 remote-join + 1 spmsi
        TASK_UTIL_EXPECT_EQ(1 + 3*groups_count_, red_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local-ad + 1 remote-ad(red1) + 1 remote-ad(blue1) +
        // 1 remote-sa(red) + 1 spmsi(red1)
        TASK_UTIL_EXPECT_EQ(3 + 2*groups_count_, green_[i-1]->Size());
    }

    // Remove type7 join route. Type-3 should go away.
    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            DeleteMvpnRoute(master_, prefix7(i, j));

    // 4 local-ad + 1 sa
    TASK_UTIL_EXPECT_EQ((4+groups_count_)*instances_set_count_ + 1,
                         master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local+ 1 join
        TASK_UTIL_EXPECT_EQ(1+groups_count_, red_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 localad + 1 remote-ad(red1) + 1 remote-ad(blue1) + 1 remote-sa(red)
        TASK_UTIL_EXPECT_EQ(3+groups_count_, green_[i-1]->Size());
    }

    // Remove type-5 source-active route.
    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            DeleteMvpnRoute(red_[i-1], prefix5(i, j));

    // 4 local-ad
    TASK_UTIL_EXPECT_EQ(4*instances_set_count_ + 1, master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(1, red_[i-1]->Size()); // 1 local+ 1 join
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local + 1 remote(red1) + 1 remote(blue1)
        TASK_UTIL_EXPECT_EQ(3, green_[i-1]->Size());
    }
}

// Receive Type-7 join route and ensure that Type-3 S-PMSI is generated.
// Type-7 join comes in first followed by Type-5 source-active
// Type-7 join route gets deleted first, afterwards.
TEST_P(BgpMvpnTest, Type3_SPMSI_4) {
    VerifyInitialState(true);
    // Inject type-7 receiver route with red1 RI vit. There is no source-active
    // route yet, hence no type-3 s-pmsi should be generated.
    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            AddMvpnRoute(master_, prefix7(i,j), "target:127.0.0.1:" +
                integerToString(red_[i-1]->routing_instance()->index()));
        }
    }

    // 4 local-ad + 1 remote-join
    TASK_UTIL_EXPECT_EQ((4+groups_count_)*instances_set_count_ + 1,
                        master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local-ad + 1 remote-sa(red1)
        TASK_UTIL_EXPECT_EQ(1+groups_count_, red_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local-ad + 1 remote-ad(red1) + 1 remote-ad(blue1)
        TASK_UTIL_EXPECT_EQ(3, green_[i-1]->Size());
    }

    // Now inject a remote type5.
    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            AddType5MvpnRoute(red_[i-1], prefix5(i, j), getRouteTarget(i, "1"),
                              "10.1.1.1");
        }
    }

    // Route should go only into red_ which has the source-active route. This
    // should cause a Type3 S-PMSI route to be originated. This route will get
    // imported into green1 but no type-4 will get generated as there is no
    // active receiver agent joined yet.

    // 4 local-ad + 1 remote-sa + 1 remote-join + 1 local-spmsi
    TASK_UTIL_EXPECT_EQ((4 + 3*groups_count_)*instances_set_count_ + 1,
        master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local-ad + 1 remote-sa(red1)+1 remote-join + 1 spmsi
        TASK_UTIL_EXPECT_EQ(1 + 3*groups_count_, red_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local-ad + 1 remote-ad(red1) + 1 remote-ad(blue1) +
        // 1 remote-sa(red) + 1 spmsi(red1)
        TASK_UTIL_EXPECT_EQ(3 + 2*groups_count_, green_[i-1]->Size());
    }

    // Remove join route.
    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            DeleteMvpnRoute(master_, prefix7(i, j));

    TASK_UTIL_EXPECT_EQ((4 + groups_count_)*instances_set_count_ + 1,
                        master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local + 1 remote(red1)
        TASK_UTIL_EXPECT_EQ(groups_count_ + 1, red_[i-1]->Size());
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local + 2 remote(red1) + 1 remote(green1)
        TASK_UTIL_EXPECT_EQ(groups_count_+3, green_[i-1]->Size());
    }

    // Remove source-active route.
    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            DeleteMvpnRoute(red_[i-1], prefix5(i, j));

    // 4 local-ad
    TASK_UTIL_EXPECT_EQ(4*instances_set_count_ + 1, master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(1, red_[i-1]->Size()); // 1 local+ 1 join
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local + 1 remote(red1) + 1 remote(blue1)
        TASK_UTIL_EXPECT_EQ(3, green_[i-1]->Size());
    }
}

// Receive Type-7 remote join, but no type-5 source-active is received at all.
TEST_P(BgpMvpnTest, Type3_SPMSI_5) {
    VerifyInitialState(preconfigure_pm_);
    // Inject type-7 receiver route with red1 RI vit. There is no source-active
    // route yet, hence no type-3 s-pmsi should be generated.
    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            AddMvpnRoute(master_, prefix7(i,j), "target:127.0.0.1:" +
                integerToString(red_[i-1]->routing_instance()->index()));
        }
    }

    if (!preconfigure_pm_) {
        VerifyInitialState(false, groups_count_, 0, 1,
                           instances_set_count_*groups_count_,
                           groups_count_, 0, 0,
                           instances_set_count_*groups_count_);
        VerifyInitialState(true, groups_count_, 0, 0,
                           instances_set_count_*groups_count_,
                           groups_count_, 0, 0,
                           instances_set_count_*groups_count_);
    } else {
        // 4 local-ad + 1 remote-join
        TASK_UTIL_EXPECT_EQ((4+groups_count_)*instances_set_count_ + 1,
                            master_->Size());
        for (size_t i = 1; i <= instances_set_count_; i++) {
            // 1 local-ad + 1 remote-sa(red1)
            TASK_UTIL_EXPECT_EQ(1+groups_count_, red_[i-1]->Size());
            TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        }
    }

    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local-ad + 1 remote-ad(red1) + 1 remote-ad(blue1)
        TASK_UTIL_EXPECT_EQ(3, green_[i-1]->Size());
    }

    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            DeleteMvpnRoute(master_, prefix7(i, j));

    // 4 local-ad
    TASK_UTIL_EXPECT_EQ(4*instances_set_count_ + 1, master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(1, red_[i-1]->Size()); // 1 local+ 1 join
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local + 1 remote(red1) + 1 remote(blue1)
        TASK_UTIL_EXPECT_EQ(3, green_[i-1]->Size());
    }
}

// Receive Type-5 source active, but no type-7 join is received at all.
TEST_P(BgpMvpnTest, Type3_SPMSI_6) {
    VerifyInitialState(preconfigure_pm_);
    for (size_t i = 1; i <= instances_set_count_; i++) {
        for (size_t j = 1; j <= groups_count_; j++) {
            AddType5MvpnRoute(red_[i-1], prefix5(i, j), getRouteTarget(i, "1"),
                              "10.1.1.1");
        }
    }

    if (!preconfigure_pm_) {
        VerifyInitialState(false, groups_count_, 0, groups_count_,
                           instances_set_count_*groups_count_, groups_count_,
                           0, groups_count_,
                           instances_set_count_*groups_count_);
        VerifyInitialState(true, groups_count_, 0, groups_count_,
                           instances_set_count_*groups_count_, groups_count_,
                           0, groups_count_,
                           instances_set_count_*groups_count_);
    } else {
        TASK_UTIL_EXPECT_EQ((4 + groups_count_)*instances_set_count_ + 1,
                            master_->Size());
        for (size_t i = 1; i <= instances_set_count_; i++) {
            // 1 local + 1 remote(red1)
            TASK_UTIL_EXPECT_EQ(groups_count_ + 1, red_[i-1]->Size());
            TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        }
    }
    for (size_t i = 1; i <= instances_set_count_; i++) {
        // 1 local + 2 remote(red1) + 1 remote(green1)
        TASK_UTIL_EXPECT_EQ(groups_count_+3, green_[i-1]->Size());
    }

    for (size_t i = 1; i <= instances_set_count_; i++)
        for (size_t j = 1; j <= groups_count_; j++)
            DeleteMvpnRoute(red_[i-1], prefix5(i,j));

    // 4 local-ad
    TASK_UTIL_EXPECT_EQ(4*instances_set_count_ + 1, master_->Size());
    for (size_t i = 1; i <= instances_set_count_; i++) {
        TASK_UTIL_EXPECT_EQ(1, red_[i-1]->Size()); // 1 local+ 1 join
        TASK_UTIL_EXPECT_EQ(1, blue_[i-1]->Size()); // 1 local
        // 1 local + 1 remote(red1) + 1 remote(blue1)
        TASK_UTIL_EXPECT_EQ(3, green_[i-1]->Size());
    }
}

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

INSTANTIATE_TEST_CASE_P(BgpMvpnTestWithParams, BgpMvpnTest,
    ::testing::Combine(::testing::Bool(),
                       ::testing::Values(1, 2, GetInstanceCount()),
                       ::testing::Values(1, GetGroupCount())));

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
