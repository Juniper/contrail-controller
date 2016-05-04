/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/path_resolver.h"

#include <boost/assign/list_of.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/program_options.hpp>

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/extended-community/load_balance.h"
#include "bgp/security_group/security_group.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet6/inet6_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"

using namespace boost::program_options;
using boost::assign::list_of;
using std::cout;
using std::endl;
using std::set;
using std::string;
using std::vector;

class PeerMock : public IPeer {
public:
    PeerMock(bool is_xmpp, const string &address_str)
        : is_xmpp_(is_xmpp) {
        boost::system::error_code ec;
        address_ = Ip4Address::from_string(address_str, ec);
        assert(ec.value() == 0);
    }
    virtual ~PeerMock() { }
    virtual std::string ToString() const { return address_.to_string(); }
    virtual std::string ToUVEKey() const { return address_.to_string(); }
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) { return true; }
    virtual BgpServer *server() { return NULL; }
    virtual IPeerClose *peer_close() { return NULL; }
    virtual IPeerDebugStats *peer_stats() { return NULL; }
    virtual const IPeerDebugStats *peer_stats() const { return NULL; }
    virtual bool IsReady() const { return true; }
    virtual bool IsXmppPeer() const { return is_xmpp_; }
    virtual bool IsRegistrationRequired() const { return false; }
    virtual void Close(bool non_graceful) { }
    BgpProto::BgpPeerType PeerType() const {
        return is_xmpp_ ? BgpProto::XMPP : BgpProto::EBGP;
    }
    virtual uint32_t bgp_identifier() const {
        return htonl(address_.to_ulong());
    }
    virtual const std::string GetStateName() const { return ""; }
    virtual void UpdateRefCount(int count) const { }
    virtual tbb::atomic<int> GetRefCount() const {
        tbb::atomic<int> count;
        count = 0;
        return count;
    }
    virtual void UpdatePrimaryPathCount(int count) const { }
    virtual int GetPrimaryPathCount() const { return 0; }

private:
    bool is_xmpp_;
    Ip4Address address_;
};

static const char *config = "\
<config>\
    <bgp-router name=\'localhost\'>\
        <identifier>192.168.0.100</identifier>\
        <address>192.168.0.100</address>\
        <autonomous-system>64512</autonomous-system>\
    </bgp-router>\
    <routing-instance name='blue'>\
        <vrf-target>target:64512:1</vrf-target>\
    </routing-instance>\
    <routing-instance name='pink'>\
        <vrf-target>target:64512:2</vrf-target>\
    </routing-instance>\
</config>\
";

// Overlay nexthop address family.
static bool nexthop_family_is_inet;

//
// Template structure to pass to fixture class template. Needed because
// gtest fixture class template can accept only one template parameter.
//
template <typename T1, typename T2>
struct TypeDefinition {
  typedef T1 TableT;
  typedef T2 PrefixT;
};

// TypeDefinitions that we want to test.
typedef TypeDefinition<InetTable, Ip4Prefix> InetDefinition;
typedef TypeDefinition<Inet6Table, Inet6Prefix> Inet6Definition;

//
// Fixture class template - instantiated later for each TypeDefinition.
//
template <typename T>
class PathResolverTest : public ::testing::Test {
protected:
    typedef typename T::TableT TableT;
    typedef typename T::PrefixT PrefixT;

    PathResolverTest()
        : bgp_server_(new BgpServerTest(&evm_, "localhost")),
          family_(GetFamily()),
          ipv6_prefix_("::ffff:"),
          bgp_peer1_(new PeerMock(false, "192.168.1.1")),
          bgp_peer2_(new PeerMock(false, "192.168.1.2")),
          xmpp_peer1_(new PeerMock(true, "172.16.1.1")),
          xmpp_peer2_(new PeerMock(true, "172.16.1.2")),
          validate_done_(false) {
        bgp_server_->session_manager()->Initialize(0);
    }
    ~PathResolverTest() {
        delete bgp_peer1_;
        delete bgp_peer2_;
        delete xmpp_peer1_;
        delete xmpp_peer2_;
    }

    virtual void SetUp() {
        bgp_server_->Configure(config);
        task_util::WaitForIdle();
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        bgp_server_->Shutdown();
        task_util::WaitForIdle();
    }

    Address::Family GetFamily() const {
        assert(false);
        return Address::UNSPEC;
    }

    // Return the overlay nexthop address.
    string BuildHostAddress(const string &ipv4_addr) const {
        if (nexthop_family_is_inet) {
            return ipv4_addr;
        } else {
            return ipv6_prefix_ + ipv4_addr;
        }
    }

    // Overlay nexthop as a prefix.
    string BuildPrefix(const string &ipv4_prefix, uint8_t ipv4_plen) const {
        if (nexthop_family_is_inet) {
            return ipv4_prefix + "/" + integerToString(ipv4_plen);
        } else {
            return ipv6_prefix_ + ipv4_prefix + "/" +
                integerToString(96 + ipv4_plen);
        }
    }

    string BuildPrefix(int index) const {
        assert(index <= 65535);
        string ipv4_prefix("10.1.");
        uint8_t ipv4_plen = Address::kMaxV4PrefixLen;
        string byte3 = integerToString(index / 256);
        string byte4 = integerToString(index % 256);
        if (family_ == Address::INET) {
            return ipv4_prefix + byte3 + "." + byte4 + "/" +
                integerToString(ipv4_plen);
        }
        if (family_ == Address::INET6) {
            return ipv6_prefix_ + ipv4_prefix + byte3 + "." + byte4 + "/" +
                integerToString(96 + ipv4_plen);
        }
        assert(false);
        return "";
    }

    string BuildNextHopAddress(const string &ipv4_addr) const {
        return ipv4_addr;
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

    BgpTable *GetTable(const string &instance) {
        return static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(GetTableName(instance)));
    }

    string GetNexthopTableName(const string &instance) const {
        if (nexthop_family_is_inet) {
            return instance + ".inet.0";
        } else {
            return instance + ".inet6.0";
        }
    }

    BgpTable *GetNexthopTable(const string &instance) {
        return static_cast<BgpTable *>(
            bgp_server_->database()->FindTable(GetNexthopTableName(instance)));
    }

