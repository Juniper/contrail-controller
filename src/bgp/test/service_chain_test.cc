/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <algorithm>
#include <boost/regex.hpp>

#include <boost/foreach.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/program_options.hpp>
#include <pugixml/pugixml.hpp>

#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_config_ifmap.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet6/inet6_table.h"
#include "bgp/inet6vpn/inet6vpn_table.h"
#include "bgp/extended-community/load_balance.h"
#include "bgp/extended-community/site_of_origin.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/iservice_chain_mgr.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/service_chaining_types.h"
#include "bgp/security_group/security_group.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/test/bgp_test_util.h"
#include "control-node/control_node.h"
#include "db/db_graph.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/test/ifmap_test_util.h"
#include "net/community_type.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"

using boost::assign::list_of;
using boost::assign::map_list_of;
using namespace boost::program_options;
using pugi::xml_document;
using pugi::xml_node;
using pugi::xml_parse_result;
using std::auto_ptr;
using std::endl;
using std::ifstream;
using std::istreambuf_iterator;
using std::istringstream;
using std::multimap;
using std::set;
using std::sort;
using std::string;
using std::stringstream;
using std::vector;

class BgpPeerMock : public IPeer {
public:
    BgpPeerMock(const Ip4Address &address)
        : address_(address),
          to_str_(address_.to_string()) {
    }
    virtual ~BgpPeerMock() { }

    virtual const string &ToString() const { return to_str_; }
    virtual const string &ToUVEKey() const { return to_str_; }
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) {
        return true;
    }
    virtual BgpServer *server() {
        return NULL;
    }
    virtual BgpServer *server() const { return NULL; }
    virtual IPeerClose *peer_close() {
        return NULL;
    }
    virtual IPeerClose *peer_close() const { return NULL; }
    virtual void UpdateCloseRouteStats(Address::Family family,
        const BgpPath *old_path, uint32_t path_flags) const {
    }
    virtual IPeerDebugStats *peer_stats() {
        return NULL;
    }
    virtual const IPeerDebugStats *peer_stats() const {
        return NULL;
    }
    virtual bool IsReady() const {
        return true;
    }
    virtual bool IsXmppPeer() const { return false; }
    virtual void Close(bool graceful) { }
    BgpProto::BgpPeerType PeerType() const {
        return BgpProto::IBGP;
    }
    virtual uint32_t bgp_identifier() const {
        return htonl(address_.to_ulong());
    }
    virtual const string GetStateName() const {
        return "";
    }
    virtual void UpdateTotalPathCount(int count) const { }
    virtual int GetTotalPathCount() const { return 0; }
    virtual void UpdatePrimaryPathCount(int count) const { }
    virtual int GetPrimaryPathCount() const { return 0; }
    virtual bool IsRegistrationRequired() const { return true; }
    virtual void MembershipRequestCallback(BgpTable *table) { }
    virtual bool MembershipPathCallback(DBTablePartBase *tpart,
        BgpRoute *route, BgpPath *path) { return false; }
    virtual bool CanUseMembershipManager() const { return true; }
    virtual bool IsInGRTimerWaitState() const { return false; }

private:
    Ip4Address address_;
    std::string to_str_;
};

static bool service_is_transparent;
static bool connected_rt_is_vpn;

//
// Template structure to pass to fixture class template. Needed because
// gtest fixture class template can accept only one template parameter.
//
template <typename T1, typename T2, typename T3, typename T4, typename T5,
          typename T6>
struct TypeDefinition {
  typedef T1 TableT;
  typedef T2 PrefixT;
  typedef T3 RouteT;
  typedef T4 VpnTableT;
  typedef T5 VpnPrefixT;
  typedef T6 VpnRouteT;
};

// TypeDefinitions that we want to test.
typedef TypeDefinition<InetTable, Ip4Prefix, InetRoute, InetVpnTable,
                       InetVpnPrefix, InetVpnRoute> InetDefinition;
typedef TypeDefinition<Inet6Table, Inet6Prefix, Inet6Route, Inet6VpnTable,
                       Inet6VpnPrefix, Inet6VpnRoute> Inet6Definition;

//
// Fixture class template - instantiated later for each TypeDefinition.
//
template <typename T>
class ServiceChainTest : public ::testing::Test {
protected:
    typedef typename T::TableT TableT;
    typedef typename T::PrefixT PrefixT;
    typedef typename T::RouteT RouteT;
    typedef typename T::VpnTableT VpnTableT;
    typedef typename T::VpnPrefixT VpnPrefixT;
    typedef typename T::VpnRouteT VpnRouteT;

    ServiceChainTest()
        : config_db_(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable")),
          bgp_server_(new BgpServer(&evm_)),
          family_(GetFamily()),
          ipv6_prefix_("::ffff:"),
          parser_(&config_db_),
          ri_mgr_(NULL),
          service_chain_mgr_(NULL),
          service_is_transparent_(service_is_transparent),
          connected_rt_is_vpn_(connected_rt_is_vpn),
          validate_done_(false) {
        IFMapLinkTable_Init(&config_db_, &config_graph_);
        bgp_schema_Server_ModuleInit(&config_db_, &config_graph_);
        vnc_cfg_Server_ModuleInit(&config_db_, &config_graph_);
    }

    ~ServiceChainTest() {
        STLDeleteValues(&peers_);
    }

    virtual void SetUp() {
        CreatePeer("192.168.0.1");
        CreatePeer("192.168.0.2");
        CreatePeer("192.168.0.3");
        CreatePeer("192.168.0.4");

        BgpIfmapConfigManager *config_manager =
                static_cast<BgpIfmapConfigManager *>(
                    bgp_server_->config_manager());
        config_manager->Initialize(&config_db_, &config_graph_, "local");
        ri_mgr_ = bgp_server_->routing_instance_mgr();
        service_chain_mgr_ = bgp_server_->service_chain_mgr(family_);
        this->EnableServiceChainAggregation();
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        bgp_server_->Shutdown();
        task_util::WaitForIdle();
        db_util::Clear(&config_db_);
    }

    void NetworkConfig(const vector<string> &instance_names,
                       const multimap<string, string> &connections) {
        bgp_util::NetworkConfigGenerate(&config_db_, instance_names,
                                        connections);
    }

    string ParseConfigFile(const string &filename) {
        string content = GetConfigFileContents(filename);
        if (family_ == Address::INET6)
            boost::replace_all(content, "service-chain-info",
                               "ipv6-service-chain-info");
        parser_.Parse(content);
        task_util::WaitForIdle();
        return content;
    }

    void ParseConfigString(const string &content) {
        parser_.Parse(content);
        task_util::WaitForIdle();
    }

    void VerifyNetworkConfig(const vector<string> &instance_names) {
        for (vector<string>::const_iterator iter = instance_names.begin();
             iter != instance_names.end(); ++iter) {
            TASK_UTIL_WAIT_NE_NO_MSG(ri_mgr_->GetRoutingInstance(*iter),
                NULL, 1000, 10000, "Wait for routing instance..");
            const RoutingInstance *rti = ri_mgr_->GetRoutingInstance(*iter);
            TASK_UTIL_WAIT_NE_NO_MSG(rti->virtual_network_index(),
                0, 1000, 10000, "Wait for vn index..");
        }
    }

    void CreatePeer(const string &address) {
        boost::system::error_code ec;
        peers_.push_back(new BgpPeerMock(Ip4Address::from_string(address, ec)));
        assert(ec.value() == 0);
    }

    void SetLifetimeManagerQueueDisable(bool disabled) {
        BgpLifetimeManagerTest *ltm = dynamic_cast<BgpLifetimeManagerTest *>(
            bgp_server_->lifetime_manager());
        assert(ltm);
        ltm->SetQueueDisable(disabled);
    }

    void DisableResolveTrigger() {
        service_chain_mgr_->DisableResolveTrigger();
    }

    void EnableResolveTrigger() {
        service_chain_mgr_->EnableResolveTrigger();
    }

    bool IsServiceChainQEmpty() {
        return service_chain_mgr_->IsQueueEmpty();
    }

    void DisableServiceChainQ() {
        service_chain_mgr_->DisableQueue();
    }

    void EnableServiceChainQ() {
        service_chain_mgr_->EnableQueue();
    }

    size_t ServiceChainPendingQSize() {
        return service_chain_mgr_->PendingQueueSize();
    }

    void DisableServiceChainAggregation() {
        service_chain_mgr_->set_aggregate_host_route(false);
    }

    void EnableServiceChainAggregation() {
        service_chain_mgr_->set_aggregate_host_route(true);
    }

    void AddRoute(IPeer *peer, const string &instance_name,
                      const string &prefix, int localpref,
                      vector<uint32_t> commlist = vector<uint32_t>(),
                      vector<uint32_t> sglist = vector<uint32_t>(),
                      set<string> encap = set<string>(),
                      const SiteOfOrigin &soo = SiteOfOrigin(),
                      string nexthop_str = "", uint32_t flags = 0,
                      int label = 0, const LoadBalance &lb = LoadBalance(),
                      const RouteDistinguisher &rd = RouteDistinguisher()) {
        boost::system::error_code error;
        PrefixT nlri = PrefixT::FromString(prefix, &error);
        EXPECT_FALSE(error);
        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(new typename TableT::RequestKey(nlri, peer));

        BgpAttrSpec attr_spec;
        boost::scoped_ptr<BgpAttrLocalPref> local_pref(
                new BgpAttrLocalPref(localpref));
        attr_spec.push_back(local_pref.get());

        BgpAttrSourceRd source_rd(rd);
        if (!rd.IsZero()) {
            attr_spec.push_back(&source_rd);
        }

        if (nexthop_str.empty())
            nexthop_str = this->BuildNextHopAddress("7.8.9.1");
        IpAddress chain_addr = Ip4Address::from_string(nexthop_str, error);
        boost::scoped_ptr<BgpAttrNextHop> nexthop_attr(
                new BgpAttrNextHop(chain_addr.to_v4().to_ulong()));
        attr_spec.push_back(nexthop_attr.get());

        CommunitySpec comm;
        if (!commlist.empty()) {
            comm.communities = commlist;
            attr_spec.push_back(&comm);
        }

        ExtCommunitySpec ext_comm;
        for (vector<uint32_t>::iterator it = sglist.begin();
            it != sglist.end(); it++) {
            SecurityGroup sgid(0, *it);
            ext_comm.communities.push_back(sgid.GetExtCommunityValue());
        }
        for (set<string>::iterator it = encap.begin();
            it != encap.end(); it++) {
            TunnelEncap tunnel_encap(*it);
            ext_comm.communities.push_back(tunnel_encap.GetExtCommunityValue());
        }
        if (!soo.IsNull())
            ext_comm.communities.push_back(soo.GetExtCommunityValue());

        if (!lb.IsDefault())
            ext_comm.communities.push_back(lb.GetExtCommunityValue());

        attr_spec.push_back(&ext_comm);

        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attr_spec);

        request.data.reset(new BgpTable::RequestData(attr, flags, label));
        BgpTable *table = GetTable(instance_name);
        ASSERT_TRUE(table != NULL);
        table->Enqueue(&request);
    }

    void AddRoute(IPeer *peer, const string &instance_name,
                      const string &prefix, int localpref,
                      const SiteOfOrigin &soo) {
        AddRoute(peer, instance_name, prefix, localpref,
            vector<uint32_t>(), vector<uint32_t>(), set<string>(), soo);
    }

    void DeleteRoute(IPeer *peer, const string &instance_name,
                         const string &prefix) {
        boost::system::error_code error;
        PrefixT nlri = PrefixT::FromString(prefix, &error);
        EXPECT_FALSE(error);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        request.key.reset(new typename TableT::RequestKey(nlri, peer));

        BgpTable *table = GetTable(instance_name);
        ASSERT_TRUE(table != NULL);

        table->Enqueue(&request);
    }

    void AddVpnRoute(IPeer *peer, const string &instance_name,
                         const string &prefix, int localpref,
                         vector<uint32_t> sglist = vector<uint32_t>(),
                         set<string> encap = set<string>(),
                         string nexthop_str = "",
                         uint32_t flags = 0, int label = 0,
                         const LoadBalance &lb = LoadBalance(),
                         const RouteDistinguisher &rd = RouteDistinguisher()) {
        BgpTable *table = GetTable(instance_name);
        ASSERT_TRUE(table != NULL);
        const RoutingInstance *rtinstance = table->routing_instance();
        ASSERT_TRUE(rtinstance != NULL);
        int rti_index = rtinstance->index();

        string vpn_prefix;
        if (!rd.IsZero()) {
            vpn_prefix = rd.ToString() + ":" + prefix;
        } else {
            string peer_str = peer ? peer->ToString() :
                BuildNextHopAddress("7.7.7.7");
            vpn_prefix = peer_str + ":" + integerToString(rti_index) + ":" + prefix;
        }

        boost::system::error_code error;
        VpnPrefixT nlri = VpnPrefixT::FromString(vpn_prefix, &error);
        EXPECT_FALSE(error);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(new typename VpnTableT::RequestKey(nlri, peer));

        BgpAttrSpec attr_spec;
        boost::scoped_ptr<BgpAttrLocalPref> local_pref(
                new BgpAttrLocalPref(localpref));
        attr_spec.push_back(local_pref.get());

        if (nexthop_str.empty())
            nexthop_str = this->BuildNextHopAddress("7.8.9.1");
        IpAddress chain_addr = Ip4Address::from_string(nexthop_str, error);
        boost::scoped_ptr<BgpAttrNextHop> nexthop_attr(
                new BgpAttrNextHop(chain_addr.to_v4().to_ulong()));
        attr_spec.push_back(nexthop_attr.get());

        RouteTarget target = *(rtinstance->GetExportList().begin());
        uint64_t extcomm_value = get_value(target.GetExtCommunity().begin(), 8);
        ExtCommunitySpec extcomm_spec;
        extcomm_spec.communities.push_back(extcomm_value);
        for (vector<uint32_t>::iterator it = sglist.begin();
            it != sglist.end(); it++) {
            SecurityGroup sgid(0, *it);
            extcomm_spec.communities.push_back(sgid.GetExtCommunityValue());
        }
        for (set<string>::iterator it = encap.begin();
            it != encap.end(); it++) {
            TunnelEncap tunnel_encap(*it);
            extcomm_spec.communities.push_back(
                    tunnel_encap.GetExtCommunityValue());
        }

        if (!lb.IsDefault())
            extcomm_spec.communities.push_back(lb.GetExtCommunityValue());

        const RoutingInstance *rti = ri_mgr_->GetRoutingInstance(instance_name);
        TASK_UTIL_EXPECT_NE(0, rti->virtual_network_index());
        OriginVn origin_vn(0, rti->virtual_network_index());
        extcomm_spec.communities.push_back(origin_vn.GetExtCommunityValue());
        attr_spec.push_back(&extcomm_spec);

        AsPathSpec path_spec;
        AsPathSpec::PathSegment *path_seg = new AsPathSpec::PathSegment;
        path_seg->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
        path_seg->path_segment.push_back(64513);
        path_seg->path_segment.push_back(64514);
        path_seg->path_segment.push_back(64515);
        path_spec.path_segments.push_back(path_seg);
        attr_spec.push_back(&path_spec);

        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attr_spec);

