/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>
#include <boost/program_options.hpp>

#include "base/test/task_test_util.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/xmpp_message_builder.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet6/inet6_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "io/test/event_manager_test.h"
#include "control-node/control_node.h"
#include "testing/gunit.h"

using namespace boost::program_options;
using std::string;

class PeerMock : public IPeer {
public:
    PeerMock(const string &address_str) {
        boost::system::error_code ec;
        address_ = Ip4Address::from_string(address_str, ec);
        assert(ec.value() == 0);
        address_str_ = address_.to_string();
    }
    virtual ~PeerMock() { }

    virtual const std::string &ToString() const { return address_str_; }
    virtual const std::string &ToUVEKey() const { return address_str_; }
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
    BgpProto::BgpPeerType PeerType() const { return BgpProto::EBGP; }
    virtual uint32_t bgp_identifier() const {
        return htonl(address_.to_ulong());
    }
    virtual const std::string GetStateName() const { return ""; }
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

static const char *cfg_template = "\
<config>\
    <bgp-router name=\'X\'>\
        <autonomous-system>64512</autonomous-system>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
              <family-attributes>\
                  <address-family>%s</address-family>\
                  <prefix-limit>\
                      <maximum>%d</maximum>\
                      <idle-timeout>%d</idle-timeout>\
                  </prefix-limit>\
              </family-attributes>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <autonomous-system>64512</autonomous-system>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.2</address>\
        <port>%d</port>\
        <session to=\'X\'>\
              <family-attributes>\
                  <address-family>%s</address-family>\
                  <prefix-limit>\
                      <maximum>%d</maximum>\
                      <idle-timeout>%d</idle-timeout>\
                  </prefix-limit>\
              </family-attributes>\
        </session>\
    </bgp-router>\
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
class BgpIpTest : public ::testing::Test {
protected:
    typedef typename T::TableT TableT;
    typedef typename T::PrefixT PrefixT;

    BgpIpTest()
        : thread_(&evm_),
          peer1_(new PeerMock("192.168.1.1")),
          peer2_(new PeerMock("192.168.1.2")),
          peer_xy_(NULL),
          peer_yx_(NULL),
          family_(GetFamily()),
          master_(BgpConfigManager::kMasterInstance),
          ipv6_prefix_("::") {
        bs_x_.reset(new BgpServerTest(&evm_, "X"));
        bs_x_->session_manager()->Initialize(0);
        bs_y_.reset(new BgpServerTest(&evm_, "Y"));
        bs_y_->session_manager()->Initialize(0);
    }

    ~BgpIpTest() {
        delete peer1_;
        delete peer2_;
    }

    void Configure(int prefix_limit = 0, int idle_timeout = 0) {
        char config[4096];
        snprintf(config, sizeof(config), cfg_template,
            bs_x_->session_manager()->GetPort(),
            GetFamily() == Address::INET ? "inet" : "inet6",
            prefix_limit, idle_timeout,
            bs_y_->session_manager()->GetPort(),
            GetFamily() == Address::INET ? "inet" : "inet6",
            prefix_limit, idle_timeout);
        bs_x_->Configure(config);
        bs_y_->Configure(config);
    }

