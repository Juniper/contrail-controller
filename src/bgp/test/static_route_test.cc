/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/foreach.hpp>
#include <boost/assign/list_of.hpp>
#include <pugixml/pugixml.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>

#include "base/regex.h"
#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "bgp/extended-community/load_balance.h"
#include "bgp/bgp_config_ifmap.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_server.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet6/inet6_table.h"
#include "bgp/inet6vpn/inet6vpn_table.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/istatic_route_mgr.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/static_route_types.h"
#include "bgp/security_group/security_group.h"
#include "bgp/test/bgp_test_util.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "control-node/control_node.h"
#include "db/db_graph.h"
#include "db/db_partition.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/test/ifmap_test_util.h"
#include "net/community_type.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"

using std::auto_ptr;
using std::ifstream;
using std::istreambuf_iterator;
using std::istringstream;
using std::ostringstream;
using std::set;
using std::string;
using std::vector;
using boost::assign::list_of;
using boost::assign::map_list_of;
using contrail::regex;
using contrail::regex_match;
using contrail::regex_search;
using pugi::xml_document;
using pugi::xml_node;
using pugi::xml_parse_result;

class BgpPeerMock : public IPeer {
public:
    explicit BgpPeerMock(const Ip4Address &address)
        : address_(address),
          address_str_(address.to_string()) {
    }
    virtual ~BgpPeerMock() { }
    virtual const string &ToString() const { return address_str_; }
    virtual const string &ToUVEKey() const { return address_str_; }
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
    Ip4Address address_;
    std::string address_str_;
};

//
// Template structure to pass to fixture class template. Needed because
// gtest fixture class template can accept only one template parameter.
//
template <typename T1, typename T2, typename T3,
         typename T4, typename T5, typename T6>
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
class StaticRouteTest : public ::testing::Test {
protected:
    typedef typename T::TableT TableT;
    typedef typename T::PrefixT PrefixT;
    typedef typename T::RouteT RouteT;
    typedef typename T::VpnTableT VpnTableT;
    typedef typename T::VpnPrefixT VpnPrefixT;
    typedef typename T::VpnRouteT VpnRouteT;