        request.data.reset(new BgpTable::RequestData(attr, flags, label));
        table = GetVpnTable();
        ASSERT_TRUE(table != NULL);
        table->Enqueue(&request);
    }

    void AddVpnRoute(IPeer *peer, const vector<string> &instance_names,
                         const string &prefix, int localpref,
                         string nexthop_str = "",
                         uint32_t flags = 0, int label = 0,
                         const LoadBalance &lb = LoadBalance(),
                         const RouteDistinguisher &rd = RouteDistinguisher()) {
        RoutingInstance *rtinstance =
            ri_mgr_->GetRoutingInstance(instance_names[0]);
        ASSERT_TRUE(rtinstance != NULL);
        int rti_index = rtinstance->index();

        string vpn_prefix;
        if (!rd.IsZero()) {
            vpn_prefix = rd.ToString() + ":" + prefix;
        } else {
            string peer_str = peer ? peer->ToString() :
                BuildNextHopAddress("7.7.7.7");
            vpn_prefix = peer_str + ":" + integerToString(rti_index) + ":" + prefix;
        }

        boost::system::error_code error;
        VpnPrefixT nlri = VpnPrefixT::FromString(vpn_prefix, &error);
        EXPECT_FALSE(error);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(new typename VpnTableT::RequestKey(nlri, peer));

        BgpAttrSpec attr_spec;
        boost::scoped_ptr<BgpAttrLocalPref> local_pref(
                new BgpAttrLocalPref(localpref));
        attr_spec.push_back(local_pref.get());

        if (nexthop_str.empty())
            nexthop_str = this->BuildNextHopAddress("7.8.9.1");
        IpAddress chain_addr = Ip4Address::from_string(nexthop_str, error);
        boost::scoped_ptr<BgpAttrNextHop> nexthop_attr(
                new BgpAttrNextHop(chain_addr.to_v4().to_ulong()));
        attr_spec.push_back(nexthop_attr.get());

        ExtCommunitySpec extcomm_spec;
        BOOST_FOREACH(const string &instance_name, instance_names) {
            RoutingInstance *rti = ri_mgr_->GetRoutingInstance(instance_name);
            ASSERT_TRUE(rti != NULL);
            ASSERT_EQ(1, rti->GetExportList().size());
            RouteTarget rtarget = *(rti->GetExportList().begin());
            extcomm_spec.communities.push_back(rtarget.GetExtCommunityValue());
        }

        if (!lb.IsDefault())
            extcomm_spec.communities.push_back(lb.GetExtCommunityValue());

        attr_spec.push_back(&extcomm_spec);
        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attr_spec);

        request.data.reset(new BgpTable::RequestData(attr, flags, label));
        BgpTable *table = GetVpnTable();
        ASSERT_TRUE(table != NULL);
        table->Enqueue(&request);
    }

    void DeleteVpnRoute(IPeer *peer, const string &instance_name,
                        const string &prefix,
                        const RouteDistinguisher &rd = RouteDistinguisher()) {
        BgpTable *table = GetTable(instance_name);
        ASSERT_TRUE(table != NULL);
        const RoutingInstance *rtinstance = table->routing_instance();
        ASSERT_TRUE(rtinstance != NULL);
        int rti_index = rtinstance->index();

        string vpn_prefix;
        if (!rd.IsZero()) {
            vpn_prefix = rd.ToString() + ":" + prefix;
        } else {
            string peer_str = peer ? peer->ToString() :
                BuildNextHopAddress("7.7.7.7");
            vpn_prefix = peer_str + ":" + integerToString(rti_index) + ":" + prefix;
        }

        boost::system::error_code error;
        VpnPrefixT nlri = VpnPrefixT::FromString(vpn_prefix, &error);
        EXPECT_FALSE(error);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        request.key.reset(new typename VpnTableT::RequestKey(nlri, peer));

        table = GetVpnTable();
        ASSERT_TRUE(table != NULL);
        table->Enqueue(&request);
    }

    void AddConnectedRoute(IPeer *peer, const string &prefix,
                   int localpref, string nexthop = "",
                   uint32_t flags = 0, int label = 0,
                   vector<uint32_t> sglist = vector<uint32_t>(),
                   set<string> encap = set<string>(),
                   const LoadBalance &lb = LoadBalance(),
                   const RouteDistinguisher &rd = RouteDistinguisher()) {
        AddConnectedRoute(1, peer, prefix, localpref, nexthop, flags, label,
                sglist, encap, lb, rd);
    }

    void AddConnectedRoute(int chain_idx, IPeer *peer, const string &prefix,
                   int localpref, string nexthop = "",
                   uint32_t flags = 0, int label = 0,
                   vector<uint32_t> sglist = vector<uint32_t>(),
                   set<string> encap = set<string>(),
                   const LoadBalance &lb = LoadBalance(),
                   const RouteDistinguisher &rd = RouteDistinguisher()) {
        assert(1 <= chain_idx && chain_idx <= 3);
        string connected_table;
        if (chain_idx == 1) {
            connected_table = service_is_transparent_ ? "blue-i1" : "blue";
        } else if (chain_idx == 2) {
            connected_table = service_is_transparent_ ? "core-i3" : "core";
        } else if (chain_idx == 3) {
            connected_table = service_is_transparent_ ? "core-i5" : "core";
        }
        if (nexthop.empty())
            nexthop = BuildNextHopAddress("7.8.9.1");
        if (connected_rt_is_vpn_) {
            AddVpnRoute(peer, connected_table, prefix,
                    localpref, sglist, encap, nexthop, flags, label, lb, rd);
        } else {
            AddRoute(peer, connected_table, prefix, localpref,
                vector<uint32_t>(), sglist, encap, SiteOfOrigin(), nexthop,
                flags, label, lb, rd);
        }
        task_util::WaitForIdle();
    }

    void DeleteConnectedRoute(IPeer *peer, const string &prefix,
                          const RouteDistinguisher &rd = RouteDistinguisher()) {
        string connected_table = service_is_transparent_ ? "blue-i1" : "blue";
        if (connected_rt_is_vpn_) {
            DeleteVpnRoute(peer, connected_table, prefix, rd);
        } else {
            DeleteRoute(peer, connected_table, prefix);
        }
        task_util::WaitForIdle();
    }

    void DeleteConnectedRoute(int chain_idx, IPeer *peer, const string &prefix,
                          const RouteDistinguisher &rd = RouteDistinguisher()) {
        assert(1 <= chain_idx && chain_idx <= 3);
        string connected_table;
        if (chain_idx == 1) {
            connected_table = service_is_transparent_ ? "blue-i1" : "blue";
        } else if (chain_idx == 2) {
            connected_table = service_is_transparent_ ? "core-i3" : "core";
        } else if (chain_idx == 3) {
            connected_table = service_is_transparent_ ? "core-i5" : "core";
        }
        if (connected_rt_is_vpn_) {
            DeleteVpnRoute(peer, connected_table, prefix, rd);
        } else {
            DeleteRoute(peer, connected_table, prefix);
        }
        task_util::WaitForIdle();
    }

    int RouteCount(const string &instance_name) {
        BgpTable *table = GetTable(instance_name);
        EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return 0;
        }
        return table->Size();
    }

    BgpRoute *RouteLookup(const string &instance_name, const string &prefix) {
        BgpTable *bgp_table = GetTable(instance_name);
        TableT *table = dynamic_cast<TableT *>(bgp_table);
        EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return NULL;
        }
        boost::system::error_code error;
        PrefixT nlri = PrefixT::FromString(prefix, &error);
        EXPECT_FALSE(error);
        typename TableT::RequestKey key(nlri, NULL);
        DBEntry *db_entry = table->Find(&key);
        if (db_entry == NULL) {
            return NULL;
        }
        return dynamic_cast<BgpRoute *>(db_entry);
    }

    bool CheckRouteExists(const string &instance, const string &prefix) {
        task_util::TaskSchedulerLock lock;
        BgpRoute *rt = RouteLookup(instance, prefix);
        return (rt && rt->BestPath() != NULL);
    }

    void VerifyRouteExists(const string &instance, const string &prefix) {
        TASK_UTIL_EXPECT_TRUE(CheckRouteExists(instance, prefix));
    }

    bool CheckRouteNoExists(const string &instance, const string &prefix) {
        task_util::TaskSchedulerLock lock;
        BgpRoute *rt = RouteLookup(instance, prefix);
        return !rt;
    }

    void VerifyRouteNoExists(const string &instance, const string &prefix) {
        TASK_UTIL_EXPECT_TRUE(CheckRouteNoExists(instance, prefix));
    }

    void VerifyRouteIsDeleted(const string &instance, const string &prefix) {
        TASK_UTIL_EXPECT_TRUE(RouteLookup(instance, prefix) != NULL);
        BgpRoute *rt = RouteLookup(instance, prefix);
        TASK_UTIL_EXPECT_TRUE(rt->IsDeleted());
    }

    bool MatchPathAttributes(const BgpPath *path,
        const string &path_id, const string &origin_vn, uint32_t label,
        const vector<uint32_t> sg_ids, const set<string> tunnel_encaps,
        const SiteOfOrigin &soo, const vector<uint32_t> &commlist,
        const vector<string> &origin_vn_path, const LoadBalance &lb) {
        BgpAttrPtr attr = path->GetAttr();
        if (attr->nexthop().to_v4().to_string() != path_id)
            return false;
        if (GetOriginVnFromRoute(path) != origin_vn)
            return false;
        if (label && path->GetLabel() != label)
            return false;
        if (attr->as_path_count())
            return false;
        if (sg_ids.size()) {
            vector<uint32_t> path_sg_ids = GetSGIDListFromRoute(path);
            if (path_sg_ids.size() != sg_ids.size())
                return false;
            for (vector<uint32_t>::const_iterator
                it1 = path_sg_ids.begin(), it2 = sg_ids.begin();
                it1 != path_sg_ids.end() && it2 != sg_ids.end();
                ++it1, ++it2) {
                if (*it1 != *it2)
                    return false;
            }
        }
        if (tunnel_encaps.size()) {
            set<string> path_tunnel_encaps = GetTunnelEncapListFromRoute(path);
            if (path_tunnel_encaps != tunnel_encaps)
                return false;
        }
        if (!soo.IsNull()) {
            SiteOfOrigin path_soo = GetSiteOfOriginFromRoute(path);
            if (path_soo != soo)
                return false;
        }
        if (origin_vn_path.size() &&
            GetOriginVnPathFromRoute(path) != origin_vn_path) {
            return false;
        }

        LoadBalance lb2;
        if (!lb.IsDefault()) {
            if (!GetLoadBalanceFromRoute(path, lb2) || !(lb == lb2))
                return false;
        } else {
            if (GetLoadBalanceFromRoute(path, lb2))
                return false;
        }

        vector<uint32_t> path_commlist = GetCommunityListFromRoute(path);
        if (path_commlist != commlist)
            return false;

        return true;
    }

    bool CheckPathAttributes(const string &instance, const string &prefix,
        const string &path_id, const string &origin_vn, int label,
        const vector<uint32_t> sg_ids, const set<string> tunnel_encaps,
        const SiteOfOrigin &soo, const vector<uint32_t> &commlist,
        const vector<string> &origin_vn_path,
        const LoadBalance &lb = LoadBalance()) {
        task_util::TaskSchedulerLock lock;
        BgpRoute *route = RouteLookup(instance, prefix);
        if (!route)
            return false;
        for (Route::PathList::iterator it = route->GetPathList().begin();
             it != route->GetPathList().end(); ++it) {
            const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
            if (BgpPath::PathIdString(path->GetPathId()) != path_id)
                continue;
            if (MatchPathAttributes(path, path_id, origin_vn, label,
                sg_ids, tunnel_encaps, soo, commlist, origin_vn_path, lb)) {
                return true;
            }
            return false;
        }

        return false;
    }

    void VerifyPathAttributes(const string &instance,
        const string &prefix, const string &path_id, const string &origin_vn,
        const set<string> tunnel_encaps) {
        task_util::WaitForIdle();
        vector<uint32_t> commlist = list_of(CommunityType::AcceptOwnNexthop);
        TASK_UTIL_EXPECT_TRUE(CheckPathAttributes(instance, prefix,
            path_id, origin_vn, 0, vector<uint32_t>(), tunnel_encaps,
            SiteOfOrigin(), commlist, vector<string>()));
    }

    bool CheckRouteAttributes(const string &instance, const string &prefix,
        const vector<string> &path_ids, const string &origin_vn, int label,
        const vector<uint32_t> sg_ids, const set<string> tunnel_encap,
        const SiteOfOrigin &soo, const vector<uint32_t> &commlist,
        const vector<string> &origin_vn_path,
        const LoadBalance &lb = LoadBalance()) {
        task_util::TaskSchedulerLock lock;
        BgpRoute *route = RouteLookup(instance, prefix);
        if (!route)
            return false;
        if (route->count() != path_ids.size())
            return false;
        for (Route::PathList::iterator it = route->GetPathList().begin();
             it != route->GetPathList().end(); ++it) {
            bool found = false;
            const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
            BOOST_FOREACH(const string &path_id, path_ids) {
                if (BgpPath::PathIdString(path->GetPathId()) != path_id)
                    continue;
                found = true;
                if (MatchPathAttributes(path, path_id, origin_vn, label,
                    sg_ids, tunnel_encap, soo, commlist, origin_vn_path, lb)) {
                    break;
                }
                return false;
            }
            if (!found)
                return false;
        }

        return true;
    }

    void VerifyRouteAttributes(const string &instance,
        const string &prefix, const string &path_id, const string &origin_vn,
        const LoadBalance &lb) {

        task_util::WaitForIdle();
        vector<string> path_ids = list_of(path_id);
        vector<uint32_t> commlist = list_of(CommunityType::AcceptOwnNexthop);
        TASK_UTIL_EXPECT_TRUE(CheckRouteAttributes(
            instance, prefix, path_ids, origin_vn, 0, vector<uint32_t>(),
            set<string>(), SiteOfOrigin(), commlist, vector<string>(), lb));
    }

    void VerifyRouteAttributes(const string &instance,
        const string &prefix, const string &path_id, const string &origin_vn,
        int label = 0) {
        task_util::WaitForIdle();
        vector<string> path_ids = list_of(path_id);
        vector<uint32_t> commlist = list_of(CommunityType::AcceptOwnNexthop);
        TASK_UTIL_EXPECT_TRUE(CheckRouteAttributes(
            instance, prefix, path_ids, origin_vn, label, vector<uint32_t>(),
            set<string>(), SiteOfOrigin(), commlist, vector<string>()));
    }

    void VerifyRouteAttributes(const string &instance, const string &prefix,
        const vector<string> &path_ids, const string &origin_vn) {
        task_util::WaitForIdle();
        vector<uint32_t> commlist = list_of(CommunityType::AcceptOwnNexthop);
        TASK_UTIL_EXPECT_TRUE(CheckRouteAttributes(
            instance, prefix, path_ids, origin_vn, 0, vector<uint32_t>(),
            set<string>(), SiteOfOrigin(), commlist, vector<string>()));
    }

    void VerifyRouteAttributes(const string &instance,
        const string &prefix, const string &path_id, const string &origin_vn,
        const vector<uint32_t> sg_ids) {
        task_util::WaitForIdle();
        vector<string> path_ids = list_of(path_id);
        vector<uint32_t> commlist = list_of(CommunityType::AcceptOwnNexthop);
        TASK_UTIL_EXPECT_TRUE(CheckRouteAttributes(
            instance, prefix, path_ids, origin_vn, 0, sg_ids, set<string>(),
            SiteOfOrigin(), commlist, vector<string>()));
    }

    void VerifyRouteAttributes(const string &instance,
        const string &prefix, const string &path_id, const string &origin_vn,
        const set<string> tunnel_encaps) {
        task_util::WaitForIdle();
        vector<string> path_ids = list_of(path_id);
        vector<uint32_t> commlist = list_of(CommunityType::AcceptOwnNexthop);
        TASK_UTIL_EXPECT_TRUE(CheckRouteAttributes(
            instance, prefix, path_ids, origin_vn, 0, vector<uint32_t>(),
            tunnel_encaps, SiteOfOrigin(), commlist, vector<string>()));
    }

    void VerifyRouteAttributes(const string &instance,
        const string &prefix, const string &path_id, const string &origin_vn,
        const SiteOfOrigin &soo) {
        task_util::WaitForIdle();
        vector<string> path_ids = list_of(path_id);
        vector<uint32_t> commlist = list_of(CommunityType::AcceptOwnNexthop);
        TASK_UTIL_EXPECT_TRUE(CheckRouteAttributes(
            instance, prefix, path_ids, origin_vn, 0, vector<uint32_t>(),
            set<string>(), soo, commlist, vector<string>()));
    }

    void VerifyRouteAttributes(const string &instance,
        const string &prefix, const string &path_id, const string &origin_vn,
        const CommunitySpec &commspec) {
        task_util::WaitForIdle();
        vector<string> path_ids = list_of(path_id);
        TASK_UTIL_EXPECT_TRUE(CheckRouteAttributes(
            instance, prefix, path_ids, origin_vn, 0, vector<uint32_t>(),
            set<string>(), SiteOfOrigin(), commspec.communities,
            vector<string>()));
    }

    void VerifyRouteAttributes(const string &instance,
        const string &prefix, const string &path_id, const string &origin_vn,
        const vector<string> &origin_vn_path) {
        task_util::WaitForIdle();
        vector<string> path_ids = list_of(path_id);
        vector<uint32_t> commlist = list_of(CommunityType::AcceptOwnNexthop);
        TASK_UTIL_EXPECT_TRUE(CheckRouteAttributes(
            instance, prefix, path_ids, origin_vn, 0, vector<uint32_t>(),
            set<string>(), SiteOfOrigin(), commlist, origin_vn_path));
    }

    string FileRead(const string &filename) {
        ifstream file(filename.c_str());
        string content((istreambuf_iterator<char>(file)),
                       istreambuf_iterator<char>());
        return content;
    }

    void AddRoutingInstance(string name, string connection) {
        stringstream target;
        target << "target:64496:" << 100;

        ifmap_test_util::IFMapMsgLink(&config_db_,
                                      "routing-instance", name,
                                      "route-target", target.str(),
                                      "instance-target");
        task_util::WaitForIdle();
        RoutingInstanceMgr *rim = bgp_server_->routing_instance_mgr();
        TASK_UTIL_EXPECT_TRUE(rim->GetRoutingInstance(name) != NULL);
        ifmap_test_util::IFMapMsgLink(&config_db_,
                                      "routing-instance", name,
                                      "routing-instance", connection,
                                      "connection");
        task_util::WaitForIdle();
    }

    void RemoveRoutingInstance(string name, string connection) {
        ifmap_test_util::IFMapMsgUnlink(&config_db_,
                                        "virtual-network", name,
                                        "routing-instance", name,
                                        "virtual-network-routing-instance");
        ifmap_test_util::IFMapMsgUnlink(&config_db_,
                                        "routing-instance", name,
                                        "routing-instance", connection,
                                        "connection");
        // Cache copy of export route-targets before instance is deleted
        RoutingInstance *rti =
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(name);
        const RoutingInstance::RouteTargetList
            target_list(rti->GetExportList());
        BOOST_FOREACH(RouteTarget tgt, target_list) {
            ifmap_test_util::IFMapMsgUnlink(&config_db_,
                                            "routing-instance", name,
                                            "route-target", tgt.ToString(),
                                            "instance-target");
        }
        task_util::WaitForIdle();
    }

    void AddConnection(const string &instance1, const string &instance2) {
        ifmap_test_util::IFMapMsgLink(&config_db_,
                                      "routing-instance", instance1,
                                      "routing-instance", instance2,
                                      "connection");
        task_util::WaitForIdle();
    }

    string GetConfigFileContents(string filename) {
        string content;

        // Convert IPv4 Prefix to IPv6 for IPv6 tests
        if (family_ == Address::INET6) {
            std::ifstream input(filename.c_str());
            boost::regex e1 ("(^.*?)(\\d+\\.\\d+\\.\\d+\\.\\d+)\\/(\\d+)(.*$)");
            boost::regex e2 ("(^.*?)(\\d+\\.\\d+\\.\\d+\\.\\d+)(.*$)");
            for (string line; getline(input, line);) {
                boost::cmatch cm;
                if (boost::regex_match(line.c_str(), cm, e1)) {
                    const string prefix(cm[2].first, cm[2].second);
                    content += string(cm[1].first, cm[1].second) +
                        BuildPrefix(prefix, atoi(string(cm[3].first,
                                                    cm[3].second).c_str())) +
                        string(cm[4].first, cm[4].second);
                } else if (boost::regex_match(line.c_str(), cm, e2)) {
                    content += string(cm[1].first, cm[1].second) +
                        BuildHostAddress(string(cm[2].first, cm[2].second)) +
                        string(cm[3].first, cm[3].second);
                } else {
                    content += line;
                }
            }
        } else {
            content = FileRead(filename);
        }

        return content;
    }

    auto_ptr<autogen::ServiceChainInfo>
        GetChainConfig(string filename) {
        auto_ptr<autogen::ServiceChainInfo>
            params (new autogen::ServiceChainInfo());
        string content = GetConfigFileContents(filename);
        EXPECT_FALSE(content.empty());
        istringstream sstream(content);
        xml_document xdoc;
        xml_parse_result result = xdoc.load(sstream);
        if (!result) {
            BGP_WARN_UT("Unable to load XML document. (status="
                << result.status << ", offset=" << result.offset << ")");
            assert(0);
        }
        xml_node node = xdoc.first_child();
        params->XmlParse(node);
        return params;
    }

    void SetServiceChainInformation(const string &instance,
        const string &filename) {
        auto_ptr<autogen::ServiceChainInfo> params = GetChainConfig(filename);
        ifmap_test_util::IFMapMsgPropertyAdd(&config_db_, "routing-instance",
            instance, family_ == Address::INET ?
                "service-chain-information" : "ipv6-service-chain-information",
            params.release(), 0);
        task_util::WaitForIdle();
    }

    void ClearServiceChainInformation(const string &instance) {
        ifmap_test_util::IFMapMsgPropertyDelete(&config_db_, "routing-instance",
            instance, family_ == Address::INET ?
                "service-chain-information" : "ipv6-service-chain-information");
        task_util::WaitForIdle();
    }

    vector<uint32_t> GetSGIDListFromRoute(const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        assert(ext_comm);
        vector<uint32_t> list;
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_comm->communities()) {
            if (!ExtCommunity::is_security_group(comm))
                continue;
            SecurityGroup security_group(comm);

            list.push_back(security_group.security_group_id());
        }
        sort(list.begin(), list.end());
        return list;
    }

    set<string> GetTunnelEncapListFromRoute(const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        assert(ext_comm);
        set<string> list;
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_comm->communities()) {
            if (!ExtCommunity::is_tunnel_encap(comm))
                continue;
            TunnelEncap encap(comm);
            list.insert(encap.ToXmppString());
        }
        return list;
    }

    bool GetLoadBalanceFromRoute(const BgpPath *path, LoadBalance &lb) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        assert(ext_comm);
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_comm->communities()) {
            if (!ExtCommunity::is_load_balance(comm))
                continue;
            lb = LoadBalance(comm);
            return true;
        }
        return false;
    }

    string GetOriginVnFromRoute(const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        assert(ext_comm);
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_comm->communities()) {
            if (!ExtCommunity::is_origin_vn(comm))
                continue;
            OriginVn origin_vn(comm);
            return ri_mgr_->GetVirtualNetworkByVnIndex(origin_vn.vn_index());
        }
        return "unresolved";
    }

    vector<string> GetOriginVnPathFromRoute(const BgpPath *path) {
        const OriginVnPath *ovnpath = path->GetAttr()->origin_vn_path();
        assert(ovnpath);
        vector<string> result;
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ovnpath->origin_vns()) {
            assert(ExtCommunity::is_origin_vn(comm));
            OriginVn origin_vn(comm);
            string vn_name =
                ri_mgr_->GetVirtualNetworkByVnIndex(origin_vn.vn_index());
            result.push_back(vn_name);
        }
        return result;
    }

    SiteOfOrigin GetSiteOfOriginFromRoute(const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        assert(ext_comm);
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_comm->communities()) {
            if (!ExtCommunity::is_site_of_origin(comm))
                continue;
            SiteOfOrigin soo(comm);
            return soo;
        }
        return SiteOfOrigin();
    }

    vector<uint32_t> GetCommunityListFromRoute(const BgpPath *path) {
        const Community *comm = path->GetAttr()->community();
        vector<uint32_t> list = comm ? comm->communities() : vector<uint32_t>();
        sort(list.begin(), list.end());
        return list;
    }

    static void ValidateShowServiceChainResponse(Sandesh *sandesh,
                                                 ServiceChainTest *self,
                                                 vector<string> &result,
                                                 const string &search_string) {
        ShowServiceChainResp *resp =
            dynamic_cast<ShowServiceChainResp *>(sandesh);
        TASK_UTIL_EXPECT_NE((ShowServiceChainResp *)NULL, resp);
        self->validate_done_ = true;

        TASK_UTIL_EXPECT_EQ(result.size(),
                            resp->get_service_chain_list().size());

        if (search_string != "pending") {
            int i = 0;
            BOOST_FOREACH(const ShowServicechainInfo &info,
                          resp->get_service_chain_list()) {
                TASK_UTIL_EXPECT_EQ(info.get_src_rt_instance(), result[i]);
                i++;
            }
        }
    }

    void VerifyServiceChainSandesh(ServiceChainTest *self,
                                   vector<string> result,
        bool filter = false, const string &search_string = string()) {
        BgpSandeshContext sandesh_context;
        sandesh_context.bgp_server = bgp_server_.get();
        sandesh_context.xmpp_peer_manager = NULL;
        Sandesh::set_client_context(&sandesh_context);
        self->validate_done_ = false;

        Sandesh::set_response_callback(
            boost::bind(ValidateShowServiceChainResponse, _1, this, result,
                search_string));
        ShowServiceChainReq *req = new ShowServiceChainReq;
        if (filter)
            req->set_search_string(search_string);
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_TRUE(self->validate_done_);
        task_util::WaitForIdle();
    }

    void VerifyPendingServiceChainSandesh(ServiceChainTest *self,
             vector<string> pending) {
        VerifyServiceChainSandesh(self, pending, true, "pending");
    }

    void VerifyServiceChainCount(uint32_t count) {
        ConcurrencyScope scope("bgp::Config");
        TASK_UTIL_EXPECT_EQ(count, bgp_server_->num_service_chains());
    }

    void VerifyDownServiceChainCount(uint32_t count) {
        ConcurrencyScope scope("bgp::Config");
        TASK_UTIL_EXPECT_EQ(count, bgp_server_->num_down_service_chains());
    }

    Address::Family GetFamily() const {
        assert(false);
        return Address::UNSPEC;
    }

    string GetTableName(const string &instance) const {
        if (family_ == Address::INET) {
            return instance + ".inet.0";
        }
        if (family_ == Address::INET6) {
            return instance + ".inet6.0";
        }
        assert(false);
        return "";
    }

    string GetVpnTableName() const {
        if (family_ == Address::INET) {
            return "bgp.l3vpn.0";
        }
        if (family_ == Address::INET6) {
            return "bgp.l3vpn-inet6.0";
        }
        assert(false);
        return "";
    }

    BgpTable *GetTable(const std::string &instance_name) {
        return static_cast<BgpTable *>(bgp_server_->database()->FindTable(
                    GetTableName(instance_name)));
    }

    BgpTable *GetVpnTable() {
        return static_cast<BgpTable *>(bgp_server_->database()->FindTable(
                    GetVpnTableName()));
    }

    string BuildHostAddress(const string &ipv4_addr) const {
        if (family_ == Address::INET) {
            return ipv4_addr;// + "/32";
        }
        if (family_ == Address::INET6) {
            return ipv6_prefix_ + ipv4_addr;// + "/128";
        }
        assert(false);
        return "";
    }

    string BuildPrefix(const string &ipv4_prefix, uint8_t ipv4_plen) const {
        if (family_ == Address::INET) {
            return ipv4_prefix + "/" + integerToString(ipv4_plen);
        }
        if (family_ == Address::INET6) {
            return ipv6_prefix_ + ipv4_prefix + "/" +
                integerToString(96 + ipv4_plen);
        }
        assert(false);
        return "";
    }

    string BuildNextHopAddress(const string &ipv4_addr) const {
        return ipv4_addr;
    }

    string GetNextHopAddress(BgpAttrPtr attr) {
        return attr->nexthop().to_v4().to_string();
    }

    EventManager evm_;
    DB config_db_;
    DBGraph config_graph_;
    boost::scoped_ptr<BgpServer> bgp_server_;
    Address::Family family_;
    string ipv6_prefix_;
    BgpConfigParser parser_;
    RoutingInstanceMgr *ri_mgr_;
    IServiceChainMgr *service_chain_mgr_;
    vector<BgpPeerMock *> peers_;
    bool service_is_transparent_;
    bool connected_rt_is_vpn_;
    bool validate_done_;
};