    virtual void SetUp() {
        thread_.Start();
        Configure();
        task_util::WaitForIdle();

        TASK_UTIL_EXPECT_TRUE(bs_x_->FindMatchingPeer(master_, "Y") != NULL);
        peer_xy_ = bs_x_->FindMatchingPeer(master_, "Y");
        TASK_UTIL_EXPECT_TRUE(bs_y_->FindMatchingPeer(master_, "X") != NULL);
        peer_yx_ = bs_y_->FindMatchingPeer(master_, "X");

    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        bs_x_->Shutdown();
        bs_y_->Shutdown();
        task_util::WaitForIdle();
        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    Address::Family GetFamily() const {
        assert(false);
        return Address::UNSPEC;
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
        if (family_ == Address::INET) {
            return ipv4_addr;
        }
        if (family_ == Address::INET6) {
            if (nexthop_family_is_inet) {
                return ipv4_addr;
            } else {
                return ipv6_prefix_ + ipv4_addr;
            }
        }
        assert(false);
        return "";
    }

    string GetTableName(const string &instance) const {
        if (family_ == Address::INET) {
            if (instance == master_) {
                return "inet.0";
            } else {
                return instance + ".inet.0";
            }
        }
        if (family_ == Address::INET6) {
            if (instance == master_) {
                return "inet6.0";
            } else {
                return instance + ".inet6.0";
            }
        }
        assert(false);
        return "";
    }

    BgpTable *GetTable(BgpServerTestPtr server, const string &instance) {
        return static_cast<BgpTable *>(
            server->database()->FindTable(GetTableName(instance)));
    }

    void AddRoute(BgpServerTestPtr server, IPeer *peer, const string &instance,
        const string &prefix_str, const string &nexthop_str) {

        boost::system::error_code ec;
        PrefixT prefix = PrefixT::FromString(prefix_str, &ec);
        EXPECT_FALSE(ec);
        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(new typename TableT::RequestKey(prefix, peer));

        BgpTable *table = GetTable(server, instance);
        BgpAttrSpec attr_spec;

        BgpAttrOrigin origin_spec(BgpAttrOrigin::IGP);
        attr_spec.push_back(&origin_spec);

        AsPath4ByteSpec path4_spec;
        AsPathSpec path_spec;
        if (!server->enable_4byte_as()) {
            AsPathSpec::PathSegment *path_seg = new AsPathSpec::PathSegment;
            path_seg->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
            path_seg->path_segment.push_back(64513);
            path_seg->path_segment.push_back(64514);
            path_seg->path_segment.push_back(64515);
            path_spec.path_segments.push_back(path_seg);
            attr_spec.push_back(&path_spec);
        } else {
            AsPath4ByteSpec::PathSegment *path_seg =
                            new AsPath4ByteSpec::PathSegment;
            path_seg->path_segment_type =
                            AsPath4ByteSpec::PathSegment::AS_SEQUENCE;
            path_seg->path_segment.push_back(64513);
            path_seg->path_segment.push_back(64514);
            path_seg->path_segment.push_back(64515);
            path4_spec.path_segments.push_back(path_seg);
            attr_spec.push_back(&path4_spec);
        }

        IpAddress nh_addr = IpAddress::from_string(nexthop_str, ec);
        EXPECT_FALSE(ec);
        BgpAttrNextHop nh_spec(nh_addr);
        attr_spec.push_back(&nh_spec);

        BgpAttrPtr attr = server->attr_db()->Locate(attr_spec);
        request.data.reset(new BgpTable::RequestData(attr, 0, 0));
        table->Enqueue(&request);
    }

    void DeleteRoute(BgpServerTestPtr server, IPeer *peer,
        const string &instance, const string &prefix_str) {
        boost::system::error_code ec;
        PrefixT prefix = PrefixT::FromString(prefix_str, &ec);
        EXPECT_FALSE(ec);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        request.key.reset(new typename TableT::RequestKey(prefix, peer));

        BgpTable *table = GetTable(server, instance);
        table->Enqueue(&request);
    }

    BgpRoute *RouteLookup(BgpServerTestPtr server, const string &instance_name,
        const string &prefix) {
        BgpTable *bgp_table = GetTable(server, instance_name);
        TableT *table = dynamic_cast<TableT *>(bgp_table);
        EXPECT_TRUE(table != NULL);

        boost::system::error_code error;
        PrefixT nlri = PrefixT::FromString(prefix, &error);
        EXPECT_FALSE(error);
        typename TableT::RequestKey key(nlri, NULL);
        BgpRoute *rt = dynamic_cast<BgpRoute *>(table->Find(&key));
        return rt;
    }

    bool CheckPathExists(BgpServerTestPtr server, const string &instance,
        const string &prefix, const IPeer *peer, const string &nexthop) {
        task_util::TaskSchedulerLock lock;
        BgpRoute *route = RouteLookup(server, instance, prefix);
        if (!route)
            return false;
        for (Route::PathList::iterator it = route->GetPathList().begin();
             it != route->GetPathList().end(); ++it) {
            const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
            if (path->GetPeer() != peer)
                continue;
            if (path->GetAttr()->nexthop().to_string() != nexthop)
                continue;
            if (path->GetAttr()->origin() != BgpAttrOrigin::IGP)
                continue;
            return true;
        }
        return false;
    }

    bool CheckPathNoExists(BgpServerTestPtr server, const string &instance,
        const string &prefix, const IPeer *peer, const string &nexthop) {
        task_util::TaskSchedulerLock lock;
        BgpRoute *route = RouteLookup(server, instance, prefix);
        if (!route)
            return false;
        for (Route::PathList::iterator it = route->GetPathList().begin();
             it != route->GetPathList().end(); ++it) {
            const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
            if (path->GetPeer() != peer)
                continue;
            if (path->GetAttr()->nexthop().to_string() != nexthop)
                continue;
            return false;
        }
        return true;
    }

    void VerifyPathExists(BgpServerTestPtr server, const string &instance,
        const string &prefix, const IPeer *peer, const string &nexthop) {
        TASK_UTIL_EXPECT_TRUE(
            CheckPathExists(server, instance, prefix, peer, nexthop));
    }

    void VerifyPathNoExists(BgpServerTestPtr server, const string &instance,
        const string &prefix, const IPeer *peer, const string &nexthop) {
        TASK_UTIL_EXPECT_TRUE(
            CheckPathNoExists(server, instance, prefix, peer, nexthop));
    }

    bool CheckRouteNoExists(BgpServerTestPtr server,
        const string &instance_name, const string &prefix) {
        task_util::TaskSchedulerLock lock;
        BgpTable *bgp_table = GetTable(server, instance_name);
        TableT *table = dynamic_cast<TableT *>(bgp_table);
        EXPECT_TRUE(table != NULL);

        boost::system::error_code error;
        PrefixT nlri = PrefixT::FromString(prefix, &error);
        EXPECT_FALSE(error);
        typename TableT::RequestKey key(nlri, NULL);
        return (table->Find(&key) == NULL);
    }

    void VerifyRouteNoExists(BgpServerTestPtr server, const string &instance,
        const string &prefix) {
        TASK_UTIL_EXPECT_TRUE(CheckRouteNoExists(server, instance, prefix));
    }

    EventManager evm_;
    ServerThread thread_;
    BgpServerTestPtr bs_x_;
    BgpServerTestPtr bs_y_;
    PeerMock *peer1_;
    PeerMock *peer2_;
    BgpPeer *peer_xy_;
    BgpPeer *peer_yx_;
    Address::Family family_;
    string master_;
    string ipv6_prefix_;
};

// Specialization of GetFamily for INET.
template<>
Address::Family BgpIpTest<InetDefinition>::GetFamily() const {
    return Address::INET;
}

// Specialization of GetFamily for INET6.
template<>
Address::Family BgpIpTest<Inet6Definition>::GetFamily() const {
    return Address::INET6;
}

// Instantiate fixture class template for each TypeDefinition.
typedef ::testing::Types <InetDefinition, Inet6Definition> TypeDefinitionList;
TYPED_TEST_CASE(BgpIpTest, TypeDefinitionList);

//
// Add a single prefix on X and verify it shows up on X and Y.
//
TYPED_TEST(BgpIpTest, SinglePrefix) {
    BgpServerTestPtr bs_x = this->bs_x_;
    BgpServerTestPtr bs_y = this->bs_y_;
    PeerMock *peer1 = this->peer1_;
    BgpPeer *peer_yx_ = this->peer_yx_;
    const string &master = this->master_;

    this->AddRoute(bs_x, peer1, master, this->BuildPrefix(1),
        this->BuildNextHopAddress(peer1->ToString()));
    this->VerifyPathExists(bs_x, master, this->BuildPrefix(1),
        peer1, this->BuildNextHopAddress(peer1->ToString()));
    this->VerifyPathExists(bs_y, master, this->BuildPrefix(1),
        peer_yx_, this->BuildNextHopAddress(peer1->ToString()));

    this->DeleteRoute(bs_x, peer1, master, this->BuildPrefix(1));
    this->VerifyRouteNoExists(bs_x, master, this->BuildPrefix(1));
    this->VerifyRouteNoExists(bs_y, master, this->BuildPrefix(1));
}

//
// Add a single prefix with 2 paths on X and verify it on X and Y.
// X should have both paths while Y will have only 1 path.
//
TYPED_TEST(BgpIpTest, SinglePrefixMultipath) {
    BgpServerTestPtr bs_x = this->bs_x_;
    BgpServerTestPtr bs_y = this->bs_y_;
    PeerMock *peer1 = this->peer1_;
    PeerMock *peer2 = this->peer2_;
    BgpPeer *peer_yx = this->peer_yx_;
    const string &master = this->master_;

    this->AddRoute(bs_x, peer2, master, this->BuildPrefix(1),
        this->BuildNextHopAddress(peer2->ToString()));
    this->VerifyPathExists(bs_x, master, this->BuildPrefix(1),
        peer2, this->BuildNextHopAddress(peer2->ToString()));
    this->VerifyPathNoExists(bs_x, master, this->BuildPrefix(1),
        peer1, this->BuildNextHopAddress(peer1->ToString()));
    this->VerifyPathExists(bs_y, master, this->BuildPrefix(1),
        peer_yx, this->BuildNextHopAddress(peer2->ToString()));
    this->VerifyPathNoExists(bs_y, master, this->BuildPrefix(1),
        peer_yx, this->BuildNextHopAddress(peer1->ToString()));

    this->AddRoute(bs_x, peer1, master, this->BuildPrefix(1),
        this->BuildNextHopAddress(peer1->ToString()));
    this->VerifyPathExists(bs_x, master, this->BuildPrefix(1),
        peer1, this->BuildNextHopAddress(peer1->ToString()));
    this->VerifyPathExists(bs_x, master, this->BuildPrefix(1),
        peer2, this->BuildNextHopAddress(peer2->ToString()));
    this->VerifyPathExists(bs_y, master, this->BuildPrefix(1),
        peer_yx, this->BuildNextHopAddress(peer1->ToString()));
    this->VerifyPathNoExists(bs_y, master, this->BuildPrefix(1),
        peer_yx, this->BuildNextHopAddress(peer2->ToString()));

    this->DeleteRoute(bs_x, peer1, master, this->BuildPrefix(1));
    this->VerifyPathNoExists(bs_x, master, this->BuildPrefix(1),
        peer1, this->BuildNextHopAddress(peer1->ToString()));
    this->VerifyPathExists(bs_x, master, this->BuildPrefix(1),
        peer2, this->BuildNextHopAddress(peer2->ToString()));
    this->VerifyPathExists(bs_y, master, this->BuildPrefix(1),
        peer_yx, this->BuildNextHopAddress(peer2->ToString()));
    this->VerifyPathNoExists(bs_y, master, this->BuildPrefix(1),
        peer_yx, this->BuildNextHopAddress(peer1->ToString()));

    this->DeleteRoute(bs_x, peer2, master, this->BuildPrefix(1));

    this->VerifyRouteNoExists(bs_x, master, this->BuildPrefix(1));
    this->VerifyRouteNoExists(bs_y, master, this->BuildPrefix(1));
}

//
// Add multiple prefixes on X and verify they show up on X and Y.
//
TYPED_TEST(BgpIpTest, MultiplePrefix) {
    BgpServerTestPtr bs_x = this->bs_x_;
    BgpServerTestPtr bs_y = this->bs_y_;
    PeerMock *peer1 = this->peer1_;
    BgpPeer *peer_yx = this->peer_yx_;
    const string &master = this->master_;

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->AddRoute(bs_x, peer1, master, this->BuildPrefix(idx),
            this->BuildNextHopAddress(peer1->ToString()));
    }
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyPathExists(bs_x, master, this->BuildPrefix(idx),
            peer1, this->BuildNextHopAddress(peer1->ToString()));
        this->VerifyPathExists(bs_y, master, this->BuildPrefix(idx),
            peer_yx, this->BuildNextHopAddress(peer1->ToString()));
    }

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->DeleteRoute(bs_x, peer1, master, this->BuildPrefix(idx));
    }
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyRouteNoExists(bs_x, master, this->BuildPrefix(idx));
        this->VerifyRouteNoExists(bs_y, master, this->BuildPrefix(idx));
    }
}