    StaticRouteTest()
      : config_db_(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable")),
        bgp_server_(new BgpServer(&evm_)),
        family_(GetFamily()),
        ipv6_prefix_("::ffff:"),
        ri_mgr_(NULL),
        validate_done_(false) {
        IFMapLinkTable_Init(&config_db_, &config_graph_);
        vnc_cfg_Server_ModuleInit(&config_db_, &config_graph_);
        bgp_schema_Server_ModuleInit(&config_db_, &config_graph_);
    }

    ~StaticRouteTest() {
        STLDeleteValues(&peers_);
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

    string BuildHostAddress(const string &ipv4_addr) const {
        if (family_ == Address::INET) {
            return ipv4_addr;
        }
        if (family_ == Address::INET6) {
            return ipv6_prefix_ + ipv4_addr;
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

    virtual void SetUp() {
        ConcurrencyScope scope("bgp::Config");
        BgpIfmapConfigManager *config_manager =
                static_cast<BgpIfmapConfigManager *>(
                    bgp_server_->config_manager());
        config_manager->Initialize(&config_db_, &config_graph_, "localhost");
        ri_mgr_ = bgp_server_->routing_instance_mgr();
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        bgp_server_->Shutdown();
        task_util::WaitForIdle();
        db_util::Clear(&config_db_);
    }

    void NetworkConfig(const vector<string> &instance_names) {
        bgp_util::NetworkConfigGenerate(&config_db_, instance_names);
    }

    void CreatePeers() {
        boost::system::error_code ec;
        this->peers_.push_back(new BgpPeerMock(Ip4Address::from_string(
            this->BuildHostAddress("192.168.0.1"), ec)));
        this->peers_.push_back(new BgpPeerMock(Ip4Address::from_string(
            this->BuildHostAddress("192.168.0.2"), ec)));
        this->peers_.push_back(new BgpPeerMock(Ip4Address::from_string(
            this->BuildHostAddress("192.168.0.3"), ec)));
        this->peers_.push_back(new BgpPeerMock(Ip4Address::from_string(
            this->BuildHostAddress("192.168.0.4"), ec)));
    }

    void DisableUnregisterTrigger(const string &instance_name) {
        RoutingInstance *rtinstance =
            ri_mgr_->GetRoutingInstance(instance_name);
        if (!rtinstance)
            return;
        IStaticRouteMgr *mgr = rtinstance->static_route_mgr(family_);
        task_util::TaskFire(
            boost::bind(&IStaticRouteMgr::DisableUnregisterTrigger, mgr),
            "bgp::Config");
    }

    void EnableUnregisterTrigger(const string &instance_name) {
        RoutingInstance *rtinstance =
            ri_mgr_->GetRoutingInstance(instance_name);
        if (!rtinstance)
            return;
        IStaticRouteMgr *mgr = rtinstance->static_route_mgr(family_);
        task_util::TaskFire(
            boost::bind(&IStaticRouteMgr::EnableUnregisterTrigger, mgr),
            "bgp::Config");
    }

    void DisableStaticRouteQ(const string &instance_name) {
        RoutingInstance *rtinstance =
            ri_mgr_->GetRoutingInstance(instance_name);
        if (!rtinstance)
            return;
        IStaticRouteMgr *mgr = rtinstance->static_route_mgr(family_);
        task_util::TaskFire(
            boost::bind(&IStaticRouteMgr::DisableQueue, mgr),
            "bgp::Config");
    }

    bool IsQueueEmpty(const string &instance_name) {
        RoutingInstance *rtinstance =
            ri_mgr_->GetRoutingInstance(instance_name);
        return rtinstance->static_route_mgr(family_)->IsQueueEmpty();
    }

    void EnableStaticRouteQ(const string &instance_name) {
        RoutingInstance *rtinstance =
            ri_mgr_->GetRoutingInstance(instance_name);
        if (!rtinstance)
            return;
        IStaticRouteMgr *mgr = rtinstance->static_route_mgr(family_);
        task_util::TaskFire(
            boost::bind(&IStaticRouteMgr::EnableQueue, mgr),
            "bgp::Config");
    }

    void AddRoute(IPeer *peer, const string &instance_name,
        const string &prefix, int localpref, string nexthop_str = "",
        set<string> encap = set<string>(),
        vector<uint32_t> sglist = vector<uint32_t>(),
        uint32_t flags = 0, int label = 0,
        const LoadBalance &lb = LoadBalance()) {
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

        boost::scoped_ptr<BgpAttrNextHop> nexthop_attr;
        if (nexthop_str.empty())
            nexthop_str = this->BuildNextHopAddress("7.8.9.1");

        IpAddress chain_addr = Ip4Address::from_string(nexthop_str, error);
        nexthop_attr.reset(new BgpAttrNextHop(chain_addr.to_v4().to_ulong()));
        attr_spec.push_back(nexthop_attr.get());

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

        if (!lb.IsDefault())
            ext_comm.communities.push_back(lb.GetExtCommunityValue());

        attr_spec.push_back(&ext_comm);

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

        BgpTable *table = GetTable(instance_name);
        ASSERT_TRUE(table != NULL);
        table->Enqueue(&request);
        task_util::WaitForIdle();
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
        task_util::WaitForIdle();
    }

    void AddVpnRoute(IPeer *peer, const vector<string> &instance_names,
        const string &prefix, int localpref, string nexthop_str = "",
        uint32_t flags = 0, int label = 0,
        const LoadBalance &lb = LoadBalance()) {
        RoutingInstance *rtinstance =
            ri_mgr_->GetRoutingInstance(instance_names[0]);
        ASSERT_TRUE(rtinstance != NULL);
        int rti_index = rtinstance->index();

        string vpn_prefix;
        string peer_str = peer ? peer->ToString() :
                                 BuildNextHopAddress("7.7.7.7");
        vpn_prefix = peer_str + ":" + integerToString(rti_index) + ":" + prefix;

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
        task_util::WaitForIdle();
    }

    void DeleteVpnRoute(IPeer *peer, const string &instance_name,
        const string &prefix) {
        BgpTable *table = GetTable(instance_name);
        ASSERT_TRUE(table != NULL);
        const RoutingInstance *rtinstance = table->routing_instance();
        ASSERT_TRUE(rtinstance != NULL);
        int rti_index = rtinstance->index();

        string vpn_prefix;
        if (peer) {
            vpn_prefix = peer->ToString() + ":" + integerToString(rti_index) +
                ":" + prefix;
        } else {
            vpn_prefix = "7.7.7.7:" + integerToString(rti_index) + ":" + prefix;
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
        task_util::WaitForIdle();
    }

    BgpRoute *RouteLookup(const string &instance_name, const string &prefix) {
        BgpTable *table = GetTable(instance_name);
        EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return NULL;
        }
        boost::system::error_code error;
        PrefixT nlri = PrefixT::FromString(prefix, &error);
        EXPECT_FALSE(error);
        typename TableT::RequestKey key(nlri, NULL);
        return static_cast<BgpRoute *>(table->Find(&key));
    }

    bool CheckRouteExists(const string &instance_name, const string &prefix,
        size_t count) {
        task_util::TaskSchedulerLock lock;
        BgpRoute *rt = RouteLookup(instance_name, prefix);
        return (rt && rt->BestPath() && rt->count() == count);
    }

    void VerifyRouteExists(const string &instance_name, const string &prefix,
        size_t count = 1) {
        TASK_UTIL_EXPECT_TRUE(CheckRouteExists(instance_name, prefix, count));
    }

    bool CheckRouteIsDeleted(const string &instance_name,
        const string &prefix) {
        task_util::TaskSchedulerLock lock;
        BgpRoute *rt = RouteLookup(instance_name, prefix);
        return (rt && rt->IsDeleted() && rt->count() == 0);
    }

    void VerifyRouteIsDeleted(const string &instance_name,
        const string &prefix) {
        TASK_UTIL_EXPECT_TRUE(CheckRouteIsDeleted(instance_name, prefix));
    }

    bool CheckRouteNoExists(const string &instance_name, const string &prefix) {
        task_util::TaskSchedulerLock lock;
        BgpRoute *rt = RouteLookup(instance_name, prefix);
        return !rt;
    }

    void VerifyRouteNoExists(const string &instance_name,
        const string &prefix) {
        TASK_UTIL_EXPECT_TRUE(CheckRouteNoExists(instance_name, prefix));
    }

    bool MatchPathAttributes(const BgpPath *path, const string &path_id,
        const string &origin_vn, const set<string> &rtarget_list,
        const vector<string> &comm_list, const set<string> &encap_list,
        const LoadBalance &lb) {
        const BgpAttr *attr = path->GetAttr();
        if (attr->nexthop().to_v4().to_string() != path_id) {
            return false;
        }
        if (attr->as_path() != NULL) {
            return false;
        }
        const Community *community = attr->community();
        if (community == NULL) {
            return false;
        }
        if (community->communities().size() != comm_list.size() + 1) {
            return false;
        }
        if (!community->ContainsValue(CommunityType::AcceptOwnNexthop)) {
            return false;
        }
        BOOST_FOREACH(const string &comm, comm_list) {
            uint32_t value = CommunityType::CommunityFromString(comm);
            if (!community->ContainsValue(value)) {
                return false;
            }
        }
        if (GetOriginVnFromPath(path) != origin_vn) {
            return false;
        }
        if (rtarget_list.size() && GetRTargetFromPath(path) != rtarget_list) {
            return false;
        }
        if (encap_list.size() &&
            GetTunnelEncapListFromPath(path) != encap_list) {
            return false;
        }
        if (!lb.IsDefault() && LoadBalance(path) != lb) {
            return false;
        }

        return true;
    }

    bool CheckPathAttributes(const string &instance_name, const string &prefix,
        const string &path_id, const string &origin_vn,
        const set<string> &rtarget_list, const vector<string> &comm_list,
        const set<string> &encap_list, const LoadBalance &lb) {
        task_util::TaskSchedulerLock lock;
        BgpRoute *route = RouteLookup(instance_name, prefix);
        if (!route)
            return false;
        for (Route::PathList::iterator it = route->GetPathList().begin();
             it != route->GetPathList().end(); ++it) {
            const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
            if (BgpPath::PathIdString(path->GetPathId()) != path_id)
                continue;
            if (MatchPathAttributes(path, path_id, origin_vn, rtarget_list,
                comm_list, encap_list, lb)) {
                return true;
            }
            return false;
        }

        return false;
    }

    void VerifyPathAttributes(const string &instance_name, const string &prefix,
        const string &path_id, const string &origin_vn) {
        TASK_UTIL_EXPECT_TRUE(CheckPathAttributes(instance_name, prefix,
            path_id, origin_vn, set<string>(), vector<string>(), set<string>(),
            LoadBalance()));
    }

    void VerifyPathAttributes(const string &instance_name, const string &prefix,
        const string &path_id, const string &origin_vn,
        const set<string> &rtarget_list) {
        TASK_UTIL_EXPECT_TRUE(CheckPathAttributes(instance_name, prefix,
            path_id, origin_vn, rtarget_list, vector<string>(), set<string>(),
            LoadBalance()));
    }

    void VerifyPathAttributes(const string &instance_name, const string &prefix,
        const string &path_id, const string &origin_vn, const LoadBalance &lb) {
        TASK_UTIL_EXPECT_TRUE(CheckPathAttributes(instance_name, prefix,
            path_id, origin_vn, set<string>(), vector<string>(), set<string>(),
            lb));
    }

    void VerifyPathAttributes(const string &instance_name, const string &prefix,
        const string &path_id, const string &origin_vn,
        const set<string> &rtarget_list, const set<string> &encap_list) {
        TASK_UTIL_EXPECT_TRUE(CheckPathAttributes(instance_name, prefix,
            path_id, origin_vn, rtarget_list, vector<string>(), encap_list,
            LoadBalance()));
    }

    void VerifyPathAttributes(const string &instance_name, const string &prefix,
        const string &path_id, const string &origin_vn,
        const vector<string> &comm_list) {
        TASK_UTIL_EXPECT_TRUE(CheckPathAttributes(instance_name, prefix,
            path_id, origin_vn, set<string>(), comm_list, set<string>(),
            LoadBalance()));
    }

    set<string> GetRTargetFromPath(const BgpPath *path) {
        const BgpAttr *attr = path->GetAttr();
        const ExtCommunity *ext_community = attr->ext_community();
        set<string> rtlist;
        if (ext_community) {
            BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                          ext_community->communities()) {
                if (!ExtCommunity::is_route_target(comm))
                    continue;
                RouteTarget rtarget(comm);
                rtlist.insert(rtarget.ToString());
            }
        }
        return rtlist;
    }

    set<string> GetTunnelEncapListFromPath(const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        set<string> list;
        if (ext_comm) {
            BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                          ext_comm->communities()) {
                if (!ExtCommunity::is_tunnel_encap(comm))
                    continue;
                TunnelEncap encap(comm);
                list.insert(encap.ToXmppString());
            }
        }
        return list;
    }

    string GetOriginVnFromPath(const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        if  (!ext_comm)
            return "unresolved";
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
            ext_comm->communities()) {
            if (!ExtCommunity::is_origin_vn(comm))
                continue;
            OriginVn origin_vn(comm);
            return ri_mgr_->GetVirtualNetworkByVnIndex(origin_vn.vn_index());
        }
        return "unresolved";
    }

    string GetNextHopAddress(BgpAttrPtr attr) {
        return attr->nexthop().to_v4().to_string();
    }

    string FileRead(const string &filename) {
        ifstream file(filename.c_str());
        string content((istreambuf_iterator<char>(file)),
                       istreambuf_iterator<char>());
        return content;
    }

    auto_ptr<autogen::StaticRouteEntriesType> GetStaticRouteConfig(
        const string &filename) {
        auto_ptr<autogen::StaticRouteEntriesType> params(
            new autogen::StaticRouteEntriesType());
        string content;

        // Convert IPv4 Prefix to IPv6 for IPv6 tests
        if (family_ == Address::INET6) {
            ifstream input(filename.c_str());
            regex e1("(^.*?)(\\d+\\.\\d+\\.\\d+\\.\\d+)\\/(\\d+)(.*$)");
            regex e2("(^.*?)(\\d+\\.\\d+\\.\\d+\\.\\d+)(.*$)");
            for (string line; getline(input, line);) {
                boost::cmatch cm;
                if (regex_match(line.c_str(), cm, e1)) {
                    const string prefix(cm[2].first, cm[2].second);
                    content += string(cm[1].first, cm[1].second) +
                        BuildPrefix(prefix, atoi(string(cm[3].first,
                                                    cm[3].second).c_str())) +
                        string(cm[4].first, cm[4].second);
                } else if (regex_match(line.c_str(), cm, e2)) {
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

    void SetStaticRouteEntries(const string &instance_name,
        const string &filename) {
        auto_ptr<autogen::StaticRouteEntriesType> params =
            GetStaticRouteConfig(filename);
        ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_,
            "routing-instance", instance_name, "static-route-entries",
            params.release(), 0);
        task_util::WaitForIdle();
    }

    void ClearStaticRouteEntries(const string &instance_name) {
        ifmap_test_util::IFMapMsgPropertyDelete(&this->config_db_,
            "routing-instance", instance_name, "static-route-entries");
        task_util::WaitForIdle();
    }

    void SetVirtualNetworkNetworkId(const string &instance_name, int id) {
        autogen::VirtualNetwork::NtProperty *property =
            new autogen::VirtualNetwork::NtProperty;
        property->data = id;
        ifmap_test_util::IFMapMsgPropertyAdd(&this->config_db_,
            "virtual-network", instance_name, "virtual-network-network-id",
            property);
        task_util::WaitForIdle();
    }

    static void ValidateShowStaticRouteResponse(Sandesh *sandesh,
                                                const string &result,
                                                StaticRouteTest *self) {
        ShowStaticRouteResp *resp =
            dynamic_cast<ShowStaticRouteResp *>(sandesh);
        TASK_UTIL_EXPECT_NE((ShowStaticRouteResp *)NULL, resp);
        self->validate_done_ = true;

        TASK_UTIL_EXPECT_EQ(1, resp->get_static_route_entries().size());
        int i = 0;
        BOOST_FOREACH(const StaticRouteEntriesInfo &info,
                      resp->get_static_route_entries()) {
            TASK_UTIL_EXPECT_EQ(info.get_ri_name(), result);
            i++;
        }
    }

    void VerifyStaticRouteSandesh(const string &instance_name) {
        BgpSandeshContext sandesh_context;
        sandesh_context.bgp_server = bgp_server_.get();
        sandesh_context.xmpp_peer_manager = NULL;
        Sandesh::set_client_context(&sandesh_context);
        Sandesh::set_response_callback(
                boost::bind(ValidateShowStaticRouteResponse, _1, instance_name,
                    this));
        ShowStaticRouteReq *req = new ShowStaticRouteReq;
        req->set_search_string(instance_name);
        validate_done_ = false;
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
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

    BgpTable *GetTable(const string &instance_name) {
        return static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(GetTableName(instance_name)));
    }

    BgpTable *GetVpnTable() {
        return static_cast<BgpTable *>(bgp_server_->database()->FindTable(
                    GetVpnTableName()));
    }

    void VerifyTableNoExists(const string &table_name) {
        TASK_UTIL_EXPECT_EQ(static_cast<BgpTable *>(NULL),
                            GetTable(table_name));
    }

    void VerifyStaticRouteCount(uint32_t count) {
        ConcurrencyScope scope("bgp::ShowCommand");
        TASK_UTIL_EXPECT_EQ(count, bgp_server_->num_static_routes());
    }

    void VerifyDownStaticRouteCount(uint32_t count) {
        ConcurrencyScope scope("bgp::ShowCommand");
        TASK_UTIL_EXPECT_EQ(count, bgp_server_->num_down_static_routes());
    }

    void VerifyRoutingInstanceDeleted(const string &instance_name) {
        TASK_UTIL_EXPECT_TRUE(
            ri_mgr_->GetRoutingInstance(instance_name) != NULL);
        RoutingInstance *rtinstance =
            ri_mgr_->GetRoutingInstance(instance_name);
        TASK_UTIL_EXPECT_TRUE(rtinstance->deleted());
    }

    void VerifyRoutingInstanceDestroyed(const string &instance_name) {
        TASK_UTIL_EXPECT_TRUE(
            ri_mgr_->GetRoutingInstance(instance_name) == NULL);
    }

    void DisableDBQueueProcessing() {
        for (size_t idx = 0; idx < DB::PartitionCount(); ++idx) {
            DBPartition *partition = bgp_server_->database()->GetPartition(idx);
            task_util::TaskFire(
                boost::bind(&DBPartition::SetQueueDisable, partition, true),
                "bgp::Config");
        }
    }

    void EnableDBQueueProcessing() {
        for (size_t idx = 0; idx < DB::PartitionCount(); ++idx) {
            DBPartition *partition = bgp_server_->database()->GetPartition(idx);
            task_util::TaskFire(
                boost::bind(&DBPartition::SetQueueDisable, partition, false),
                "bgp::Config");
        }
    }

    EventManager evm_;
    DB config_db_;
    DBGraph config_graph_;
    boost::scoped_ptr<BgpServer> bgp_server_;
    Address::Family family_;
    string ipv6_prefix_;
    RoutingInstanceMgr *ri_mgr_;
    vector<BgpPeerMock *> peers_;
    bool validate_done_;
};

// Specialization of GetFamily for INET.
template<>
Address::Family StaticRouteTest<InetDefinition>::GetFamily() const {
    return Address::INET;
}

// Specialization of GetFamily for INET6.
template<>
Address::Family StaticRouteTest<Inet6Definition>::GetFamily() const {
    return Address::INET6;
}

// Instantiate fixture class template for each TypeDefinition.
typedef ::testing::Types <InetDefinition, Inet6Definition> TypeDefinitionList;
TYPED_TEST_CASE(StaticRouteTest, TypeDefinitionList);

TYPED_TEST(StaticRouteTest, InvalidNextHop) {
    vector<string> instance_names = {"nat"};
    this->NetworkConfig(instance_names);

    // Add Nexthop route.
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_9a.xml");
    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.3.0", 24));
    this->VerifyRouteNoExists("nat", this->BuildPrefix("192.168.2.0", 24));

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_9b.xml");
    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.2.0", 24));
    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.3.0", 24));
    this->VerifyRouteNoExists("nat", this->BuildPrefix("192.168.1.0", 24));

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_9c.xml");
    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.2.0", 24));
    this->VerifyRouteNoExists("nat", this->BuildPrefix("192.168.3.0", 24));

    // Delete nexthop route.
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
}

TYPED_TEST(StaticRouteTest, InvalidPrefix) {
    vector<string> instance_names = {"nat"};
    this->NetworkConfig(instance_names);

    // Add Nexthop route.
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_10a.xml");
    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.3.0", 24));

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_10b.xml");
    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.2.0", 24));
    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.3.0", 24));

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_10c.xml");
    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.2.0", 24));

    // Delete nexthop route.
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
}

TYPED_TEST(StaticRouteTest, InvalidRouteTarget) {
    vector<string> instance_names = {"nat"};
    this->NetworkConfig(instance_names);

    // Add Nexthop route.
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    set<string> rtarget_list;
    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_11a.xml");
    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    rtarget_list = list_of("target:64496:1")("target:64496:3")
        .convert_to_container<set<string> >();
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_11b.xml");
    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    rtarget_list = list_of("target:64496:2")("target:64496:3")
        .convert_to_container<set<string> >();
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_11c.xml");
    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    rtarget_list = list_of("target:64496:1")("target:64496:2")
        .convert_to_container<set<string> >();
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);

    // Delete nexthop route.
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
}

//
// Basic Test
// 1. Configure routing instance with static route property
// 2. Add the nexthop route
// 3. Validate the static route in both source (nat) and destination
// routing instance
TYPED_TEST(StaticRouteTest, Basic) {
    vector<string> instance_names = {"blue", "nat"};
    this->NetworkConfig(instance_names);

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_1.xml");
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add nexthop route.
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "blue");

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    set<string> rtarget_list = {"target:64496:1", "target:64496:2",
                                "target:64496:3"};
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);

    this->VerifyStaticRouteSandesh("nat");

    // Delete nexthop route.
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));

    // Check for Static route
    this->VerifyRouteNoExists("nat", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
}