// Specialization of GetFamily for INET.
template<>
Address::Family ServiceChainTest<InetDefinition>::GetFamily() const {
    return Address::INET;
}

// Specialization of GetFamily for INET6.
template<>
Address::Family ServiceChainTest<Inet6Definition>::GetFamily() const {
    return Address::INET6;
}

// Instantiate fixture class template for each TypeDefinition.
typedef ::testing::Types <InetDefinition, Inet6Definition> TypeDefinitionList;
TYPED_TEST_CASE(ServiceChainTest, TypeDefinitionList);

TYPED_TEST(ServiceChainTest, Basic) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->VerifyServiceChainCount(0);
    this->VerifyDownServiceChainCount(0);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    this->VerifyServiceChainCount(1);
    this->VerifyDownServiceChainCount(1);

    // Add More specific
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);

    // Check for aggregated route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    this->VerifyServiceChainCount(1);
    this->VerifyDownServiceChainCount(0);

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Delete More specific
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));

    // Delete connected route
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));

    this->VerifyServiceChainCount(1);
    this->VerifyDownServiceChainCount(1);
}

TYPED_TEST(ServiceChainTest, IgnoreNonInetv46ServiceChainAdd1) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    // Add chain with bad service chain address.
    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_8a.xml");

    // Add More specifics
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.2.1", 32), 100);

    // Check for aggregated routes
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.2.0", 24));

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Verify that service chain is on pending list.
    TASK_UTIL_EXPECT_EQ(1, this->ServiceChainPendingQSize());
    this->VerifyPendingServiceChainSandesh(this, list_of("blue-i1"));
    this->VerifyPendingServiceChainSandesh(this, list_of("blue-i1"));
    this->VerifyPendingServiceChainSandesh(this, list_of("blue-i1"));

    // Fix service chain address.
    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");
    TASK_UTIL_EXPECT_EQ(0, this->ServiceChainPendingQSize());

    // Check for aggregated routes
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.2.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.2.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Delete More specifics
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.2.1", 32));

    // Delete connected route
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, IgnoreNonInetv46ServiceChainAdd2) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    // Add chain with bad service chain address.
    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_8b.xml");

    // Add More specifics
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.2.1", 32), 100);

    // Check for aggregated routes
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.2.0", 24));

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Verify that service chain is on pending list.
    this->VerifyPendingServiceChainSandesh(this, list_of("blue-i1"));
    this->VerifyPendingServiceChainSandesh(this, list_of("blue-i1"));
    this->VerifyPendingServiceChainSandesh(this, list_of("blue-i1"));

    // Fix service chain address.
    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Check for aggregated routes
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.2.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.2.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Delete More specifics
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.2.1", 32));

    // Delete connected route
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, IgnoreNonInetv46Subnets) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_7.xml");

    // Add More specifics
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.2.1", 32), 100);

    // Check for aggregated routes
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.2.0", 24));

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for aggregated routes
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.2.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.2.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Delete More specifics
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.2.1", 32));

    // Delete connected route
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, MoreSpecificAddDelete) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);

    // Check for aggregated route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Delete More specific
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));

    // Check for aggregated route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add different more specific
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.34", 32), 100);

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add different more specific
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.2.34", 32), 100);

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Delete More specific & connected
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.2.34", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.34", 32));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, ConnectedAddDelete) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific & connected
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Delete connected route
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));

    // Check for aggregated route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Delete More specific & connected
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}