//
// Add multiple prefixes with 2 paths each on X and verify them on X and Y.
// X should have both paths while Y will have only 1 path for each prefix.
//
TYPED_TEST(BgpIpTest, MultiplePrefixMultipath) {
    BgpServerTestPtr bs_x = this->bs_x_;
    BgpServerTestPtr bs_y = this->bs_y_;
    PeerMock *peer1 = this->peer1_;
    PeerMock *peer2 = this->peer2_;
    BgpPeer *peer_yx = this->peer_yx_;
    const string &master = this->master_;

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->AddRoute(bs_x, peer2, master, this->BuildPrefix(idx),
            this->BuildNextHopAddress(peer2->ToString()));
        this->AddRoute(bs_x, peer1, master, this->BuildPrefix(idx),
            this->BuildNextHopAddress(peer1->ToString()));
    }
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyPathExists(bs_x, master, this->BuildPrefix(idx),
            peer2, this->BuildNextHopAddress(peer2->ToString()));
        this->VerifyPathExists(bs_x, master, this->BuildPrefix(idx),
            peer1, this->BuildNextHopAddress(peer1->ToString()));
        this->VerifyPathExists(bs_y, master, this->BuildPrefix(idx),
            peer_yx, this->BuildNextHopAddress(peer1->ToString()));
        this->VerifyPathNoExists(bs_y, master, this->BuildPrefix(idx),
            peer_yx, this->BuildNextHopAddress(peer2->ToString()));
    }

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->DeleteRoute(bs_x, peer1, master, this->BuildPrefix(idx));
        this->DeleteRoute(bs_x, peer2, master, this->BuildPrefix(idx));
    }
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyRouteNoExists(bs_x, master, this->BuildPrefix(idx));
        this->VerifyRouteNoExists(bs_y, master, this->BuildPrefix(idx));
    }
}

