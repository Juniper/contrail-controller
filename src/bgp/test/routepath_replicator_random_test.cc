/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/routepath_replicator.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/rtarget_group.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"

#include <algorithm> 
#include <iostream>
#include <sstream>
#include <set>
#include <string>

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif
#include <boost/lambda/lambda.hpp> 
#ifdef __clang__
#pragma clang diagnostic pop
#endif


#include <boost/program_options.hpp>

#include "base/test/task_test_util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/inet/inet_table.h"
#include "bgp/l3vpn/inetvpn_route.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/test/bgp_test_util.h"
#include "control-node/control_node.h"
#include "db/db_graph.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/test/ifmap_test_util.h"
#include "io/event_manager.h"
#include "schema/bgp_schema_types.h"
#include "testing/gunit.h"

using namespace std;
using boost::assign::list_of;
using boost::assign::map_list_of;

using namespace boost::program_options;
using ::testing::TestWithParam;
using ::testing::ValuesIn;
using ::testing::Combine;

static char **gargv;
static int    gargc;

typedef std::tr1::tuple<int, int, int> TestParams;

class BgpPeerMock : public IPeer {
public:
    BgpPeerMock(const Ip4Address &address) : address_(address) { }
    virtual ~BgpPeerMock() { }
    virtual std::string ToString() const {
        return address_.to_string();
    }
    virtual std::string ToUVEKey() const {
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
    virtual const std::string GetStateName() const {
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

#define VERIFY_EQ(expected, actual) \
    TASK_UTIL_EXPECT_EQ(expected, actual)

class ReplicationTest : public ::testing::TestWithParam<TestParams> {
public:
    // .second == "vpn" for VPN table
    typedef std::map<string, string> RouteAddMap;
    typedef std::vector<string> VrfList;
    typedef std::multimap<string, string> ConnectionMap;
protected:
    ReplicationTest()
        : bgp_server_(new BgpServer(&evm_)) {
        min_vrf_ = 32;
        IFMapLinkTable_Init(&config_db_, &config_graph_);
        vnc_cfg_Server_ModuleInit(&config_db_, &config_graph_);
        bgp_schema_Server_ModuleInit(&config_db_, &config_graph_);
    }
    ~ReplicationTest() {
        STLDeleteValues(&peers_);
    }

    virtual void SetUp() {
        InitParams();
        IFMapServerParser *parser = IFMapServerParser::GetInstance("schema");
        vnc_cfg_ParserInit(parser);
        bgp_schema_ParserInit(parser);
        bgp_server_->config_manager()->Initialize(&config_db_, &config_graph_,
                                                  "localhost");
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        bgp_server_->Shutdown();
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ_MSG(0, bgp_server_->routing_instance_mgr()->count(),
                                "Waiting for all routing-instances' deletion");
        task_util::WaitForIdle();
        db_util::Clear(&config_db_);
        task_util::WaitForIdle();
        IFMapServerParser *parser = IFMapServerParser::GetInstance("schema");
        parser->MetadataClear("schema");
        task_util::WaitForIdle();
    }

    void InitParams() {
        max_vrf_ = std::tr1::get<0>(GetParam());
        max_num_connections_ = std::tr1::get<1>(GetParam());
        max_iterations_ = std::tr1::get<2>(GetParam());
    }

    void NetworkConfig(const VrfList &instance_names,
                       const ConnectionMap &connections) {
        string netconf(
            bgp_util::NetworkConfigGenerate(instance_names, connections));
        IFMapServerParser *parser =
            IFMapServerParser::GetInstance("schema");
        parser->Receive(&config_db_, netconf.data(), netconf.length(), 0);
    }

    void DeleteRoutingInstance(string name, bool wait = true) {
        BGP_DEBUG_UT("DELETE routing instance " << name);
        // Remove All connections
        for(ConnectionMap::iterator it = connections_.find(name); 
            ((it != connections_.end()) && (it->first == name)); it++) {
            RemoveConnection(name, it->second);
        }
        connections_.erase(name);

        // Remove All connections to this VRF
        for(ConnectionMap::iterator it = connections_.begin(), next; 
            it != connections_.end(); it = next) {
            next = it;
            next++;
            if (it->second == name) {
                RemoveConnection(it->first, name);
                connections_.erase(it);
            }
        }

        // Delete Route
        for(RouteAddMap::iterator it = routes_added_.begin(), next; 
            it != routes_added_.end(); it = next) {
            next = it;
            next++;
            if (it->second == name) {
                DeleteInetRoute(peers_[0], it->second, it->first);
                routes_added_.erase(it);
            }
        }

        TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(name));
        RoutingInstance *rti =
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(name);

        //
        // Cache a copy of the export route-targets before the instance is
        // deleted
        //
        const RoutingInstance::RouteTargetList
            target_list(rti->GetExportList());
        BOOST_FOREACH(RouteTarget tgt, target_list) {
            ifmap_test_util::IFMapMsgUnlink(&config_db_,
                                            "routing-instance", name,
                                            "route-target", tgt.ToString(),
                                            "instance-target");
        }

        for(VrfList::iterator it = vrfs_.begin(); it != vrfs_.end(); it++) {
            if (*it == name) {
                *it = string("DELETED");
                break;
            }
        }
        if (wait) task_util::WaitForIdle();
    }


    void AddRoutingInstance(string name, bool wait = true) {
        TASK_UTIL_EXPECT_EQ(static_cast<RoutingInstance *>(NULL),
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(name));

        stringstream target;
        target << "target:64496:" << (vrfs_.size()+1);

        BGP_DEBUG_UT("ADD routing instance " << name << " Route Target " 
                     << target.str());
        ifmap_test_util::IFMapMsgLink(&config_db_,
                                      "routing-instance", name,
                                      "route-target", target.str(),
                                      "instance-target");
        vrfs_.push_back(name);
        if (wait) task_util::WaitForIdle();
        TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
            bgp_server_->routing_instance_mgr()->GetRoutingInstance(name));
    }

    void RemoveConnection(string src, string tgt, bool wait = true) {
        BGP_DEBUG_UT("DELETE connection " << src << "<->" << tgt); 
        ifmap_test_util::IFMapMsgUnlink(&config_db_,
                                        "routing-instance", src,
                                        "routing-instance", tgt,
                                        "connection");

        if (wait) task_util::WaitForIdle();
    }

    void AddConnection(string src, string tgt, bool wait = true) {
        BGP_DEBUG_UT("ADD connection " << src << "<->" << tgt); 
        ifmap_test_util::IFMapMsgLink(&config_db_,
                                      "routing-instance", src,
                                      "routing-instance", tgt,
                                      "connection");
        connections_.insert(std::pair<string, string>(src, tgt));
        if (wait) task_util::WaitForIdle();
    }

    void AddInetRoute(IPeer *peer, const string &vrf,
                      const string &prefix, int localpref, bool wait = true) {
        BGP_DEBUG_UT("ADD inet route " << vrf << ":" << prefix);
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
        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attr_spec);
        request.data.reset(new BgpTable::RequestData(attr, 0, 0));

        string tbl_name(vrf+".inet.0");
        TASK_UTIL_WAIT_NE_NO_MSG(bgp_server_->database()->FindTable(tbl_name), 
                                 NULL, 1000, 10000, "Wait for Inet table..");
        TASK_UTIL_EXPECT_NE(static_cast<BgpTable *>(NULL),
                            static_cast<BgpTable *>(
                                bgp_server_->database()->FindTable(tbl_name)));
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(tbl_name));
        table->Enqueue(&request);
        if (wait) task_util::WaitForIdle();
    }

    void DeleteInetRoute(IPeer *peer, const string &instance_name,
                         const string &prefix, bool wait = true) {
        BGP_DEBUG_UT("DELETE inet route " << instance_name << ":" << prefix);
        boost::system::error_code error;
        Ip4Prefix nlri = Ip4Prefix::FromString(prefix, &error);
        EXPECT_FALSE(error);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        request.key.reset(new InetTable::RequestKey(nlri, peer));

        TASK_UTIL_EXPECT_NE(static_cast<BgpTable *>(NULL),
            static_cast<BgpTable *>(
                bgp_server_->database()->FindTable(instance_name + ".inet.0")));
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(instance_name + ".inet.0"));

        table->Enqueue(&request);
        if (wait) task_util::WaitForIdle();
    }

    BgpRoute *VPNRouteLookup(const string &prefix) {
        BgpTable *table = static_cast<BgpTable *>(
            bgp_server_->database()->FindTable("bgp.l3vpn.0"));
        EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return NULL;
        }
        boost::system::error_code error;
        InetVpnPrefix nlri = InetVpnPrefix::FromString(prefix, &error);
        EXPECT_FALSE(error);
        InetVpnTable::RequestKey key(nlri, NULL);
        BgpRoute *rt = static_cast<BgpRoute *>(table->Find(&key));
        return rt;
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

    void GenerateVrfs(VrfList &list_vrf, ConnectionMap &connections) {
        uint32_t num_vrf_to_begin = min_vrf_ + rand() % max_vrf_;
        for (uint32_t i = 0; i < num_vrf_to_begin; i++) {
            stringstream oss;
            oss << "vrf_" << i;
            list_vrf.push_back(oss.str());
        }

        for (uint32_t i = 0; i < max_num_connections_; i++) {
            uint32_t src = 0;
            uint32_t dest = 0;
            do {
                do { 
                    src = rand() % num_vrf_to_begin;
                    dest = rand() % num_vrf_to_begin;
                } while (src == dest);
                ConnectionMap::iterator it = 
                    connections.find(list_vrf[src]);
                while (it != connections.end() && 
                       (it->first == list_vrf[src])) {
                    if (it->second == list_vrf[dest]) {
                        break;
                    }
                    it++;
                }

                if (it != connections.end() && it->second == list_vrf[dest]) {
                    continue;
                }
                connections.insert(std::pair<string, string>(list_vrf[src], 
                                                             list_vrf[dest]));
                break;
            } while (1);
        }
    }


    void WalkDone(DBTableBase *table) {
    }

    bool TableVerify(BgpServer *server, 
                     DBTablePartBase *root,
                     DBEntryBase *entry) {
        BgpRoute *rt = static_cast<BgpRoute *> (entry);
        BgpTable *table = static_cast<BgpTable *>(root->parent());
        RoutePathReplicator *replicator = server->replicator(Address::INETVPN);

        const RtReplicated *dbstate = 
            replicator->GetReplicationState(table, rt);

        if (entry->IsDeleted()) {
            assert(!dbstate);
            return true;
        } 

        for (Route::PathList::iterator path_it = rt->GetPathList().begin(); 
            path_it != rt->GetPathList().end(); path_it++) {
            BgpPath *path = static_cast<BgpPath *>(path_it.operator->());
            if (path->IsReplicated()) {
                const BgpSecondaryPath *secpath = 
                    static_cast<const BgpSecondaryPath *>(path);
                const BgpTable *pri_tbl = secpath->src_table();
                const BgpRoute *pri_rt = secpath->src_rt();
                BgpRoute *rt_to_check =  NULL;
                if (pri_tbl->family() == Address::INETVPN) {
                    // In this test the routes are added only to inet table, 
                    // hence it is an assert to have primary route from VPN table
                    assert(0);
                } else {
                    if (table->family() == Address::INETVPN) {
                        InetVpnRoute *vpn = static_cast<InetVpnRoute *>(rt);
                        Ip4Prefix prefix(vpn->GetPrefix().addr(), 
                                         vpn->GetPrefix().prefixlen());
                        RouteAddMap::iterator it = 
                            routes_added_.find(prefix.ToString());
                        assert(it != routes_added_.end());
                        assert(it->second == 
                               pri_tbl->routing_instance()->name());
                        rt_to_check = 
                            InetRouteLookup(pri_tbl->routing_instance()->name(),
                                            prefix.ToString());
                    } else {
                        RouteAddMap::iterator it = 
                            routes_added_.find(rt->ToString());
                        assert(it != routes_added_.end());
                        assert(it->second == 
                               pri_tbl->routing_instance()->name());
                        rt_to_check = 
                            InetRouteLookup(pri_tbl->routing_instance()->name(),
                                            rt->ToString());
                    }
                }
                assert(rt_to_check == pri_rt);
                continue;
            }

            RoutingInstance *rtinstance = table->routing_instance();
            assert(rtinstance);

            RouteAddMap::iterator it = routes_added_.find(rt->ToString());
            assert(it != routes_added_.end());
            assert(it->second == rtinstance->name());

            const ExtCommunity *ext_community = NULL;
            ExtCommunityPtr new_ext_community(NULL);
            const BgpAttr *attr = path->GetAttr();
            if (table->family() != Address::INETVPN) {
                ext_community = attr->ext_community();
                ExtCommunity::ExtCommunityList export_list;
                BOOST_FOREACH(RouteTarget rtarget, rtinstance->GetExportList()) {
                    export_list.push_back(rtarget.GetExtCommunity());
                }

                new_ext_community = 
                    server->extcomm_db()->AppendAndLocate(ext_community, 
                                                          export_list);
                ext_community = new_ext_community.get();
            } else {
                // In this test the routes are added only to inet table, 
                // hence it is an assert to have primary route from VPN table
                assert(0);
            }

            if (!ext_community) {
                assert(!dbstate);
                continue;
            }

            RtGroup::RtGroupMemberList super_set;

            BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm, 
                          ext_community->communities()) {
                RtGroup *rtgroup = 
                    server->rtarget_group_mgr()->GetRtGroup(comm);
                if (rtgroup) {
                    RtGroup::RtGroupMemberList import_list = 
                        rtgroup->GetImportTables(replicator->family());
                    if (!import_list.empty()) 
                        super_set.insert(import_list.begin(), 
                                         import_list.end());

                }
            }

            if (super_set.empty()) {
                assert(!dbstate);
                continue;
            }

            set<string> replicated_routes;
            BOOST_FOREACH(BgpTable *dest, super_set) {
                if (dest != table) {
                    BgpRoute *replicated_route = NULL;
                    if (dest->family() == Address::INETVPN) {
                        InetRoute *inet = static_cast<InetRoute *>(rt);
                        InetVpnPrefix vpn(*rtinstance->GetRD(),
                                          inet->GetPrefix().ip4_addr(),
                                          inet->GetPrefix().prefixlen());

                        replicated_route = VPNRouteLookup(vpn.ToString());
                    } else {
                        replicated_route = 
                            InetRouteLookup(dest->routing_instance()->name(), 
                                            rt->ToString());
                    }
                    assert(replicated_route->FindSecondaryPath(rt, 
                                           path->GetSource(), path->GetPeer(),
                                           path->GetPathId()));
                    string route_to_insert(dest->routing_instance()->name());
                    route_to_insert += ":";
                    route_to_insert += path->GetPeer()->ToString();
                    route_to_insert += ":";
                    route_to_insert += replicated_route->ToString();
                    replicated_routes.insert(route_to_insert);
                }
            }

            // secondary routes which are no longer replicated
            for (RtReplicated::ReplicatedRtPathList::iterator iter =
                 dbstate->GetList().begin();
                 iter != dbstate->GetList().end(); iter++) {
                RtReplicated::SecondaryRouteInfo rinfo = *iter;

                string route_to_find(rinfo.table_->routing_instance()->name());
                route_to_find += ":";
                route_to_find += rinfo.peer_->ToString();
                route_to_find += ":";
                route_to_find += rinfo.rt_->ToString();
                set<string>::iterator it = 
                    replicated_routes.find(route_to_find);

                assert(it != replicated_routes.end());
                replicated_routes.erase(it);
            }

            assert(replicated_routes.empty());
        }
        return true;
    }

    void DumpConnections() {
        for(ConnectionMap::iterator con_verify_it = connections_.begin(); 
            con_verify_it != connections_.end(); con_verify_it++) {
            BGP_DEBUG_UT(con_verify_it->first << "<->" 
                         << con_verify_it->second);
        }
    }

    bool VerifyConnection(string from_rt, string to_rt) {
        if (from_rt == to_rt) {
            return true;
        }

        for(ConnectionMap::iterator con_verify_it = connections_.begin(); 
            con_verify_it != connections_.end(); con_verify_it++) {
            if (((con_verify_it->first == from_rt) && 
                 (con_verify_it->second == to_rt)) || 
                ((con_verify_it->first == to_rt) && 
                 (con_verify_it->second == from_rt))) {
                return true;
            }
        }
        return false;
    }

    bool VerifyReplicationState() {
        DBTableWalker::WalkCompleteFn walk_complete 
            = boost::bind(&ReplicationTest::WalkDone, this, _1);

        DBTableWalker::WalkFn walker 
            = boost::bind(&ReplicationTest::TableVerify, this, 
                          bgp_server_.get(), _1, _2);

        DB *db = bgp_server_->database();
        RoutingInstanceMgr *rtinst_mgr = bgp_server_->routing_instance_mgr();

        for (RoutingInstanceMgr::RoutingInstanceIterator it = 
             rtinst_mgr->begin(); it != rtinst_mgr->end(); it++) {
            BOOST_FOREACH(RouteTarget tgt, it->GetImportList()) {
                const RoutingInstance *from_rt 
                    = rtinst_mgr->GetInstanceByTarget(tgt);
                assert(from_rt);
                assert(VerifyConnection(from_rt->name(), it->name()));
            }
            BgpTable *table = it->GetTable(Address::INET);
            if (table != NULL) {
                db->GetWalker()->WalkTable(table, NULL, walker, walk_complete);
            }
            table = it->GetTable(Address::INETVPN);
            if (table != NULL) {
                db->GetWalker()->WalkTable(table, NULL, walker, walk_complete);

            }
        }
        return true;
    }

    EventManager evm_;
    DB config_db_;
    DBGraph config_graph_;
    boost::scoped_ptr<BgpServer> bgp_server_;
    vector<BgpPeerMock *> peers_;
    uint32_t min_vrf_;
    uint32_t max_vrf_;
    uint32_t max_num_connections_;
    uint32_t max_iterations_;
    RouteAddMap routes_added_;
    VrfList vrfs_;
    ConnectionMap connections_;
};