TYPED_TEST(ServiceChainTest, DeleteConnected) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific & connected
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Delete connected route
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));

    // Check for aggregated route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.2.1", 32), 100);

    // Check for aggregated route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.2.0", 24));

    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.2.0", 24));

    // Delete More specific & connected
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.2.1", 32));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, StopServiceChain) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific & connected
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));

    this->ClearServiceChainInformation("blue-i1");

    // Check for aggregated route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Delete More specific & connected
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, ServiceChainWithExistingRouteEntries) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    // Add More specific & connected
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.2", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.3", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.4", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.2.1", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.2.2", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.2.3", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.2.4", 32), 100);
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.2.0", 24));

    this->ClearServiceChainInformation("blue-i1");

    // Check for aggregated route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Delete More specific & connected
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.2", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.3", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.4", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.2.1", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.2.2", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.2.3", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.2.4", 32));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, UpdateLoadBalanceAttributeNoAggregates) {
    this->DisableServiceChainAggregation();
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific & Connected
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.1", 32),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("3.4.5.6"));

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.1", 32),
                                this->BuildNextHopAddress("3.4.5.6"), "red");

    // Create non-default load balance attribute.
    LoadBalance lbc;
    lbc.SetL3SourceAddress(false);
    LoadBalance lbo;
    lbo.SetL3DestinationAddress(false);

    // Add load-balance attribute to connected route and verify
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("3.4.5.6"),
                            0, 0, vector<uint32_t>(), set<string>(), lbc);
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.1", 32),
                                this->BuildNextHopAddress("3.4.5.6"), "red",
                                lbc);

    // Add load-balance attribute to original route and verify
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100,
                   vector<uint32_t>(), vector<uint32_t>(), set<string>(),
                   SiteOfOrigin(), "", 0, 0, lbo);
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.1", 32),
                                this->BuildNextHopAddress("3.4.5.6"), "red",
                                lbc);

    // Remove load-balance attribute from original route and verify
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.1", 32),
                                this->BuildNextHopAddress("3.4.5.6"), "red",
                                lbc);

    // Remove load-balance attribute from connected route and verify
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("3.4.5.6"));
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.1", 32),
                                this->BuildNextHopAddress("3.4.5.6"), "red");

    // Add load-balance attribute to original route and verify
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100,
                   vector<uint32_t>(), vector<uint32_t>(), set<string>(),
                   SiteOfOrigin(), "", 0, 0, lbo);
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.1", 32),
                                this->BuildNextHopAddress("3.4.5.6"), "red",
                                lbo);

    // Add load-balance attribute to connected route and verify
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("3.4.5.6"), 0, 0,
                            vector<uint32_t>(), set<string>(), lbc);
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.1", 32),
                                this->BuildNextHopAddress("3.4.5.6"), "red",
                                lbc);

    // Remove load-balance attribute from connected route and verify
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("3.4.5.6"));
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.1", 32),
                                this->BuildNextHopAddress("3.4.5.6"), "red",
                                lbo);

    // Remove load-balance attribute from original route and verify
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.1", 32),
                                this->BuildNextHopAddress("3.4.5.6"), "red");

    // Delete More specific & connected
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, UpdateLoadBalanceAttribute) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific & Connected
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("3.4.5.6"));

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("3.4.5.6"), "red");

    // Create non-default load balance attribute.
    LoadBalance lbc;
    lbc.SetL3SourceAddress(false);
    LoadBalance lbo;
    lbo.SetL3DestinationAddress(false);

    // Add load-balance attribute to connected route and verify
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("3.4.5.6"),
                            0, 0, vector<uint32_t>(), set<string>(), lbc);
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("3.4.5.6"), "red",
                                lbc);

    // Add load-balance attribute to original route and verify
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100,
                   vector<uint32_t>(), vector<uint32_t>(), set<string>(),
                   SiteOfOrigin(), "", 0, 0, lbo);
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("3.4.5.6"), "red",
                                lbc);

    // Remove load-balance attribute from original route and verify
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("3.4.5.6"), "red",
                                lbc);

    // Remove load-balance attribute from connected route and verify
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("3.4.5.6"));
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("3.4.5.6"), "red");

    // Add load-balance attribute to original route and verify
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100,
                   vector<uint32_t>(), vector<uint32_t>(), set<string>(),
                   SiteOfOrigin(), "", 0, 0, lbo);
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("3.4.5.6"), "red");

    // Add load-balance attribute to connected route and verify
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("3.4.5.6"), 0, 0,
                            vector<uint32_t>(), set<string>(), lbc);
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("3.4.5.6"), "red",
                                lbc);

    // Remove load-balance attribute from connected route and verify
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("3.4.5.6"));
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("3.4.5.6"), "red");

    // Remove load-balance attribute from original route and verify
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("3.4.5.6"), "red");

    // Delete More specific & connected
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, UpdateNexthop) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific & Connected
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("3.4.5.6"));

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("3.4.5.6"), "red");

    // Delete More specific & connected
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}


TYPED_TEST(ServiceChainTest, UpdateLabel) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific & Connected
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"), 0, 16);

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red",
                                16);

    // Add Connected with updated label
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"), 0, 32);

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red",
                                32);

    // Delete More specific & connected
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, DeleteRoutingInstance) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific & Connected
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));

    this->RemoveRoutingInstance("blue-i1", "blue");

    // Delete More specific & connected
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}


TYPED_TEST(ServiceChainTest, PendingChain) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->VerifyServiceChainCount(0);
    this->VerifyDownServiceChainCount(0);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    this->VerifyServiceChainCount(1);
    this->VerifyDownServiceChainCount(1);

    // Check for aggregated route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    this->VerifyPendingServiceChainSandesh(this, list_of("blue-i1"));
    this->VerifyPendingServiceChainSandesh(this, list_of("blue-i1"));
    this->VerifyPendingServiceChainSandesh(this, list_of("blue-i1"));

    // Add "red" routing instance and create connection with "red-i2"
    instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    connections = map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->VerifyServiceChainCount(1);
    this->VerifyDownServiceChainCount(0);

    // Add MoreSpecific
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Delete More specific & connected
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));

    this->VerifyServiceChainCount(1);
    this->VerifyDownServiceChainCount(1);
}

TYPED_TEST(ServiceChainTest, UnresolvedPendingChain) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    this->VerifyPendingServiceChainSandesh(this, list_of("blue-i1"));
    this->VerifyPendingServiceChainSandesh(this, list_of("blue-i1"));
    this->VerifyPendingServiceChainSandesh(this, list_of("blue-i1"));

    this->ClearServiceChainInformation("blue-i1");

    // Delete connected
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, DeletePendingChain) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    // Configure chain with bad service chain address to ensure that it gets
    // added to the pending queue.
    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_9.xml");

    // Verify that it's on the pending queue.
    TASK_UTIL_EXPECT_EQ(1, this->ServiceChainPendingQSize());

    // Pause processing of pending chains.
    this->DisableResolveTrigger();

    // Pause the lifetime manager and remove configuration for blue-i1.
    // This ensures that blue-i1 is marked deleted but Shutdown does not
    // get invoked yet.
    this->SetLifetimeManagerQueueDisable(true);
    this->RemoveRoutingInstance("blue-i1", "blue");
    this->ClearServiceChainInformation("blue-i1");

    // Resume processing of pending chains.
    // This ensures that we try to resolve the chain after the instance has
    // been deleted but before shutdown has been called for it.
    this->EnableResolveTrigger();

    // Resume lifetime manager so that it's can proceed and destroy blue-i1.
    this->SetLifetimeManagerQueueDisable(false);
    task_util::WaitForIdle();
}