// Basic Test 2
// 1. Configure routing instance with multiple static routes for same prefix
// 2. Add nexthop routes to make both static routes active.
// 3. Validate the static route in both source (nat) and destination ri
TYPED_TEST(StaticRouteTest, Basic2) {
    vector<string> instance_names = {"blue", "nat"};
    this->NetworkConfig(instance_names);

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_1b.xml");
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add nexthop routes.
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.253", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    // Verify that only one static route is now acitve.
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "blue");

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    set<string> rtarget_list = {"target:64496:1", "target:64496:2",
                                "target:64496:3"};
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);
    this->VerifyStaticRouteSandesh("nat");

    // Add second nexthop route.
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.6"));

    // Verify that both static routes are active.
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24), 2);
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "blue");
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.6"), "blue");

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24), 2);
    rtarget_list =
        list_of("target:64496:1")("target:64496:2")("target:64496:3")
            .convert_to_container<set<string> >();
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.6"), "unresolved", rtarget_list);

    this->VerifyStaticRouteSandesh("nat");

    // Delete one of the nexthop routes.
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.253", 32));

    // Verify that one active path still remains.
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.6"), "blue");

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    rtarget_list =
        list_of("target:64496:1")("target:64496:2")("target:64496:3")
            .convert_to_container<set<string> >();
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.6"), "unresolved", rtarget_list);

    this->VerifyStaticRouteSandesh("nat");

    // Remove the other nexthop route also.
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));

    // Ensure that static routes are no longer active.
    this->VerifyRouteNoExists("nat", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
}