//
// Configure a prefix limit and a 0 idle timeout.
// Add multiple prefixes such that the prefix limit configured on Y for
// the peering to X is exceeded.
// Verify that the peering keeps flapping.
// Then increase the prefix limit and verify the peering gets re-established
// and all the expected paths exist on Y.
// Then lower the limit again and verify that the peering keeps flapping.
//
TYPED_TEST(BgpIpTest, PrefixLimit1) {
    BgpServerTestPtr bs_x = this->bs_x_;
    BgpServerTestPtr bs_y = this->bs_y_;
    PeerMock *peer1 = this->peer1_;
    PeerMock *peer2 = this->peer2_;
    BgpPeer *peer_xy = this->peer_xy_;
    BgpPeer *peer_yx = this->peer_yx_;
    const string &master = this->master_;

    this->Configure(DB::PartitionCount() * 2 - 1);
    task_util::WaitForIdle();

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->AddRoute(bs_x, peer2, master, this->BuildPrefix(idx),
            this->BuildNextHopAddress(peer2->ToString()));
        this->AddRoute(bs_x, peer1, master, this->BuildPrefix(idx),
            this->BuildNextHopAddress(peer1->ToString()));
    }
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyPathExists(bs_x, master, this->BuildPrefix(idx),
            peer2, this->BuildNextHopAddress(peer2->ToString()));
        this->VerifyPathExists(bs_x, master, this->BuildPrefix(idx),
            peer1, this->BuildNextHopAddress(peer1->ToString()));
    }

    TASK_UTIL_EXPECT_TRUE(peer_yx->flap_count() > 3);
    TASK_UTIL_EXPECT_TRUE(peer_xy->flap_count() > 3);

    this->Configure(DB::PartitionCount() * 2);
    task_util::WaitForIdle();

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyPathExists(bs_x, master, this->BuildPrefix(idx),
            peer2, this->BuildNextHopAddress(peer2->ToString()));
        this->VerifyPathExists(bs_x, master, this->BuildPrefix(idx),
            peer1, this->BuildNextHopAddress(peer1->ToString()));
        this->VerifyPathExists(bs_y, master, this->BuildPrefix(idx),
            peer_yx, this->BuildNextHopAddress(peer1->ToString()));
        this->VerifyPathNoExists(bs_y, master, this->BuildPrefix(idx),
            peer_yx, this->BuildNextHopAddress(peer2->ToString()));
    }

    uint64_t flap_count_yx = peer_yx->flap_count();
    uint64_t flap_count_xy = peer_xy->flap_count();

    this->Configure(DB::PartitionCount() * 2 - 1);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(peer_yx->flap_count() > flap_count_yx + 3);
    TASK_UTIL_EXPECT_TRUE(peer_xy->flap_count() > flap_count_xy + 3);

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->DeleteRoute(bs_x, peer1, master, this->BuildPrefix(idx));
        this->DeleteRoute(bs_x, peer2, master, this->BuildPrefix(idx));
    }
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyRouteNoExists(bs_x, master, this->BuildPrefix(idx));
        this->VerifyRouteNoExists(bs_y, master, this->BuildPrefix(idx));
    }
}