    // Add a BgpPath that requires resolution.
    void AddBgpPath(IPeer *bgp_peer, const string &instance,
        const string &prefix_str, const string &nexthop_str, uint32_t med = 0,
        const vector<uint32_t> &comm_list = vector<uint32_t>(),
        const vector<uint16_t> &as_list = vector<uint16_t>(),
        uint32_t flags = 0) {
        assert(!bgp_peer->IsXmppPeer());

        boost::system::error_code ec;
        PrefixT prefix = PrefixT::FromString(prefix_str, &ec);
        EXPECT_FALSE(ec);
        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(new typename TableT::RequestKey(prefix, bgp_peer));

        BgpTable *table = GetTable(instance);
        BgpAttrSpec attr_spec;

        IpAddress nh_addr = IpAddress::from_string(nexthop_str, ec);
        EXPECT_FALSE(ec);
        BgpAttrNextHop nh_spec(nh_addr);
        attr_spec.push_back(&nh_spec);

        BgpAttrMultiExitDisc med_spec(med);
        if (med)
            attr_spec.push_back(&med_spec);

        CommunitySpec comm_spec;
        if (!comm_list.empty()) {
            comm_spec.communities = comm_list;
            attr_spec.push_back(&comm_spec);
        }

        AsPathSpec aspath_spec;
        AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
        ps->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
        BOOST_FOREACH(uint16_t asn, as_list) {
            ps->path_segment.push_back(asn);
        }
        aspath_spec.path_segments.push_back(ps);
        attr_spec.push_back(&aspath_spec);

        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attr_spec);
        request.data.reset(
            new BgpTable::RequestData(attr, flags|BgpPath::ResolveNexthop, 0));
        table->Enqueue(&request);
    }

    void AddBgpPathWithMed(IPeer *bgp_peer, const string &instance,
        const string &prefix_str, const string &nexthop_str, uint32_t med) {
        AddBgpPath(bgp_peer, instance, prefix_str, nexthop_str, med);
    }

    void AddBgpPathWithCommunities(IPeer *bgp_peer, const string &instance,
        const string &prefix_str, const string &nexthop_str,
        const vector<uint32_t> &comm_list) {
        AddBgpPath(bgp_peer, instance, prefix_str, nexthop_str, 0, comm_list);
    }

    void AddBgpPathWithAsList(IPeer *bgp_peer, const string &instance,
        const string &prefix_str, const string &nexthop_str,
        const vector<uint16_t> &as_list) {
        AddBgpPath(bgp_peer, instance, prefix_str, nexthop_str, 0,
            vector<uint32_t>(), as_list);
    }

    void AddBgpPathWithFlags(IPeer *bgp_peer, const string &instance,
        const string &prefix_str, const string &nexthop_str, uint32_t flags) {
        AddBgpPath(bgp_peer, instance, prefix_str, nexthop_str, 0,
            vector<uint32_t>(), vector<uint16_t>(), flags);
    }

    template <typename XmppTableT, typename XmppPrefixT>
    void AddXmppPathCommon(IPeer *xmpp_peer, const string &instance,
        const string &prefix_str, const string &nexthop_str1,
        int label, const string &nexthop_str2, vector<uint32_t> sgid_list,
        set<string> encap_list, const LoadBalance &lb) {

        boost::system::error_code ec;
        XmppPrefixT prefix = XmppPrefixT::FromString(prefix_str, &ec);
        EXPECT_FALSE(ec);
        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(
            new typename XmppTableT::RequestKey(prefix, xmpp_peer));

        BgpTable *table = GetNexthopTable(instance);
        int index = table->routing_instance()->index();
        BgpAttrSpec attr_spec;

        Ip4Address nh_addr1 = Ip4Address::from_string(nexthop_str1, ec);
        EXPECT_FALSE(ec);
        BgpAttrNextHop nh_spec(nh_addr1);
        attr_spec.push_back(&nh_spec);

        BgpAttrSourceRd source_rd_spec(
            RouteDistinguisher(nh_addr1.to_ulong(), index));
        attr_spec.push_back(&source_rd_spec);

        ExtCommunitySpec extcomm_spec;
        for (vector<uint32_t>::iterator it = sgid_list.begin();
             it != sgid_list.end(); ++it) {
            SecurityGroup sgid(64512, *it);
            uint64_t value = sgid.GetExtCommunityValue();
            extcomm_spec.communities.push_back(value);
        }
        for (set<string>::iterator it = encap_list.begin();
             it != encap_list.end(); ++it) {
            TunnelEncap encap(*it);
            uint64_t value = encap.GetExtCommunityValue();
            extcomm_spec.communities.push_back(value);
        }
        if (!lb.IsDefault()) {
            uint64_t value = lb.GetExtCommunityValue();
            extcomm_spec.communities.push_back(value);
        }
        if (!extcomm_spec.communities.empty())
            attr_spec.push_back(&extcomm_spec);

        BgpAttrPtr attr = bgp_server_->attr_db()->Locate(attr_spec);

        typename XmppTableT::RequestData::NextHops nexthops;
        typename XmppTableT::RequestData::NextHop nexthop1;
        nexthop1.flags_ = 0;
        nexthop1.address_ = nh_addr1;
        nexthop1.label_ = label;
        nexthop1.source_rd_ = RouteDistinguisher(nh_addr1.to_ulong(), index);
        nexthops.push_back(nexthop1);

        typename XmppTableT::RequestData::NextHop nexthop2;
        if (!nexthop_str2.empty()) {
            Ip4Address nh_addr2 = Ip4Address::from_string(nexthop_str2, ec);
            EXPECT_FALSE(ec);
            nexthop2.flags_ = 0;
            nexthop2.address_ = nh_addr2;
            nexthop2.label_ = label;
            nexthop2.source_rd_ =
                RouteDistinguisher(nh_addr2.to_ulong(), index);
            nexthops.push_back(nexthop2);
        }

        request.data.reset(new BgpTable::RequestData(attr, nexthops));
        table->Enqueue(&request);
    }

    // Add a BgpPath that that can be used to resolve other paths.
    void AddXmppPath(IPeer *xmpp_peer, const string &instance,
        const string &prefix_str, const string &nexthop_str1,
        int label, const string &nexthop_str2 = string(),
        vector<uint32_t> sgid_list = vector<uint32_t>(),
        set<string> encap_list = set<string>(),
        const LoadBalance &lb = LoadBalance()) {
        assert(xmpp_peer->IsXmppPeer());
        assert(!nexthop_str1.empty());
        assert(label);
        if (nexthop_family_is_inet) {
            AddXmppPathCommon<InetTable, Ip4Prefix>(
                xmpp_peer, instance, prefix_str, nexthop_str1, label,
                nexthop_str2, sgid_list, encap_list, lb);
        } else {
            AddXmppPathCommon<Inet6Table, Inet6Prefix>(
                xmpp_peer, instance, prefix_str, nexthop_str1, label,
                nexthop_str2, sgid_list, encap_list, lb);
        }
    }

    void AddXmppPathWithSecurityGroups(IPeer *xmpp_peer,
        const string &instance, const string &prefix_str,
        const string &nexthop_str, int label, vector<uint32_t> sgid_list) {
        AddXmppPath(xmpp_peer, instance, prefix_str, nexthop_str, label,
            string(), sgid_list);
    }

    void AddXmppPathWithTunnelEncapsulations(IPeer *xmpp_peer,
        const string &instance, const string &prefix_str,
        const string &nexthop_str, int label, set<string> encap_list) {
        AddXmppPath(xmpp_peer, instance, prefix_str, nexthop_str, label,
            string(), vector<uint32_t>(), encap_list);
    }

    void AddXmppPathWithLoadBalance(IPeer *xmpp_peer,
        const string &instance, const string &prefix_str,
        const string &nexthop_str, int label, const LoadBalance &lb) {
        AddXmppPath(xmpp_peer, instance, prefix_str, nexthop_str, label,
            string(), vector<uint32_t>(), set<string>(), lb);
    }

    void DeletePath(IPeer *peer, const string &instance,
        const string &prefix_str) {
    }

    void DeleteBgpPath(IPeer *bgp_peer, const string &instance,
        const string &prefix_str) {
        boost::system::error_code ec;
        PrefixT prefix = PrefixT::FromString(prefix_str, &ec);
        EXPECT_FALSE(ec);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        request.key.reset(new typename TableT::RequestKey(prefix, bgp_peer));

        BgpTable *table = GetTable(instance);
        table->Enqueue(&request);
    }

    template <typename XmppTableT, typename XmppPrefixT>
    void DeleteXmppPathCommon(IPeer *xmpp_peer, const string &instance,
        const string &prefix_str) {
        boost::system::error_code ec;
        XmppPrefixT prefix = XmppPrefixT::FromString(prefix_str, &ec);
        EXPECT_FALSE(ec);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        request.key.reset(
            new typename XmppTableT::RequestKey(prefix, xmpp_peer));

        BgpTable *table = GetNexthopTable(instance);
        table->Enqueue(&request);
    }

    void DeleteXmppPath(IPeer *xmpp_peer, const string &instance,
        const string &prefix_str) {
        if (nexthop_family_is_inet) {
            DeleteXmppPathCommon<InetTable, Ip4Prefix>(
                xmpp_peer, instance, prefix_str);
        } else {
            DeleteXmppPathCommon<Inet6Table, Inet6Prefix>(
                xmpp_peer, instance, prefix_str);
        }
    }

    void DisableConditionListener() {
        Address::Family family =
            nexthop_family_is_inet ? Address::INET : Address::INET6;
        BgpConditionListener *condition_listener =
            bgp_server_->condition_listener(family);
        condition_listener->DisableTableWalkProcessing();
    }

    void EnableConditionListener() {
        Address::Family family =
            nexthop_family_is_inet ? Address::INET : Address::INET6;
        BgpConditionListener *condition_listener =
            bgp_server_->condition_listener(family);
        condition_listener->EnableTableWalkProcessing();
    }

    size_t ResolverNexthopMapSize(const string &instance) {
        BgpTable *table = GetTable(instance);
        return table->path_resolver()->GetResolverNexthopMapSize();
    }

    size_t ResolverNexthopDeleteListSize(const string &instance) {
        BgpTable *table = GetTable(instance);
        return table->path_resolver()->GetResolverNexthopDeleteListSize();
    }

    void DisableResolverNexthopRegUnregProcessing(const string &instance) {
        BgpTable *table = GetTable(instance);
        table->path_resolver()->DisableResolverNexthopRegUnregProcessing();
    }

    void EnableResolverNexthopRegUnregProcessing(const string &instance) {
        BgpTable *table = GetTable(instance);
        table->path_resolver()->EnableResolverNexthopRegUnregProcessing();
    }

    size_t ResolverNexthopRegUnregListSize(const string &instance) {
        BgpTable *table = GetTable(instance);
        return table->path_resolver()->GetResolverNexthopRegUnregListSize();
    }

    void DisableResolverNexthopUpdateProcessing(const string &instance) {
        BgpTable *table = GetTable(instance);
        table->path_resolver()->DisableResolverNexthopUpdateProcessing();
    }

    void EnableResolverNexthopUpdateProcessing(const string &instance) {
        BgpTable *table = GetTable(instance);
        table->path_resolver()->EnableResolverNexthopUpdateProcessing();
    }

    size_t ResolverNexthopUpdateListSize(const string &instance) {
        BgpTable *table = GetTable(instance);
        return table->path_resolver()->GetResolverNexthopUpdateListSize();
    }

    void DisableResolverPathUpdateProcessing(const string &instance) {
        BgpTable *table = GetTable(instance);
        table->path_resolver()->DisableResolverPathUpdateProcessing();
    }

    void EnableResolverPathUpdateProcessing(const string &instance) {
        BgpTable *table = GetTable(instance);
        table->path_resolver()->EnableResolverPathUpdateProcessing();
    }

    void PauseResolverPathUpdateProcessing(const string &instance) {
        BgpTable *table = GetTable(instance);
        table->path_resolver()->PauseResolverPathUpdateProcessing();
    }

    void ResumeResolverPathUpdateProcessing(const string &instance) {
        BgpTable *table = GetTable(instance);
        table->path_resolver()->ResumeResolverPathUpdateProcessing();
    }

    size_t ResolverPathUpdateListSize(const string &instance) {
        BgpTable *table = GetTable(instance);
        return table->path_resolver()->GetResolverPathUpdateListSize();
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
        BgpRoute *rt = dynamic_cast<BgpRoute *>(table->Find(&key));
        return rt;
    }

    vector<uint32_t> GetSGIDListFromPath(const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        vector<uint32_t> list;
        if (!ext_comm)
            return list;
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

    set<string> GetTunnelEncapListFromPath(const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        set<string> list;
        if (!ext_comm)
            return list;
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_comm->communities()) {
            if (!ExtCommunity::is_tunnel_encap(comm))
                continue;
            TunnelEncap encap(comm);
            list.insert(encap.ToXmppString());
        }
        return list;
    }

    LoadBalance GetLoadBalanceFromPath(const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        if (!ext_comm)
            return LoadBalance();
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_comm->communities()) {
            if (!ExtCommunity::is_load_balance(comm))
                continue;
            return LoadBalance(comm);
        }
        return LoadBalance();
    }

    vector<uint32_t> GetCommunityListFromPath(const BgpPath *path) {
        const Community *comm = path->GetAttr()->community();
        vector<uint32_t> list = comm ? comm->communities() : vector<uint32_t>();
        sort(list.begin(), list.end());
        return list;
    }

    vector<uint16_t> GetAsListFromPath(const BgpPath *path) {
        vector<uint16_t> list;
        const AsPath *aspath = path->GetAttr()->as_path();
        const AsPathSpec &aspath_spec = aspath->path();
        if (aspath_spec.path_segments.size() != 1)
            return list;
        if (aspath_spec.path_segments[0] == NULL)
            return list;
        if (aspath_spec.path_segments[0]->path_segment_type !=
            AsPathSpec::PathSegment::AS_SEQUENCE)
            return list;
        list = aspath_spec.path_segments[0]->path_segment;
        return list;
    }

    bool MatchPathAttributes(const BgpPath *path, const string &path_id,
        uint32_t label, const vector<uint32_t> &sgid_list,
        const set<string> &encap_list, const LoadBalance &lb, uint32_t med,
        const CommunitySpec &comm_spec, const vector<uint16_t> &as_list) {
        const BgpAttr *attr = path->GetAttr();
        if (attr->nexthop().to_v4().to_string() != path_id)
            return false;
        if (label && path->GetLabel() != label)
            return false;
        if (sgid_list.size()) {
            vector<uint32_t> path_sgid_list = GetSGIDListFromPath(path);
            if (path_sgid_list.size() != sgid_list.size())
                return false;
            for (vector<uint32_t>::const_iterator
                it1 = path_sgid_list.begin(), it2 = sgid_list.begin();
                it1 != path_sgid_list.end() && it2 != sgid_list.end();
                ++it1, ++it2) {
                if (*it1 != *it2)
                    return false;
            }
        }
        if (encap_list.size()) {
            set<string> path_encap_list = GetTunnelEncapListFromPath(path);
            if (path_encap_list != encap_list)
                return false;
        }
        if (!lb.IsDefault()) {
            LoadBalance path_lb = GetLoadBalanceFromPath(path);
            if (path_lb != lb)
                return false;
        }
        if (attr->med() != med) {
            return false;
        }
        vector<uint32_t> path_comm_list = GetCommunityListFromPath(path);
        if (path_comm_list != comm_spec.communities) {
            return false;
        }
        vector<uint16_t> path_as_list = GetAsListFromPath(path);
        if (path_as_list != as_list) {
            return false;
        }

        return true;
    }

    bool CheckPathAttributes(const string &instance, const string &prefix,
        const string &path_id, int label, const vector<uint32_t> &sgid_list,
        const set<string> &encap_list, const LoadBalance &lb, uint32_t med,
        const CommunitySpec &comm_spec, const vector<uint16_t> &as_list) {
        task_util::TaskSchedulerLock lock;
        BgpRoute *route = RouteLookup(instance, prefix);
        if (!route)
            return false;
        for (Route::PathList::iterator it = route->GetPathList().begin();
             it != route->GetPathList().end(); ++it) {
            const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
            if ((path->GetFlags() & BgpPath::ResolvedPath) == 0)
                continue;
            if (BgpPath::PathIdString(path->GetPathId()) != path_id)
                continue;
            if (MatchPathAttributes(path, path_id, label, sgid_list,
                encap_list, lb, med, comm_spec, as_list)) {
                return true;
            }
            return false;
        }

        return false;
    }

    bool CheckPathNoExists(const string &instance, const string &prefix,
        const string &path_id) {
        task_util::TaskSchedulerLock lock;
        BgpRoute *route = RouteLookup(instance, prefix);
        if (!route)
            return true;
        for (Route::PathList::iterator it = route->GetPathList().begin();
             it != route->GetPathList().end(); ++it) {
            const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
            if ((path->GetFlags() & BgpPath::ResolvedPath) == 0)
                continue;
            if (BgpPath::PathIdString(path->GetPathId()) == path_id)
                return false;
        }

        return true;
    }

    void VerifyPathAttributes(const string &instance, const string &prefix,
        const string &path_id, uint32_t label) {
        TASK_UTIL_EXPECT_TRUE(CheckPathAttributes(instance, prefix,
            path_id, label, vector<uint32_t>(), set<string>(), LoadBalance(),
            0, CommunitySpec(), vector<uint16_t>()));
    }

    void VerifyPathAttributes(const string &instance, const string &prefix,
        const string &path_id, uint32_t label, const set<string> &encap_list) {
        TASK_UTIL_EXPECT_TRUE(CheckPathAttributes(instance, prefix,
            path_id, label, vector<uint32_t>(), encap_list, LoadBalance(),
            0, CommunitySpec(), vector<uint16_t>()));
    }

    void VerifyPathAttributes(const string &instance, const string &prefix,
        const string &path_id, uint32_t label,
        const vector<uint32_t> &sgid_list) {
        TASK_UTIL_EXPECT_TRUE(CheckPathAttributes(instance, prefix,
            path_id, label, sgid_list, set<string>(), LoadBalance(),
            0, CommunitySpec(), vector<uint16_t>()));
    }

    void VerifyPathAttributes(const string &instance, const string &prefix,
        const string &path_id, uint32_t label, const LoadBalance &lb) {
        TASK_UTIL_EXPECT_TRUE(CheckPathAttributes(instance, prefix,
            path_id, label, vector<uint32_t>(), set<string>(), lb,
            0, CommunitySpec(), vector<uint16_t>()));
    }

    void VerifyPathAttributes(const string &instance, const string &prefix,
        const string &path_id, uint32_t label, uint32_t med) {
        TASK_UTIL_EXPECT_TRUE(CheckPathAttributes(instance, prefix,
            path_id, label, vector<uint32_t>(), set<string>(), LoadBalance(),
            med, CommunitySpec(), vector<uint16_t>()));
    }

    void VerifyPathAttributes(const string &instance, const string &prefix,
        const string &path_id, uint32_t label, const CommunitySpec &comm_spec) {
        TASK_UTIL_EXPECT_TRUE(CheckPathAttributes(instance, prefix,
            path_id, label, vector<uint32_t>(), set<string>(), LoadBalance(),
            0, comm_spec, vector<uint16_t>()));
    }

    void VerifyPathAttributes(const string &instance, const string &prefix,
        const string &path_id, uint32_t label,
        const vector<uint16_t> &as_list) {
        TASK_UTIL_EXPECT_TRUE(CheckPathAttributes(instance, prefix,
            path_id, label, vector<uint32_t>(), set<string>(), LoadBalance(),
            0, CommunitySpec(), as_list));
    }

    void VerifyPathNoExists(const string &instance, const string &prefix,
        const string &path_id) {
        TASK_UTIL_EXPECT_TRUE(CheckPathNoExists(instance, prefix, path_id));
    }

    template <typename RespT>
    void ValidateResponse(Sandesh *sandesh,
        const string &instance, size_t path_count, size_t nexthop_count) {
        RespT *resp = dynamic_cast<RespT *>(sandesh);
        TASK_UTIL_EXPECT_NE((RespT *) NULL, resp);
        TASK_UTIL_EXPECT_EQ(1, resp->get_resolvers().size());
        const ShowPathResolver &spr = resp->get_resolvers().at(0);
        TASK_UTIL_EXPECT_TRUE(spr.get_name().find(instance) != string::npos);
        TASK_UTIL_EXPECT_EQ(path_count, spr.get_path_count());
        TASK_UTIL_EXPECT_EQ(0, spr.get_modified_path_count());
        TASK_UTIL_EXPECT_EQ(nexthop_count, spr.get_nexthop_count());
        TASK_UTIL_EXPECT_EQ(0, spr.get_modified_nexthop_count());
        cout << spr.log() << endl;
        validate_done_ = true;
    }

    void VerifyPathResolverSandesh(const string &instance, size_t path_count,
        size_t nexthop_count) {
        BgpSandeshContext sandesh_context;
        sandesh_context.bgp_server = bgp_server_.get();
        sandesh_context.xmpp_peer_manager = NULL;
        Sandesh::set_client_context(&sandesh_context);

        Sandesh::set_response_callback(boost::bind(
            &PathResolverTest<T>::ValidateResponse<ShowPathResolverResp>,
            this, _1, instance, path_count, nexthop_count));
        ShowPathResolverReq *req = new ShowPathResolverReq;
        req->set_search_string(instance);
        validate_done_ = false;
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);

        Sandesh::set_response_callback(boost::bind(
            &PathResolverTest<T>::ValidateResponse<ShowPathResolverSummaryResp>,
            this, _1, instance, path_count, nexthop_count));
        ShowPathResolverSummaryReq *sreq = new ShowPathResolverSummaryReq;
        sreq->set_search_string(instance);
        validate_done_ = false;
        sreq->HandleRequest();
        sreq->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }

    EventManager evm_;
    BgpServerTestPtr bgp_server_;
    Address::Family family_;
    string ipv6_prefix_;
    PeerMock *bgp_peer1_;
    PeerMock *bgp_peer2_;
    PeerMock *xmpp_peer1_;
    PeerMock *xmpp_peer2_;
    bool validate_done_;
};