TEST_P(ReplicationTest, RandomTest) {
    boost::system::error_code ec;
    peers_.push_back(new BgpPeerMock(Ip4Address::from_string("192.168.0.1", 
                                                             ec)));
    GenerateVrfs(vrfs_, connections_);
    NetworkConfig(vrfs_, connections_);
    task_util::WaitForIdle();
    // Start with random number of routes in VRFs
    for (uint32_t num_iteration = 0; 
         num_iteration < max_iterations_; 
         num_iteration++) {
        uint32_t vrf_index = rand() % vrfs_.size();
        while(true) {
            stringstream oss;
            oss << "172.168.";
            oss << (rand() % 255) << ".";
            oss << (rand() % 255) << "/32";
            // Add Route
            pair<RouteAddMap::iterator, bool> ret = 
                routes_added_.insert(pair<string, string>(oss.str(), 
                                                          vrfs_[vrf_index]));
            if (ret.second == false) {
                continue;
            }
            AddInetRoute(peers_[0], vrfs_[vrf_index], oss.str(), 100);
            break;
        }
    }

    task_util::WaitForIdle();

    for (uint32_t num_iteration = 0; 
         num_iteration < max_iterations_; 
         num_iteration++) {
        uint32_t vrf_index = 0;
        do {
            vrf_index = rand() % vrfs_.size();
        } while(vrfs_[vrf_index] == "DELETED");

        uint8_t vrf_or_route = rand() % 2;
        if (vrf_or_route) {
            // Play with VRF and connections
            uint8_t vrf_or_connection = rand() % 2;
            if (vrf_or_connection) {
                // VRF
                uint8_t add_or_del_vrf = rand() % 2;
                if (add_or_del_vrf) {
                    // Add 
                    stringstream oss;
                    oss << "vrf_" << vrfs_.size();
                    AddRoutingInstance(oss.str());
                } else {
                    size_t count = count_if(vrfs_.begin(), vrfs_.end(), 
                                            (boost::lambda::_1 == "DELETED"));
                    if (count < (vrfs_.size() - 2)) {
                        DeleteRoutingInstance(vrfs_[vrf_index]);
                    }
                }
            } else {
                // Connections
                uint8_t add_or_del_link = rand() % 2;
                if (add_or_del_link) {
                    // Add 
                    uint32_t dest = 0;
                    do {
                        do { 
                            dest = rand() % vrfs_.size();
                        } while ((vrfs_[dest] == "DELETED") || 
                                 (vrf_index == dest));
                        ConnectionMap::iterator it = 
                            connections_.find(vrfs_[vrf_index]);
                        while (it != connections_.end() && 
                               (it->first == vrfs_[vrf_index])) {
                            if (it->second == vrfs_[dest]) {
                                break;
                            }
                            it++;
                        }

                        if (it != connections_.end() && 
                            it->second == vrfs_[dest]) {
                            continue;
                        }
                        break;
                    } while (1);
                    AddConnection(vrfs_[vrf_index], vrfs_[dest]);
                } else {
                    // Delete a Link
                    ConnectionMap::iterator it = 
                        connections_.find(vrfs_[vrf_index]);
                    if (it != connections_.end()) {
                        RemoveConnection(it->first, it->second);
                        connections_.erase(it);
                    }
                }
            }
        } else {
            // Play with Routes
            uint8_t adc_route = rand() % 3;
            if (adc_route == 0) {
                while(true) {
                    stringstream oss;
                    oss << "172.168.";
                    oss << (rand() % 255) << ".";
                    oss << (rand() % 255) << "/32";
                    // Add Route
                    pair<RouteAddMap::iterator, bool> ret = 
                        routes_added_.insert(pair<string, 
                                             string>(oss.str(), 
                                                     vrfs_[vrf_index]));
                    if (ret.second == false) {
                        continue;
                    }
                    AddInetRoute(peers_[0], vrfs_[vrf_index], oss.str(), 100);
                    break;
                }
            } else if (adc_route == 1) {
                if (routes_added_.size() == 0) {
                    continue;
                }
                uint32_t at_index = rand() % routes_added_.size();
                uint32_t count = 0;
                // Delete Route
                for(RouteAddMap::iterator it = routes_added_.begin(); 
                    it != routes_added_.end(); it++, count++) {
                    if (count == at_index) {
                        DeleteInetRoute(peers_[0], it->second, it->first);
                        routes_added_.erase(it);
                        break;
                    }
                }
            } else {
                if (routes_added_.size() == 0) {
                    continue;
                }
                // Update Route
                uint32_t at_index = rand() % routes_added_.size();
                uint32_t count = 0;
                // Delete Route
                for(RouteAddMap::iterator it = routes_added_.begin(); 
                    it != routes_added_.end(); it++, count++) {
                    if (count == at_index) {
                        AddInetRoute(peers_[0], it->second, it->first, 10);
                        break;
                    }
                }
            }
        }
        task_util::WaitForIdle();
    }

    // Verify whether the routes are in consistent state
    // Need to start multiple walks
    VerifyReplicationState();

    task_util::WaitForIdle();

    BOOST_FOREACH(RouteAddMap::value_type mapref, routes_added_) {
        DeleteInetRoute(peers_[0], mapref.second, mapref.first);
    }

    task_util::WaitForIdle();
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
}