//
// Configure a prefix limit and a very high idle timeout.
// Add multiple prefixes such that the prefix limit configured on Y for
// the peering to X is exceeded
// Verify that the peering flaps once and doesn't get re-established.
// Then increase the prefix limit and verify that that the peering gets
// re-established and all the expected paths exist on Y.
//
TYPED_TEST(BgpIpTest, PrefixLimit2) {
    BgpServerTestPtr bs_x = this->bs_x_;
    BgpServerTestPtr bs_y = this->bs_y_;
    PeerMock *peer1 = this->peer1_;
    PeerMock *peer2 = this->peer2_;
    BgpPeer *peer_xy = this->peer_xy_;
    BgpPeer *peer_yx = this->peer_yx_;
    const string &master = this->master_;

    this->Configure(DB::PartitionCount() * 2 - 1, 3600);
    task_util::WaitForIdle();

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->AddRoute(bs_x, peer2, master, this->BuildPrefix(idx),
            this->BuildNextHopAddress(peer2->ToString()));
        this->AddRoute(bs_x, peer1, master, this->BuildPrefix(idx),
            this->BuildNextHopAddress(peer1->ToString()));
    }
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyPathExists(bs_x, master, this->BuildPrefix(idx),
            peer2, this->BuildNextHopAddress(peer2->ToString()));
        this->VerifyPathExists(bs_x, master, this->BuildPrefix(idx),
            peer1, this->BuildNextHopAddress(peer1->ToString()));
    }

    usleep(3000000);
    TASK_UTIL_EXPECT_EQ(1U, peer_yx->flap_count());
    TASK_UTIL_EXPECT_EQ(1U, peer_xy->flap_count());

    this->Configure(DB::PartitionCount() * 2, 3600);
    task_util::WaitForIdle();

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyPathExists(bs_x, master, this->BuildPrefix(idx),
            peer2, this->BuildNextHopAddress(peer2->ToString()));
        this->VerifyPathExists(bs_x, master, this->BuildPrefix(idx),
            peer1, this->BuildNextHopAddress(peer1->ToString()));
        this->VerifyPathExists(bs_y, master, this->BuildPrefix(idx),
            peer_yx, this->BuildNextHopAddress(peer1->ToString()));
        this->VerifyPathNoExists(bs_y, master, this->BuildPrefix(idx),
            peer_yx, this->BuildNextHopAddress(peer2->ToString()));
    }

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->DeleteRoute(bs_x, peer1, master, this->BuildPrefix(idx));
        this->DeleteRoute(bs_x, peer2, master, this->BuildPrefix(idx));
    }
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyRouteNoExists(bs_x, master, this->BuildPrefix(idx));
        this->VerifyRouteNoExists(bs_y, master, this->BuildPrefix(idx));
    }
}