// Specialization of GetFamily for INET.
template<>
Address::Family PathResolverTest<InetDefinition>::GetFamily() const {
    return Address::INET;
}

// Specialization of GetFamily for INET6.
template<>
Address::Family PathResolverTest<Inet6Definition>::GetFamily() const {
    return Address::INET6;
}

// Instantiate fixture class template for each TypeDefinition.
typedef ::testing::Types <InetDefinition, Inet6Definition> TypeDefinitionList;
TYPED_TEST_CASE(PathResolverTest, TypeDefinitionList);

//
// Add BGP path before XMPP path.
//
TYPED_TEST(PathResolverTest, SinglePrefix1) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
}

//
// Add BGP path after XMPP path.
//
TYPED_TEST(PathResolverTest, SinglePrefix2) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
}

//
// Add BGP path after XMPP path.
// Delete and add BGP path multiple times when path update list processing is
// disabled.
//
TYPED_TEST(PathResolverTest, SinglePrefixAddDelete) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    TASK_UTIL_EXPECT_EQ(0, this->ResolverPathUpdateListSize("blue"));
    this->DisableResolverPathUpdateProcessing("blue");

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
    TASK_UTIL_EXPECT_EQ(1, this->ResolverPathUpdateListSize("blue"));

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    TASK_UTIL_EXPECT_EQ(2, this->ResolverPathUpdateListSize("blue"));

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, this->ResolverPathUpdateListSize("blue"));

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    TASK_UTIL_EXPECT_EQ(3, this->ResolverPathUpdateListSize("blue"));

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(3, this->ResolverPathUpdateListSize("blue"));

    this->EnableResolverPathUpdateProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, this->ResolverPathUpdateListSize("blue"));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
}

