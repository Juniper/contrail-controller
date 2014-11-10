/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <algorithm>

#include <boost/foreach.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/assign/list_of.hpp>
#include <pugixml/pugixml.hpp>

#include "base/test/task_test_util.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/community.h"
#include "bgp/inet/inet_table.h"
#include "bgp/extended-community/site_of_origin.h"
#include "bgp/l3vpn/inetvpn_route.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/service_chaining.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/routepath_replicator.h"
#include "bgp/security_group/security_group.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "bgp/test/bgp_test_util.h"
#include "control-node/control_node.h"
#include "db/db_graph.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/test/ifmap_test_util.h"
#include "io/event_manager.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

using boost::assign::list_of;
using boost::assign::map_list_of;
using pugi::xml_document;
using pugi::xml_node;
using pugi::xml_parse_result;
using std::auto_ptr;
using std::cout;
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
    BgpPeerMock(const Ip4Address &address) : address_(address) { }
    virtual ~BgpPeerMock() { }
    virtual string ToString() const {
        return address_.to_string();
    }
    virtual string ToUVEKey() const {
        return address_.to_string();
    }
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) {
        return true;
    }
    virtual BgpServer *server() {
        return NULL;
    }
    virtual IPeerClose *peer_close() {
        return NULL;
    }
    virtual IPeerDebugStats *peer_stats() {
        return NULL;
    }
    virtual bool IsReady() const {
        return true;
    }
    virtual bool IsXmppPeer() const { return false; }
    virtual void Close() {
    }
    BgpProto::BgpPeerType PeerType() const {
        return BgpProto::IBGP;
    }
    virtual uint32_t bgp_identifier() const {
        return htonl(address_.to_ulong());
    }
    virtual const string GetStateName() const {
        return "";
    }
    virtual void UpdateRefCount(int count) const { }
    virtual tbb::atomic<int> GetRefCount() const {
        tbb::atomic<int> count;
        count = 0;
        return count;
    }

private:
    Ip4Address address_;
};

class ServiceChainTest : public ::testing::Test {
protected:
    ServiceChainTest()
        : bgp_server_(new BgpServer(&evm_)),
          parser_(&config_db_),
          ri_mgr_(NULL),
          service_is_transparent_(false),
          connected_rt_is_inetvpn_(false) {
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

        IFMapServerParser *parser = IFMapServerParser::GetInstance("schema");
        bgp_schema_ParserInit(parser);
        vnc_cfg_ParserInit(parser);
        bgp_server_->config_manager()->Initialize(
            &config_db_, &config_graph_, "local");
        bgp_server_->service_chain_mgr()->set_aggregate_host_route(true);
        ri_mgr_ = bgp_server_->routing_instance_mgr();
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        bgp_server_->Shutdown();
        task_util::WaitForIdle();
        db_util::Clear(&config_db_);
        IFMapServerParser *parser = IFMapServerParser::GetInstance("schema");
        parser->MetadataClear("schema");
    }

    void NetworkConfig(const vector<string> &instance_names,
                       const multimap<string, string> &connections) {
        string netconf(
            bgp_util::NetworkConfigGenerate(instance_names, connections));
        IFMapServerParser *parser = IFMapServerParser::GetInstance("schema");
        parser->Receive(&config_db_, netconf.data(), netconf.length(), 0);
        task_util::WaitForIdle();
    }