TYPED_TEST(ServiceChainTest, UpdateChain) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_3.xml");

    // Add More specific
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("192.169.2.1", 32), 100);

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteExists("blue", this->BuildPrefix("192.169.2.0", 24));

    this->VerifyServiceChainSandesh(this, list_of("blue-i1"));
    this->VerifyServiceChainSandesh(this, list_of("blue-i1"), true, string());
    this->VerifyServiceChainSandesh(this, list_of("blue-i1"), true,
                                    string("blue"));

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_2.xml");

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.0.0", 16));
    this->VerifyRouteExists("blue", this->BuildPrefix("192.169.2.0", 24));

    this->VerifyServiceChainSandesh(this, list_of("blue-i1"));
    this->VerifyServiceChainSandesh(this, list_of("blue-i1"), true, string());
    this->VerifyServiceChainSandesh(this, list_of("blue-i1"), true,
                                    string("blue"));

    // Delete More specific & connected
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.169.2.1", 32));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, PeerUpdate) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);

    // Add Connected
    this->AddConnectedRoute(this->peers_[0], this->BuildPrefix("1.1.2.3", 32),
                            90, this->BuildNextHopAddress("2.3.0.5"));

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.0.5"), "red");

    this->AddConnectedRoute(this->peers_[1], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.1.5"));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.1.5"), "red");

    this->AddConnectedRoute(this->peers_[2], this->BuildPrefix("1.1.2.3", 32),
                            95, this->BuildNextHopAddress("2.3.2.5"));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.1.5"), "red");

    this->DeleteConnectedRoute(this->peers_[1],
                               this->BuildPrefix("1.1.2.3", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.2.5"), "red");

    // Delete More specific
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));

    // Delete connected route
    this->DeleteConnectedRoute(this->peers_[0],
                               this->BuildPrefix("1.1.2.3", 32));
    this->DeleteConnectedRoute(this->peers_[2],
                               this->BuildPrefix("1.1.2.3", 32));
}

//
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add more specific route 192.168.1.1/32
// 3. Add MX leaked route 10.1.1.0/24
// 4. Add connected routes from 2 peers with same forwarding information
// 5. Verify aggregate route exists and has only one path
// 6. Verify ext connected route exists and has only one path
// 7. Remove one of the connected routes
// 8. Verify aggregate route exists and still has one path
// 9. Verify ext connected route exists and still has one path
//
TYPED_TEST(ServiceChainTest, DuplicateForwardingPaths) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);

    // Add Ext connect route
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24), 100);

    // Add Connected with duplicate forwarding information
    this->AddConnectedRoute(this->peers_[0], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.4.5"));
    this->AddConnectedRoute(this->peers_[1], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.4.5"));

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Check for ExtConnect route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Delete connected route from peers_[0]
    this->DeleteConnectedRoute(this->peers_[0],
                               this->BuildPrefix("1.1.2.3", 32));

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Check for ExtConnect route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Delete More specific
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));

    // Delete Ext connect route
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24));

    // Delete connected route
    this->DeleteConnectedRoute(this->peers_[1],
                               this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, EcmpPaths) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);

    // Add Connected
    this->AddConnectedRoute(this->peers_[0], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.0.5"));

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.0.5"), "red");

    this->AddConnectedRoute(this->peers_[1], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.1.5"));

    vector<string> path_ids = list_of(this->BuildNextHopAddress(
                "2.3.0.5"))(this->BuildNextHopAddress("2.3.1.5"));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                path_ids, "red");

    this->DeleteConnectedRoute(this->peers_[1],
                               this->BuildPrefix("1.1.2.3", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.0.5"), "red");

    // Delete More specific
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));

    // Delete connected route
    this->DeleteConnectedRoute(this->peers_[0],
                               this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, EcmpPathUpdate) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);
    this->AddConnectedRoute(this->peers_[0], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.0.5"));
    this->AddConnectedRoute(this->peers_[1], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.1.5"));

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    vector<string> path_ids = list_of(this->BuildNextHopAddress(
                "2.3.0.5"))(this->BuildNextHopAddress("2.3.1.5"));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                path_ids, "red");

    this->AddConnectedRoute(this->peers_[1], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.1.8"));
    path_ids = list_of(this->BuildNextHopAddress("2.3.0.5"))
        (this->BuildNextHopAddress("2.3.1.8"));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                path_ids, "red");

    // Delete More specific
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));

    // Delete connected route
    this->DeleteConnectedRoute(this->peers_[0],
                               this->BuildPrefix("1.1.2.3", 32));
    this->DeleteConnectedRoute(this->peers_[1],
                               this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, EcmpPathAdd) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);
    this->AddConnectedRoute(this->peers_[0], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.0.5"));

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.0.5"), "red");

    this->DisableServiceChainQ();
    this->DeleteConnectedRoute(this->peers_[0],
                               this->BuildPrefix("1.1.2.3", 32));
    this->AddConnectedRoute(this->peers_[1], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.1.5"));
    this->AddConnectedRoute(this->peers_[2], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.2.5"));
    this->AddConnectedRoute(this->peers_[3], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.3.5"));
    this->EnableServiceChainQ();

    vector<string> path_ids = list_of(
        this->BuildNextHopAddress("2.3.1.5"))
        (this->BuildNextHopAddress("2.3.2.5"))
        (this->BuildNextHopAddress("2.3.3.5"));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                path_ids, "red");

    this->DisableServiceChainQ();
    this->DeleteConnectedRoute(this->peers_[1],
                               this->BuildPrefix("1.1.2.3", 32));
    this->DeleteConnectedRoute(this->peers_[2],
                               this->BuildPrefix("1.1.2.3", 32));
    this->DeleteConnectedRoute(this->peers_[3],
                               this->BuildPrefix("1.1.2.3", 32));
    this->AddConnectedRoute(this->peers_[0],
                            this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.0.5"),
        BgpPath::AsPathLooped);
    this->EnableServiceChainQ();

    // Check for aggregated route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Delete More specific
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    // Delete connected route
    this->DeleteConnectedRoute(this->peers_[0],
                               this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, EcmpPathDelete) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific
    this->DisableServiceChainQ();
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);
    this->AddConnectedRoute(this->peers_[0], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.0.5"));
    this->AddConnectedRoute(this->peers_[1], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.1.5"));
    this->AddConnectedRoute(this->peers_[2], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.2.5"));
    this->AddConnectedRoute(this->peers_[3], this->BuildPrefix("1.1.2.3", 32),
                            90, this->BuildNextHopAddress("2.3.3.5"));
    this->EnableServiceChainQ();

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    vector<string> path_ids = list_of(
        this->BuildNextHopAddress("2.3.0.5"))
        (this->BuildNextHopAddress("2.3.1.5"))
        (this->BuildNextHopAddress("2.3.2.5"));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                path_ids, "red");

    this->DisableServiceChainQ();
    this->DeleteConnectedRoute(this->peers_[0],
                               this->BuildPrefix("1.1.2.3", 32));
    this->DeleteConnectedRoute(this->peers_[1],
                               this->BuildPrefix("1.1.2.3", 32));
    this->DeleteConnectedRoute(this->peers_[2],
                               this->BuildPrefix("1.1.2.3", 32));
    this->EnableServiceChainQ();

    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.3.5"), "red");

    // Delete More specific
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    // Delete connected route
    this->DeleteConnectedRoute(this->peers_[3],
                               this->BuildPrefix("1.1.2.3", 32));
}

//
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add more specific route 192.168.1.1/32
// 3. Add MX leaked route 10.1.1.0/24
// 4. Add connected routes from 2 peers with forwarding information F1
// 5. Add connected routes from 2 peers with forwarding information F2
// 6. Verify aggregate route exists and has only two paths
// 7. Verify ext connected route exists and has only two paths
//
TYPED_TEST(ServiceChainTest, EcmpWithDuplicateForwardingPaths) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);

    // Add Ext connect route
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24), 100);

    // Add Connected with duplicate forwarding information F1
    this->AddConnectedRoute(this->peers_[0], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.4.5"));
    this->AddConnectedRoute(this->peers_[1], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.4.5"));

    // Add Connected with duplicate forwarding information F2
    this->AddConnectedRoute(this->peers_[2], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.4.6"));
    this->AddConnectedRoute(this->peers_[3], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.4.6"));

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    vector<string> path_ids = list_of(
        this->BuildNextHopAddress("2.3.4.5"))
        (this->BuildNextHopAddress("2.3.4.6"));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                path_ids, "red");

    // Check for ExtConnect route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.1.0", 24),
                                path_ids, "red");

    // Delete connected routes from peers_[0] and peers_[2]
    this->DeleteConnectedRoute(this->peers_[0],
                               this->BuildPrefix("1.1.2.3", 32));
    this->DeleteConnectedRoute(this->peers_[2],
                               this->BuildPrefix("1.1.2.3", 32));

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    path_ids = list_of(
        this->BuildNextHopAddress("2.3.4.5"))
        (this->BuildNextHopAddress("2.3.4.6"));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                path_ids, "red");

    // Check for ExtConnect route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.1.0", 24),
                                path_ids, "red");

    // Delete More specific
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));

    // Delete Ext connect route
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24));

    // Delete connected routes
    this->DeleteConnectedRoute(this->peers_[1],
                               this->BuildPrefix("1.1.2.3", 32));
    this->DeleteConnectedRoute(this->peers_[3],
                               this->BuildPrefix("1.1.2.3", 32));
}

//
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add connected route
// 3. Add MX leaked route 192.168.1.0/24
// 4. Verify that ext connect route 192.168.1.0/24 is not added
// 5. Add VM route(192.168.1.1/32) and verify aggregate route 192.168.1.0/24
//
TYPED_TEST(ServiceChainTest, IgnoreAggregateRoute) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Add MX leaked route
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.0", 24), 100);

    // Check for absence of ExtConnect route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add more specific
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);

    // Check for Aggregate route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Delete MX leaked, More specific and connected route
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.0", 24));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

//
// 0. Disable aggregation
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add connected route
// 3. Add MX leaked route 192.168.1.0/24
// 4. Verify that ext connect route 192.168.1.0/24 is added
//
TYPED_TEST(ServiceChainTest, ValidateAggregateRoute) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->DisableServiceChainAggregation();
    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Add MX leaked route
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.0", 24), 100);

    // Check for Aggregate route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Delete MX leaked and connected route
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.0", 24));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

//
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add MX leaked route 10.1.1.0/24
// 3. Add connected route
// 4. Verify that ext connect route 10.1.1.0/24 is added
// 5. Remove connected route
// 6. Verify that ext connect route is removed
// 7. Add connected route
// 8. Add VM route(192.168.1.1/32) and verify aggregate route 192.168.1.0/24
//
TYPED_TEST(ServiceChainTest, ExtConnectRoute) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Ext connect route
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24), 100);

    // Check for ExtConnect route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("10.1.1.0", 24));

    // Check for Aggregate route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for ExtConnect route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));

    // Check for absence Aggregate route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Delete Connected route
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));

    // Check for ExtConnect route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
    // Check for Aggregate route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add more specific
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for Aggregate route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Check for ExtConnect route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Delete ExtRoute, More specific and connected route
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

//
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add MX leaked route 10.1.1.0/24
// 3. Add connected route
// 4. Verify that ext connect route 10.1.1.0/24 is added
// 5. Change ext connected route to have NO_ADVERTISE community
// 6. Verify that ext connect route is removed
// 7. Change ext connected route to not have NO_ADVERTISE community
// 8. Verify that ext connect route 10.1.1.0/24 is added
//
TYPED_TEST(ServiceChainTest, ExtConnectRouteNoAdvertiseCommunity) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Ext connect route
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24), 100);

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for ExtConnect route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));

    // Change Ext connect route to have NO_ADVERTISE community
    vector<uint32_t> commlist = list_of(CommunityType::NoAdvertise);
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24), 100,
                   commlist);

    // Check for ExtConnect route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("10.1.1.0", 24));

    // Change Ext connect route to not have NO_ADVERTISE community
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24), 100);

    // Check for ExtConnect route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Delete ExtRoute and connected route
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

//
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add MX leaked route 10.1.1.0/24
// 3. Add connected route
// 4. Verify that ext connect route 10.1.1.0/24 is added
// 5. Change ext connected route to have NO_REORIGINATE community
// 6. Verify that ext connect route is removed
// 7. Change ext connected route to not have NO_REORIGINATE community
// 8. Verify that ext connect route 10.1.1.0/24 is added
//
TYPED_TEST(ServiceChainTest, ExtConnectRouteNoReOriginateCommunity) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Ext connect route
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24), 100);

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for ExtConnect route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));

    // Change Ext connect route to have NO_REORIGINATE community
    vector<uint32_t> commlist = list_of(CommunityType::NoReOriginate);
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24), 100,
                   commlist);

    // Check for ExtConnect route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("10.1.1.0", 24));

    // Change Ext connect route to not have NO_REORIGINATE community
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24), 100);

    // Check for ExtConnect route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Delete ExtRoute and connected route
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

//
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add connected route
// 3. Add VM route 192.168.1.1/32
// 4. Add MX leaked route 10.1.1.0/24
// 5. Add non-OriginVn route 20.1.1.0/24
// 8. Verify that aggregate route 192.168.1.0/24 is added
// 7. Verify that ext connect route 10.1.1.0/24 is added
// 8. Verify that non-OriginVn route 20.1.1.0/24 is not added
//
TYPED_TEST(ServiceChainTest, ExtConnectRouteOriginVnOnly) {
    vector<string> instance_names =
        list_of("blue")("blue-i1")("red-i2")("red")("green");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red") ("red", "green");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Add more specific
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);

    // Add Ext connect route
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24), 100);

    // Add route to green VN which gets imported into red
    this->AddRoute(NULL, "green", this->BuildPrefix("20.1.1.0", 24), 100);

    // Check for Aggregate route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Check for ExtConnect route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Check for non-OriginVn route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("20.1.1.0", 24));

    // Delete ExtRoute, More specific, non-OriginVn and connected route
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24));
    this->DeleteRoute(NULL, "green", this->BuildPrefix("20.1.1.0", 24));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