TYPED_TEST(StaticRouteTest, UpdateRtList) {
    vector<string> instance_names = {"blue", "nat"};
    this->NetworkConfig(instance_names);

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_3.xml");

    this->VerifyRouteNoExists("nat", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add nexthop route.
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_1.xml");

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "blue");

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    set<string> rtarget_list = {"target:64496:1", "target:64496:2",
                                "target:64496:3"};
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_3.xml");

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    rtarget_list = list_of("target:1:1").convert_to_container<set<string> >();
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));

    this->VerifyRouteNoExists("nat", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
}

TYPED_TEST(StaticRouteTest, UpdateCommunityList) {
    vector<string> instance_names = {"blue", "nat"};
    this->NetworkConfig(instance_names);

    // Add nexthop route.
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_15a.xml");

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    vector<string> comm_list = {"64496:101", "64496:102"};
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "blue", comm_list);

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_15b.xml");

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    comm_list = list_of("64496:201")("64496:202")
        .convert_to_container<vector<string> >();
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "blue", comm_list);

    // Delete nexthop route.
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
}

TYPED_TEST(StaticRouteTest, UpdateNexthop) {
    vector<string> instance_names = {"blue", "nat"};
    this->NetworkConfig(instance_names);

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_1.xml");

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add nexthop route.
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "blue");

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    set<string> rtarget_list = {"target:64496:1", "target:64496:2",
                                "target:64496:3"};
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_4.xml");
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add nexthop route.
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.1", 32), 100,
        this->BuildNextHopAddress("5.4.3.2"));

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("5.4.3.2"), "blue");

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    rtarget_list = list_of("target:64496:1")
        .convert_to_container<set<string> >();
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("5.4.3.2"), "unresolved", rtarget_list);

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.1", 32));
}

//
// Duplicate prefix list
// 1. Configure multiple static routes with same prefix
// 2. Validate that only one of the routes is active
// 3. Now update the config with only one static route entry
// 4. Validate that static route is active with correct rtargets
//
TYPED_TEST(StaticRouteTest, DuplicatePrefix) {
    vector<string> instance_names = {"blue", "nat"};
    this->NetworkConfig(instance_names);

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_16.xml");
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add nexthop route.
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "blue");

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    set<string> rtarget_list = {"target:64496:1", "target:64496:2",
                                "target:64496:3"};
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);

    this->VerifyStaticRouteSandesh("nat");

    // Now update the config to contain only one prefix
    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_16a.xml");
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add the NEW nexthop route.
    this->AddRoute(NULL, "nat", this->BuildPrefix("1.1.1.1", 32), 100,
        this->BuildNextHopAddress("5.4.3.2"));

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    rtarget_list = list_of("target:1:1")("target:1:2")("target:1:3")
        .convert_to_container<set<string> >();
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("5.4.3.2"), "unresolved", rtarget_list);

    this->VerifyStaticRouteSandesh("nat");

    // Delete nexthop route.
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("1.1.1.1", 32));

    // Check for Static route
    this->VerifyRouteNoExists("nat", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
}


//
// Duplicate prefix list
// 1. Configure single static route
// 2. Validate that the static route is active
// 3. Now update the config with multiple static routes with same prefix
// 4. Validate that both static routes are active with correct rtargets
//
TYPED_TEST(StaticRouteTest, DuplicatePrefix_1) {
    vector<string> instance_names = {"blue", "nat"};
    this->NetworkConfig(instance_names);

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_16a.xml");
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add nexthop route.
    this->AddRoute(NULL, "nat", this->BuildPrefix("1.1.1.1", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    set<string> rtarget_list = {"target:1:1", "target:1:2", "target:1:3"};
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);

    this->VerifyStaticRouteSandesh("nat");

    // Now update the config to contain multiple static routes with same prefix
    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_16.xml");
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add nexthop route.
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("5.4.3.2"));

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("5.4.3.2"), "blue");

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24), 2);
    rtarget_list = list_of("target:64496:1")("target:64496:2")("target:64496:3")
        .convert_to_container<set<string> >();
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("5.4.3.2"), "unresolved", rtarget_list);

    this->VerifyStaticRouteSandesh("nat");

    // Delete nexthop route.
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("1.1.1.1", 32));

    // Check for Static route
    this->VerifyRouteNoExists("nat", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
}