    string ParseConfigFile(const string &filename) {
        string content = FileRead(filename);
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

    void DisableServiceChainQ() {
        bgp_server_->service_chain_mgr()->DisableQueue();
    }

    void EnableServiceChainQ() {
        bgp_server_->service_chain_mgr()->EnableQueue();
    }

    void AddInetRoute(IPeer *peer, const string &instance_name,
                      const string &prefix, int localpref, 
                      vector<uint32_t> commlist = vector<uint32_t>(),
                      vector<uint32_t> sglist = vector<uint32_t>(),
                      set<string> encap = set<string>(),
                      const SiteOfOrigin &soo = SiteOfOrigin(),
                      string nexthop = "7.8.9.1",
                      uint32_t flags = 0, int label = 0) {
        boost::system::error_code error;
        Ip4Prefix nlri = Ip4Prefix::FromString(prefix, &error);
        EXPECT_FALSE(error);
        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(new InetTable::RequestKey(nlri, peer));

        BgpAttrSpec attr_spec;
        boost::scoped_ptr<BgpAttrLocalPref> local_pref(
                new BgpAttrLocalPref(localpref));
        attr_spec.push_back(local_pref.get());

        IpAddress chain_addr = Ip4Address::from_string(nexthop, error);
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
        attr_spec.push_back(&ext_comm);

        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attr_spec);

        request.data.reset(new BgpTable::RequestData(attr, flags, label));
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(instance_name + ".inet.0"));
        ASSERT_TRUE(table != NULL);
        table->Enqueue(&request);
    }

    void AddInetRoute(IPeer *peer, const string &instance_name,
                      const string &prefix, int localpref,
                      const SiteOfOrigin &soo) {
        AddInetRoute(peer, instance_name, prefix, localpref,
            vector<uint32_t>(), vector<uint32_t>(), set<string>(), soo);
    }

    void DeleteInetRoute(IPeer *peer, const string &instance_name,
                         const string &prefix) {
        boost::system::error_code error;
        Ip4Prefix nlri = Ip4Prefix::FromString(prefix, &error);
        EXPECT_FALSE(error);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        request.key.reset(new InetTable::RequestKey(nlri, peer));

        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(instance_name + ".inet.0"));
        ASSERT_TRUE(table != NULL);

        table->Enqueue(&request);
    }

    void AddInetVpnRoute(IPeer *peer, const string &instance_name,
                         const string &prefix, int localpref,
                         vector<uint32_t> sglist = vector<uint32_t>(),
                         set<string> encap = set<string>(),
                         string nexthop = "7.8.9.1",
                         uint32_t flags = 0, int label = 0) {
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(instance_name + ".inet.0"));
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
        InetVpnPrefix nlri = InetVpnPrefix::FromString(vpn_prefix, &error);
        EXPECT_FALSE(error);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(new InetVpnTable::RequestKey(nlri, peer));

        BgpAttrSpec attr_spec;
        boost::scoped_ptr<BgpAttrLocalPref> local_pref(
                new BgpAttrLocalPref(localpref));
        attr_spec.push_back(local_pref.get());

        IpAddress chain_addr = Ip4Address::from_string(nexthop, error);
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
            extcomm_spec.communities.push_back(tunnel_encap.GetExtCommunityValue());
        }
        const RoutingInstance *rti = ri_mgr_->GetRoutingInstance(instance_name);
        TASK_UTIL_EXPECT_NE(0, rti->virtual_network_index());
        OriginVn origin_vn(0, rti->virtual_network_index());
        extcomm_spec.communities.push_back(origin_vn.GetExtCommunityValue());
        attr_spec.push_back(&extcomm_spec);

        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attr_spec);

        request.data.reset(new BgpTable::RequestData(attr, flags, label));
        InetVpnTable *inetvpn_table = dynamic_cast<InetVpnTable *>(
            bgp_server_->database()->FindTable("bgp.l3vpn.0"));
        ASSERT_TRUE(inetvpn_table != NULL);
        inetvpn_table->Enqueue(&request);
    }

    void AddInetVpnRoute(IPeer *peer, const vector<string> &instance_names,
                         const string &prefix, int localpref,
                         string nexthop = "7.8.9.1",
                         uint32_t flags = 0, int label = 0) {
        RoutingInstance *rtinstance =
            ri_mgr_->GetRoutingInstance(instance_names[0]);
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
        InetVpnPrefix nlri = InetVpnPrefix::FromString(vpn_prefix, &error);
        EXPECT_FALSE(error);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(new InetVpnTable::RequestKey(nlri, peer));

        BgpAttrSpec attr_spec;
        boost::scoped_ptr<BgpAttrLocalPref> local_pref(
                new BgpAttrLocalPref(localpref));
        attr_spec.push_back(local_pref.get());

        IpAddress chain_addr = Ip4Address::from_string(nexthop, error);
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
        attr_spec.push_back(&extcomm_spec);
        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attr_spec);

        request.data.reset(new BgpTable::RequestData(attr, flags, label));
        InetVpnTable *inetvpn_table = dynamic_cast<InetVpnTable *>(
            bgp_server_->database()->FindTable("bgp.l3vpn.0"));
        ASSERT_TRUE(inetvpn_table != NULL);
        inetvpn_table->Enqueue(&request);
    }

    void DeleteInetVpnRoute(IPeer *peer, const string &instance_name,
                            const string &prefix) {
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(instance_name + ".inet.0"));
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
        InetVpnPrefix nlri = InetVpnPrefix::FromString(vpn_prefix, &error);
        EXPECT_FALSE(error);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        request.key.reset(new InetVpnTable::RequestKey(nlri, peer));

        InetVpnTable *inetvpn_table = dynamic_cast<InetVpnTable *>(
            bgp_server_->database()->FindTable("bgp.l3vpn.0"));
        ASSERT_TRUE(inetvpn_table != NULL);
        inetvpn_table->Enqueue(&request);
    }

    void AddConnectedRoute(IPeer *peer, const string &prefix,
                   int localpref, string nexthop = "7.8.9.1",
                   uint32_t flags = 0, int label = 0,
                   vector<uint32_t> sglist = vector<uint32_t>(),
                   set<string> encap = set<string>()) {
        string connected_table = service_is_transparent_ ? "blue-i1" : "blue";
        if (connected_rt_is_inetvpn_) {
            AddInetVpnRoute(peer, connected_table, prefix, localpref,
                sglist, encap, nexthop, flags, label);
        } else {
            AddInetRoute(peer, connected_table, prefix, localpref,
                vector<uint32_t>(), sglist, encap, SiteOfOrigin(), nexthop,
                flags, label);
        }
        task_util::WaitForIdle();
    }

    void AddConnectedRoute(int chain_idx, IPeer *peer, const string &prefix,
                   int localpref, string nexthop = "7.8.9.1",
                   uint32_t flags = 0, int label = 0,
                   vector<uint32_t> sglist = vector<uint32_t>(),
                   set<string> encap = set<string>()) {
        assert(1 <= chain_idx && chain_idx <= 3);
        string connected_table;
        if (chain_idx == 1) {
            connected_table = service_is_transparent_ ? "blue-i1" : "blue";
        } else if (chain_idx == 2) {
            connected_table = service_is_transparent_ ? "core-i3" : "core";
        } else if (chain_idx == 3) {
            connected_table = service_is_transparent_ ? "core-i5" : "core";
        }
        if (connected_rt_is_inetvpn_) {
            AddInetVpnRoute(peer, connected_table, prefix,
                    localpref, sglist, encap, nexthop, flags, label);
        } else {
            AddInetRoute(peer, connected_table, prefix, localpref,
                vector<uint32_t>(), sglist, encap, SiteOfOrigin(), nexthop,
                flags, label);
        }
        task_util::WaitForIdle();
    }

    void DeleteConnectedRoute(IPeer *peer, const string &prefix) {
        string connected_table = service_is_transparent_ ? "blue-i1" : "blue";
        if (connected_rt_is_inetvpn_) {
            DeleteInetVpnRoute(peer, connected_table, prefix);
        } else {
            DeleteInetRoute(peer, connected_table, prefix);
        }
        task_util::WaitForIdle();
    }

    void DeleteConnectedRoute(int chain_idx, IPeer *peer, const string &prefix) {
        assert(1 <= chain_idx && chain_idx <= 3);
        string connected_table;
        if (chain_idx == 1) {
            connected_table = service_is_transparent_ ? "blue-i1" : "blue";
        } else if (chain_idx == 2) {
            connected_table = service_is_transparent_ ? "core-i3" : "core";
        } else if (chain_idx == 3) {
            connected_table = service_is_transparent_ ? "core-i5" : "core";
        }
        if (connected_rt_is_inetvpn_) {
            DeleteInetVpnRoute(peer, connected_table, prefix);
        } else {
            DeleteInetRoute(peer, connected_table, prefix);
        }
        task_util::WaitForIdle();
    }

    int RouteCount(const string &instance_name) const {
        string tablename(instance_name);
        tablename.append(".inet.0");
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(tablename));
        EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return 0;
        }
        return table->Size();
    }

    BgpRoute *InetRouteLookup(const string &instance_name, 
                              const string &prefix) {
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(instance_name + ".inet.0"));
        EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return NULL;
        }
        boost::system::error_code error;
        Ip4Prefix nlri = Ip4Prefix::FromString(prefix, &error);
        EXPECT_FALSE(error);
        InetTable::RequestKey key(nlri, NULL);
        BgpRoute *rt = static_cast<BgpRoute *>(table->Find(&key));
        return rt;
    }

    BgpRoute *VerifyInetRouteExists(const string &instance,
                                    const string &prefix) {
        TASK_UTIL_EXPECT_TRUE(InetRouteLookup(instance, prefix) != NULL);
        BgpRoute *rt = InetRouteLookup(instance, prefix);
        TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
        return rt;
    }

    void VerifyInetRouteNoExists(const string &instance,
                                 const string &prefix) {
        TASK_UTIL_EXPECT_TRUE(InetRouteLookup(instance, prefix) == NULL);
    }

    void VerifyInetRouteIsDeleted(const string &instance,
                                  const string &prefix) {
        TASK_UTIL_EXPECT_TRUE(InetRouteLookup(instance, prefix) != NULL);
        BgpRoute *rt = InetRouteLookup(instance, prefix);
        TASK_UTIL_EXPECT_TRUE(rt->IsDeleted());
    }

    bool MatchInetPathAttributes(const BgpPath *path,
        const string &path_id, const string &origin_vn, int label,
        const vector<uint32_t> sg_ids, const set<string> tunnel_encaps,
        const SiteOfOrigin &soo, const vector<uint32_t> &commlist) {
        BgpAttrPtr attr = path->GetAttr();
        if (attr->nexthop().to_v4().to_string() != path_id)
            return false;
        if (GetOriginVnFromRoute(path) != origin_vn)
            return false;
        if (label && path->GetLabel() != label)
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

        vector<uint32_t> path_commlist = GetCommunityListFromRoute(path);
        if (path_commlist != commlist)
            return false;

        return true;
    }

    bool CheckInetPathAttributes(const string &instance, const string &prefix,
        const string &path_id, const string &origin_vn, int label,
        const vector<uint32_t> sg_ids, const set<string> tunnel_encaps,
        const SiteOfOrigin &soo, const vector<uint32_t> &commlist) {
        task_util::TaskSchedulerLock lock;
        BgpRoute *route = InetRouteLookup(instance, prefix);
        if (!route)
            return false;
        for (Route::PathList::iterator it = route->GetPathList().begin();
             it != route->GetPathList().end(); ++it) {
            const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
            if (BgpPath::PathIdString(path->GetPathId()) != path_id)
                continue;
            if (MatchInetPathAttributes(path, path_id, origin_vn, label,
                sg_ids, tunnel_encaps, soo, commlist)) {
                return true;
            }
            return false;
        }

        return false;
    }

    void VerifyInetPathAttributes(const string &instance,
        const string &prefix, const string &path_id, const string &origin_vn,
        const set<string> tunnel_encaps) {
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_TRUE(CheckInetPathAttributes(instance, prefix,
            path_id, origin_vn, 0, vector<uint32_t>(), tunnel_encaps,
            SiteOfOrigin(), vector<uint32_t>()));
    }

    bool CheckInetRouteAttributes(const string &instance, const string &prefix,
        const vector<string> &path_ids, const string &origin_vn, int label,
        const vector<uint32_t> sg_ids, const set<string> tunnel_encap,
        const SiteOfOrigin &soo, const vector<uint32_t> &commlist) {
        task_util::TaskSchedulerLock lock;
        BgpRoute *route = InetRouteLookup(instance, prefix);
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
                if (MatchInetPathAttributes(path, path_id, origin_vn, label,
                    sg_ids, tunnel_encap, soo, commlist)) {
                    break;
                }
                return false;
            }
            if (!found)
                return false;
        }

        return true;
    }

    void VerifyInetRouteAttributes(const string &instance,
        const string &prefix, const string &path_id, const string &origin_vn,
        int label = 0) {
        task_util::WaitForIdle();
        vector<string> path_ids = list_of(path_id);
        TASK_UTIL_EXPECT_TRUE(CheckInetRouteAttributes(
            instance, prefix, path_ids, origin_vn, label, vector<uint32_t>(),
            set<string>(), SiteOfOrigin(), vector<uint32_t>()));
    }

    void VerifyInetRouteAttributes(const string &instance, const string &prefix,
        const vector<string> &path_ids, const string &origin_vn) {
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_TRUE(CheckInetRouteAttributes(
            instance, prefix, path_ids, origin_vn, 0, vector<uint32_t>(),
            set<string>(), SiteOfOrigin(), vector<uint32_t>()));
    }

    void VerifyInetRouteAttributes(const string &instance,
        const string &prefix, const string &path_id, const string &origin_vn,
        const vector<uint32_t> sg_ids) {
        task_util::WaitForIdle();
        vector<string> path_ids = list_of(path_id);
        TASK_UTIL_EXPECT_TRUE(CheckInetRouteAttributes(
            instance, prefix, path_ids, origin_vn, 0, sg_ids, set<string>(),
            SiteOfOrigin(), vector<uint32_t>()));
    }

    void VerifyInetRouteAttributes(const string &instance,
        const string &prefix, const string &path_id, const string &origin_vn,
        const set<string> tunnel_encaps) {
        task_util::WaitForIdle();
        vector<string> path_ids = list_of(path_id);
        TASK_UTIL_EXPECT_TRUE(CheckInetRouteAttributes(
            instance, prefix, path_ids, origin_vn, 0, vector<uint32_t>(),
            tunnel_encaps, SiteOfOrigin(), vector<uint32_t>()));
    }

    void VerifyInetRouteAttributes(const string &instance,
        const string &prefix, const string &path_id, const string &origin_vn,
        const SiteOfOrigin &soo) {
        task_util::WaitForIdle();
        vector<string> path_ids = list_of(path_id);
        TASK_UTIL_EXPECT_TRUE(CheckInetRouteAttributes(
            instance, prefix, path_ids, origin_vn, 0, vector<uint32_t>(),
            set<string>(), soo, vector<uint32_t>()));
    }

    void VerifyInetRouteAttributes(const string &instance,
        const string &prefix, const string &path_id, const string &origin_vn,
        const CommunitySpec &commspec) {
        task_util::WaitForIdle();
        vector<string> path_ids = list_of(path_id);
        TASK_UTIL_EXPECT_TRUE(CheckInetRouteAttributes(
            instance, prefix, path_ids, origin_vn, 0, vector<uint32_t>(),
            set<string>(), SiteOfOrigin(), commspec.communities));
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

    auto_ptr<autogen::ServiceChainInfo>
        GetChainConfig(string filename) {
        auto_ptr<autogen::ServiceChainInfo>
            params (new autogen::ServiceChainInfo());
        string content = FileRead(filename);
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
            instance, "service-chain-information", params.release(), 0);
        task_util::WaitForIdle();
    }

    void ClearServiceChainInformation(const string &instance) {
        ifmap_test_util::IFMapMsgPropertyDelete(&config_db_, "routing-instance",
            instance, "service-chain-information");
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
                                                 vector<string> &result) {
        ShowServiceChainResp *resp = 
            dynamic_cast<ShowServiceChainResp *>(sandesh);
        TASK_UTIL_EXPECT_NE((ShowServiceChainResp *)NULL, resp);
        validate_done_ = 1;

        TASK_UTIL_EXPECT_EQ(result.size(), 
                              resp->get_service_chain_list().size());
        int i = 0;
        cout << "*******************************************************"<<endl;
        BOOST_FOREACH(const ShowServicechainInfo &info, 
                      resp->get_service_chain_list()) {
            TASK_UTIL_EXPECT_EQ(info.get_src_rt_instance(), result[i]);
            cout << info.log() << endl;
            i++;
        }
        cout << "*******************************************************"<<endl;
    }

    static void ValidateShowPendingServiceChainResponse(Sandesh *sandesh, 
                                                 vector<string> &result) {
        ShowPendingServiceChainResp *resp = 
            dynamic_cast<ShowPendingServiceChainResp *>(sandesh);
        TASK_UTIL_EXPECT_NE((ShowPendingServiceChainResp *)NULL, resp);

        TASK_UTIL_EXPECT_TRUE((result == resp->get_pending_chains()));

        validate_done_ = 1;
    }


    void VerifyServiceChainSandesh(vector<string> result) {
        BgpSandeshContext sandesh_context;
        sandesh_context.bgp_server = bgp_server_.get();
        sandesh_context.xmpp_peer_manager = NULL;
        Sandesh::set_client_context(&sandesh_context);
        Sandesh::set_response_callback(boost::bind(ValidateShowServiceChainResponse,
                                                   _1, result));
        ShowServiceChainReq *req = new ShowServiceChainReq;
        validate_done_ = 0;
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_EQ(1, validate_done_);
    }

    void VerifyPendingServiceChainSandesh(vector<string> pending) {
        BgpSandeshContext sandesh_context;
        sandesh_context.bgp_server = bgp_server_.get();
        sandesh_context.xmpp_peer_manager = NULL;
        Sandesh::set_client_context(&sandesh_context);
        Sandesh::set_response_callback(boost::bind(ValidateShowPendingServiceChainResponse,
                                                   _1, pending));
        ShowPendingServiceChainReq *req = new ShowPendingServiceChainReq;
        validate_done_ = 0;
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_EQ(1, validate_done_);
    }

    EventManager evm_;
    DB config_db_;
    DBGraph config_graph_;
    boost::scoped_ptr<BgpServer> bgp_server_;
    BgpConfigParser parser_;
    RoutingInstanceMgr *ri_mgr_;
    vector<BgpPeerMock *> peers_;
    bool service_is_transparent_;
    bool connected_rt_is_inetvpn_;
    static int validate_done_;
};