//
// Service chain route should be added for routes with unresolved origin
// vn if there is at least one route target matching an export target of
// the destination instance.
//
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add connected route
// 3. Add MX leaked route 10.1.1.0/24 with unresolved OriginVn
// 4. Verify that ext connect route 10.1.1.0/24 is added
//
TYPED_TEST(ServiceChainTest, ExtConnectRouteOriginVnUnresolved1) {
    vector<string> instance_names =
        list_of("blue")("blue-i1")("red-i2")("red")("green");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Add Ext connect route with targets of both red and green.
    vector<string> instances = list_of("red")("green");
    this->AddVpnRoute(NULL, instances, this->BuildPrefix("10.1.1.0", 24), 100);

    // Verify that MX leaked route is present in red
    this->VerifyRouteExists("red", this->BuildPrefix("10.1.1.0", 24));

    // Verify that ExtConnect route is present in blue
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Delete ExtRoute and connected route
    this->DeleteVpnRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

//
// Service chain route must not be added for routes with unresolved origin
// vn if there is no route target matching an export target of destination
// instance.
//
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add connected route
// 3. Add MX leaked route 10.1.1.0/24 with unresolved OriginVn
// 4. Verify that ext connect route 10.1.1.0/24 is not added
//
TYPED_TEST(ServiceChainTest, ExtConnectRouteOriginVnUnresolved2) {
    vector<string> instance_names =
        list_of("blue")("blue-i1")("red-i2")("red")("green")("yellow");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red") ("red", "green");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Add Ext connect route with targets of green and yellow.
    vector<string> instances = list_of("green")("yellow");
    this->AddVpnRoute(NULL, instances, this->BuildPrefix("10.1.1.0", 24), 100);

    // Verify that MX leaked route is present in red
    this->VerifyRouteExists("red", this->BuildPrefix("10.1.1.0", 24));

    // Verify that ExtConnect route is not present in blue
    this->VerifyRouteNoExists("blue", this->BuildPrefix("10.1.1.0", 24));

    // Delete ExtRoute and connected route
    this->DeleteVpnRoute(NULL, "green", this->BuildPrefix("10.1.1.0", 24));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add MX leaked route covering the VN subnet 192.168.0.0/16
// 3. Add VM route and connected route
// 4. Verify that Aggregate route 192.168.1.0/24 is added
// 5. Verify that ext connect route 192.168.0.0/16 is added
TYPED_TEST(ServiceChainTest, ExtConnectRouteCoveringSubnetPrefix) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Ext connect route.. Say MX leaks /16 route
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.0.0", 16), 100);

    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);

    // Check for ExtConnect route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.0.0", 16));
    // Check for Aggregate route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for ExtConnect route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.0.0", 16));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.0.0", 16),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Check for Aggregate route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Delete ExtRoute, More specific and connected route
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.0.0", 16));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add MX leaked route within the VN subnet 192.168.1.252/30
// 3. Add VM route and connected route
// 4. Verify that Aggregate route is added with connected route nexthop
// 5. Verify that MX added ext connect route is treated as more specific itself
TYPED_TEST(ServiceChainTest, ExtConnectRouteWithinSubnetPrefix) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Ext connect route.. Say MX leaks /30 route
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.252", 30), 100);

    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);

    // Check for ExtConnect route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.252", 30));

    // Check for Aggregate route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for ExtConnect route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.252", 30));

    // Check for Aggregate route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Delete ExtRoute, More specific and connected route
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.252", 30));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

//
// 1. Add service chain with vn subnet as 192.168.1.0/24
// 2. Add ExtConnecting route 192.168.1.252/30 within the VN subnet
// 3. Add VM route 192.168.1.1/32 as more specific
// 4. Add connected route 1.1.2.3/32
// 5. Verify aggregate route(192.168.1.0/24) & ExtConnect route 192.168.1.252/30
//    is not added as it more specific of vn subnet
// 6. Update the service chain to contain only 10.1.1.0/24 as subnet prefix.
//    Removed 192.168.1.0/24
// 7. Verify ext connect route 192.168.1.252/30 and 192.168.1.1/32 is added and
//    old aggregate(192.168.1.0/24) should be removed
// 7.1 Add 192.168.0.0/16 and verify this is added as ext connect route
// 8. Add new VM route in new subnet 10.1.1.1/32 and
//    verify aggregate route 10.1.1/24
// 9. Update the service chain to contain only 192.168.1.0/24
// 10. Verify 10.1.1.1/32 is added as ext connect route
//     Verify 192.168.0.0/16 is added as ext connect route
//     Verify 192.168.1.0/24 is added as aggregate route
//     Verify 192.168.1.1/32 is removed as ext connecting route
//     Verify 192.168.1.250/30 is removed as ext connecting route
//
TYPED_TEST(ServiceChainTest, ExtConnectRouteServiceChainUpdate) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Ext connect route.. Say MX leaks /30 route
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.252", 30), 100);

    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);

    // Check for ExtConnect route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.252", 30));

    // Check for Aggregate route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for ExtConnect route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.252", 30));

    // Check for Aggregate route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_4.xml");

    // Check for ExtConnect route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.252", 30));
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.1", 32));

    // Check for Aggregate route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("10.1.1.0", 24));

    // Check for Previous Aggregate route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add Ext connect route.. Say MX leaks /16 route
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.0.0", 16), 100);

    // Check for ExtConnect route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.0.0", 16));

    // Add more specific for new subnet prefix
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.1", 32), 100);

    // Check for Aggregate route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Check for ext connect route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.1", 32));
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.0.0", 16)),

    // Check for new Aggregate route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Check for removal of ExtConnect route it is now more specific
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.252", 30));

    // Check for removal of ExtConnect route it is now more specific
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.1", 32));

    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.252", 30));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.0.0", 16));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.1.1", 32));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, ExtConnectedEcmpPaths) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add MX leaked route
    this->AddRoute(NULL, "red", this->BuildPrefix("10.10.1.0", 24), 100);

    // Add Connected
    this->AddConnectedRoute(this->peers_[0], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.0.5"));

    // Check for external connected route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.10.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.10.1.0", 24),
                                this->BuildNextHopAddress("2.3.0.5"), "red");

    // Add Connected
    this->AddConnectedRoute(this->peers_[1], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.1.5"));
    vector<string> path_ids = list_of(
        this->BuildNextHopAddress("2.3.0.5"))
        (this->BuildNextHopAddress("2.3.1.5"));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.10.1.0", 24),
                                path_ids, "red");

    this->DeleteConnectedRoute(this->peers_[1], this->BuildPrefix("1.1.2.3",
                               32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.10.1.0", 24),
                                this->BuildNextHopAddress("2.3.0.5"), "red");

    // Delete MX route
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.10.1.0", 24));

    // Delete connected route
    this->DeleteConnectedRoute(this->peers_[0],
                               this->BuildPrefix("1.1.2.3", 32));
}


TYPED_TEST(ServiceChainTest, ExtConnectedMoreSpecificEcmpPaths) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Ext connect route
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24), 100);

    // Add Connected
    this->AddConnectedRoute(this->peers_[0], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.0.5"));

    // Add more specific
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);

    // Check for Aggregate route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.0.5"), "red");

    // Check for ExtConnect route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.1.0", 24),
                                this->BuildNextHopAddress("2.3.0.5"), "red");

    // Connected path is infeasible
    this->AddConnectedRoute(this->peers_[0], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.0.5"),
                            BgpPath::AsPathLooped);

    // Verify that Aggregate route and ExtConnect route is gone
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("10.1.1.0", 24));

    // Connected path again from two peers
    this->AddConnectedRoute(this->peers_[0], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.0.5"));
    this->AddConnectedRoute(this->peers_[1], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.1.5"));

    // Check for Aggregate & ExtConnect route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));
    vector<string> path_ids = list_of(
        this->BuildNextHopAddress("2.3.0.5"))
        (this->BuildNextHopAddress("2.3.1.5"));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                path_ids, "red");
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.1.0", 24),
                                path_ids, "red");

    // Connected path is infeasible
    this->AddConnectedRoute(this->peers_[0], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.0.5"),
                            BgpPath::AsPathLooped);

    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.1.5"), "red");
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.1.0", 24),
                                this->BuildNextHopAddress("2.3.1.5"), "red");

    // Delete ExtRoute, More specific and connected route
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24));
    this->DeleteConnectedRoute(this->peers_[0],
                               this->BuildPrefix("1.1.2.3", 32));
    this->DeleteConnectedRoute(this->peers_[1],
                               this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, ServiceChainRouteSGID) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->DisableServiceChainAggregation();
    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    vector<uint32_t> sgid_list_more_specific_1 = list_of(1)(2)(3)(4);
    vector<uint32_t> sgid_list_more_specific_2 = list_of(5)(6)(7)(8);
    vector<uint32_t> sgid_list_connected = list_of(9)(10)(11)(12);
    vector<uint32_t> sgid_list_ext = list_of(13)(14)(15)(16);

    // Add Ext connect route
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24), 100,
                   vector<uint32_t>(),
        sgid_list_ext);

    // Add Connected
    this->AddConnectedRoute(this->peers_[0],
                            this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.0.5"), 0, 0,
                      sgid_list_connected);

    // Add more specific
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100,
                   vector<uint32_t>(),
        sgid_list_more_specific_1);
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.2", 32), 100,
                   vector<uint32_t>(),
        sgid_list_more_specific_2);

    // Check for More specific routes leaked in src instance
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.1", 32),
                                this->BuildNextHopAddress("2.3.0.5"), "red",
                                sgid_list_more_specific_1);
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.2", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.2", 32),
                                this->BuildNextHopAddress("2.3.0.5"), "red",
                                sgid_list_more_specific_2);

    // Check for ExtConnect route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.1.0", 24),
                                this->BuildNextHopAddress("2.3.0.5"), "red",
                                sgid_list_ext);

    // Delete ExtRoute, More specific and connected route
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.2", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24));
    this->DeleteConnectedRoute(this->peers_[0],
                               this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, ServiceChainRouteUpdateSGID) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->DisableServiceChainAggregation();
    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    vector<uint32_t> sgid_list_more_specific_1 = list_of(1)(2)(3)(4);
    vector<uint32_t> sgid_list_more_specific_2 = list_of(5)(6)(7)(8);
    vector<uint32_t> sgid_list_connected = list_of(9)(10)(11)(12);
    vector<uint32_t> sgid_list_ext = list_of(13)(14)(15)(16);

    // Add Ext connect route
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24), 100,
                   vector<uint32_t>(),
        sgid_list_ext);

    // Add Connected
    this->AddConnectedRoute(this->peers_[0], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.0.5"), 0, 0,
                            sgid_list_connected);

    // Add more specific
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100,
                   vector<uint32_t>(), sgid_list_more_specific_1);
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.2", 32), 100,
                   vector<uint32_t>(), sgid_list_more_specific_2);

    // Check for More specific routes leaked in src instance
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.1", 32),
                                this->BuildNextHopAddress("2.3.0.5"), "red",
                                sgid_list_more_specific_1);
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.2", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.2", 32),
                                this->BuildNextHopAddress("2.3.0.5"), "red",
                                sgid_list_more_specific_2);

    // Check for ExtConnect route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.1.0", 24),
                                this->BuildNextHopAddress("2.3.0.5"), "red",
                                sgid_list_ext);

    // Update Ext connect route with different SGID list
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24), 100,
                   vector<uint32_t>(), sgid_list_more_specific_1);

    // Add Connected
    this->AddConnectedRoute(this->peers_[0], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.0.5"), 0, 0,
                            sgid_list_more_specific_2);

    // Add more specific
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100,
                   vector<uint32_t>(), sgid_list_ext);
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.2", 32), 100,
                   vector<uint32_t>(), sgid_list_connected);

    // Check for More specific routes leaked in src rtinstance
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.1", 32),
                                this->BuildNextHopAddress("2.3.0.5"), "red",
                                sgid_list_ext);
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.2", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.2", 32),
                                this->BuildNextHopAddress("2.3.0.5"), "red",
                                sgid_list_connected);

    // Check for ExtConnect route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.1.0", 24),
                                this->BuildNextHopAddress("2.3.0.5"), "red",
                                sgid_list_more_specific_1);

    // Delete ExtRoute, More specific and connected route
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.2", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24));
    this->DeleteConnectedRoute(this->peers_[0],
                               this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, ValidateTunnelEncapAggregate) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific
    set<string> encap_more_specific = list_of("udp");
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100,
                   vector<uint32_t>(), vector<uint32_t>(), encap_more_specific);

    // Add Connected
    set<string> encap = list_of("vxlan");
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"), 0, 0,
                            vector<uint32_t>(), encap);

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red",
                                encap);

    // Add Connected
    encap = list_of("gre");
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"), 0, 0,
                            vector<uint32_t>(), encap);

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red",
                                encap);

    // Delete More specific
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));

    // Delete connected route
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, ValidateTunnelEncapExtRoute) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Ext connect route
    set<string> encap_ext = list_of("vxlan");
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24), 100,
                   vector<uint32_t>(), vector<uint32_t>(), encap_ext);

    // Add Connected
    set<string> encap = list_of("gre");
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"), 0, 0,
                            vector<uint32_t>(), encap);

    // Check for service Chain router
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red",
                                encap);

    // Add Connected
    encap = list_of("udp");
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"), 0, 0,
                            vector<uint32_t>(), encap);
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red",
                                encap);

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));

    // Delete ext connected route
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24));

    // Delete connected route
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, MultiPathTunnelEncap) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific
    set<string> encap_1 = list_of("gre");
    set<string> encap_2 = list_of("udp");
    set<string> encap_3 = list_of("vxlan");
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32), 100);
    this->AddConnectedRoute(this->peers_[0], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.1.5"), 0, 0,
                            vector<uint32_t>(), encap_1);
    this->AddConnectedRoute(this->peers_[1], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.2.5"), 0, 0,
                            vector<uint32_t>(), encap_2);
    this->AddConnectedRoute(this->peers_[2], this->BuildPrefix("1.1.2.3", 32),
                            100, this->BuildNextHopAddress("2.3.3.5"), 0, 0,
                            vector<uint32_t>(), encap_3);

    // Check for aggregated route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    vector<string> path_ids = list_of(
        this->BuildNextHopAddress("2.3.1.5"))
        (this->BuildNextHopAddress("2.3.2.5"))
        (this->BuildNextHopAddress("2.3.3.5"));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                                path_ids, "red");
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                               this->BuildNextHopAddress("2.3.1.5"), "red",
                               encap_1);
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                               this->BuildNextHopAddress("2.3.2.5"), "red",
                               encap_2);
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
                               this->BuildNextHopAddress("2.3.3.5"), "red",
                               encap_3);

    // Delete More specific
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.1.1", 32));

    // Delete connected route
    this->DeleteConnectedRoute(this->peers_[0],
                               this->BuildPrefix("1.1.2.3", 32));
    this->DeleteConnectedRoute(this->peers_[1],
                               this->BuildPrefix("1.1.2.3", 32));
    this->DeleteConnectedRoute(this->peers_[2],
                               this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, ValidateSiteOfOriginExtRoute) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Ext connect route
    SiteOfOrigin soo1 = SiteOfOrigin::FromString("soo:65001:100");
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24), 100, soo1);

    // Check for service chain route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("10.1.1.0", 24));

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for service chain route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red",
                                soo1);

    // Update Ext connect route
    SiteOfOrigin soo2 = SiteOfOrigin::FromString("soo:65001:200");
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24), 100, soo2);

    // Check for service chain route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red",
                                soo2);

    // Delete Ext connect route
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24));

    // Delete connected route
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