//
// Change BGP path med after BGP path has been resolved.
//
TYPED_TEST(PathResolverTest, SinglePrefixChangeBgpPath1) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->AddBgpPathWithMed(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()), 100);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000, 100);

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->AddBgpPathWithMed(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()), 200);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000, 200);

    this->AddBgpPathWithMed(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()), 300);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000, 300);

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
}

//
// Change BGP path communities after BGP path has been resolved.
//
TYPED_TEST(PathResolverTest, SinglePrefixChangeBgpPath2) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    vector<uint32_t> comm_list1 = list_of(0xFFFFA101)(0xFFFFA102)(0xFFFFA103);
    CommunitySpec comm_spec1;
    comm_spec1.communities = comm_list1;
    this->AddBgpPathWithCommunities(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()), comm_list1);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000, comm_spec1);

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    vector<uint32_t> comm_list2 = list_of(0xFFFFA201)(0xFFFFA202)(0xFFFFA203);
    CommunitySpec comm_spec2;
    comm_spec2.communities = comm_list2;
    this->AddBgpPathWithCommunities(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()), comm_list2);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000, comm_spec2);

    vector<uint32_t> comm_list3 = list_of(0xFFFFA301)(0xFFFFA302)(0xFFFFA303);
    CommunitySpec comm_spec3;
    comm_spec3.communities = comm_list3;
    this->AddBgpPathWithCommunities(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()), comm_list3);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000, comm_spec3);

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
}