int ServiceChainTest::validate_done_;
// Parameterize the service type (transparent vs. in-network).

typedef std::tr1::tuple<bool, bool> TestParams;

class ServiceChainParamTest :
    public ServiceChainTest,
    public ::testing::WithParamInterface<TestParams> {
    virtual void SetUp() {
        service_is_transparent_ = std::tr1::get<0>(GetParam());
        connected_rt_is_inetvpn_ = std::tr1::get<1>(GetParam());
        ServiceChainTest::SetUp();
    }

    virtual void TearDown() {
        ServiceChainTest::TearDown();
    }
};


TEST_P(ServiceChainParamTest, Basic) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);

    // Check for aggregated route
    VerifyInetRouteNoExists("blue", "192.168.1.0/24");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "2.3.4.5", "red");

    // Delete More specific
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");

    // Delete connected route
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
}

TEST_P(ServiceChainParamTest, MoreSpecificAddDelete) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);

    // Check for aggregated route
    VerifyInetRouteNoExists("blue", "192.168.1.0/24");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");

    // Delete More specific
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");

    // Check for aggregated route
    VerifyInetRouteNoExists("blue", "192.168.1.0/24");

    // Add different more specific
    AddInetRoute(NULL, "red", "192.168.1.34/32", 100);

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");

    // Add different more specific
    AddInetRoute(NULL, "red", "192.168.2.34/32", 100);

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");

    // Delete More specific & connected
    DeleteInetRoute(NULL, "red", "192.168.2.34/32");
    DeleteInetRoute(NULL, "red", "192.168.1.34/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
}

TEST_P(ServiceChainParamTest, ConnectedAddDelete) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific & connected
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");

    // Delete connected route
    DeleteConnectedRoute(NULL, "1.1.2.3/32");

    // Check for aggregated route
    VerifyInetRouteNoExists("blue", "192.168.1.0/24");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");

    // Delete More specific & connected
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
}


TEST_P(ServiceChainParamTest, DeleteConnected) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific & connected
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");

    // Delete connected route
    DeleteConnectedRoute(NULL, "1.1.2.3/32");

    // Check for aggregated route
    VerifyInetRouteNoExists("blue", "192.168.1.0/24");

    AddInetRoute(NULL, "red", "192.168.2.1/32", 100);

    // Check for aggregated route
    VerifyInetRouteNoExists("blue", "192.168.1.0/24");
    VerifyInetRouteNoExists("blue", "192.168.2.0/24");

    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteExists("blue", "192.168.2.0/24");

    // Delete More specific & connected
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteInetRoute(NULL, "red", "192.168.2.1/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
}

TEST_P(ServiceChainParamTest, StopServiceChain) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific & connected
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");

    ClearServiceChainInformation("blue-i1");

    // Check for aggregated route
    VerifyInetRouteNoExists("blue", "192.168.1.0/24");

    // Delete More specific & connected
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
}

TEST_P(ServiceChainParamTest, ServiceChainWithExistingRouteEntries) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    // Add More specific & connected
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddInetRoute(NULL, "red", "192.168.1.2/32", 100);
    AddInetRoute(NULL, "red", "192.168.1.3/32", 100);
    AddInetRoute(NULL, "red", "192.168.1.4/32", 100);
    AddInetRoute(NULL, "red", "192.168.2.1/32", 100);
    AddInetRoute(NULL, "red", "192.168.2.2/32", 100);
    AddInetRoute(NULL, "red", "192.168.2.3/32", 100);
    AddInetRoute(NULL, "red", "192.168.2.4/32", 100);
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteExists("blue", "192.168.2.0/24");

    ClearServiceChainInformation("blue-i1");

    // Check for aggregated route
    VerifyInetRouteNoExists("blue", "192.168.1.0/24");
    VerifyInetRouteNoExists("blue", "192.168.1.0/24");

    // Delete More specific & connected
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteInetRoute(NULL, "red", "192.168.1.2/32");
    DeleteInetRoute(NULL, "red", "192.168.1.3/32");
    DeleteInetRoute(NULL, "red", "192.168.1.4/32");
    DeleteInetRoute(NULL, "red", "192.168.2.1/32");
    DeleteInetRoute(NULL, "red", "192.168.2.2/32");
    DeleteInetRoute(NULL, "red", "192.168.2.3/32");
    DeleteInetRoute(NULL, "red", "192.168.2.4/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
}

TEST_P(ServiceChainParamTest, UpdateNexthop) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific & Connected
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "2.3.4.5", "red");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "3.4.5.6");

    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "3.4.5.6", "red");

    // Delete More specific & connected
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
}


TEST_P(ServiceChainParamTest, UpdateLabel) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific & Connected
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5", 0, 16);

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "2.3.4.5", "red", 16);

    // Add Connected with updated label
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5", 0, 32);

    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "2.3.4.5", "red", 32);

    // Delete More specific & connected
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
}

TEST_P(ServiceChainParamTest, DeleteRoutingInstance) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific & Connected
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");

    RemoveRoutingInstance("blue-i1", "blue");

    // Delete More specific & connected
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
}


TEST_P(ServiceChainParamTest, PendingChain) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Check for aggregated route
    VerifyInetRouteNoExists("blue", "192.168.1.0/24");

    VerifyPendingServiceChainSandesh(list_of("blue-i1"));

    // Add "red" routing instance and create connection with "red-i2"
    instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    connections = map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    // Add MoreSpecific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");

    // Delete More specific & connected
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
}

TEST_P(ServiceChainParamTest, UnresolvedPendingChain) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    VerifyPendingServiceChainSandesh(list_of("blue-i1"));

    ClearServiceChainInformation("blue-i1");

    // Delete connected
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
}

TEST_P(ServiceChainParamTest, UpdateChain) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_3.xml");

    // Add More specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddInetRoute(NULL, "red", "192.169.2.1/32", 100);

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteExists("blue", "192.169.2.0/24");

    VerifyServiceChainSandesh(list_of("blue-i1"));

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_2.xml");

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.0.0/16");
    VerifyInetRouteExists("blue", "192.169.2.0/24");

    VerifyServiceChainSandesh(list_of("blue-i1"));

    // Delete More specific & connected
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteInetRoute(NULL, "red", "192.169.2.1/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
}

TEST_P(ServiceChainParamTest, PeerUpdate) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);

    // Add Connected
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 90, "2.3.0.5");

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "2.3.0.5", "red");

    AddConnectedRoute(peers_[1], "1.1.2.3/32", 100, "2.3.1.5");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "2.3.1.5", "red");

    AddConnectedRoute(peers_[2], "1.1.2.3/32", 95, "2.3.2.5");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "2.3.1.5", "red");

    DeleteConnectedRoute(peers_[1], "1.1.2.3/32");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "2.3.2.5", "red");

    // Delete More specific
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");

    // Delete connected route
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
    DeleteConnectedRoute(peers_[2], "1.1.2.3/32");
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
TEST_P(ServiceChainParamTest, DuplicateForwardingPaths) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);

    // Add Ext connect route
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100);

    // Add Connected with duplicate forwarding information
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.4.5");
    AddConnectedRoute(peers_[1], "1.1.2.3/32", 100, "2.3.4.5");

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "2.3.4.5", "red");

    // Check for ExtConnect route
    VerifyInetRouteExists("blue", "10.1.1.0/24");
    VerifyInetRouteAttributes("blue", "10.1.1.0/24", "2.3.4.5", "red");

    // Delete connected route from peers_[0]
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "2.3.4.5", "red");

    // Check for ExtConnect route
    VerifyInetRouteExists("blue", "10.1.1.0/24");
    VerifyInetRouteAttributes("blue", "10.1.1.0/24", "2.3.4.5", "red");

    // Delete More specific
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");

    // Delete Ext connect route
    DeleteInetRoute(NULL, "red", "10.1.1.0/24");

    // Delete connected route
    DeleteConnectedRoute(peers_[1], "1.1.2.3/32");
}

TEST_P(ServiceChainParamTest, EcmpPaths) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);

    // Add Connected
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5");

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "2.3.0.5", "red");

    AddConnectedRoute(peers_[1], "1.1.2.3/32", 100, "2.3.1.5");

    vector<string> path_ids = list_of("2.3.0.5")("2.3.1.5");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", path_ids, "red");

    DeleteConnectedRoute(peers_[1], "1.1.2.3/32");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "2.3.0.5", "red");

    // Delete More specific
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");

    // Delete connected route
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
}

TEST_P(ServiceChainParamTest, EcmpPathUpdate) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5");
    AddConnectedRoute(peers_[1], "1.1.2.3/32", 100, "2.3.1.5");

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    vector<string> path_ids = list_of("2.3.0.5")("2.3.1.5");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", path_ids, "red");

    AddConnectedRoute(peers_[1], "1.1.2.3/32", 100, "2.3.1.8");
    path_ids = list_of("2.3.0.5")("2.3.1.8");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", path_ids, "red");

    // Delete More specific
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");

    // Delete connected route
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
    DeleteConnectedRoute(peers_[1], "1.1.2.3/32");
}