//
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add MX leaked route 10.1.1.0/24
// 3. Add connected route
// 4. Verify that ext connect route 10.1.1.0/24 is added
// 5. Change ext connected route to have some communities
// 6. Verify that service chain route has communities
// 7. Change ext connected route to not have communities
// 8. Verify that service chain route doesn't have communities
//
TYPED_TEST(ServiceChainTest, ValidateCommunityExtRoute) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Ext connect route
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24), 100);

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for ExtConnect route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));

    // Change Ext connect route to have some communities.
    vector<uint32_t> commlist = list_of(0xFFFFAA01)(0xFFFFAA02)(0xFFFFAA03);
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24), 100,
                   commlist);

    // Check for ExtConnect route
    CommunitySpec commspec;
    commspec.communities.push_back(CommunityType::AcceptOwnNexthop);
    commspec.communities.insert(
        commspec.communities.end(), commlist.begin(), commlist.end());
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red",
                                commspec);

    // Change Ext connect route to not have communities.
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24), 100);

    // Check for ExtConnect route
    commspec.communities.clear();
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.1.0", 24),
                                this->BuildNextHopAddress("2.3.4.5"), "red");

    // Delete ExtRoute and connected route
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, DeleteConnectedWithExtConnectRoute) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Ext connect route
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.1", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.2", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.1.3", 32), 100);

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for ExtConnect route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.1", 32));
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.2", 32));
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.3", 32));

    this->DisableServiceChainQ();
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 200,
                            this->BuildNextHopAddress("2.3.4.5"));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.1.1", 32));

    this->VerifyRouteIsDeleted("red", this->BuildPrefix("10.1.1.1", 32));

    // Check for ExtConnect route
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.1", 32));
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.2", 32));
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.1.3", 32));

    this->EnableServiceChainQ();

    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty());

    this->VerifyRouteNoExists("blue", this->BuildPrefix("10.1.1.1", 32));

    // Delete ExtRoute, More specific and connected route
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.1.2", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.1.3", 32));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));
}

TYPED_TEST(ServiceChainTest, DeleteEntryReuse) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    vector<string> routes_to_play = list_of(
        this->BuildPrefix("10.1.1.1", 32))
        (this->BuildPrefix("10.1.1.1", 32))
        (this->BuildPrefix("10.1.1.1", 32));
    // Add Ext connect route
    for (vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++)
        this->AddRoute(NULL, "red", *it, 100);

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for ExtConnect route
    for (vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++) {
        this->VerifyRouteExists("blue", *it);
    }
    this->DisableServiceChainQ();
    for (vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++)
        this->DeleteRoute(NULL, "red", *it);
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));

    for (vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++) {
        this->VerifyRouteIsDeleted("red", *it);
    }

    for (vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++)
        this->AddRoute(NULL, "red", *it, 100);
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));


    for (vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++)
        this->DeleteRoute(NULL, "red", *it);
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));

    for (vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++) {
        this->VerifyRouteIsDeleted("red", *it);
    }

    this->EnableServiceChainQ();
    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty());
}

TYPED_TEST(ServiceChainTest, EntryAfterStop) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    vector<string> routes_to_play;
    // Add Ext connect route
    for (int i = 0; i < 255; i++) {
        stringstream route;
        route << "10.1.1." << i;
        routes_to_play.push_back(this->BuildPrefix(route.str(), 32));
    }

    for (vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++)
        this->AddRoute(NULL, "red", *it, 100);

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    // Check for ExtConnect route
    for (vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++) {
        this->VerifyRouteExists("blue", *it);
    }
    this->DisableServiceChainQ();

    this->ClearServiceChainInformation("blue-i1");

    // Add more Ext connect route
    for (int i = 0; i < 255; i++) {
        stringstream route;
        route << "10.2.1." << i;
        routes_to_play.push_back(this->BuildPrefix(route.str(), 32));
    }

    for (vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++)
        this->AddRoute(NULL, "red", *it, 200);
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("2.3.4.5"));

    this->EnableServiceChainQ();
    TASK_UTIL_EXPECT_TRUE(this->IsServiceChainQEmpty());

    for (vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++)
        this->DeleteRoute(NULL, "red", *it);
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32));

    TASK_UTIL_EXPECT_EQ(0, this->RouteCount("red"));
}

//
// Instances are (blue)(blue-i1)(core-i2)(core)(core-i3)(red-i4)(red)
//
TYPED_TEST(ServiceChainTest, TransitNetworkRemoteVMRoutes) {
    this->ParseConfigFile(
            "controller/src/bgp/testdata/service_chain_test_1.xml");
    this->AddConnection("blue", "blue-i1");
    this->AddConnection("core", "core-i3");

    // Add more specific routes to red
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.3.101", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.3.102", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.3.103", 32), 100);

    // Add Connected routes for the 2 chains
    this->AddConnectedRoute(1, NULL, this->BuildPrefix("192.168.1.253", 32),
                            100, this->BuildNextHopAddress("20.1.1.1"));
    this->AddConnectedRoute(2, NULL, this->BuildPrefix("192.168.2.253", 32),
                            100, this->BuildNextHopAddress("20.1.1.2"));

    // Check for Aggregate route in blue
    vector<string> origin_vn_path = list_of("core-vn")("red-vn");
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.3.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.3.0", 24),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);

    // Delete more specific routes and connected routes
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.3.101", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.3.102", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.3.103", 32));
    this->DeleteConnectedRoute(1, NULL, this->BuildPrefix("192.168.1.253", 32));
    this->DeleteConnectedRoute(2, NULL, this->BuildPrefix("192.168.2.253", 32));
}

//
// Instances are (blue)(blue-i1)(core-i2)(core)(core-i3)(red-i4)(red)
//
TYPED_TEST(ServiceChainTest, TransitNetworkLocalVMRoutes) {
    this->ParseConfigFile(
            "controller/src/bgp/testdata/service_chain_test_1.xml");
    this->AddConnection("blue", "blue-i1");
    this->AddConnection("core", "core-i3");

    // Add more specific routes to core
    this->AddRoute(NULL, "core", this->BuildPrefix("192.168.2.101", 32), 100);
    this->AddRoute(NULL, "core", this->BuildPrefix("192.168.2.102", 32), 100);
    this->AddRoute(NULL, "core", this->BuildPrefix("192.168.2.103", 32), 100);

    // Add Connected routes for the blue-core chain
    this->AddConnectedRoute(1, NULL, this->BuildPrefix("192.168.1.253", 32),
                            100, this->BuildNextHopAddress("20.1.1.1"));

    // Check for Aggregate route in blue
    vector<string> origin_vn_path = list_of("core-vn");
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.2.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.2.0", 24),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);

    // Delete more specific routes and connected routes
    this->DeleteRoute(NULL, "core", this->BuildPrefix("192.168.2.101", 32));
    this->DeleteRoute(NULL, "core", this->BuildPrefix("192.168.2.102", 32));
    this->DeleteRoute(NULL, "core", this->BuildPrefix("192.168.2.103", 32));
    this->DeleteConnectedRoute(1, NULL, this->BuildPrefix("192.168.1.253", 32));
}

//
// Instances are (blue)(blue-i1)(core-i2)(core)(core-i3)(red-i4)(red)
//
TYPED_TEST(ServiceChainTest, TransitNetworkRemoteExtConnectRoute) {
    this->ParseConfigFile(
            "controller/src/bgp/testdata/service_chain_test_1.xml");
    this->AddConnection("blue", "blue-i1");
    this->AddConnection("core", "core-i3");

    // Add Ext connect routes to red
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.3.1", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.3.2", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.3.3", 32), 100);

    // Add Connected routes for the 2 chains.
    this->AddConnectedRoute(1, NULL, this->BuildPrefix("192.168.1.253", 32),
                            100, this->BuildNextHopAddress("20.1.1.1"));
    this->AddConnectedRoute(2, NULL, this->BuildPrefix("192.168.2.253", 32),
                            100, this->BuildNextHopAddress("20.1.1.2"));

    // Check for ExtConnect route in blue
    vector<string> origin_vn_path = list_of("core-vn")("red-vn");
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.3.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.3.1", 32),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.3.2", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.3.2", 32),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.3.3", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.3.3", 32),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);

    // Delete Ext connect routes and connected routes
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.3.1", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.3.2", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.3.3", 32));
    this->DeleteConnectedRoute(1, NULL, this->BuildPrefix("192.168.1.253", 32));
    this->DeleteConnectedRoute(2, NULL, this->BuildPrefix("192.168.2.253", 32));
}

TYPED_TEST(ServiceChainTest, TransitNetworkLocalExtConnectRoute) {
    this->ParseConfigFile(
            "controller/src/bgp/testdata/service_chain_test_1.xml");
    this->AddConnection("blue", "blue-i1");
    this->AddConnection("core", "core-i3");

    // Add Ext connect routes to core
    this->AddRoute(NULL, "core", this->BuildPrefix("10.1.2.1", 32), 100);
    this->AddRoute(NULL, "core", this->BuildPrefix("10.1.2.2", 32), 100);
    this->AddRoute(NULL, "core", this->BuildPrefix("10.1.2.3", 32), 100);

    // Add Connected routes for the blue-core chain
    this->AddConnectedRoute(1, NULL, this->BuildPrefix("192.168.1.253", 32),
                            100, this->BuildNextHopAddress("20.1.1.1"));

    // Check for ExtConnect route in blue
    vector<string> origin_vn_path = list_of("core-vn");
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.2.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.2.1", 32),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.2.2", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.2.2", 32),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.2.3", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.2.3", 32),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);

    // Delete Ext connect routes and connected routes
    this->DeleteRoute(NULL, "core", this->BuildPrefix("10.1.2.1", 32));
    this->DeleteRoute(NULL, "core", this->BuildPrefix("10.1.2.2", 32));
    this->DeleteRoute(NULL, "core", this->BuildPrefix("10.1.2.3", 32));
    this->DeleteConnectedRoute(1, NULL, this->BuildPrefix("192.168.1.253", 32));
}

//
// Instances are (blue)(blue-i1)(core-i2)(core)(core-i3)(red-i4)(red)
//
TYPED_TEST(ServiceChainTest, TransitNetworkAddDeleteConnectedRoute1) {
    this->ParseConfigFile(
            "controller/src/bgp/testdata/service_chain_test_1.xml");
    this->AddConnection("blue", "blue-i1");
    this->AddConnection("core", "core-i3");

    // Add more specific routes to red
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.3.101", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.3.102", 32), 100);

    // Add Ext connect routes to red
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.3.1", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.3.2", 32), 100);

    // Add Connected routes for the 2 chains
    this->AddConnectedRoute(1, NULL, this->BuildPrefix("192.168.1.253", 32),
                            100, this->BuildNextHopAddress("20.1.1.1"));
    this->AddConnectedRoute(2, NULL, this->BuildPrefix("192.168.2.253", 32),
                            100, this->BuildNextHopAddress("20.1.1.2"));

    // Check for Aggregate route in blue
    vector<string> origin_vn_path = list_of("core-vn")("red-vn");
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.3.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.3.0", 24),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);

    // Check for ExtConnect routes in blue
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.3.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.3.1", 32),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.3.2", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.3.2", 32),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);

    // Remove connected route for blue-core chain.
    this->DeleteConnectedRoute(1, NULL, this->BuildPrefix("192.168.1.253", 32));

    // Check for Aggregate route in blue
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.3.0", 24));

    // Check for ExtConnect routes in blue
    this->VerifyRouteNoExists("blue", this->BuildPrefix("10.1.3.1", 32));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("10.1.3.2", 32));

    // Add connected route for blue-core chain.
    this->AddConnectedRoute(1, NULL, this->BuildPrefix("192.168.1.253", 32),
                            100, this->BuildNextHopAddress("20.1.1.1"));

    // Check for Aggregate route in blue
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.3.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.3.0", 24),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);

    // Check for ExtConnect routes in blue
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.3.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.3.1", 32),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.3.2", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.3.2", 32),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);

    // Delete Ext connect routes and connected routes
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.3.101", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.3.102", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.3.1", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.3.2", 32));
    this->DeleteConnectedRoute(1, NULL, this->BuildPrefix("192.168.1.253", 32));
    this->DeleteConnectedRoute(2, NULL, this->BuildPrefix("192.168.2.253", 32));
}