//
// Change BGP path aspath after BGP path has been resolved.
//
TYPED_TEST(PathResolverTest, SinglePrefixChangeBgpPath3) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    vector<uint16_t> as_list1 = list_of(64512)(64513)(64514);
    this->AddBgpPathWithAsList(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()), as_list1);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000, as_list1);

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    vector<uint16_t> as_list2 = list_of(64522)(64523)(64524);
    this->AddBgpPathWithAsList(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()), as_list2);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000, as_list2);

    vector<uint16_t> as_list3 = list_of(64532)(64533)(64534);
    this->AddBgpPathWithAsList(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()), as_list3);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000, as_list3);

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
}

//
// Change BGP path flags to infeasible after BGP path has been resolved.
//
TYPED_TEST(PathResolverTest, SinglePrefixChangeBgpPath4) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->AddBgpPathWithFlags(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()), BgpPath::AsPathLooped);
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
}

//
// Change XMPP path label after BGP path has been resolved.
//
TYPED_TEST(PathResolverTest, SinglePrefixChangeXmppPath1) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10001);

    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10001);

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
}

//
// Change XMPP path nexthop after BGP path has been resolved.
//
TYPED_TEST(PathResolverTest, SinglePrefixChangeXmppPath2) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.2.1"), 10000);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"), 10000);

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
}

//
// Change XMPP path multiple times when nh update list processing is disabled.
//
TYPED_TEST(PathResolverTest, SinglePrefixChangeXmppPath3) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    TASK_UTIL_EXPECT_EQ(0, this->ResolverNexthopUpdateListSize("blue"));
    this->DisableResolverNexthopUpdateProcessing("blue");

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10001);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, this->ResolverNexthopUpdateListSize("blue"));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10002);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, this->ResolverNexthopUpdateListSize("blue"));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10003);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, this->ResolverNexthopUpdateListSize("blue"));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->EnableResolverNexthopUpdateProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, this->ResolverNexthopUpdateListSize("blue"));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10003);

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
}

//
// Change and delete XMPP path when nh update list processing is disabled.
//
TYPED_TEST(PathResolverTest, SinglePrefixChangeXmppPath4) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    TASK_UTIL_EXPECT_EQ(0, this->ResolverNexthopUpdateListSize("blue"));
    this->DisableResolverNexthopUpdateProcessing("blue");

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10001);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, this->ResolverNexthopUpdateListSize("blue"));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, this->ResolverNexthopUpdateListSize("blue"));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->EnableResolverNexthopUpdateProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, this->ResolverNexthopUpdateListSize("blue"));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
}

//
// Delete and resurrect XMPP path when nh update list processing is disabled.
//
TYPED_TEST(PathResolverTest, SinglePrefixChangeXmppPath5) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    TASK_UTIL_EXPECT_EQ(0, this->ResolverNexthopUpdateListSize("blue"));
    this->DisableResolverNexthopUpdateProcessing("blue");

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, this->ResolverNexthopUpdateListSize("blue"));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10001);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, this->ResolverNexthopUpdateListSize("blue"));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->EnableResolverNexthopUpdateProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, this->ResolverNexthopUpdateListSize("blue"));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10001);

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
}

//
// Change XMPP path sgid list.
//
TYPED_TEST(PathResolverTest, SinglePrefixChangeXmppPath6) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    vector<uint32_t> sgid_list1 = list_of(1)(2)(3)(4);
    this->AddXmppPathWithSecurityGroups(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000, sgid_list1);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000, sgid_list1);

    vector<uint32_t> sgid_list2 = list_of(3)(4)(5)(6);
    this->AddXmppPathWithSecurityGroups(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000, sgid_list2);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000, sgid_list2);

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
}

//
// Change XMPP path encap list.
//
TYPED_TEST(PathResolverTest, SinglePrefixChangeXmppPath7) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    set<string> encap_list1 = list_of("gre")("udp");
    this->AddXmppPathWithTunnelEncapsulations(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000, encap_list1);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000, encap_list1);

    set<string> encap_list2 = list_of("udp");
    this->AddXmppPathWithTunnelEncapsulations(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000, encap_list2);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000, encap_list2);

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
}