TEST_P(ServiceChainParamTest, EcmpPathAdd) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5");

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "2.3.0.5", "red");

    DisableServiceChainQ();
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
    AddConnectedRoute(peers_[1], "1.1.2.3/32", 100, "2.3.1.5");
    AddConnectedRoute(peers_[2], "1.1.2.3/32", 100, "2.3.2.5");
    AddConnectedRoute(peers_[3], "1.1.2.3/32", 100, "2.3.3.5");
    EnableServiceChainQ();

    vector<string> path_ids = list_of("2.3.1.5")("2.3.2.5")("2.3.3.5");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", path_ids, "red");

    DisableServiceChainQ();
    DeleteConnectedRoute(peers_[1], "1.1.2.3/32");
    DeleteConnectedRoute(peers_[2], "1.1.2.3/32");
    DeleteConnectedRoute(peers_[3], "1.1.2.3/32");
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5",
        BgpPath::AsPathLooped);
    EnableServiceChainQ();

    // Check for aggregated route
    VerifyInetRouteNoExists("blue", "192.168.1.0/24");

    // Delete More specific
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    // Delete connected route
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
}

TEST_P(ServiceChainParamTest, EcmpPathDelete) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific
    DisableServiceChainQ();
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5");
    AddConnectedRoute(peers_[1], "1.1.2.3/32", 100, "2.3.1.5");
    AddConnectedRoute(peers_[2], "1.1.2.3/32", 100, "2.3.2.5");
    AddConnectedRoute(peers_[3], "1.1.2.3/32",  90, "2.3.3.5");
    EnableServiceChainQ();

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    vector<string> path_ids = list_of("2.3.0.5")("2.3.1.5")("2.3.2.5");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", path_ids, "red");

    DisableServiceChainQ();
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
    DeleteConnectedRoute(peers_[1], "1.1.2.3/32");
    DeleteConnectedRoute(peers_[2], "1.1.2.3/32");
    EnableServiceChainQ();

    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "2.3.3.5", "red");

    // Delete More specific
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    // Delete connected route
    DeleteConnectedRoute(peers_[3], "1.1.2.3/32");
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
TEST_P(ServiceChainParamTest, EcmpWithDuplicateForwardingPaths) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);

    // Add Ext connect route
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100);

    // Add Connected with duplicate forwarding information F1
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.4.5");
    AddConnectedRoute(peers_[1], "1.1.2.3/32", 100, "2.3.4.5");

    // Add Connected with duplicate forwarding information F2
    AddConnectedRoute(peers_[2], "1.1.2.3/32", 100, "2.3.4.6");
    AddConnectedRoute(peers_[3], "1.1.2.3/32", 100, "2.3.4.6");

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    vector<string> path_ids = list_of("2.3.4.5")("2.3.4.6");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", path_ids, "red");

    // Check for ExtConnect route
    VerifyInetRouteExists("blue", "10.1.1.0/24");
    VerifyInetRouteAttributes("blue", "10.1.1.0/24", path_ids, "red");

    // Delete connected routes from peers_[0] and peers_[2]
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
    DeleteConnectedRoute(peers_[2], "1.1.2.3/32");

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    path_ids = list_of("2.3.4.5")("2.3.4.6");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", path_ids, "red");

    // Check for ExtConnect route
    VerifyInetRouteExists("blue", "10.1.1.0/24");
    VerifyInetRouteAttributes("blue", "10.1.1.0/24", path_ids, "red");

    // Delete More specific
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");

    // Delete Ext connect route
    DeleteInetRoute(NULL, "red", "10.1.1.0/24");

    // Delete connected routes
    DeleteConnectedRoute(peers_[1], "1.1.2.3/32");
    DeleteConnectedRoute(peers_[3], "1.1.2.3/32");
}

//
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add connected route
// 3. Add MX leaked route 192.168.1.0/24
// 4. Verify that ext connect route 192.168.1.0/24 is not added
// 5. Add VM route(192.168.1.1/32) and verify aggregate route 192.168.1.0/24
//
TEST_P(ServiceChainParamTest, IgnoreAggregateRoute) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Add MX leaked route
    AddInetRoute(NULL, "red", "192.168.1.0/24", 100);

    // Check for absence of ExtConnect route
    VerifyInetRouteNoExists("blue", "192.168.1.0/24");

    // Add more specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);

    // Check for Aggregate route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "2.3.4.5", "red");

    // Delete MX leaked, More specific and connected route
    DeleteInetRoute(NULL, "red", "192.168.1.0/24");
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
}

//
// 0. Disable aggregation
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add connected route
// 3. Add MX leaked route 192.168.1.0/24
// 4. Verify that ext connect route 192.168.1.0/24 is added
//
TEST_P(ServiceChainParamTest, ValidateAggregateRoute) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    bgp_server_->service_chain_mgr()->set_aggregate_host_route(false);
    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Add MX leaked route
    AddInetRoute(NULL, "red", "192.168.1.0/24", 100);

    // Check for Aggregate route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "2.3.4.5", "red");

    // Delete MX leaked and connected route
    DeleteInetRoute(NULL, "red", "192.168.1.0/24");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
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
TEST_P(ServiceChainParamTest, ExtConnectRoute) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Ext connect route
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100);

    // Check for ExtConnect route
    VerifyInetRouteNoExists("blue", "10.1.1.0/24");

    // Check for Aggregate route
    VerifyInetRouteNoExists("blue", "192.168.1.0/24");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Check for ExtConnect route
    VerifyInetRouteExists("blue", "10.1.1.0/24");

    // Check for absence Aggregate route
    VerifyInetRouteNoExists("blue", "192.168.1.0/24");

    // Delete Connected route
    DeleteConnectedRoute(NULL, "1.1.2.3/32");

    // Check for ExtConnect route
    VerifyInetRouteNoExists("blue", "192.168.1.0/24");
    // Check for Aggregate route
    VerifyInetRouteNoExists("blue", "192.168.1.0/24");

    // Add more specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");
 
    // Check for Aggregate route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "2.3.4.5", "red");

    // Check for ExtConnect route
    VerifyInetRouteExists("blue", "10.1.1.0/24");
    VerifyInetRouteAttributes("blue", "10.1.1.0/24", "2.3.4.5", "red");

    // Delete ExtRoute, More specific and connected route
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteInetRoute(NULL, "red", "10.1.1.0/24");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
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
TEST_P(ServiceChainParamTest, ExtConnectRouteNoAdvertiseCommunity) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Ext connect route
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100);

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Check for ExtConnect route
    VerifyInetRouteExists("blue", "10.1.1.0/24");

    // Change Ext connect route to have NO_ADVERTISE community
    vector<uint32_t> commlist = list_of(Community::NoAdvertise);
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100, commlist);

    // Check for ExtConnect route
    VerifyInetRouteNoExists("blue", "10.1.1.0/24");

    // Change Ext connect route to not have NO_ADVERTISE community
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100);

    // Check for ExtConnect route
    VerifyInetRouteExists("blue", "10.1.1.0/24");
    VerifyInetRouteAttributes("blue", "10.1.1.0/24", "2.3.4.5", "red");

    // Delete ExtRoute and connected route
    DeleteInetRoute(NULL, "red", "10.1.1.0/24");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
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
TEST_P(ServiceChainParamTest, ExtConnectRouteOriginVnOnly) {
    vector<string> instance_names =
        list_of("blue")("blue-i1")("red-i2")("red")("green");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red") ("red", "green");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Add more specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);

    // Add Ext connect route
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100);

    // Add route to green VN which gets imported into red
    AddInetRoute(NULL, "green", "20.1.1.0/24", 100);

    // Check for Aggregate route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "2.3.4.5", "red");

    // Check for ExtConnect route
    VerifyInetRouteExists("blue", "10.1.1.0/24");
    VerifyInetRouteAttributes("blue", "10.1.1.0/24", "2.3.4.5", "red");

    // Check for non-OriginVn route
    VerifyInetRouteNoExists("blue", "20.1.1.0/24");

    // Delete ExtRoute, More specific, non-OriginVn and connected route
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteInetRoute(NULL, "red", "10.1.1.0/24");
    DeleteInetRoute(NULL, "green", "20.1.1.0/24");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
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
TEST_P(ServiceChainParamTest, ExtConnectRouteOriginVnUnresolved1) {
    vector<string> instance_names =
        list_of("blue")("blue-i1")("red-i2")("red")("green");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Add Ext connect route with targets of both red and green.
    vector<string> instances = list_of("red")("green");
    AddInetVpnRoute(NULL, instances, "10.1.1.0/24", 100);

    // Verify that MX leaked route is present in red
    VerifyInetRouteExists("red", "10.1.1.0/24");

    // Verify that ExtConnect route is present in blue
    VerifyInetRouteExists("blue", "10.1.1.0/24");
    VerifyInetRouteAttributes("blue", "10.1.1.0/24", "2.3.4.5", "red");

    // Delete ExtRoute and connected route
    DeleteInetVpnRoute(NULL, "red", "10.1.1.0/24");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
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
TEST_P(ServiceChainParamTest, ExtConnectRouteOriginVnUnresolved2) {
    vector<string> instance_names =
        list_of("blue")("blue-i1")("red-i2")("red")("green")("yellow");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red") ("red", "green");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Add Ext connect route with targets of green and yellow.
    vector<string> instances = list_of("green")("yellow");
    AddInetVpnRoute(NULL, instances, "10.1.1.0/24", 100);

    // Verify that MX leaked route is present in red
    VerifyInetRouteExists("red", "10.1.1.0/24");

    // Verify that ExtConnect route is not present in blue
    VerifyInetRouteNoExists("blue", "10.1.1.0/24");

    // Delete ExtRoute and connected route
    DeleteInetVpnRoute(NULL, "green", "10.1.1.0/24");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
}

//
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add MX leaked route covering the VN subnet 192.168.0.0/16
// 3. Add VM route and connected route
// 4. Verify that Aggregate route 192.168.1.0/24 is added 
//    Verify that ext connect route 192.168.0.0/16 is added
//
TEST_P(ServiceChainParamTest, ExtConnectRouteCoveringSubnetPrefix) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Ext connect route.. Say MX leaks /16 route
    AddInetRoute(NULL, "red", "192.168.0.0/16", 100);

    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);

    // Check for ExtConnect route
    VerifyInetRouteNoExists("blue", "192.168.0.0/16");
    // Check for Aggregate route
    VerifyInetRouteNoExists("blue", "192.168.1.0/24");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Check for ExtConnect route
    VerifyInetRouteExists("blue", "192.168.0.0/16");
    VerifyInetRouteAttributes("blue", "192.168.0.0/16", "2.3.4.5", "red");

    // Check for Aggregate route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "2.3.4.5", "red");

    // Delete ExtRoute, More specific and connected route
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteInetRoute(NULL, "red", "192.168.0.0/16");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
}