TYPED_TEST(StaticRouteTest, MultiplePrefix) {
    vector<string> instance_names = {"blue", "nat"};
    this->NetworkConfig(instance_names);

    this->VerifyStaticRouteCount(0);
    this->VerifyDownStaticRouteCount(0);

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_2.xml");

    this->VerifyStaticRouteCount(3);
    this->VerifyDownStaticRouteCount(3);

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    this->VerifyStaticRouteCount(3);
    this->VerifyDownStaticRouteCount(2);

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "blue");

    set<string> rtarget_list = {"target:64496:1"};
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.2.1", 32), 100,
        this->BuildNextHopAddress("9.8.7.6"));

    this->VerifyStaticRouteCount(3);
    this->VerifyDownStaticRouteCount(0);

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.2.0", 24));
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.2.0", 24),
        this->BuildNextHopAddress("9.8.7.6"), "blue");

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.0.0", 16));
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.0.0", 16),
        this->BuildNextHopAddress("9.8.7.6"), "blue");

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.2.0", 24));
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.2.0", 24),
        this->BuildNextHopAddress("9.8.7.6"), "unresolved", rtarget_list);

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.0.0", 16));
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.0.0", 16),
        this->BuildNextHopAddress("9.8.7.6"), "unresolved", rtarget_list);

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.2.1", 32));

    this->VerifyStaticRouteCount(3);
    this->VerifyDownStaticRouteCount(3);
}

TYPED_TEST(StaticRouteTest, MultiplePrefixSameNexthopAndUpdateNexthop) {
    vector<string> instance_names = {"blue", "nat"};
    this->NetworkConfig(instance_names);

    set<string> rtarget_list = {"target:64496:1"};
    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_5.xml");

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.2.1", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.2.0", 24));
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.3.0", 24));

    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "blue");
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.2.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "blue");
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.3.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "blue");

    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.2.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.3.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);

    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.2.1", 32), 100,
        this->BuildNextHopAddress("5.3.4.5"));

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.2.0", 24));
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.3.0", 24));

    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("5.3.4.5"), "blue");
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.2.0", 24),
        this->BuildNextHopAddress("5.3.4.5"), "blue");
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.3.0", 24),
        this->BuildNextHopAddress("5.3.4.5"), "blue");

    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("5.3.4.5"), "unresolved", rtarget_list);
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.2.0", 24),
        this->BuildNextHopAddress("5.3.4.5"), "unresolved", rtarget_list);
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.3.0", 24),
        this->BuildNextHopAddress("5.3.4.5"), "unresolved", rtarget_list);

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.2.1", 32));

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.2.0", 24));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.3.0", 24));
}

//
// Test static route config on multiple routing instance
//
TYPED_TEST(StaticRouteTest, MultipleRoutingInstance) {
    vector<string> instance_names = {"nat-1", "nat-2"};
    this->NetworkConfig(instance_names);

    this->SetStaticRouteEntries("nat-1",
        "controller/src/bgp/testdata/static_route_14.xml");
    this->SetStaticRouteEntries("nat-2",
        "controller/src/bgp/testdata/static_route_1.xml");

    // Add Nexthop Route
    this->AddRoute(NULL, "nat-1", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));
    this->AddRoute(NULL, "nat-2", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    this->VerifyRouteExists("nat-2", this->BuildPrefix("192.168.1.0", 24));
    set<string> rtarget_list =
        list_of("target:64496:1")("target:64496:2")("target:64496:3");
    this->VerifyPathAttributes("nat-2", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);

    this->VerifyRouteExists("nat-1", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteExists("nat-1", this->BuildPrefix("1.1.0.0", 16));
    rtarget_list = list_of("target:1:1")("target:1:2")
        .convert_to_container<set<string> >();
    this->VerifyPathAttributes("nat-1", this->BuildPrefix("1.1.0.0", 16),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);

    this->VerifyStaticRouteSandesh("nat-1");
    this->VerifyStaticRouteSandesh("nat-2");

    this->ClearStaticRouteEntries("nat-1");
    this->ClearStaticRouteEntries("nat-2");

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat-1", this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(NULL, "nat-2", this->BuildPrefix("192.168.1.254", 32));
}

//
// Test static route config on multiple routing instance
// Test with unregister trigger disabled
//
TYPED_TEST(StaticRouteTest, MultipleRoutingInstance_DisableUnregisterTrigger1) {
    vector<string> instance_names = {"nat-1", "nat-2"};
    this->NetworkConfig(instance_names);

    this->SetStaticRouteEntries("nat-1",
        "controller/src/bgp/testdata/static_route_14.xml");
    this->SetStaticRouteEntries("nat-2",
        "controller/src/bgp/testdata/static_route_1.xml");

    // Add Nexthop Route
    this->AddRoute(NULL, "nat-1", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));
    this->AddRoute(NULL, "nat-2", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    this->VerifyRouteExists("nat-2", this->BuildPrefix("192.168.1.0", 24));
    set<string> rtarget_list =
        list_of("target:64496:1")("target:64496:2")("target:64496:3");
    this->VerifyPathAttributes("nat-2", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);

    this->VerifyRouteExists("nat-1", this->BuildPrefix("1.1.0.0", 16));
    rtarget_list = list_of("target:1:1")("target:1:2")
        .convert_to_container<set<string> >();
    this->VerifyPathAttributes("nat-1", this->BuildPrefix("1.1.0.0", 16),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);

    this->VerifyStaticRouteSandesh("nat-1");
    this->VerifyStaticRouteSandesh("nat-2");

    // Disable unregister trigger
    this->DisableUnregisterTrigger("nat-1");
    this->DisableUnregisterTrigger("nat-2");

    // Delete the static route config
    this->ClearStaticRouteEntries("nat-1");
    this->ClearStaticRouteEntries("nat-2");

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat-1", this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(NULL, "nat-2", this->BuildPrefix("192.168.1.254", 32));

    this->VerifyRouteNoExists("nat-2", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteNoExists("nat-1", this->BuildPrefix("1.1.0.0", 16));

    ifmap_test_util::IFMapMsgUnlink(
            &this->config_db_, "routing-instance", "nat-1",
            "virtual-network", "nat-1", "virtual-network-routing-instance");
    ifmap_test_util::IFMapMsgUnlink(&this->config_db_, "routing-instance",
            "nat-1", "route-target", "target:64496:1", "instance-target");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "virtual-network", "nat-1");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "routing-instance", "nat-1");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "route-target", "target:64496:1");

    ifmap_test_util::IFMapMsgUnlink(
            &this->config_db_, "routing-instance", "nat-2",
            "virtual-network", "nat-2", "virtual-network-routing-instance");
    ifmap_test_util::IFMapMsgUnlink(&this->config_db_, "routing-instance",
            "nat-2", "route-target", "target:64496:2", "instance-target");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "virtual-network", "nat-2");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "routing-instance", "nat-2");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "route-target", "target:64496:2");
    task_util::WaitForIdle();

    this->EnableUnregisterTrigger("nat-1");
    this->EnableUnregisterTrigger("nat-2");
}