static void TearDown() {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

static vector<int> n_vrfs = boost::assign::list_of(5)(10);
static vector<int> n_connections = boost::assign::list_of(5)(10);
static vector<int> n_iterations = boost::assign::list_of(5)(10);

static void process_command_line_args(int argc, char **argv) {
    int vrfs = 1, connections = 1, iterations = 1;
    options_description desc("Allowed options");
    bool cmd_line_arg_set = false;
    desc.add_options()
        ("help", "produce help message")
        ("vrfs", value<int>(), "set number of VRFs")
        ("connections", value<int>(), "set number of connectios")
        ("iterations", value<int>(), "set number of iterations")
        ;

    variables_map vm;
    store(parse_command_line(argc, argv, desc), vm);
    notify(vm);

    if (vm.count("help")) {
        cout << desc << "\n";
        exit(1);
    }

    if (vm.count("vrfs")) {
        vrfs = vm["vrfs"].as<int>();
        cmd_line_arg_set = true;
    }

    if (vm.count("connections")) {
        connections = vm["connections"].as<int>();
        cmd_line_arg_set = true;
    }

    if (vm.count("iterations")) {
        iterations = vm["iterations"].as<int>();
        cmd_line_arg_set = true;
    }

    if (cmd_line_arg_set) {
        n_vrfs.clear();
        n_vrfs.push_back(vrfs);
        n_connections.clear();
        n_connections.push_back(connections);
        n_iterations.clear();
        n_iterations.push_back(iterations);
    }
}


static vector<int> GetTestParam() {
    static bool cmd_line_processed;

    if (!cmd_line_processed) {
        cmd_line_processed = true;
        process_command_line_args(gargc, gargv);
    }

    return n_vrfs;
}

#define COMBINE_PARAMS \
    Combine(ValuesIn(GetTestParam()), \
            ValuesIn(n_connections), \
            ValuesIn(n_iterations))

INSTANTIATE_TEST_CASE_P(ReplicatorRandomTestWithParams, ReplicationTest, 
                        COMBINE_PARAMS);

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