//
// 1. Create Service Chain with 192.168.1.0/24 as vn subnet
// 2. Add MX leaked route within the VN subnet 192.168.1.252/30
// 3. Add VM route and connected route
// 4. Verify that Aggregate route is added with connected route nexthop
//    Verify that MX added ext connect route is treated as more specific itself
//
TEST_P(ServiceChainParamTest, ExtConnectRouteWithinSubnetPrefix) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Ext connect route.. Say MX leaks /30 route
    AddInetRoute(NULL, "red", "192.168.1.252/30", 100);

    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);

    // Check for ExtConnect route
    VerifyInetRouteNoExists("blue", "192.168.1.252/30");

    // Check for Aggregate route
    VerifyInetRouteNoExists("blue", "192.168.1.0/24");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Check for ExtConnect route
    VerifyInetRouteNoExists("blue", "192.168.1.252/30");

    // Check for Aggregate route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "2.3.4.5", "red");

    // Delete ExtRoute, More specific and connected route
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteInetRoute(NULL, "red", "192.168.1.252/30");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
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
TEST_P(ServiceChainParamTest, ExtConnectRouteServiceChainUpdate) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Ext connect route.. Say MX leaks /30 route
    AddInetRoute(NULL, "red", "192.168.1.252/30", 100);

    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);

    // Check for ExtConnect route
    VerifyInetRouteNoExists("blue", "192.168.1.252/30");

    // Check for Aggregate route
    VerifyInetRouteNoExists("blue", "192.168.1.0/24");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Check for ExtConnect route
    VerifyInetRouteNoExists("blue", "192.168.1.252/30");

    // Check for Aggregate route
    VerifyInetRouteExists("blue", "192.168.1.0/24");

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_4.xml");

    // Check for ExtConnect route
    VerifyInetRouteExists("blue", "192.168.1.252/30");
    VerifyInetRouteExists("blue", "192.168.1.1/32");

    // Check for Aggregate route
    VerifyInetRouteNoExists("blue", "10.1.1.0/24");

    // Check for Previous Aggregate route
    VerifyInetRouteNoExists("blue", "192.168.1.0/24");

    // Add Ext connect route.. Say MX leaks /16 route
    AddInetRoute(NULL, "red", "192.168.0.0/16", 100);

    // Check for ExtConnect route
    VerifyInetRouteExists("blue", "192.168.0.0/16");

    // Add more specific for new subnet prefix 
    AddInetRoute(NULL, "red", "10.1.1.1/32", 100);

    // Check for Aggregate route
    VerifyInetRouteExists("blue", "10.1.1.0/24");

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Check for ext connect route
    VerifyInetRouteExists("blue", "10.1.1.1/32");
    VerifyInetRouteExists("blue", "192.168.0.0/16"),

    // Check for new Aggregate route
    VerifyInetRouteExists("blue", "192.168.1.0/24");

    // Check for removal of ExtConnect route it is now more specific
    VerifyInetRouteNoExists("blue", "192.168.1.252/30");

    // Check for removal of ExtConnect route it is now more specific
    VerifyInetRouteNoExists("blue", "192.168.1.1/32");

    DeleteInetRoute(NULL, "red", "192.168.1.252/30");
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteInetRoute(NULL, "red", "192.168.0.0/16");
    DeleteInetRoute(NULL, "red", "10.1.1.1/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
}

TEST_P(ServiceChainParamTest, ExtConnectedEcmpPaths) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add MX leaked route 
    AddInetRoute(NULL, "red", "10.10.1.0/24", 100);

    // Add Connected
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5");

    // Check for external connected route 
    VerifyInetRouteExists("blue", "10.10.1.0/24");
    VerifyInetRouteAttributes("blue", "10.10.1.0/24", "2.3.0.5", "red");

    // Add Connected
    AddConnectedRoute(peers_[1], "1.1.2.3/32", 100, "2.3.1.5");
    vector<string> path_ids = list_of("2.3.0.5")("2.3.1.5");
    VerifyInetRouteAttributes("blue", "10.10.1.0/24", path_ids, "red");

    DeleteConnectedRoute(peers_[1], "1.1.2.3/32");
    VerifyInetRouteAttributes("blue", "10.10.1.0/24", "2.3.0.5", "red");

    // Delete MX route
    DeleteInetRoute(NULL, "red", "10.10.1.0/24");

    // Delete connected route
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
}


TEST_P(ServiceChainParamTest, ExtConnectedMoreSpecificEcmpPaths) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Ext connect route
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100);

    // Add Connected
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5");

    // Add more specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);

    // Check for Aggregate route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "2.3.0.5", "red");

    // Check for ExtConnect route
    VerifyInetRouteExists("blue", "10.1.1.0/24");
    VerifyInetRouteAttributes("blue", "10.1.1.0/24", "2.3.0.5", "red");

    // Connected path is infeasible
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5",
        BgpPath::AsPathLooped);

    // Verify that Aggregate route and ExtConnect route is gone
    VerifyInetRouteNoExists("blue", "192.168.1.0/24");
    VerifyInetRouteNoExists("blue", "10.1.1.0/24");

    // Connected path again from two peers
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5");
    AddConnectedRoute(peers_[1], "1.1.2.3/32", 100, "2.3.1.5");

    // Check for Aggregate & ExtConnect route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteExists("blue", "10.1.1.0/24");
    vector<string> path_ids = list_of("2.3.0.5")("2.3.1.5");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", path_ids, "red");
    VerifyInetRouteAttributes("blue", "10.1.1.0/24", path_ids, "red");

    // Connected path is infeasible
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5",
        BgpPath::AsPathLooped);

    VerifyInetRouteAttributes("blue", "192.168.1.0/24", "2.3.1.5", "red");
    VerifyInetRouteAttributes("blue", "10.1.1.0/24", "2.3.1.5", "red");

    // Delete ExtRoute, More specific and connected route
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteInetRoute(NULL, "red", "10.1.1.0/24");
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
    DeleteConnectedRoute(peers_[1], "1.1.2.3/32");
}

TEST_P(ServiceChainParamTest, ServiceChainRouteSGID) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    bgp_server_->service_chain_mgr()->set_aggregate_host_route(false);
    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    vector<uint32_t> sgid_list_more_specific_1 = list_of(1)(2)(3)(4);
    vector<uint32_t> sgid_list_more_specific_2 = list_of(5)(6)(7)(8);
    vector<uint32_t> sgid_list_connected = list_of(9)(10)(11)(12);
    vector<uint32_t> sgid_list_ext = list_of(13)(14)(15)(16);

    // Add Ext connect route
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100, vector<uint32_t>(),
        sgid_list_ext);

    // Add Connected
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5", 0, 0, 
                      sgid_list_connected);

    // Add more specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100, vector<uint32_t>(),
        sgid_list_more_specific_1);
    AddInetRoute(NULL, "red", "192.168.1.2/32", 100, vector<uint32_t>(),
        sgid_list_more_specific_2);

    // Check for More specific routes leaked in src instance
    VerifyInetRouteExists("blue", "192.168.1.1/32");
    VerifyInetRouteAttributes(
        "blue", "192.168.1.1/32", "2.3.0.5", "red", sgid_list_more_specific_1);
    VerifyInetRouteExists("blue", "192.168.1.2/32");
    VerifyInetRouteAttributes(
        "blue", "192.168.1.2/32", "2.3.0.5", "red", sgid_list_more_specific_2);

    // Check for ExtConnect route
    VerifyInetRouteExists("blue", "10.1.1.0/24");
    VerifyInetRouteAttributes(
        "blue", "10.1.1.0/24", "2.3.0.5", "red", sgid_list_ext);

    // Delete ExtRoute, More specific and connected route
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteInetRoute(NULL, "red", "192.168.1.2/32");
    DeleteInetRoute(NULL, "red", "10.1.1.0/24");
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
}

TEST_P(ServiceChainParamTest, ServiceChainRouteUpdateSGID) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    bgp_server_->service_chain_mgr()->set_aggregate_host_route(false);
    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    vector<uint32_t> sgid_list_more_specific_1 = list_of(1)(2)(3)(4);
    vector<uint32_t> sgid_list_more_specific_2 = list_of(5)(6)(7)(8);
    vector<uint32_t> sgid_list_connected = list_of(9)(10)(11)(12);
    vector<uint32_t> sgid_list_ext = list_of(13)(14)(15)(16);

    // Add Ext connect route
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100, vector<uint32_t>(),
        sgid_list_ext);

    // Add Connected
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5", 0, 0, 
                      sgid_list_connected);

    // Add more specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100, vector<uint32_t>(),
        sgid_list_more_specific_1);
    AddInetRoute(NULL, "red", "192.168.1.2/32", 100, vector<uint32_t>(),
        sgid_list_more_specific_2);

    // Check for More specific routes leaked in src instance
    VerifyInetRouteExists("blue", "192.168.1.1/32");
    VerifyInetRouteAttributes(
        "blue", "192.168.1.1/32", "2.3.0.5", "red", sgid_list_more_specific_1);
    VerifyInetRouteExists("blue", "192.168.1.2/32");
    VerifyInetRouteAttributes(
        "blue", "192.168.1.2/32", "2.3.0.5", "red", sgid_list_more_specific_2);

    // Check for ExtConnect route
    VerifyInetRouteExists("blue", "10.1.1.0/24");
    VerifyInetRouteAttributes(
        "blue", "10.1.1.0/24", "2.3.0.5", "red", sgid_list_ext);

    // Update Ext connect route with different SGID list
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100, vector<uint32_t>(),
        sgid_list_more_specific_1);

    // Add Connected
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.0.5", 0, 0, 
                      sgid_list_more_specific_2);

    // Add more specific
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100, vector<uint32_t>(),
        sgid_list_ext);
    AddInetRoute(NULL, "red", "192.168.1.2/32", 100, vector<uint32_t>(),
        sgid_list_connected);

    // Check for More specific routes leaked in src rtinstance
    VerifyInetRouteExists("blue", "192.168.1.1/32");
    VerifyInetRouteAttributes(
        "blue", "192.168.1.1/32", "2.3.0.5", "red", sgid_list_ext);
    VerifyInetRouteExists("blue", "192.168.1.2/32");
    VerifyInetRouteAttributes(
        "blue", "192.168.1.2/32", "2.3.0.5", "red", sgid_list_connected);

    // Check for ExtConnect route
    VerifyInetRouteExists("blue", "10.1.1.0/24");
    VerifyInetRouteAttributes(
        "blue", "10.1.1.0/24", "2.3.0.5", "red", sgid_list_more_specific_1);

    // Delete ExtRoute, More specific and connected route
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");
    DeleteInetRoute(NULL, "red", "192.168.1.2/32");
    DeleteInetRoute(NULL, "red", "10.1.1.0/24");
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
}