//
// Instances are (blue)(blue-i1)(core-i2)(core)(core-i3)(red-i4)(red)
//
TYPED_TEST(ServiceChainTest, TransitNetworkAddDeleteConnectedRoute2) {
    this->ParseConfigFile(
            "controller/src/bgp/testdata/service_chain_test_1.xml");
    this->AddConnection("blue", "blue-i1");
    this->AddConnection("core", "core-i3");

    // Add more specific routes to red
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.3.101", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.3.102", 32), 100);

    // Add Ext connect routes to red
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.3.1", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.3.2", 32), 100);

    // Add Connected routes for the 2 chains
    this->AddConnectedRoute(1, NULL, this->BuildPrefix("192.168.1.253", 32),
                           100, this->BuildNextHopAddress("20.1.1.1"));
    this->AddConnectedRoute(2, NULL, this->BuildPrefix("192.168.2.253", 32),
                           100, this->BuildNextHopAddress("20.1.1.2"));

    // Check for Aggregate route in blue
    vector<string> origin_vn_path = list_of("core-vn")("red-vn");
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.3.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.3.0", 24),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);

    // Check for ExtConnect routes in blue
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.3.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.3.1", 32),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.3.2", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.3.2", 32),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);

    // Remove connected route for core-red chain.
    this->DeleteConnectedRoute(2, NULL, this->BuildPrefix("192.168.2.253", 32));

    // Check for Aggregate route in blue
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.3.0", 24));

    // Check for ExtConnect routes in blue
    this->VerifyRouteNoExists("blue", this->BuildPrefix("10.1.3.1", 32));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("10.1.3.2", 32));

    // Add connected route for core-red chain.
    this->AddConnectedRoute(2, NULL, this->BuildPrefix("192.168.2.253", 32),
                            100, this->BuildNextHopAddress("20.1.1.1"));

    // Check for Aggregate route in blue
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.3.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.3.0", 24),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);

    // Check for ExtConnect routes in blue
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.3.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.3.1", 32),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.3.2", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.3.2", 32),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);

    // Delete Ext connect routes and connected routes
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.3.101", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.3.102", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.3.1", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.3.2", 32));
    this->DeleteConnectedRoute(1, NULL, this->BuildPrefix("192.168.1.253", 32));
    this->DeleteConnectedRoute(2, NULL, this->BuildPrefix("192.168.2.253", 32));
}

//
// Instances are (blue)(blue-i1)(core-i2)(core)(core-i3)(red-i4)(red)
//
TYPED_TEST(ServiceChainTest, TransitNetworkToggleAllowTransit) {
    string content = this->ParseConfigFile(
            "controller/src/bgp/testdata/service_chain_test_1.xml");
    this->AddConnection("blue", "blue-i1");
    this->AddConnection("core", "core-i3");

    // Add more specific routes to red
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.3.101", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.3.102", 32), 100);

    // Add Ext connect routes to red
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.3.1", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.3.2", 32), 100);

    // Add Connected routes for the 2 chains
    this->AddConnectedRoute(1, NULL, this->BuildPrefix("192.168.1.253", 32),
                            100, this->BuildNextHopAddress("20.1.1.1"));
    this->AddConnectedRoute(2, NULL, this->BuildPrefix("192.168.2.253", 32),
                            100, this->BuildNextHopAddress("20.1.1.2"));

    // Check for Aggregate route in blue
    vector<string> origin_vn_path = list_of("core-vn")("red-vn");
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.3.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.3.0", 24),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);

    // Check for ExtConnect routes in blue
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.3.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.3.1", 32),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.3.2", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.3.2", 32),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);

    // Disable allow-transit
    boost::replace_all(content,
        "<allow-transit>true</allow-transit>",
        "<allow-transit>false</allow-transit>");
    this->ParseConfigString(content);

    // Check for Aggregate route in blue
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.3.0", 24));

    // Check for ExtConnect routes in blue
    this->VerifyRouteNoExists("blue", this->BuildPrefix("10.1.3.1", 32));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("10.1.3.2", 32));

    // Enable allow-transit
    boost::replace_all(content,
        "<allow-transit>false</allow-transit>",
        "<allow-transit>true</allow-transit>");
    this->ParseConfigString(content);

    // Check for Aggregate route in blue
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.3.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.3.0", 24),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);

    // Check for ExtConnect routes in blue
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.3.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.3.1", 32),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.3.2", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.3.2", 32),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path);

    // Delete Ext connect routes and connected routes
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.3.101", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.3.102", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.3.1", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.3.2", 32));
    this->DeleteConnectedRoute(1, NULL, this->BuildPrefix("192.168.1.253", 32));
    this->DeleteConnectedRoute(2, NULL, this->BuildPrefix("192.168.2.253", 32));
}

//
// Instances are (blue)(blue-i1)(core-i2)(core)(core-i3)(red-i4)(red)
//                                             (core-i5)(green-i6)(green)
//
TYPED_TEST(ServiceChainTest, TransitNetworkMultipleNetworks) {
    this->ParseConfigFile(
            "controller/src/bgp/testdata/service_chain_test_2.xml");
    this->AddConnection("blue", "blue-i1");
    this->AddConnection("core", "core-i3");
    this->AddConnection("core", "core-i5");

    // Add more specific routes to red and green
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.3.101", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.3.102", 32), 100);
    this->AddRoute(NULL, "green", this->BuildPrefix("192.168.4.101", 32), 100);
    this->AddRoute(NULL, "green", this->BuildPrefix("192.168.4.102", 32), 100);

    // Add Ext connect routes to red and green
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.3.1", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.3.2", 32), 100);
    this->AddRoute(NULL, "green", this->BuildPrefix("10.1.4.1", 32), 100);
    this->AddRoute(NULL, "green", this->BuildPrefix("10.1.4.2", 32), 100);

    // Add Connected routes for the 3 chains
    this->AddConnectedRoute(1, NULL, this->BuildPrefix("192.168.1.253", 32),
                            100, this->BuildNextHopAddress("20.1.1.1"));
    this->AddConnectedRoute(2, NULL, this->BuildPrefix("192.168.2.253", 32),
                            100, this->BuildNextHopAddress("20.1.1.2"));
    this->AddConnectedRoute(3, NULL, this->BuildPrefix("192.168.2.252", 32),
                            100, this->BuildNextHopAddress("20.1.1.3"));

    // Check for Aggregate routes in blue
    vector<string> origin_vn_path_red = list_of("core-vn")("red-vn");
    vector<string> origin_vn_path_green = list_of("core-vn")("green-vn");
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.3.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.3.0", 24),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path_red);
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.4.0", 24));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("192.168.4.0", 24),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path_green);

    // Check for ExtConnect routes in blue
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.3.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.3.1", 32),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path_red);
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.3.2", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.3.2", 32),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path_red);
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.4.1", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.4.1", 32),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path_green);
    this->VerifyRouteExists("blue", this->BuildPrefix("10.1.4.2", 32));
    this->VerifyRouteAttributes("blue", this->BuildPrefix("10.1.4.2", 32),
                                this->BuildNextHopAddress("20.1.1.1"),
                                "core-vn", origin_vn_path_green);

    // Delete Ext connect routes and connected routes
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.3.101", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.3.102", 32));
    this->DeleteRoute(NULL, "green", this->BuildPrefix("192.168.4.101", 32));
    this->DeleteRoute(NULL, "green", this->BuildPrefix("192.168.4.102", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.3.1", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.3.2", 32));
    this->DeleteRoute(NULL, "green", this->BuildPrefix("10.1.4.1", 32));
    this->DeleteRoute(NULL, "green", this->BuildPrefix("10.1.4.2", 32));
    this->DeleteConnectedRoute(1, NULL, this->BuildPrefix("192.168.1.253", 32));
    this->DeleteConnectedRoute(2, NULL, this->BuildPrefix("192.168.2.253", 32));
    this->DeleteConnectedRoute(3, NULL, this->BuildPrefix("192.168.2.252", 32));
}

//
// Instances are (blue)(blue-i1)(core-i2)(core)(core-i3)(red-i4)(red)
// VN index for blue and red is same.
//
TYPED_TEST(ServiceChainTest, TransitNetworkOriginVnLoop) {
    string content = this->ParseConfigFile(
            "controller/src/bgp/testdata/service_chain_test_3.xml");
    this->AddConnection("blue", "blue-i1");
    this->AddConnection("core", "core-i3");

    // Add more specific routes to red
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.3.101", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("192.168.3.102", 32), 100);

    // Add Ext connect routes to red
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.3.1", 32), 100);
    this->AddRoute(NULL, "red", this->BuildPrefix("10.1.3.2", 32), 100);

    // Add Connected routes for the 2 chains
    this->AddConnectedRoute(1, NULL, this->BuildPrefix("192.168.1.253", 32),
                            100, this->BuildNextHopAddress("20.1.1.1"));
    this->AddConnectedRoute(2, NULL, this->BuildPrefix("192.168.2.253", 32),
                            100, this->BuildNextHopAddress("20.1.1.2"));

    // Check for Aggregate route in core
    vector<string> origin_vn_path = list_of("red-vn");
    this->VerifyRouteExists("core", this->BuildPrefix("192.168.3.0", 24));
    this->VerifyRouteAttributes("core", this->BuildPrefix("192.168.3.0", 24),
                                this->BuildNextHopAddress("20.1.1.2"), "red-vn",
                                origin_vn_path);

    // Check for ExtConnect routes in core
    this->VerifyRouteExists("core", this->BuildPrefix("10.1.3.1", 32));
    this->VerifyRouteAttributes("core", this->BuildPrefix("10.1.3.1", 32),
                                this->BuildNextHopAddress("20.1.1.2"), "red-vn",
                                origin_vn_path);
    this->VerifyRouteExists("core", this->BuildPrefix("10.1.3.2", 32));
    this->VerifyRouteAttributes("core", this->BuildPrefix("10.1.3.2", 32),
                                this->BuildNextHopAddress("20.1.1.2"), "red-vn",
                                origin_vn_path);

    // Check for Aggregate route in blue
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.3.0", 24));

    // Check for ExtConnect routes in blue
    this->VerifyRouteNoExists("blue", this->BuildPrefix("10.1.3.1", 32));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("10.1.3.2", 32));

    // Delete Ext connect routes and connected routes
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.3.101", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("192.168.3.102", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.3.1", 32));
    this->DeleteRoute(NULL, "red", this->BuildPrefix("10.1.3.2", 32));
    this->DeleteConnectedRoute(1, NULL, this->BuildPrefix("192.168.1.253", 32));
    this->DeleteConnectedRoute(2, NULL, this->BuildPrefix("192.168.2.253", 32));
}

//
// Verify that routes are not re-originated when source RD of the route matches
// connected path's source RD
//
TYPED_TEST(ServiceChainTest, ExtConnectRouteSourceRDSame) {
    vector<string> instance_names =
        list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    this->NetworkConfig(instance_names, connections);
    this->VerifyNetworkConfig(instance_names);

    this->SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Connected
    this->AddConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32), 100,
                            this->BuildNextHopAddress("3.4.5.6"),
                            0, 0, vector<uint32_t>(), set<string>(),
                            LoadBalance(),
                            RouteDistinguisher::FromString("192.168.1.1:2"));

    // Add Ext connect route with targets of red
    vector<string> instances = list_of("red");
    this->AddVpnRoute(NULL, instances, this->BuildPrefix("10.1.1.0", 24), 100,
                      this->BuildNextHopAddress("1.2.3.4"),
                      0, 0, LoadBalance(),
                      RouteDistinguisher::FromString("192.168.1.1:2"));

    // Verify that MX leaked route is present in red
    this->VerifyRouteExists("red", this->BuildPrefix("10.1.1.0", 24));

    // Verify that ExtConnect route is NOT present in blue
    // Verify that re-origination skipped as original route has same source RD
    // as connected route source RD
    this->VerifyRouteNoExists("blue", this->BuildPrefix("10.1.1.0", 24));

    // Delete ExtRoute and connected route
    this->DeleteVpnRoute(NULL, "red", this->BuildPrefix("10.1.1.0", 24),
                         RouteDistinguisher::FromString("192.168.1.1:2"));
    this->DeleteConnectedRoute(NULL, this->BuildPrefix("1.1.2.3", 32),
                         RouteDistinguisher::FromString("192.168.1.1:2"));
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpObjectFactory::Register<BgpConfigManager>(
        boost::factory<BgpIfmapConfigManager *>());
    BgpObjectFactory::Register<BgpLifetimeManager>(
        boost::factory<BgpLifetimeManagerTest *>());
}

static void TearDown() {
    task_util::WaitForIdle();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

static void process_command_line_args(int argc, const char **argv) {
    options_description desc("ServiceChainTest");
    desc.add_options()
        ("help", "produce help message")
        ("address-family", value<string>()->default_value("ip"),
             "set address family (ip/vpn)")
        ("service-type", value<string>()->default_value("non-transparent"),
             "set service type (transparent/non-transparent)");
    variables_map vm;
    store(parse_command_line(argc, argv, desc), vm);
    notify(vm);

    if (vm.count("service-type")) {
        service_is_transparent =
            (vm["service-type"].as<string>() == "transparent");
    }

    if (vm.count("address-family")) {
        connected_rt_is_vpn =
            (vm["address-family"].as<string>() == "vpn");
    }
}

int service_chain_test_main(int argc, const char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, const_cast<char **>(argv));
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    process_command_line_args(argc, const_cast<const char **>(argv));
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}

#ifndef __SERVICE_CHAIN_TEST_WRAPPER_TEST_SUITE__

int main(int argc, char **argv) {
    return service_chain_test_main(argc, const_cast<const char **>(argv));
}

#endif