//
// Change XMPP path load balance property.
//
TYPED_TEST(PathResolverTest, SinglePrefixChangeXmppPath8) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);

    LoadBalance::bytes_type data1 = { { BgpExtendedCommunityType::Opaque,
        BgpExtendedCommunityOpaqueSubType::LoadBalance,
        0xFE, 0x00, 0x80, 0x00, 0x00, 0x00 } };
    LoadBalance lb1(data1);
    this->AddXmppPathWithLoadBalance(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000, lb1);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000, lb1);

    LoadBalance::bytes_type data2 = { { BgpExtendedCommunityType::Opaque,
        BgpExtendedCommunityOpaqueSubType::LoadBalance,
        0xaa, 0x00, 0x80, 0x00, 0x00, 0x00 } };
    LoadBalance lb2(data2);
    this->AddXmppPathWithLoadBalance(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000, lb2);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000, lb2);

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
}

//
// XMPP path has multiple ECMP nexthops.
// Change XMPP route from ECMP to non-ECMP and back.
//
TYPED_TEST(PathResolverTest, SinglePrefixWithEcmp) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000,
        this->BuildNextHopAddress("172.16.2.1"));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"), 10000);

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.2.1"), 10000);
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"), 10000);

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000,
        this->BuildNextHopAddress("172.16.2.1"));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"), 10000);

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"));

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000,
        this->BuildNextHopAddress("172.16.2.1"));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"), 10000);

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"));

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
}

//
// BGP has multiple paths for same prefix, each with a different nexthop.
// Add and remove paths for the prefix.
//
TYPED_TEST(PathResolverTest, SinglePrefixWithMultipath) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *bgp_peer2 = this->bgp_peer2_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;
    PeerMock *xmpp_peer2 = this->xmpp_peer2_;

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->AddBgpPath(bgp_peer2, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer2->ToString()));

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10001);
    this->AddXmppPath(xmpp_peer2, "blue",
        this->BuildPrefix(bgp_peer2->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.2"), 10002);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10001);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"), 10002);

    this->DeleteBgpPath(bgp_peer2, "blue", this->BuildPrefix(1));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10001);
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"));

    this->AddBgpPath(bgp_peer2, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer2->ToString()));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10001);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"), 10002);

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"), 10002);

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10001);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"), 10002);

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
    this->DeleteXmppPath(xmpp_peer2, "blue",
        this->BuildPrefix(bgp_peer2->ToString(), 32));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"));

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
    this->DeleteBgpPath(bgp_peer2, "blue", this->BuildPrefix(1));
}

//
// BGP has multiple paths for same prefix, each with a different nexthop.
// XMPP route for the nexthop is ECMP.
//
TYPED_TEST(PathResolverTest, SinglePrefixWithMultipathAndEcmp) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *bgp_peer2 = this->bgp_peer2_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;
    PeerMock *xmpp_peer2 = this->xmpp_peer2_;

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->AddBgpPath(bgp_peer2, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer2->ToString()));

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10001,
        this->BuildNextHopAddress("172.16.2.1"));
    this->AddXmppPath(xmpp_peer2, "blue",
        this->BuildPrefix(bgp_peer2->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.2"), 10002,
        this->BuildNextHopAddress("172.16.2.2"));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10001);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"), 10001);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"), 10002);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.2"), 10002);

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.2.1"), 10001);
    this->AddXmppPath(xmpp_peer2, "blue",
        this->BuildPrefix(bgp_peer2->ToString(), 32),
        this->BuildNextHopAddress("172.16.2.2"), 10002);
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"), 10001);
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.2"), 10002);

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10001,
        this->BuildNextHopAddress("172.16.2.1"));
    this->AddXmppPath(xmpp_peer2, "blue",
        this->BuildPrefix(bgp_peer2->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.2"), 10002,
        this->BuildNextHopAddress("172.16.2.2"));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10001);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"), 10001);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"), 10002);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.2"), 10002);

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10001);
    this->AddXmppPath(xmpp_peer2, "blue",
        this->BuildPrefix(bgp_peer2->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.2"), 10002);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10001);
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"), 10002);
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.2"));

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10003,
        this->BuildNextHopAddress("172.16.2.1"));
    this->AddXmppPath(xmpp_peer2, "blue",
        this->BuildPrefix(bgp_peer2->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.2"), 10004,
        this->BuildNextHopAddress("172.16.2.2"));
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"), 10003);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"), 10003);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"), 10004);
    this->VerifyPathAttributes("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.2"), 10004);

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
    this->DeleteXmppPath(xmpp_peer2, "blue",
        this->BuildPrefix(bgp_peer2->ToString(), 32));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.1"));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.1"));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.1.2"));
    this->VerifyPathNoExists("blue", this->BuildPrefix(1),
        this->BuildNextHopAddress("172.16.2.2"));

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
    this->DeleteBgpPath(bgp_peer2, "blue", this->BuildPrefix(1));
}

//
// BGP has multiple prefixes, each with the same nexthop.
//
TYPED_TEST(PathResolverTest, MultiplePrefix) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(idx),
            this->BuildHostAddress(bgp_peer1->ToString()));
    }

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyPathAttributes("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"), 10000);
    }

    this->VerifyPathResolverSandesh("blue", DB::PartitionCount() * 2, 1);

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyPathNoExists("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"));
    }

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(idx));
    }
}

//
// BGP has multiple prefixes, each with the same nexthop.
// Change XMPP path multiple times when path update list processing is disabled.
//
TYPED_TEST(PathResolverTest, MultiplePrefixChangeXmppPath1) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(idx),
            this->BuildHostAddress(bgp_peer1->ToString()));
    }

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyPathAttributes("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"), 10000);
    }

    TASK_UTIL_EXPECT_EQ(0, this->ResolverPathUpdateListSize("blue"));
    this->DisableResolverPathUpdateProcessing("blue");

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10001);
    TASK_UTIL_EXPECT_EQ(DB::PartitionCount() * 2,
        this->ResolverPathUpdateListSize("blue"));
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyPathAttributes("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"), 10000);
    }

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10002);
    TASK_UTIL_EXPECT_EQ(DB::PartitionCount() * 2,
        this->ResolverPathUpdateListSize("blue"));
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyPathAttributes("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"), 10000);
    }

    this->EnableResolverPathUpdateProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, this->ResolverPathUpdateListSize("blue"));
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyPathAttributes("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"), 10002);
    }

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyPathNoExists("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"));
    }

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(idx));
    }
}

//
// BGP has multiple prefixes, each with the same nexthop.
// Change XMPP path and delete it path update list processing is disabled.
//
TYPED_TEST(PathResolverTest, MultiplePrefixChangeXmppPath2) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(idx),
            this->BuildHostAddress(bgp_peer1->ToString()));
    }

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyPathAttributes("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"), 10000);
    }

    TASK_UTIL_EXPECT_EQ(0, this->ResolverPathUpdateListSize("blue"));
    this->DisableResolverPathUpdateProcessing("blue");

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10001);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(DB::PartitionCount() * 2,
        this->ResolverPathUpdateListSize("blue"));
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyPathAttributes("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"), 10000);
    }

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(DB::PartitionCount() * 2,
        this->ResolverPathUpdateListSize("blue"));
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyPathAttributes("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"), 10000);
    }

    this->EnableResolverPathUpdateProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, this->ResolverPathUpdateListSize("blue"));
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyPathNoExists("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"));
    }

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(idx));
    }
}