TEST_P(ServiceChainParamTest, ValidateTunnelEncapAggregate) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific
    set<string> encap_more_specific = list_of("udp");
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100, vector<uint32_t>(),
        vector<uint32_t>(), encap_more_specific);

    // Add Connected
    set<string> encap = list_of("vxlan");
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5", 0, 0, 
                      vector<uint32_t>(), encap);

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteAttributes(
        "blue", "192.168.1.0/24", "2.3.4.5", "red", encap);

    // Add Connected
    encap = list_of("gre");
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5", 0, 0, 
                      vector<uint32_t>(), encap);

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    VerifyInetRouteAttributes(
        "blue", "192.168.1.0/24", "2.3.4.5", "red", encap);

    // Delete More specific
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");

    // Delete connected route
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
}

TEST_P(ServiceChainParamTest, ValidateTunnelEncapExtRoute) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Ext connect route
    set<string> encap_ext = list_of("vxlan");
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100, vector<uint32_t>(),
        vector<uint32_t>(), encap_ext);

    // Add Connected
    set<string> encap = list_of("gre");
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5", 0, 0, 
                      vector<uint32_t>(), encap);

    // Check for service Chain router
    VerifyInetRouteExists("blue", "10.1.1.0/24");
    VerifyInetRouteAttributes("blue", "10.1.1.0/24", "2.3.4.5", "red", encap);

    // Add Connected
    encap = list_of("udp");
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5", 0, 0, 
                      vector<uint32_t>(), encap);
    VerifyInetRouteAttributes("blue", "10.1.1.0/24", "2.3.4.5", "red", encap);

    // Check for aggregated route
    VerifyInetRouteExists("blue", "10.1.1.0/24");

    // Delete ext connected route
    DeleteInetRoute(NULL, "red", "10.1.1.0/24");

    // Delete connected route
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
}

TEST_P(ServiceChainParamTest, MultiPathTunnelEncap) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add More specific
    set<string> encap_1 = list_of("gre");
    set<string> encap_2 = list_of("udp");
    set<string> encap_3 = list_of("vxlan");
    AddInetRoute(NULL, "red", "192.168.1.1/32", 100);
    AddConnectedRoute(peers_[0], "1.1.2.3/32", 100, "2.3.1.5", 0, 0, vector<uint32_t>(), encap_1);
    AddConnectedRoute(peers_[1], "1.1.2.3/32", 100, "2.3.2.5", 0, 0, vector<uint32_t>(), encap_2);
    AddConnectedRoute(peers_[2], "1.1.2.3/32", 100, "2.3.3.5", 0, 0, vector<uint32_t>(), encap_3);

    // Check for aggregated route
    VerifyInetRouteExists("blue", "192.168.1.0/24");
    vector<string> path_ids = list_of("2.3.1.5")("2.3.2.5")("2.3.3.5");
    VerifyInetRouteAttributes("blue", "192.168.1.0/24", path_ids, "red");
    VerifyInetPathAttributes(
        "blue", "192.168.1.0/24", "2.3.1.5", "red", encap_1);
    VerifyInetPathAttributes(
        "blue", "192.168.1.0/24", "2.3.2.5", "red", encap_2);
    VerifyInetPathAttributes(
        "blue", "192.168.1.0/24", "2.3.3.5", "red", encap_3);

    // Delete More specific
    DeleteInetRoute(NULL, "red", "192.168.1.1/32");

    // Delete connected route
    DeleteConnectedRoute(peers_[0], "1.1.2.3/32");
    DeleteConnectedRoute(peers_[1], "1.1.2.3/32");
    DeleteConnectedRoute(peers_[2], "1.1.2.3/32");
}

TEST_P(ServiceChainParamTest, ValidateSiteOfOriginExtRoute) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Ext connect route
    SiteOfOrigin soo1 = SiteOfOrigin::FromString("soo:65001:100");
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100, soo1);

    // Check for service chain route
    VerifyInetRouteNoExists("blue", "10.1.1.0/24");

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Check for service chain route
    VerifyInetRouteExists("blue", "10.1.1.0/24");
    VerifyInetRouteAttributes("blue", "10.1.1.0/24", "2.3.4.5", "red", soo1);

    // Update Ext connect route
    SiteOfOrigin soo2 = SiteOfOrigin::FromString("soo:65001:200");
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100, soo2);

    // Check for service chain route
    VerifyInetRouteExists("blue", "10.1.1.0/24");
    VerifyInetRouteAttributes("blue", "10.1.1.0/24", "2.3.4.5", "red", soo2);

    // Delete Ext connect route
    DeleteInetRoute(NULL, "red", "10.1.1.0/24");

    // Delete connected route
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
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
TEST_P(ServiceChainParamTest, ValidateCommunityExtRoute) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections =
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);
    VerifyNetworkConfig(instance_names);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Ext connect route
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100);

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Check for ExtConnect route
    VerifyInetRouteExists("blue", "10.1.1.0/24");

    // Change Ext connect route to have some communities.
    vector<uint32_t> commlist = list_of(0xFFFFAA01)(0xFFFFAA02)(0xFFFFAA03);
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100, commlist);

    // Check for ExtConnect route
    CommunitySpec commspec;
    commspec.communities = commlist;
    VerifyInetRouteExists("blue", "10.1.1.0/24");
    VerifyInetRouteAttributes(
        "blue", "10.1.1.0/24", "2.3.4.5", "red", commspec);

    // Change Ext connect route to not have communities.
    AddInetRoute(NULL, "red", "10.1.1.0/24", 100);

    // Check for ExtConnect route
    commspec.communities.clear();
    VerifyInetRouteExists("blue", "10.1.1.0/24");
    VerifyInetRouteAttributes(
        "blue", "10.1.1.0/24", "2.3.4.5", "red", commspec);

    // Delete ExtRoute and connected route
    DeleteInetRoute(NULL, "red", "10.1.1.0/24");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
}

TEST_P(ServiceChainParamTest, DeleteConnectedWithExtConnectRoute) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    // Add Ext connect route
    AddInetRoute(NULL, "red", "10.1.1.1/32", 100);
    AddInetRoute(NULL, "red", "10.1.1.2/32", 100);
    AddInetRoute(NULL, "red", "10.1.1.3/32", 100);

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Check for ExtConnect route
    VerifyInetRouteExists("blue", "10.1.1.1/32");
    VerifyInetRouteExists("blue", "10.1.1.2/32");
    VerifyInetRouteExists("blue", "10.1.1.3/32");

    DisableServiceChainQ();
    AddConnectedRoute(NULL, "1.1.2.3/32", 200, "2.3.4.5");
    DeleteInetRoute(NULL, "red", "10.1.1.1/32");

    VerifyInetRouteIsDeleted("red", "10.1.1.1/32");

    // Check for ExtConnect route
    VerifyInetRouteExists("blue", "10.1.1.1/32");
    VerifyInetRouteExists("blue", "10.1.1.2/32");
    VerifyInetRouteExists("blue", "10.1.1.3/32");

    EnableServiceChainQ();

    TASK_UTIL_EXPECT_TRUE(bgp_server_->service_chain_mgr()->IsQueueEmpty());

    VerifyInetRouteNoExists("blue", "10.1.1.1/32");

    // Delete ExtRoute, More specific and connected route
    DeleteInetRoute(NULL, "red", "10.1.1.2/32");
    DeleteInetRoute(NULL, "red", "10.1.1.3/32");
    DeleteConnectedRoute(NULL, "1.1.2.3/32");
}

TEST_P(ServiceChainParamTest, DeleteEntryReuse) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    vector<string> routes_to_play =
        list_of("10.1.1.1/32")("10.1.1.2/32")("10.1.1.3/32");
    // Add Ext connect route
    for (vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++)
        AddInetRoute(NULL, "red", *it, 100);

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Check for ExtConnect route
    for (vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++) {
        VerifyInetRouteExists("blue", *it);
    }
    DisableServiceChainQ();
    for (vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++)
        DeleteInetRoute(NULL, "red", *it);
    DeleteConnectedRoute(NULL, "1.1.2.3/32");

    for (vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++) {
        VerifyInetRouteIsDeleted("red", *it);
    }

    for (vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++)
        AddInetRoute(NULL, "red", *it, 100);
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");


    for (vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++)
        DeleteInetRoute(NULL, "red", *it);
    DeleteConnectedRoute(NULL, "1.1.2.3/32");

    for (vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++) {
        VerifyInetRouteIsDeleted("red", *it);
    }

    EnableServiceChainQ();
    TASK_UTIL_EXPECT_TRUE(bgp_server_->service_chain_mgr()->IsQueueEmpty());
}

TEST_P(ServiceChainParamTest, EntryAfterStop) {
    vector<string> instance_names = list_of("blue")("blue-i1")("red-i2")("red");
    multimap<string, string> connections = 
        map_list_of("blue", "blue-i1") ("red-i2", "red");
    NetworkConfig(instance_names, connections);

    SetServiceChainInformation("blue-i1",
        "controller/src/bgp/testdata/service_chain_1.xml");

    vector<string> routes_to_play;
    // Add Ext connect route
    for (int i = 0; i < 255; i++) {
        stringstream route;
        route << "10.1.1." << i << "/32";
        routes_to_play.push_back(route.str());
    }

    for (vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++)
        AddInetRoute(NULL, "red", *it, 100);

    // Add Connected
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    // Check for ExtConnect route
    for (vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++) {
        VerifyInetRouteExists("blue", *it);
    }
    DisableServiceChainQ();

    ClearServiceChainInformation("blue-i1");

    // Add more Ext connect route
    for (int i = 0; i < 255; i++) {
        stringstream route;
        route << "10.2.1." << i << "/32";
        routes_to_play.push_back(route.str());
    }

    for (vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++)
        AddInetRoute(NULL, "red", *it, 200);
    AddConnectedRoute(NULL, "1.1.2.3/32", 100, "2.3.4.5");

    EnableServiceChainQ();
    TASK_UTIL_EXPECT_TRUE(bgp_server_->service_chain_mgr()->IsQueueEmpty());

    for (vector<string>::iterator it = routes_to_play.begin();
         it != routes_to_play.end(); it++)
        DeleteInetRoute(NULL, "red", *it);
    DeleteConnectedRoute(NULL, "1.1.2.3/32");

    TASK_UTIL_EXPECT_EQ(0, RouteCount("red"));
}