//
// Test static route config on multiple routing instance
// Test with unregister trigger disabled
// In this test, routing instance destroy is delayed by holding a route
//
TYPED_TEST(StaticRouteTest, MultipleRoutingInstance_DisableUnregisterTrigger2) {
    vector<string> instance_names = {"nat-1", "nat-2"};
    this->NetworkConfig(instance_names);

    this->SetStaticRouteEntries("nat-1",
        "controller/src/bgp/testdata/static_route_14.xml");
    this->SetStaticRouteEntries("nat-2",
        "controller/src/bgp/testdata/static_route_1.xml");

    // Add Nexthop Route
    this->AddRoute(NULL, "nat-1", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));
    this->AddRoute(NULL, "nat-2", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    this->VerifyRouteExists("nat-2", this->BuildPrefix("192.168.1.0", 24));
    set<string> rtarget_list =
        list_of("target:64496:1")("target:64496:2")("target:64496:3");
    this->VerifyPathAttributes("nat-2", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);

    this->VerifyRouteExists("nat-1", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteExists("nat-1", this->BuildPrefix("1.1.0.0", 16));
    rtarget_list = list_of("target:1:1")("target:1:2")
        .convert_to_container<set<string> >();
    this->VerifyPathAttributes("nat-1", this->BuildPrefix("1.1.0.0", 16),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);

    this->VerifyStaticRouteSandesh("nat-1");
    this->VerifyStaticRouteSandesh("nat-2");

    // Disable unregister trigger
    this->DisableUnregisterTrigger("nat-1");
    this->DisableUnregisterTrigger("nat-2");

    // Delete the static route config
    this->ClearStaticRouteEntries("nat-1");
    this->ClearStaticRouteEntries("nat-2");

    this->VerifyRouteNoExists("nat-1", this->BuildPrefix("1.1.0.0", 16));
    this->VerifyRouteNoExists("nat-2", this->BuildPrefix("192.168.1.0", 24));

    ifmap_test_util::IFMapMsgUnlink(
            &this->config_db_, "routing-instance", "nat-1",
            "virtual-network", "nat-1", "virtual-network-routing-instance");
    ifmap_test_util::IFMapMsgUnlink(&this->config_db_, "routing-instance",
            "nat-1", "route-target", "target:64496:1", "instance-target");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "virtual-network", "nat-1");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "routing-instance", "nat-1");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "route-target", "target:64496:1");

    ifmap_test_util::IFMapMsgUnlink(
            &this->config_db_, "routing-instance", "nat-2",
            "virtual-network", "nat-2", "virtual-network-routing-instance");
    ifmap_test_util::IFMapMsgUnlink(&this->config_db_, "routing-instance",
            "nat-2", "route-target", "target:64496:2", "instance-target");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "virtual-network", "nat-2");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "routing-instance", "nat-2");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "route-target", "target:64496:2");
    task_util::WaitForIdle();

    this->VerifyRoutingInstanceDeleted("nat-1");
    this->VerifyRoutingInstanceDeleted("nat-2");

    // Since the nexthop route is not yet deleted, routing instance is
    // not destroyed.
    this->EnableUnregisterTrigger("nat-1");
    this->EnableUnregisterTrigger("nat-2");

    // Delete nexthop route.
    this->DeleteRoute(NULL, "nat-1", this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(NULL, "nat-2", this->BuildPrefix("192.168.1.254", 32));

    this->VerifyRoutingInstanceDestroyed("nat-1");
    this->VerifyRoutingInstanceDestroyed("nat-2");
}

TYPED_TEST(StaticRouteTest, ConfigUpdate) {
    vector<string> instance_names = {"blue", "nat", "red"};
    this->NetworkConfig(instance_names);

    set<string> rtarget_list = {"target:64496:1"};

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_6.xml");

    // Add nexthop route.
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.2.1", 32), 100,
        this->BuildNextHopAddress("3.4.5.6"));
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.3.1", 32), 100,
        this->BuildNextHopAddress("9.8.7.6"));

    // Check for Static route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.2.0", 24));
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.0.0", 16));

    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.2.0", 24),
        this->BuildNextHopAddress("9.8.7.6"), "unresolved", rtarget_list);
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.0.0", 16),
        this->BuildNextHopAddress("3.4.5.6"), "unresolved", rtarget_list);

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_7.xml");

    // Check for Static route
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.0.0", 24));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.2.0", 24));
    this->VerifyRouteExists("red", this->BuildPrefix("192.168.2.0", 24));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.3.0", 24));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.4.0", 24));

    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.4.1", 32), 100,
        this->BuildNextHopAddress("9.8.7.6"));

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.3.0", 24));
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.4.0", 24));

    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.3.0", 24),
        this->BuildNextHopAddress("9.8.7.6"), "unresolved", rtarget_list);
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.4.0", 24),
        this->BuildNextHopAddress("9.8.7.6"), "unresolved", rtarget_list);
    rtarget_list = list_of("target:64496:3")
        .convert_to_container<set<string> >();
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.2.0", 24),
        this->BuildNextHopAddress("9.8.7.6"), "unresolved", rtarget_list);

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.2.1", 32));
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.3.1", 32));
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.4.1", 32));
}

TYPED_TEST(StaticRouteTest, EcmpPathAdd) {
    vector<string> instance_names = {"blue", "nat"};
    this->NetworkConfig(instance_names);

    this->CreatePeers();

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_1.xml");

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add Nexthop Route
    this->AddRoute(this->peers_[0], "nat",
        this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "blue");

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    set<string> rtarget_list = {"target:64496:1", "target:64496:2",
                                "target:64496:3"};
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);

    this->DeleteRoute(this->peers_[0], "nat",
        this->BuildPrefix("192.168.1.254", 32));
    this->AddRoute(this->peers_[1], "nat",
        this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.1.5"));
    this->AddRoute(this->peers_[2], "nat",
        this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.2.5"));
    this->AddRoute(this->peers_[3], "nat",
        this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.3.5"));

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24), 3);
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.1.5"), "unresolved", rtarget_list);
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.2.5"), "unresolved", rtarget_list);
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.1.5"), "unresolved", rtarget_list);

    // Delete nexthop route
    this->DeleteRoute(
        this->peers_[1], "nat", this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(
        this->peers_[2], "nat", this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(
        this->peers_[3], "nat", this->BuildPrefix("192.168.1.254", 32));

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
}

TYPED_TEST(StaticRouteTest, EcmpPathDelete) {
    vector<string> instance_names = {"blue", "nat"};
    this->NetworkConfig(instance_names);

    this->CreatePeers();

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_1.xml");

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add Nexthop Route
    this->AddRoute(this->peers_[0], "nat",
        this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.1.5"));
    this->AddRoute(this->peers_[1], "nat",
        this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.2.5"));
    this->AddRoute(this->peers_[2], "nat",
        this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.3.5"));
    this->AddRoute(this->peers_[3], "nat",
        this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24), 4);
    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24), 4);
    set<string> rtarget_list = {"target:64496:1", "target:64496:2",
                                "target:64496:3"};
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.1.5"), "unresolved", rtarget_list);
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.2.5"), "unresolved", rtarget_list);
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.3.5"), "unresolved", rtarget_list);
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);

    this->DeleteRoute(this->peers_[0], "nat",
        this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(this->peers_[1], "nat",
        this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(this->peers_[2], "nat",
        this->BuildPrefix("192.168.1.254", 32));

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);

    // Delete nexthop route
    this->DeleteRoute(
        this->peers_[3], "nat", this->BuildPrefix("192.168.1.254", 32));

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
}

TYPED_TEST(StaticRouteTest, TunnelEncap) {
    vector<string> instance_names = {"blue", "nat"};
    this->NetworkConfig(instance_names);

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_1.xml");
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add Nexthop Route
    set<string> encap_list = {"gre", "vxlan"};
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"), encap_list);

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "blue");

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    set<string> rtarget_list =
        list_of("target:64496:1")("target:64496:2")("target:64496:3");
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list,
        encap_list);

    // Update Nexthop Route
    encap_list = list_of("udp").convert_to_container<set<string> >();
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"), encap_list);

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list,
        encap_list);

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
}