//
// Configure a prefix limit and a small non-zero idle timeout.
// Add multiple prefixes such that the prefix limit configured on Y for
// the peering to X is exceeded.
// Verify that the peering keeps flapping.
//
TYPED_TEST(BgpIpTest, PrefixLimit3) {
    BgpServerTestPtr bs_x = this->bs_x_;
    BgpServerTestPtr bs_y = this->bs_y_;
    PeerMock *peer1 = this->peer1_;
    PeerMock *peer2 = this->peer2_;
    BgpPeer *peer_xy = this->peer_xy_;
    BgpPeer *peer_yx = this->peer_yx_;
    const string &master = this->master_;

    this->Configure(DB::PartitionCount() * 2 - 1, 1);
    task_util::WaitForIdle();

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->AddRoute(bs_x, peer2, master, this->BuildPrefix(idx),
            this->BuildNextHopAddress(peer2->ToString()));
        this->AddRoute(bs_x, peer1, master, this->BuildPrefix(idx),
            this->BuildNextHopAddress(peer1->ToString()));
    }
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyPathExists(bs_x, master, this->BuildPrefix(idx),
            peer2, this->BuildNextHopAddress(peer2->ToString()));
        this->VerifyPathExists(bs_x, master, this->BuildPrefix(idx),
            peer1, this->BuildNextHopAddress(peer1->ToString()));
    }

    usleep(3000000);
    TASK_UTIL_EXPECT_TRUE(peer_yx->flap_count() > 3);
    TASK_UTIL_EXPECT_TRUE(peer_xy->flap_count() > 3);

    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->DeleteRoute(bs_x, peer1, master, this->BuildPrefix(idx));
        this->DeleteRoute(bs_x, peer2, master, this->BuildPrefix(idx));
    }
    for (int idx = 1; idx <= DB::PartitionCount() * 2; ++idx) {
        this->VerifyRouteNoExists(bs_x, master, this->BuildPrefix(idx));
        this->VerifyRouteNoExists(bs_y, master, this->BuildPrefix(idx));
    }
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    BgpServer::Initialize();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<StateMachine>(
        boost::factory<StateMachineTest *>());
}

static void TearDown() {
    task_util::WaitForIdle();
    BgpServer::Terminate();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

static void process_command_line_args(int argc, const char **argv) {
    options_description desc("BgpIpTest");
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

int bgp_ip_test_main(int argc, const char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, const_cast<char **>(argv));
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    process_command_line_args(argc, const_cast<const char **>(argv));
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}

#ifndef __BGP_IP_TEST_WRAPPER_TEST_SUITE__

int main(int argc, char **argv) {
    return bgp_ip_test_main(argc, const_cast<const char **>(argv));
}

#endif