//
// Instances are (blue)(blue-i1)(core-i2)(core)(core-i3)(red-i4)(red)
//
TEST_P(ServiceChainParamTest, TransitNetworkRemoteVMRoutes) {
    ParseConfigFile("controller/src/bgp/testdata/service_chain_test_1.xml");
    AddConnection("blue", "blue-i1");
    AddConnection("core", "core-i3");

    // Add more specific routes to red
    AddInetRoute(NULL, "red", "192.168.3.101/32", 100);
    AddInetRoute(NULL, "red", "192.168.3.102/32", 100);
    AddInetRoute(NULL, "red", "192.168.3.103/32", 100);

    // Add Connected routes for the 2 chains
    AddConnectedRoute(1, NULL, "192.168.1.253/32", 100, "20.1.1.1");
    AddConnectedRoute(2, NULL, "192.168.2.253/32", 100, "20.1.1.2");

    // Check for Aggregate route in blue
    BgpRoute *aggregate_rt = VerifyInetRouteExists("blue", "192.168.3.0/24");
    EXPECT_EQ(GetOriginVnFromRoute(aggregate_rt->BestPath()), "core-vn");

    // Delete more specific routes and connected routes
    DeleteInetRoute(NULL, "red", "192.168.3.101/32");
    DeleteInetRoute(NULL, "red", "192.168.3.102/32");
    DeleteInetRoute(NULL, "red", "192.168.3.103/32");
    DeleteConnectedRoute(1, NULL, "192.168.1.253/32");
    DeleteConnectedRoute(2, NULL, "192.168.2.253/32");
}

//
// Instances are (blue)(blue-i1)(core-i2)(core)(core-i3)(red-i4)(red)
//
TEST_P(ServiceChainParamTest, TransitNetworkLocalVMRoutes) {
    ParseConfigFile("controller/src/bgp/testdata/service_chain_test_1.xml");
    AddConnection("blue", "blue-i1");
    AddConnection("core", "core-i3");

    // Add more specific routes to core
    AddInetRoute(NULL, "core", "192.168.2.101/32", 100);
    AddInetRoute(NULL, "core", "192.168.2.102/32", 100);
    AddInetRoute(NULL, "core", "192.168.2.103/32", 100);

    // Add Connected routes for the blue-core chain
    AddConnectedRoute(1, NULL, "192.168.1.253/32", 100, "20.1.1.1");

    // Check for Aggregate route in blue
    BgpRoute *aggregate_rt = VerifyInetRouteExists("blue", "192.168.2.0/24");
    EXPECT_EQ(GetOriginVnFromRoute(aggregate_rt->BestPath()), "core-vn");

    // Delete more specific routes and connected routes
    DeleteInetRoute(NULL, "core", "192.168.2.101/32");
    DeleteInetRoute(NULL, "core", "192.168.2.102/32");
    DeleteInetRoute(NULL, "core", "192.168.2.103/32");
    DeleteConnectedRoute(1, NULL, "192.168.1.253/32");
}

//
// Instances are (blue)(blue-i1)(core-i2)(core)(core-i3)(red-i4)(red)
//
TEST_P(ServiceChainParamTest, TransitNetworkRemoteExtConnectRoute) {
    ParseConfigFile("controller/src/bgp/testdata/service_chain_test_1.xml");
    AddConnection("blue", "blue-i1");
    AddConnection("core", "core-i3");

    // Add Ext connect routes to red
    AddInetRoute(NULL, "red", "10.1.3.1/32", 100);
    AddInetRoute(NULL, "red", "10.1.3.2/32", 100);
    AddInetRoute(NULL, "red", "10.1.3.3/32", 100);

    // Add Connected routes for the 2 chains.
    AddConnectedRoute(1, NULL, "192.168.1.253/32", 100, "20.1.1.1");
    AddConnectedRoute(2, NULL, "192.168.2.253/32", 100, "20.1.1.2");

    // Check for ExtConnect route in blue
    BgpRoute *ext_rt;
    ext_rt = VerifyInetRouteExists("blue", "10.1.3.1/32");
    EXPECT_EQ(GetOriginVnFromRoute(ext_rt->BestPath()), "core-vn");
    ext_rt = VerifyInetRouteExists("blue", "10.1.3.2/32");
    EXPECT_EQ(GetOriginVnFromRoute(ext_rt->BestPath()), "core-vn");
    ext_rt = VerifyInetRouteExists("blue", "10.1.3.3/32");
    EXPECT_EQ(GetOriginVnFromRoute(ext_rt->BestPath()), "core-vn");

    // Delete Ext connect routes and connected routes
    DeleteInetRoute(NULL, "red", "10.1.3.1/32");
    DeleteInetRoute(NULL, "red", "10.1.3.2/32");
    DeleteInetRoute(NULL, "red", "10.1.3.3/32");
    DeleteConnectedRoute(1, NULL, "192.168.1.253/32");
    DeleteConnectedRoute(2, NULL, "192.168.2.253/32");
}

TEST_P(ServiceChainParamTest, TransitNetworkLocalExtConnectRoute) {
    ParseConfigFile("controller/src/bgp/testdata/service_chain_test_1.xml");
    AddConnection("blue", "blue-i1");
    AddConnection("core", "core-i3");

    // Add Ext connect routes to core
    AddInetRoute(NULL, "core", "10.1.2.1/32", 100);
    AddInetRoute(NULL, "core", "10.1.2.2/32", 100);
    AddInetRoute(NULL, "core", "10.1.2.3/32", 100);

    // Add Connected routes for the blue-core chain
    AddConnectedRoute(1, NULL, "192.168.1.253/32", 100, "20.1.1.1");

    // Check for ExtConnect route in blue
    BgpRoute *ext_rt;
    ext_rt = VerifyInetRouteExists("blue", "10.1.2.1/32");
    EXPECT_EQ(GetOriginVnFromRoute(ext_rt->BestPath()), "core-vn");
    ext_rt = VerifyInetRouteExists("blue", "10.1.2.1/32");
    EXPECT_EQ(GetOriginVnFromRoute(ext_rt->BestPath()), "core-vn");
    ext_rt = VerifyInetRouteExists("blue", "10.1.2.1/32");
    EXPECT_EQ(GetOriginVnFromRoute(ext_rt->BestPath()), "core-vn");

    // Delete Ext connect routes and connected routes
    DeleteInetRoute(NULL, "core", "10.1.2.1/32");
    DeleteInetRoute(NULL, "core", "10.1.2.2/32");
    DeleteInetRoute(NULL, "core", "10.1.2.3/32");
    DeleteConnectedRoute(1, NULL, "192.168.1.253/32");
}

//
// Instances are (blue)(blue-i1)(core-i2)(core)(core-i3)(red-i4)(red)
//
TEST_P(ServiceChainParamTest, TransitNetworkAddDeleteConnectedRoute1) {
    ParseConfigFile("controller/src/bgp/testdata/service_chain_test_1.xml");
    AddConnection("blue", "blue-i1");
    AddConnection("core", "core-i3");

    // Add more specific routes to red
    AddInetRoute(NULL, "red", "192.168.3.101/32", 100);
    AddInetRoute(NULL, "red", "192.168.3.102/32", 100);

    // Add Ext connect routes to red
    AddInetRoute(NULL, "red", "10.1.3.1/32", 100);
    AddInetRoute(NULL, "red", "10.1.3.2/32", 100);

    // Add Connected routes for the 2 chains
    AddConnectedRoute(1, NULL, "192.168.1.253/32", 100, "20.1.1.1");
    AddConnectedRoute(2, NULL, "192.168.2.253/32", 100, "20.1.1.2");

    // Check for Aggregate route in blue
    BgpRoute *aggregate_rt;
    aggregate_rt = VerifyInetRouteExists("blue", "192.168.3.0/24");
    EXPECT_EQ(GetOriginVnFromRoute(aggregate_rt->BestPath()), "core-vn");

    // Check for ExtConnect routes in blue
    BgpRoute *ext_rt;
    ext_rt = VerifyInetRouteExists("blue", "10.1.3.1/32");
    EXPECT_EQ(GetOriginVnFromRoute(ext_rt->BestPath()), "core-vn");
    ext_rt = VerifyInetRouteExists("blue", "10.1.3.2/32");
    EXPECT_EQ(GetOriginVnFromRoute(ext_rt->BestPath()), "core-vn");

    // Remove connected route for blue-core chain.
    DeleteConnectedRoute(1, NULL, "192.168.1.253/32");

    // Check for Aggregate route in blue
    VerifyInetRouteNoExists("blue", "192.168.3.0/24");

    // Check for ExtConnect routes in blue
    VerifyInetRouteNoExists("blue", "10.1.3.1/32");
    VerifyInetRouteNoExists("blue", "10.1.3.2/32");

    // Add connected route for blue-core chain.
    AddConnectedRoute(1, NULL, "192.168.1.253/32", 100, "20.1.1.1");

    // Check for Aggregate route in blue
    aggregate_rt = VerifyInetRouteExists("blue", "192.168.3.0/24");
    EXPECT_EQ(GetOriginVnFromRoute(aggregate_rt->BestPath()), "core-vn");

    // Check for ExtConnect routes in blue
    ext_rt = VerifyInetRouteExists("blue", "10.1.3.1/32");
    EXPECT_EQ(GetOriginVnFromRoute(ext_rt->BestPath()), "core-vn");
    ext_rt = VerifyInetRouteExists("blue", "10.1.3.2/32");
    EXPECT_EQ(GetOriginVnFromRoute(ext_rt->BestPath()), "core-vn");

    // Delete Ext connect routes and connected routes
    DeleteInetRoute(NULL, "red", "192.168.3.101/32");
    DeleteInetRoute(NULL, "red", "192.168.3.102/32");
    DeleteInetRoute(NULL, "red", "10.1.3.1/32");
    DeleteInetRoute(NULL, "red", "10.1.3.2/32");
    DeleteConnectedRoute(1, NULL, "192.168.1.253/32");
    DeleteConnectedRoute(2, NULL, "192.168.2.253/32");
}