TYPED_TEST(StaticRouteTest, LoadBalance) {
    vector<string> instance_names = {"blue", "nat"};
    this->NetworkConfig(instance_names);

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_1.xml");

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Create non-default load balance attribute
    LoadBalance lb = LoadBalance();
    lb.SetL3SourceAddress(false);

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"), set<string>(),
        vector<uint32_t>(), 0, 0, lb);

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "blue");

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    set<string> rtarget_list = {"target:64496:1", "target:64496:2",
                                "target:64496:3"};
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", lb);

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "blue", lb);

    // Update Nexthop Route
    lb.SetL3DestinationAddress(false);
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"), set<string>(),
        vector<uint32_t>(), 0, 0, lb);

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", rtarget_list);
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved", lb);

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "blue", lb);

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
}


TYPED_TEST(StaticRouteTest, MultiPathTunnelEncap) {
    vector<string> instance_names = {"blue", "nat"};
    this->NetworkConfig(instance_names);

    this->CreatePeers();

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_1.xml");

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add Nexthop Route
    set<string> encap_1 = {"gre"};
    set<string> encap_2 = {"udp"};
    set<string> encap_3 = {"vxlan"};
    this->AddRoute(this->peers_[0], "nat",
        this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.1.5"), encap_1, vector<uint32_t>());
    this->AddRoute(this->peers_[1], "nat",
        this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.2.5"), encap_2, vector<uint32_t>());
    this->AddRoute(this->peers_[2], "nat",
        this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.3.5"), encap_3, vector<uint32_t>());

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24), 3);
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.1.5"), "blue");
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.2.5"), "blue");
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.3.5"), "blue");

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24), 3);
    set<string> rtarget_list = {"target:64496:1", "target:64496:2",
                                "target:64496:3"};
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.1.5"), "unresolved", rtarget_list,
        encap_1);
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.2.5"), "unresolved", rtarget_list,
        encap_2);
    this->VerifyPathAttributes("nat", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.3.5"), "unresolved", rtarget_list,
        encap_3);

    // Delete nexthop route
    this->DeleteRoute(
        this->peers_[0], "nat", this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(
        this->peers_[1], "nat", this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(
        this->peers_[2], "nat", this->BuildPrefix("192.168.1.254", 32));

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
}

TYPED_TEST(StaticRouteTest, DeleteEntryReuse) {
    vector<string> instance_names = {"blue", "nat"};
    this->NetworkConfig(instance_names);

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_1.xml");

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));

    this->DisableStaticRouteQ("nat");

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    this->VerifyRouteIsDeleted("nat", this->BuildPrefix("192.168.1.254", 32));

    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));
    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.254", 32));

    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    this->VerifyRouteIsDeleted("nat", this->BuildPrefix("192.168.1.254", 32));

    this->EnableStaticRouteQ("nat");

    TASK_UTIL_EXPECT_TRUE(this->IsQueueEmpty("nat"));

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
}

TYPED_TEST(StaticRouteTest, EntryAfterStop) {
    vector<string> instance_names = {"blue", "nat"};
    this->NetworkConfig(instance_names);

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_1.xml");

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));

    this->DisableStaticRouteQ("nat");
    this->ClearStaticRouteEntries("nat");
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 200,
        this->BuildNextHopAddress("2.3.4.5"));
    this->EnableStaticRouteQ("nat");

    TASK_UTIL_EXPECT_TRUE(this->IsQueueEmpty("nat"));

    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
}

//
// Delete the routing instance that imports the static route and make sure
// the inet table gets deleted. Objective is to check that the static route
// is removed from the table even though the static route config has not
// changed.
//
TYPED_TEST(StaticRouteTest, DeleteRoutingInstance) {
    vector<string> instance_names = {"blue", "nat"};
    this->NetworkConfig(instance_names);

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_1.xml");

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Delete the configuration for the blue instance.
    ifmap_test_util::IFMapMsgUnlink(
            &this->config_db_, "routing-instance", "blue",
            "virtual-network", "blue", "virtual-network-routing-instance");
    ifmap_test_util::IFMapMsgUnlink(&this->config_db_, "routing-instance",
            "blue", "route-target", "target:64496:1", "instance-target");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "virtual-network", "blue");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "routing-instance", "blue");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "route-target", "target:64496:1");
    task_util::WaitForIdle();

    // Make sure that the blue inet table is gone.
    this->VerifyTableNoExists(this->GetTableName("blue"));

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
}

//
// Delete static route config and instance with unregister_trigger disabled.
// Allow the routing instance to get deleted with unregister trigger running.
//
TYPED_TEST(StaticRouteTest, DeleteRoutingInstance_DisabledUnregisterTrigger1) {
    vector<string> instance_names = {"blue", "nat"};
    this->NetworkConfig(instance_names);

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_1.xml");

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Disable unregister processing
    this->DisableUnregisterTrigger("nat");

    // Delete the configuration for the nat instance.
    this->ClearStaticRouteEntries("nat");

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Delete the nat routing instance.
    ifmap_test_util::IFMapMsgUnlink(
            &this->config_db_, "routing-instance", "nat",
            "virtual-network", "nat", "virtual-network-routing-instance");
    ifmap_test_util::IFMapMsgUnlink(&this->config_db_, "routing-instance",
        "nat", "route-target", "target:64496:2", "instance-target");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "virtual-network", "nat");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "routing-instance", "nat");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "route-target", "target:64496:2");
    task_util::WaitForIdle();

    this->EnableUnregisterTrigger("nat");
}

//
// Delete static route config and instance with unregister_trigger disabled.
// Routing instance is not destroyed when the task trigger is enabled.
// Verify that enabling the task trigger ignores the deleted routing instance.
//
TYPED_TEST(StaticRouteTest, DeleteRoutingInstance_DisabledUnregisterTrigger2) {
    vector<string> instance_names = {"blue", "nat"};
    this->NetworkConfig(instance_names);

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_1.xml");

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    // Check for Static route
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Disable unregister processing
    this->DisableUnregisterTrigger("nat");

    // Delete the configuration for the nat instance.
    this->ClearStaticRouteEntries("nat");

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Delete the nat routing instance.
    ifmap_test_util::IFMapMsgUnlink(
            &this->config_db_, "routing-instance", "nat",
            "virtual-network", "nat", "virtual-network-routing-instance");
    ifmap_test_util::IFMapMsgUnlink(&this->config_db_, "routing-instance",
            "nat", "route-target", "target:64496:2", "instance-target");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "virtual-network", "nat");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "routing-instance", "nat");
    ifmap_test_util::IFMapMsgNodeDelete(
        &this->config_db_, "route-target", "target:64496:2");
    task_util::WaitForIdle();

    this->VerifyRoutingInstanceDeleted("nat");

    // Since the nexthop route is not yet deleted, routing instance is
    // not destroyed.
    this->EnableUnregisterTrigger("nat");

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
    this->VerifyRoutingInstanceDestroyed("nat");
}