//
// BGP has 2 paths for all prefixes.
//
TYPED_TEST(PathResolverTest, MultiplePrefixWithMultipath) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *bgp_peer2 = this->bgp_peer2_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;
    PeerMock *xmpp_peer2 = this->xmpp_peer2_;

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(idx),
            this->BuildHostAddress(bgp_peer1->ToString()));
        this->AddBgpPath(bgp_peer2, "blue", this->BuildPrefix(idx),
            this->BuildHostAddress(bgp_peer2->ToString()));
    }

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10001);
    this->AddXmppPath(xmpp_peer2, "blue",
        this->BuildPrefix(bgp_peer2->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.2"), 10002);
    task_util::WaitForIdle();

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyPathAttributes("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"), 10001);
        this->VerifyPathAttributes("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.2"), 10002);
    }

    this->VerifyPathResolverSandesh("blue", DB::PartitionCount() * 4, 2);

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
    this->DeleteXmppPath(xmpp_peer2, "blue",
        this->BuildPrefix(bgp_peer2->ToString(), 32));

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyPathNoExists("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"));
        this->VerifyPathNoExists("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.2"));
    }

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(idx));
        this->DeleteBgpPath(bgp_peer2, "blue", this->BuildPrefix(idx));
    }
}

//
// BGP has multiple prefixes in blue and pink tables, each with same nexthop.
//
TYPED_TEST(PathResolverTest, MultipleTableMultiplePrefix) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(idx),
            this->BuildHostAddress(bgp_peer1->ToString()));
        this->AddBgpPath(bgp_peer1, "pink", this->BuildPrefix(idx),
            this->BuildHostAddress(bgp_peer1->ToString()));
    }

    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    this->AddXmppPath(xmpp_peer1, "pink",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyPathAttributes("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"), 10000);
        this->VerifyPathAttributes("pink", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"), 10000);
    }

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
    this->DeleteXmppPath(xmpp_peer1, "pink",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyPathNoExists("blue", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"));
        this->VerifyPathNoExists("pink", this->BuildPrefix(idx),
            this->BuildNextHopAddress("172.16.1.1"));
    }

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(idx));
        this->DeleteBgpPath(bgp_peer1, "pink", this->BuildPrefix(idx));
    }
}

//
// Delete nexthop before it's registered to condition listener.
// Intent is verify that we don't try to remove and unregister a nexthop
// that was never registered.  It should simply get destroyed.
//
TYPED_TEST(PathResolverTest, ResolverNexthopCleanup1) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *bgp_peer2 = this->bgp_peer2_;

    this->DisableResolverNexthopRegUnregProcessing("blue");
    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->AddBgpPath(bgp_peer2, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer2->ToString()));
    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
    this->DeleteBgpPath(bgp_peer2, "blue", this->BuildPrefix(1));
    this->EnableResolverNexthopRegUnregProcessing("blue");
}

//
// Resume processing the update list only after the nexthop gets destroyed.
// Intent is to ensure that the nexthop doesn't get added to the update list
// after it's marked deleted.
//
TYPED_TEST(PathResolverTest, ResolverNexthopCleanup2) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *bgp_peer2 = this->bgp_peer2_;
    PeerMock *xmpp_peer1 = this->xmpp_peer1_;
    PeerMock *xmpp_peer2 = this->xmpp_peer2_;

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->AddBgpPath(bgp_peer2, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer2->ToString()));
    this->AddXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.1"), 10000);
    this->AddXmppPath(xmpp_peer2, "blue",
        this->BuildPrefix(bgp_peer2->ToString(), 32),
        this->BuildNextHopAddress("172.16.1.2"), 10002);
    task_util::WaitForIdle();

    this->DisableResolverNexthopUpdateProcessing("blue");
    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
    this->DeleteBgpPath(bgp_peer2, "blue", this->BuildPrefix(1));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, this->ResolverNexthopRegUnregListSize("blue"));
    TASK_UTIL_EXPECT_EQ(0, this->ResolverNexthopUpdateListSize("blue"));
    this->EnableResolverNexthopUpdateProcessing("blue");
    task_util::WaitForIdle();

    this->DeleteXmppPath(xmpp_peer1, "blue",
        this->BuildPrefix(bgp_peer1->ToString(), 32));
    this->DeleteXmppPath(xmpp_peer2, "blue",
        this->BuildPrefix(bgp_peer2->ToString(), 32));
}

//
// Recreate nexthop before the previous incarnation has been removed (and
// then unregistered) from condition listener.
// Intent is to verify that the previous incarnation is reused.
//
TYPED_TEST(PathResolverTest, ResolverNexthopCleanup3) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *bgp_peer2 = this->bgp_peer2_;

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->AddBgpPath(bgp_peer2, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer2->ToString()));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, this->ResolverNexthopMapSize("blue"));
    TASK_UTIL_EXPECT_EQ(0, this->ResolverNexthopRegUnregListSize("blue"));

    this->DisableResolverNexthopRegUnregProcessing("blue");

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
    this->DeleteBgpPath(bgp_peer2, "blue", this->BuildPrefix(1));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, this->ResolverNexthopMapSize("blue"));
    TASK_UTIL_EXPECT_EQ(2, this->ResolverNexthopRegUnregListSize("blue"));

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->AddBgpPath(bgp_peer2, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer2->ToString()));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, this->ResolverNexthopMapSize("blue"));
    TASK_UTIL_EXPECT_EQ(2, this->ResolverNexthopRegUnregListSize("blue"));

    this->EnableResolverNexthopRegUnregProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, this->ResolverNexthopRegUnregListSize("blue"));
    TASK_UTIL_EXPECT_EQ(2, this->ResolverNexthopMapSize("blue"));

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
    this->DeleteBgpPath(bgp_peer2, "blue", this->BuildPrefix(1));
}

//
// Recreate nexthop after the previous incarnation has been removed but before
// it has been unregistered from condition listener.
// Intent is to verify that the previous incarnation is not reused.
//
TYPED_TEST(PathResolverTest, ResolverNexthopCleanup4) {
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *bgp_peer2 = this->bgp_peer2_;

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->AddBgpPath(bgp_peer2, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer2->ToString()));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, this->ResolverNexthopMapSize("blue"));
    TASK_UTIL_EXPECT_EQ(0, this->ResolverNexthopDeleteListSize("blue"));

    this->DisableConditionListener();

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
    this->DeleteBgpPath(bgp_peer2, "blue", this->BuildPrefix(1));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, this->ResolverNexthopMapSize("blue"));
    TASK_UTIL_EXPECT_EQ(2, this->ResolverNexthopDeleteListSize("blue"));

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->AddBgpPath(bgp_peer2, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer2->ToString()));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, this->ResolverNexthopMapSize("blue"));
    TASK_UTIL_EXPECT_EQ(2, this->ResolverNexthopDeleteListSize("blue"));

    this->EnableConditionListener();

    TASK_UTIL_EXPECT_EQ(2, this->ResolverNexthopMapSize("blue"));
    TASK_UTIL_EXPECT_EQ(0, this->ResolverNexthopDeleteListSize("blue"));

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
    this->DeleteBgpPath(bgp_peer2, "blue", this->BuildPrefix(1));
}