//
// Instances are (blue)(blue-i1)(core-i2)(core)(core-i3)(red-i4)(red)
//
TEST_P(ServiceChainParamTest, TransitNetworkAddDeleteConnectedRoute2) {
    ParseConfigFile("controller/src/bgp/testdata/service_chain_test_1.xml");
    AddConnection("blue", "blue-i1");
    AddConnection("core", "core-i3");

    // Add more specific routes to red
    AddInetRoute(NULL, "red", "192.168.3.101/32", 100);
    AddInetRoute(NULL, "red", "192.168.3.102/32", 100);

    // Add Ext connect routes to red
    AddInetRoute(NULL, "red", "10.1.3.1/32", 100);
    AddInetRoute(NULL, "red", "10.1.3.2/32", 100);

    // Add Connected routes for the 2 chains
    AddConnectedRoute(1, NULL, "192.168.1.253/32", 100, "20.1.1.1");
    AddConnectedRoute(2, NULL, "192.168.2.253/32", 100, "20.1.1.2");

    // Check for Aggregate route in blue
    BgpRoute *aggregate_rt;
    aggregate_rt = VerifyInetRouteExists("blue", "192.168.3.0/24");
    EXPECT_EQ(GetOriginVnFromRoute(aggregate_rt->BestPath()), "core-vn");

    // Check for ExtConnect routes in blue
    BgpRoute *ext_rt;
    ext_rt = VerifyInetRouteExists("blue", "10.1.3.1/32");
    EXPECT_EQ(GetOriginVnFromRoute(ext_rt->BestPath()), "core-vn");
    ext_rt = VerifyInetRouteExists("blue", "10.1.3.2/32");
    EXPECT_EQ(GetOriginVnFromRoute(ext_rt->BestPath()), "core-vn");

    // Remove connected route for core-red chain.
    DeleteConnectedRoute(2, NULL, "192.168.2.253/32");

    // Check for Aggregate route in blue
    VerifyInetRouteNoExists("blue", "192.168.3.0/24");

    // Check for ExtConnect routes in blue
    VerifyInetRouteNoExists("blue", "10.1.3.1/32");
    VerifyInetRouteNoExists("blue", "10.1.3.2/32");

    // Add connected route for core-red chain.
    AddConnectedRoute(2, NULL, "192.168.2.253/32", 100, "20.1.1.1");

    // Check for Aggregate route in blue
    aggregate_rt = VerifyInetRouteExists("blue", "192.168.3.0/24");
    EXPECT_EQ(GetOriginVnFromRoute(aggregate_rt->BestPath()), "core-vn");

    // Check for ExtConnect routes in blue
    ext_rt = VerifyInetRouteExists("blue", "10.1.3.1/32");
    EXPECT_EQ(GetOriginVnFromRoute(ext_rt->BestPath()), "core-vn");
    ext_rt = VerifyInetRouteExists("blue", "10.1.3.2/32");
    EXPECT_EQ(GetOriginVnFromRoute(ext_rt->BestPath()), "core-vn");

    // Delete Ext connect routes and connected routes
    DeleteInetRoute(NULL, "red", "192.168.3.101/32");
    DeleteInetRoute(NULL, "red", "192.168.3.102/32");
    DeleteInetRoute(NULL, "red", "10.1.3.1/32");
    DeleteInetRoute(NULL, "red", "10.1.3.2/32");
    DeleteConnectedRoute(1, NULL, "192.168.1.253/32");
    DeleteConnectedRoute(2, NULL, "192.168.2.253/32");
}

//
// Instances are (blue)(blue-i1)(core-i2)(core)(core-i3)(red-i4)(red)
//
TEST_P(ServiceChainParamTest, TransitNetworkToggleAllowTransit) {
    string content =
        ParseConfigFile("controller/src/bgp/testdata/service_chain_test_1.xml");
    AddConnection("blue", "blue-i1");
    AddConnection("core", "core-i3");

    // Add more specific routes to red
    AddInetRoute(NULL, "red", "192.168.3.101/32", 100);
    AddInetRoute(NULL, "red", "192.168.3.102/32", 100);

    // Add Ext connect routes to red
    AddInetRoute(NULL, "red", "10.1.3.1/32", 100);
    AddInetRoute(NULL, "red", "10.1.3.2/32", 100);

    // Add Connected routes for the 2 chains
    AddConnectedRoute(1, NULL, "192.168.1.253/32", 100, "20.1.1.1");
    AddConnectedRoute(2, NULL, "192.168.2.253/32", 100, "20.1.1.2");

    // Check for Aggregate route in blue
    BgpRoute *aggregate_rt;
    aggregate_rt = VerifyInetRouteExists("blue", "192.168.3.0/24");
    EXPECT_EQ(GetOriginVnFromRoute(aggregate_rt->BestPath()), "core-vn");

    // Check for ExtConnect routes in blue
    BgpRoute *ext_rt;
    ext_rt = VerifyInetRouteExists("blue", "10.1.3.1/32");
    EXPECT_EQ(GetOriginVnFromRoute(ext_rt->BestPath()), "core-vn");
    ext_rt = VerifyInetRouteExists("blue", "10.1.3.2/32");
    EXPECT_EQ(GetOriginVnFromRoute(ext_rt->BestPath()), "core-vn");

    // Disable allow-transit
    boost::replace_all(content,
        "<allow-transit>true</allow-transit>",
        "<allow-transit>false</allow-transit>");
    ParseConfigString(content);

    // Check for Aggregate route in blue
    VerifyInetRouteNoExists("blue", "192.168.3.0/24");

    // Check for ExtConnect routes in blue
    VerifyInetRouteNoExists("blue", "10.1.3.1/32");
    VerifyInetRouteNoExists("blue", "10.1.3.2/32");

    // Enable allow-transit
    boost::replace_all(content,
        "<allow-transit>false</allow-transit>",
        "<allow-transit>true</allow-transit>");
    ParseConfigString(content);

    // Check for Aggregate route in blue
    aggregate_rt = VerifyInetRouteExists("blue", "192.168.3.0/24");
    EXPECT_EQ(GetOriginVnFromRoute(aggregate_rt->BestPath()), "core-vn");

    // Check for ExtConnect routes in blue
    ext_rt = VerifyInetRouteExists("blue", "10.1.3.1/32");
    EXPECT_EQ(GetOriginVnFromRoute(ext_rt->BestPath()), "core-vn");
    ext_rt = VerifyInetRouteExists("blue", "10.1.3.2/32");
    EXPECT_EQ(GetOriginVnFromRoute(ext_rt->BestPath()), "core-vn");

    // Delete Ext connect routes and connected routes
    DeleteInetRoute(NULL, "red", "192.168.3.101/32");
    DeleteInetRoute(NULL, "red", "192.168.3.102/32");
    DeleteInetRoute(NULL, "red", "10.1.3.1/32");
    DeleteInetRoute(NULL, "red", "10.1.3.2/32");
    DeleteConnectedRoute(1, NULL, "192.168.1.253/32");
    DeleteConnectedRoute(2, NULL, "192.168.2.253/32");
}

//
// Instances are (blue)(blue-i1)(core-i2)(core)(core-i3)(red-i4)(red)
//                                             (core-i5)(green-i6)(green)
//
TEST_P(ServiceChainParamTest, TransitNetworkMultipleNetworks) {
    ParseConfigFile("controller/src/bgp/testdata/service_chain_test_2.xml");
    AddConnection("blue", "blue-i1");
    AddConnection("core", "core-i3");
    AddConnection("core", "core-i5");

    // Add more specific routes to red and green
    AddInetRoute(NULL, "red", "192.168.3.101/32", 100);
    AddInetRoute(NULL, "red", "192.168.3.102/32", 100);
    AddInetRoute(NULL, "green", "192.168.4.101/32", 100);
    AddInetRoute(NULL, "green", "192.168.4.102/32", 100);

    // Add Ext connect routes to red and green
    AddInetRoute(NULL, "red", "10.1.3.1/32", 100);
    AddInetRoute(NULL, "red", "10.1.3.2/32", 100);
    AddInetRoute(NULL, "green", "10.1.4.1/32", 100);
    AddInetRoute(NULL, "green", "10.1.4.2/32", 100);

    // Add Connected routes for the 3 chains
    AddConnectedRoute(1, NULL, "192.168.1.253/32", 100, "20.1.1.1");
    AddConnectedRoute(2, NULL, "192.168.2.253/32", 100, "20.1.1.2");
    AddConnectedRoute(3, NULL, "192.168.2.252/32", 100, "20.1.1.3");

    // Check for Aggregate routes in blue
    BgpRoute *aggregate_rt;
    aggregate_rt = VerifyInetRouteExists("blue", "192.168.3.0/24");
    EXPECT_EQ(GetOriginVnFromRoute(aggregate_rt->BestPath()), "core-vn");
    aggregate_rt = VerifyInetRouteExists("blue", "192.168.4.0/24");
    EXPECT_EQ(GetOriginVnFromRoute(aggregate_rt->BestPath()), "core-vn");

    // Check for ExtConnect routes in blue
    BgpRoute *ext_rt;
    ext_rt = VerifyInetRouteExists("blue", "10.1.3.1/32");
    EXPECT_EQ(GetOriginVnFromRoute(ext_rt->BestPath()), "core-vn");
    ext_rt = VerifyInetRouteExists("blue", "10.1.3.2/32");
    EXPECT_EQ(GetOriginVnFromRoute(ext_rt->BestPath()), "core-vn");
    ext_rt = VerifyInetRouteExists("blue", "10.1.4.1/32");
    EXPECT_EQ(GetOriginVnFromRoute(ext_rt->BestPath()), "core-vn");
    ext_rt = VerifyInetRouteExists("blue", "10.1.4.2/32");
    EXPECT_EQ(GetOriginVnFromRoute(ext_rt->BestPath()), "core-vn");

    // Delete Ext connect routes and connected routes
    DeleteInetRoute(NULL, "red", "192.168.3.101/32");
    DeleteInetRoute(NULL, "red", "192.168.3.102/32");
    DeleteInetRoute(NULL, "green", "192.168.4.101/32");
    DeleteInetRoute(NULL, "green", "192.168.4.102/32");
    DeleteInetRoute(NULL, "red", "10.1.3.1/32");
    DeleteInetRoute(NULL, "red", "10.1.3.2/32");
    DeleteInetRoute(NULL, "green", "10.1.4.1/32");
    DeleteInetRoute(NULL, "green", "10.1.4.2/32");
    DeleteConnectedRoute(1, NULL, "192.168.1.253/32");
    DeleteConnectedRoute(2, NULL, "192.168.2.253/32");
    DeleteConnectedRoute(3, NULL, "192.168.2.252/32");
}

INSTANTIATE_TEST_CASE_P(Instance, ServiceChainParamTest,
        ::testing::Combine(::testing::Bool(), ::testing::Bool()));

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

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
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