//
// Add multiple VPN routes in different db partition
// With multiple routing instance having static route config, add nexthop routes
// for static route such that static routes can be added
// Above steps are done with
//      1. DBPartition queue disabled
//      2. Static route queue disabled
// Enable both static route queue and DBPartition queue and verify
//   1. static route is replicated to routing instance that import static route
//   2. all VPN routes are replicated to VRF table from bgp.l3vpn.0 table
// Repeat the above steps for delete of VPN routes and static routes
//
TYPED_TEST(StaticRouteTest, MultipleVpnRoutes) {
    vector<string> instance_names = {"blue", "nat-1", "nat-2"};
    this->NetworkConfig(instance_names);

    this->SetStaticRouteEntries("nat-1",
        "controller/src/bgp/testdata/static_route_1.xml");
    this->SetStaticRouteEntries("nat-2",
        "controller/src/bgp/testdata/static_route_14.xml");

    // Disable static route processing
    this->DisableStaticRouteQ("nat-1");
    this->DisableStaticRouteQ("nat-2");

    // Disable DBPartition processing on all DBPartition
    this->DisableDBQueueProcessing();

    // Add Nexthop Route
    this->AddRoute(NULL, "nat-1", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));
    this->AddRoute(NULL, "nat-2", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    // Add VPN routes to import to blue table
    vector<string> instances = {"blue"};
    for (int i = 0; i < 255; i++) {
        ostringstream oss;
        oss << "10.0.1." << i;
        string addr = oss.str();
        this->AddVpnRoute(NULL, instances, this->BuildPrefix(addr, 32), 100);
    }

    // Enable static route processing and DBPartition
    this->EnableDBQueueProcessing();

    this->EnableStaticRouteQ("nat-1");
    this->EnableStaticRouteQ("nat-2");

    TASK_UTIL_EXPECT_TRUE(this->IsQueueEmpty("nat-1"));
    TASK_UTIL_EXPECT_TRUE(this->IsQueueEmpty("nat-2"));
    for (int i = 0; i < DB::PartitionCount(); i++) {
        DBPartition *partition = this->bgp_server_->database()->GetPartition(i);
        TASK_UTIL_EXPECT_TRUE(partition->IsDBQueueEmpty());
    }

    // Check for Static route
    this->VerifyRouteExists("nat-1", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteExists("nat-2", this->BuildPrefix("1.1.0.0", 16));

    // Verify that vpn routes are replicated to blue
    for (int i = 0; i < 255; i++) {
        ostringstream oss;
        oss << "10.0.1." << i;
        string addr = oss.str();
        this->VerifyRouteExists("blue", this->BuildPrefix(addr, 32));
    }

    // Disable static route processing for delete operation
    this->DisableStaticRouteQ("nat-1");
    this->DisableStaticRouteQ("nat-2");

    // Disable DBPartition processing on all DBPartition
    this->DisableDBQueueProcessing();

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat-1", this->BuildPrefix("192.168.1.254", 32));
    this->DeleteRoute(NULL, "nat-2", this->BuildPrefix("192.168.1.254", 32));
    for (int i = 0; i < 255; i++) {
        ostringstream oss;
        oss << "10.0.1." << i;
        string addr = oss.str();
        this->DeleteVpnRoute(NULL, "blue", this->BuildPrefix(addr, 32));
    }

    // Enable static route processing and DBPartition
    this->EnableDBQueueProcessing();

    this->EnableStaticRouteQ("nat-1");
    this->EnableStaticRouteQ("nat-2");

    TASK_UTIL_EXPECT_TRUE(this->IsQueueEmpty("nat-1"));
    TASK_UTIL_EXPECT_TRUE(this->IsQueueEmpty("nat-2"));
    for (int i = 0; i < DB::PartitionCount(); i++) {
        DBPartition *partition = this->bgp_server_->database()->GetPartition(i);
        TASK_UTIL_EXPECT_TRUE(partition->IsDBQueueEmpty());
    }

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteNoExists("nat-2", this->BuildPrefix("1.1.0.0", 16));

    // Verify that vpn routes are no longer replicated to blue
    for (int i = 0; i < 255; i++) {
        ostringstream oss;
        oss << "10.0.1." << i;
        string addr = oss.str();
        this->VerifyRouteNoExists("blue", this->BuildPrefix(addr, 32));
    }
}

//
// Add the routing instance that imports the static route after the static
// route has already been added. Objective is to check that the static route
// is replicated to the table in the new instance without any triggers to
// the static route module.
//
TYPED_TEST(StaticRouteTest, AddRoutingInstance) {
    vector<string> instance_names = {"nat"};
    this->NetworkConfig(instance_names);

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_1.xml");

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));

    // Add the blue instance.
    // Make sure that the id and route target for nat instance don't change.
    instance_names = list_of("nat")("blue")
        .convert_to_container<vector<string> >();
    this->NetworkConfig(instance_names);

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));
}

//
// Validate static route functionality in VN's default routing instance
//
// 1. Configure VN's default routing instance with static route property
// 2. Add the nexthop route in the same instance
// 3. Validate the static route in the VN's default routing instance
//
TYPED_TEST(StaticRouteTest, DefaultRoutingInstance) {
    vector<string> instance_names = {"blue"};
    this->NetworkConfig(instance_names);

    this->SetStaticRouteEntries("blue",
        "controller/src/bgp/testdata/static_route_12.xml");

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));

    // Add Nexthop Route
    set<string> encap_list = {"gre", "udp"};
    this->AddRoute(NULL, "blue", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"), encap_list);

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    set<string> rtarget_list;
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "blue", rtarget_list, encap_list);

    this->VerifyStaticRouteSandesh("blue");

    // Delete nexthop route
    this->DeleteRoute(NULL, "blue", this->BuildPrefix("192.168.1.254", 32));

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
}

//
// Verify that a change in VN index is reflected in static routes for VN's
// default routing instance.
//
TYPED_TEST(StaticRouteTest, VirtualNetworkIndexChange) {
    vector<string> instance_names = {"blue"};
    this->NetworkConfig(instance_names);

    this->SetVirtualNetworkNetworkId("blue", 0);

    this->SetStaticRouteEntries("blue",
        "controller/src/bgp/testdata/static_route_12.xml");

    // Add Nexthop Route
    this->AddRoute(NULL, "blue", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "unresolved");

    this->SetVirtualNetworkNetworkId("blue", 1);

    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyPathAttributes("blue", this->BuildPrefix("192.168.1.0", 24),
        this->BuildNextHopAddress("2.3.4.5"), "blue");

    // Delete nexthop route
    this->DeleteRoute(NULL, "blue", this->BuildPrefix("192.168.1.254", 32));

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
}

// Sandesh introspect test
// Verify http introspect output
//   1. After creating the config and before nexthop route is published
//   2. After creating the config and after nexthop route is published
//   3. After updating the config(nexthop) and before new nexthop route is
//      published
//   4. After updating the config(nexthop) and after new nexthop route is
//      published
TYPED_TEST(StaticRouteTest, SandeshTest) {
    vector<string> instance_names = {"blue", "nat"};
    this->NetworkConfig(instance_names);

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_1.xml");

    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyStaticRouteSandesh("nat");

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32), 100,
        this->BuildNextHopAddress("2.3.4.5"));

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyStaticRouteSandesh("nat");

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.254", 32));

    this->SetStaticRouteEntries("nat",
        "controller/src/bgp/testdata/static_route_4.xml");

    this->VerifyRouteNoExists("nat", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteNoExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyStaticRouteSandesh("nat");

    // Add Nexthop Route
    this->AddRoute(NULL, "nat", this->BuildPrefix("192.168.1.1", 32), 100,
        this->BuildNextHopAddress("5.4.3.2"));

    this->VerifyRouteExists("nat", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyRouteExists("blue", this->BuildPrefix("192.168.1.0", 24));
    this->VerifyStaticRouteSandesh("nat");

    // Delete nexthop route
    this->DeleteRoute(NULL, "nat", this->BuildPrefix("192.168.1.1", 32));
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpObjectFactory::Register<BgpConfigManager>(
        boost::factory<BgpIfmapConfigManager *>());
}

static void TearDown() {
    task_util::WaitForIdle();
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