//
// Shutdown server and resolver before all BGP paths are deleted.
// Deletion does not finish till BGP paths are deleted.
//
TYPED_TEST(PathResolverTest, Shutdown1) {
    BgpServerTestPtr bgp_server = this->bgp_server_;
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *bgp_peer2 = this->bgp_peer2_;

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->AddBgpPath(bgp_peer2, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer2->ToString()));

    bgp_server->Shutdown(false);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, bgp_server->routing_instance_mgr()->count());

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
    this->DeleteBgpPath(bgp_peer2, "blue", this->BuildPrefix(1));
    TASK_UTIL_EXPECT_EQ(0, bgp_server->routing_instance_mgr()->count());
}

//
// Shutdown server and resolver before all BGP paths are deleted.
// Deletion does not finish till BGP paths are deleted and nexthop gets
// unregistered from condition listener.
//
TYPED_TEST(PathResolverTest, Shutdown2) {
    BgpServerTestPtr bgp_server = this->bgp_server_;
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *bgp_peer2 = this->bgp_peer2_;

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->AddBgpPath(bgp_peer2, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer2->ToString()));

    bgp_server->Shutdown(false);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, bgp_server->routing_instance_mgr()->count());

    this->DisableResolverNexthopRegUnregProcessing("blue");
    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
    this->DeleteBgpPath(bgp_peer2, "blue", this->BuildPrefix(1));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, bgp_server->routing_instance_mgr()->count());

    this->EnableResolverNexthopRegUnregProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, bgp_server->routing_instance_mgr()->count());
}

//
// Shutdown server and resolver before all resolver paths are deleted.
// Deletion does not finish till resolver paths are deleted and nexthop
// gets unregistered from condition listener.
//
TYPED_TEST(PathResolverTest, Shutdown3) {
    BgpServerTestPtr bgp_server = this->bgp_server_;
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *bgp_peer2 = this->bgp_peer2_;

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->AddBgpPath(bgp_peer2, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer2->ToString()));

    task_util::WaitForIdle();
    this->DisableResolverPathUpdateProcessing("blue");

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
    this->DeleteBgpPath(bgp_peer2, "blue", this->BuildPrefix(1));
    TASK_UTIL_EXPECT_EQ(2, this->ResolverPathUpdateListSize("blue"));

    bgp_server->Shutdown(false);
    TASK_UTIL_EXPECT_EQ(1, bgp_server->routing_instance_mgr()->count());
    TASK_UTIL_EXPECT_EQ(2, this->ResolverPathUpdateListSize("blue"));

    // Ensure that all bgp::ResolverPath tasks are created before any of
    // them run. Otherwise, it's possible that some of them run, trigger
    // creation of bgp::ResolverNexthop task which runs and triggers the
    // creation of bgp::Config task, which in turn destroys PathResolver.
    // If this sequence happens before all the bgp::ResolverPath tasks
    // are created, then would be accessing freed memory.
    TaskScheduler::GetInstance()->Stop();
    this->EnableResolverPathUpdateProcessing("blue");
    TaskScheduler::GetInstance()->Start();

    TASK_UTIL_EXPECT_EQ(0, bgp_server->routing_instance_mgr()->count());
}

//
// Shutdown server and resolver before nexthops are registered with the
// condition listener.
// Deletion does not finish till BGP paths are deleted.
// Should not attempt to register nexthops with condition listener since
// that would cause an assertion.
//
TYPED_TEST(PathResolverTest, Shutdown4) {
    BgpServerTestPtr bgp_server = this->bgp_server_;
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *bgp_peer2 = this->bgp_peer2_;

    this->DisableResolverNexthopRegUnregProcessing("blue");

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->AddBgpPath(bgp_peer2, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer2->ToString()));
    TASK_UTIL_EXPECT_EQ(2, this->ResolverNexthopRegUnregListSize("blue"));

    bgp_server->Shutdown(false);
    TASK_UTIL_EXPECT_EQ(1, bgp_server->routing_instance_mgr()->count());

    this->EnableResolverNexthopRegUnregProcessing("blue");
    TASK_UTIL_EXPECT_EQ(0, this->ResolverNexthopRegUnregListSize("blue"));

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
    this->DeleteBgpPath(bgp_peer2, "blue", this->BuildPrefix(1));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, bgp_server->routing_instance_mgr()->count());
}

//
// Shutdown server and resolver before all resolver paths are deleted.
// Deletion does not finish till resolver paths are deleted and nexthop
// gets unregistered from condition listener.
//
TYPED_TEST(PathResolverTest, Shutdown5) {
    BgpServerTestPtr bgp_server = this->bgp_server_;
    PeerMock *bgp_peer1 = this->bgp_peer1_;
    PeerMock *bgp_peer2 = this->bgp_peer2_;

    this->AddBgpPath(bgp_peer1, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer1->ToString()));
    this->AddBgpPath(bgp_peer2, "blue", this->BuildPrefix(1),
        this->BuildHostAddress(bgp_peer2->ToString()));

    task_util::WaitForIdle();
    this->PauseResolverPathUpdateProcessing("blue");

    this->DeleteBgpPath(bgp_peer1, "blue", this->BuildPrefix(1));
    this->DeleteBgpPath(bgp_peer2, "blue", this->BuildPrefix(1));
    TASK_UTIL_EXPECT_EQ(2, this->ResolverPathUpdateListSize("blue"));

    TASK_UTIL_EXPECT_EQ(3, bgp_server->routing_instance_mgr()->count());
    bgp_server->Shutdown(false, false);

    // Verify that all instances are intact.
    for (int idx = 0; idx < 100; ++idx) {
        usleep(10000);
        TASK_UTIL_EXPECT_EQ(3, bgp_server->routing_instance_mgr()->count());
    }

    // Ensure that all bgp::ResolverPath tasks are resumed before any of
    // them run. Otherwise, it's possible that some of them run, trigger
    // creation of bgp::ResolverNexthop task which runs and triggers the
    // creation of bgp::Config task, which in turn destroys PathResolver.
    // If this sequence happens before all the bgp::ResolverPath tasks
    // are resumed, then would be accessing freed memory.
    TaskScheduler::GetInstance()->Stop();
    this->ResumeResolverPathUpdateProcessing("blue");
    TaskScheduler::GetInstance()->Start();

    TASK_UTIL_EXPECT_EQ(0, bgp_server->routing_instance_mgr()->count());
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
}

static void TearDown() {
    task_util::WaitForIdle();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

static void process_command_line_args(int argc, const char **argv) {
    options_description desc("PathResolverTest");
    desc.add_options()
        ("help", "produce help message")
        ("nexthop-address-family", value<string>()->default_value("inet"),
             "set nexthop address family (inet/inet6)");
    variables_map vm;
    store(parse_command_line(argc, argv, desc), vm);
    notify(vm);

    if (vm.count("nexthop-address-family")) {
        nexthop_family_is_inet =
            (vm["nexthop-address-family"].as<string>() == "inet");
    }
}

int path_resolver_test_main(int argc, const char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, const_cast<char **>(argv));
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    process_command_line_args(argc, const_cast<const char **>(argv));
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}

#ifndef __PATH_RESOLVER_TEST_WRAPPER_TEST_SUITE__

int main(int argc, char **argv) {
    return path_resolver_test_main(argc, const_cast<const char **>(argv));
}

#endif
