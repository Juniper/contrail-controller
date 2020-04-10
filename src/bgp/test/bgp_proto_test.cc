/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/proto.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include <boost/assign/list_of.hpp>
#include "net/bgp_af.h"
#include "bgp/bgp_log.h"
#include "bgp_message_test.h"

using namespace std;

namespace {

class BgpProtoTest : public testing::Test {
protected:
    bool ParseAndVerifyError(const uint8_t *data, size_t size, int error,
            int subcode, std::string type, int offset, int err_size) {
        ParseErrorContext ec;
        const BgpProto::BgpMessage *result =
                BgpProto::Decode(&data[0], size, &ec, false);
        EXPECT_TRUE(result == NULL);

        EXPECT_EQ(error, ec.error_code);
        EXPECT_EQ(subcode, ec.error_subcode);

        TASK_UTIL_EXPECT_EQ_TYPE_NAME(type, ec.type_name);
        EXPECT_EQ(offset, ec.data-data);
        EXPECT_EQ(err_size, ec.data_size);
        if (result) delete result;
        return true;
    }


    void GenerateByteError(uint8_t *data, size_t data_size){
        enum {
            InsertByte,
            DeleteByte,
            ChangeByte
        };

        uint8_t new_data[data_size + 1];
        int pos = rand() % data_size;
        int error_type = rand() % 3;
        if (error_type == InsertByte) {
            memcpy(new_data, data, pos);
            new_data[pos] = rand();
            memcpy(new_data + pos + 1, data + pos, data_size - pos);
            data_size++;
        } else if (error_type == DeleteByte) {
            memcpy(new_data, data, pos);
            memcpy(new_data + pos, data + pos + 1, data_size - pos - 1);
            data_size --;
        } else if (error_type == ChangeByte) {
            memcpy(new_data, data, data_size);
            new_data[pos] = rand();
        }

        BgpProto::BgpMessage *msg = BgpProto::Decode(new_data, data_size, NULL, false);
        if (msg) delete msg;
    }

    const BgpAttribute *BgpFindAttribute(const BgpProto::Update *update,
        BgpAttribute::Code code) {
        for (vector<BgpAttribute *>::const_iterator it =
             update->path_attributes.begin();
             it != update->path_attributes.end(); ++it) {
            if ((*it)->code == code)
                return *it;
        }
        return NULL;
    }
};

class BuildUpdateMessage {
public:
    static void Generate(BgpProto::Update *msg) {
        for (size_t i = 0; i < build_params_.size(); i++) {
            int chance = rand() % build_params_[i].second;
            if (chance == 0) {
                build_params_[i].first(msg);
            }
        }
    }

private:

    typedef boost::function<void(BgpProto::Update *msg)> BuilderFn;
    // Pair with a builder function and probability factor (chance of  1/ factor)
    typedef std::pair<BuilderFn, int> BuildUpdateParam;
    static std::vector<BuildUpdateParam> build_params_;


    static void AddOrigin(BgpProto::Update *msg) {
        const static int kNumOriginValues = 3;
        BgpAttrOrigin *origin = new BgpAttrOrigin(rand() % kNumOriginValues);
        msg->path_attributes.push_back(origin);
    }

    static void AddNextHop(BgpProto::Update *msg) {
        BgpAttrNextHop *nexthop = new BgpAttrNextHop(rand());
        msg->path_attributes.push_back(nexthop);
    }

    static void AddMultiExitDisc(BgpProto::Update *msg) {
        BgpAttrMultiExitDisc *med = new BgpAttrMultiExitDisc(rand());
        msg->path_attributes.push_back(med);
    }

    static void AddLocalPref(BgpProto::Update *msg) {
        BgpAttrLocalPref *lp = new BgpAttrLocalPref(rand());
        msg->path_attributes.push_back(lp);
    }

    static void AddAtomicAggregate(BgpProto::Update *msg) {
        BgpAttrAtomicAggregate *aa = new BgpAttrAtomicAggregate;
        msg->path_attributes.push_back(aa);
    }

    static void AddAggregator(BgpProto::Update *msg) {
        BgpAttrAggregator *agg = new BgpAttrAggregator(rand(), rand());
        msg->path_attributes.push_back(agg);
    }

    static void AddAsPath(BgpProto::Update *msg) {
        static const int kMaxPathSegments = 20;
        AsPathSpec *path_spec = new AsPathSpec;
        AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
        ps->path_segment_type = rand() % 2;
        for (int ps_len = rand() % kMaxPathSegments; ps_len > 0; ps_len--) {
            ps->path_segment.push_back(rand());
        }
        path_spec->path_segments.push_back(ps);
        msg->path_attributes.push_back(path_spec);
    }

    static void AddCommunity(BgpProto::Update *msg) {
        CommunitySpec *community = new CommunitySpec;
        for (int len = rand() % 32; len > 0; len--) {
            community->communities.push_back(rand());
        }
        msg->path_attributes.push_back(community);
    }

    static void AddMpNlri(BgpProto::Update *msg) {
        static const int kMaxRoutes = 500;
        BgpMpNlri *mp_nlri = new BgpMpNlri;
        mp_nlri->flags = BgpAttribute::Optional;
        mp_nlri->code = BgpAttribute::MPReachNlri;
        mp_nlri->afi = 1;
        mp_nlri->safi = 128;
        for (int nh_len = 12; nh_len > 0; nh_len--) {
            mp_nlri->nexthop.push_back((char)rand());
        }
        int len = rand() % kMaxRoutes;
        for (int i = 0; i < len; i++) {
            BgpProtoPrefix *prefix = new BgpProtoPrefix;
            int len = rand() % 12;
            prefix->prefixlen = len*8;
            for (int j = 0; j < len; j++)
                prefix->prefix.push_back(rand() % 256);
            mp_nlri->nlri.push_back(prefix);
        }
        msg->path_attributes.push_back(mp_nlri);
    }

    static void AddExtCommunity(BgpProto::Update *msg) {
        ExtCommunitySpec *ext_community = new ExtCommunitySpec;
        for (int i = rand() % 64; i > 0; i--) {
            ext_community->communities.push_back(rand());
        }
        msg->path_attributes.push_back(ext_community);
    }

    static void AddPmsiTunnel(BgpProto::Update *msg) {
        PmsiTunnelSpec *pmsispec = new PmsiTunnelSpec;
        pmsispec->tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
        pmsispec->tunnel_type = PmsiTunnelSpec::IngressReplication;
        boost::system::error_code ec;
        pmsispec->SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));
        msg->path_attributes.push_back(pmsispec);
    }

    static void AddEdgeDiscovery(BgpProto::Update *msg) {
        EdgeDiscoverySpec *edspec = new EdgeDiscoverySpec;
        for (int i = rand() % 64; i > 0; i--) {
            boost::system::error_code ec;
            EdgeDiscoverySpec::Edge *edge = new EdgeDiscoverySpec::Edge;
            edge->SetIp4Address(Ip4Address::from_string("10.1.1.1", ec));
            edge->SetLabels(10000, 20000);
            edspec->edge_list.push_back(edge);
        }
        msg->path_attributes.push_back(edspec);
    }

    static void AddEdgeForwarding(BgpProto::Update *msg) {
        EdgeForwardingSpec *efspec = new EdgeForwardingSpec;
        for (int i = rand() % 64; i > 0; i--) {
            boost::system::error_code ec;
            EdgeForwardingSpec::Edge *edge = new EdgeForwardingSpec::Edge;
            edge->SetInboundIp4Address(Ip4Address::from_string("10.1.1.1", ec));
            edge->inbound_label = rand() % 10000;
            edge->SetOutboundIp4Address(Ip4Address::from_string("10.1.1.2", ec));
            edge->outbound_label = rand() % 10000;
            efspec->edge_list.push_back(edge);
        }
        msg->path_attributes.push_back(efspec);
    }

    static void AddOriginVnPath(BgpProto::Update *msg) {
        OriginVnPathSpec *ovnpath = new OriginVnPathSpec;
        for (int i = rand() % 64; i > 0; i--) {
            ovnpath->origin_vns.push_back(rand());
        }
        msg->path_attributes.push_back(ovnpath);
    }

    static void AddUnknown(BgpProto::Update *msg) {
        BgpAttrUnknown *unk = new BgpAttrUnknown;
        unk->flags = BgpAttribute::Optional;
        // Code must be different than attributes define in BgpAttribute::Code
        unk->code = (rand() % 64) + 64;
        for (int len = rand() % 8; len > 0; len--) {
            unk->value.push_back(rand());
        }
        msg->path_attributes.push_back(unk);
    }
};

std::vector<BuildUpdateMessage::BuildUpdateParam>
    BuildUpdateMessage::build_params_ = {
        std::make_pair(&BuildUpdateMessage::AddOrigin, 1),
        std::make_pair(&BuildUpdateMessage::AddNextHop, 1),
        std::make_pair(&BuildUpdateMessage::AddMultiExitDisc, 5),
        std::make_pair(&BuildUpdateMessage::AddLocalPref, 5),
        std::make_pair(&BuildUpdateMessage::AddAtomicAggregate, 5),
        std::make_pair(&BuildUpdateMessage::AddAggregator, 5),
        std::make_pair(&BuildUpdateMessage::AddAsPath, 1),
        std::make_pair(&BuildUpdateMessage::AddCommunity, 5),
        std::make_pair(&BuildUpdateMessage::AddMpNlri, 5),
        std::make_pair(&BuildUpdateMessage::AddExtCommunity, 5),
        std::make_pair(&BuildUpdateMessage::AddPmsiTunnel, 5),
        std::make_pair(&BuildUpdateMessage::AddEdgeDiscovery, 5),
        std::make_pair(&BuildUpdateMessage::AddEdgeForwarding, 5),
        std::make_pair(&BuildUpdateMessage::AddOriginVnPath, 5),
        std::make_pair(&BuildUpdateMessage::AddUnknown, 5)};

static void TestOpenMessage(BgpProto::OpenMessage &open) {
    BgpMessageTest::GenerateOpenMessage(&open);

    uint8_t data[256];
    int res = BgpProto::Encode(&open, data, 256);
    EXPECT_NE(-1, res);

    if (detail::debug_) {
        for (int i = 0; i < res; i++) {
            printf("%02x ", data[i]);
        }
        printf("\n");
    }

    const BgpProto::OpenMessage *result;
    result = static_cast<const BgpProto::OpenMessage *>(BgpProto::Decode(data, res));
    EXPECT_EQ(open.as_num, result->as_num);
    EXPECT_EQ(open.holdtime, result->holdtime);
    EXPECT_EQ(open.identifier, result->identifier);
    EXPECT_EQ(open.opt_params.size(), result->opt_params.size());
    for (size_t i = 0; i < open.opt_params.size(); i++) {
        EXPECT_EQ(open.opt_params[i]->capabilities.size(),
                  result->opt_params[i]->capabilities.size());
        for (size_t j = 0; j < open.opt_params[i]->capabilities.size(); j++) {
            EXPECT_EQ(open.opt_params[i]->capabilities[j]->code,
                      result->opt_params[i]->capabilities[j]->code);
            EXPECT_EQ(open.opt_params[i]->capabilities[i]->capability,
                      result->opt_params[i]->capabilities[i]->capability);
        }
    }
    delete result;
}

TEST_F(BgpProtoTest, HeaderError) {
    uint8_t data[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                       0x00, 0x12 };

    // msg length error - length is 18
    ParseAndVerifyError(data, sizeof(data), BgpProto::Notification::MsgHdrErr,
            BgpProto::Notification::BadMsgLength, "BgpMsgLength", 16, 2);

    // msg length error - length is less than 18
    data[17] = 17;
    ParseAndVerifyError(data, sizeof(data), BgpProto::Notification::MsgHdrErr,
            BgpProto::Notification::BadMsgLength, "BgpMsgLength", 16, 2);
}

TEST_F(BgpProtoTest, Open1) {
    BgpProto::OpenMessage open;
    BgpMessageTest::GenerateOpenMessage(&open);

    open.identifier = 1;
    TestOpenMessage(open);
}

TEST_F(BgpProtoTest, Open2) {
    BgpProto::OpenMessage open;
    BgpMessageTest::GenerateOpenMessage(&open);

    // Similar to test Open1, but use a larger id which is -ve in int32 format
    open.identifier = 2952792063U;

    TestOpenMessage(open);
}

TEST_F(BgpProtoTest, Open3) {

    uint8_t data[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                      0x00, 0x35, 0x01, 0x04, 0x1d, 0xfb, 0x00, 0xb4,
                      0xc0, 0xa8, 0x38, 0x65, 0x18, 0x02, 0x06, 0x01,
                      0x04, 0x00, 0x01, 0x00, 0x01, 0x02, 0x02, 0x80,
                      0x00, 0x02, 0x02, 0x02, 0x00, 0x02, 0x06, 0x41,
                      0x04, 0x00, 0x00, 0x1d, 0xfb };

    const BgpProto::OpenMessage *result =
            static_cast<const BgpProto::OpenMessage *>(BgpProto::Decode(data, sizeof(data)));
    EXPECT_TRUE(result != NULL);
    EXPECT_EQ(4U, result->opt_params.size());
    for (size_t i = 0; i < result->opt_params.size(); i++) {
        EXPECT_EQ(1U, result->opt_params[i]->capabilities.size());
    }
    const BgpProto::OpenMessage::Capability *cap = result->opt_params[0]->capabilities[0];
    EXPECT_EQ(1, cap->code);
    const unsigned char temp[] = {0, 1, 0, 1};
    EXPECT_EQ(cap->capability, vector<uint8_t>(temp, temp + 4));

    cap = result->opt_params[1]->capabilities[0];
    EXPECT_EQ(0x80, cap->code);
    EXPECT_EQ(0U, cap->capability.size());

    cap = result->opt_params[2]->capabilities[0];
    EXPECT_EQ(0x02, cap->code);
    EXPECT_EQ(0U, cap->capability.size());

    cap = result->opt_params[3]->capabilities[0];
    EXPECT_EQ(0x41, cap->code);
    const unsigned char temp2[] = {0, 0, 0x1d, 0xfb};
    EXPECT_EQ(cap->capability, vector<uint8_t>(temp2, temp2 + 4));
    delete result;
}

TEST_F(BgpProtoTest, Open4) {

    uint8_t data[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                      0x00, 0x4b, 0x01, 0x04, 0x1d, 0xfb, 0x00, 0xb4,
                      0xc0, 0xa8, 0x38, 0x65, 0x2e, 0x02, 0x06, 0x01,
                      0x04, 0x00, 0x01, 0x00, 0x01, 0x02, 0x02, 0x80,
                      0x00, 0x02, 0x02, 0x02, 0x00, 0x02, 0x06, 0x41,
                      0x04, 0x00, 0x00, 0x1d, 0xfb,

                      0x02, 0x14, 0x40, 0x12, // Graceful Restart 18 Bytes
                      0x00, 0x5a,             // Restart time: 90 Seconds
                      0x00, 0x01, 0x80, 0x80, // IPv4 Vpn with Forwarding
                      0x00, 0x01, 0x84, 0x80, // IPv4 RTarget with Forwarding
                      0x00, 0x19, 0x46, 0x80, // L2Vpn EVpn with Forwarding
                      0x00, 0x01, 0xf3, 0x80, // IPv4 ErmVpn with Forwarding
    };

    const BgpProto::OpenMessage *result =
            static_cast<const BgpProto::OpenMessage *>(
                    BgpProto::Decode(data, sizeof(data)));
    EXPECT_TRUE(result != NULL);
    EXPECT_EQ(5U, result->opt_params.size());
    for (size_t i = 0; i < result->opt_params.size(); i++) {
        EXPECT_EQ(1U, result->opt_params[i]->capabilities.size());
    }
    const BgpProto::OpenMessage::Capability *cap =
        result->opt_params[0]->capabilities[0];
    EXPECT_EQ(1, cap->code);
    const unsigned char temp[] = {0, 1, 0, 1};
    EXPECT_EQ(cap->capability, vector<uint8_t>(temp, temp + 4));

    cap = result->opt_params[1]->capabilities[0];
    EXPECT_EQ(0x80, cap->code);
    EXPECT_EQ(0U, cap->capability.size());

    cap = result->opt_params[2]->capabilities[0];
    EXPECT_EQ(0x02, cap->code);
    EXPECT_EQ(0U, cap->capability.size());

    cap = result->opt_params[3]->capabilities[0];
    EXPECT_EQ(0x41, cap->code);
    const unsigned char temp2[] = {0, 0, 0x1d, 0xfb};
    EXPECT_EQ(cap->capability, vector<uint8_t>(temp2, temp2 + 4));

    cap = result->opt_params[4]->capabilities[0];
    EXPECT_EQ(BgpProto::OpenMessage::Capability::GracefulRestart, cap->code);
    BgpProto::OpenMessage::Capability::GR gr_params =
        BgpProto::OpenMessage::Capability::GR();
    BgpProto::OpenMessage::Capability::GR::Decode(&gr_params,
            result->opt_params[4]->capabilities);

    EXPECT_EQ(gr_params.flags, 0x00);
    EXPECT_EQ(gr_params.time, 0x5a);
    EXPECT_EQ(4U, gr_params.families.size());

    EXPECT_EQ(BgpAf::IPv4, gr_params.families[0].afi);
    EXPECT_EQ(BgpAf::Vpn, gr_params.families[0].safi);
    EXPECT_EQ(0x80, gr_params.families[0].flags);

    EXPECT_EQ(BgpAf::IPv4, gr_params.families[1].afi);
    EXPECT_EQ(BgpAf::RTarget, gr_params.families[1].safi);
    EXPECT_EQ(0x80, gr_params.families[1].flags);

    EXPECT_EQ(BgpAf::L2Vpn, gr_params.families[2].afi);
    EXPECT_EQ(BgpAf::EVpn, gr_params.families[2].safi);
    EXPECT_EQ(0x80, gr_params.families[2].flags);

    EXPECT_EQ(BgpAf::IPv4, gr_params.families[3].afi);
    EXPECT_EQ(BgpAf::ErmVpn, gr_params.families[3].safi);
    EXPECT_EQ(0x80, gr_params.families[3].flags);

    delete result;
}

TEST_F(BgpProtoTest, Open5) {

    uint8_t data[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                       0x00, 0x6b, 0x01, 0x04, 0x1d, 0xfb, 0x00, 0xb4,
                       0xc0, 0xa8, 0x38, 0x65, 0x4e, 0x02, 0x06, 0x01,
                       0x04, 0x00, 0x01, 0x00, 0x01, 0x02, 0x02, 0x80,
                       0x00, 0x02, 0x02, 0x02, 0x00, 0x02, 0x06, 0x41,
                       0x04, 0x00, 0x00, 0x1d, 0xfb,

                       0x02, 0x14, 0x40, 0x12, // Graceful Restart 18 Bytes
                       0x00, 0x5a,             // Restart time: 90 Seconds
                       0x00, 0x01, 0x80, 0x80, // IPv4 Vpn with Forwarding
                       0x00, 0x01, 0x84, 0x80, // IPv4 RTarget with Forwarding
                       0x00, 0x19, 0x46, 0x80, // L2Vpn EVpn with Forwarding
                       0x00, 0x01, 0xf3, 0x80, // IPv4 ErmVpn with Forwarding

                       0x02, 0x1e, 0x47, 0x1c, // LL Graceful Restart 28 Bytes
                       0x00, 0x01, 0x80, 0x80, // IPv4 Vpn with Forwarding
                       0x00, 0x00, 0xFF,       // Restart Time in Seconds
                       0x00, 0x01, 0x84, 0x80, // IPv4 RTarget with Forwarding
                       0x00, 0xFF, 0x00,       // Restart Time in Seconds
                       0x00, 0x19, 0x46, 0x80, // L2Vpn EVpn with Forwarding
                       0xFF, 0x00, 0x00,       // Restart Time in Seconds
                       0x00, 0x01, 0xf3, 0x80, // IPv4 ErmVpn with Forwarding
                       0xFF, 0xFF, 0xFF,       // Restart Time in Seconds
    };

    const BgpProto::OpenMessage *result =
            static_cast<const BgpProto::OpenMessage *>(
                    BgpProto::Decode(data, sizeof(data)));
    EXPECT_TRUE(result != NULL);
    EXPECT_EQ(6U, result->opt_params.size());
    for (size_t i = 0; i < result->opt_params.size(); i++) {
        EXPECT_EQ(1U, result->opt_params[i]->capabilities.size());
    }
    const BgpProto::OpenMessage::Capability *cap =
        result->opt_params[0]->capabilities[0];
    EXPECT_EQ(1, cap->code);
    const unsigned char temp[] = {0, 1, 0, 1};
    EXPECT_EQ(cap->capability, vector<uint8_t>(temp, temp + 4));

    cap = result->opt_params[1]->capabilities[0];
    EXPECT_EQ(0x80, cap->code);
    EXPECT_EQ(0U, cap->capability.size());

    cap = result->opt_params[2]->capabilities[0];
    EXPECT_EQ(0x02, cap->code);
    EXPECT_EQ(0U, cap->capability.size());

    cap = result->opt_params[3]->capabilities[0];
    EXPECT_EQ(0x41, cap->code);
    const unsigned char temp2[] = {0, 0, 0x1d, 0xfb};
    EXPECT_EQ(cap->capability, vector<uint8_t>(temp2, temp2 + 4));

    cap = result->opt_params[4]->capabilities[0];
    EXPECT_EQ(BgpProto::OpenMessage::Capability::GracefulRestart, cap->code);
    BgpProto::OpenMessage::Capability::GR gr_params =
        BgpProto::OpenMessage::Capability::GR();
    BgpProto::OpenMessage::Capability::GR::Decode(&gr_params,
            result->opt_params[4]->capabilities);

    EXPECT_EQ(gr_params.flags, 0x00);
    EXPECT_EQ(gr_params.time, 0x5a);
    EXPECT_EQ(4U, gr_params.families.size());

    EXPECT_EQ(BgpAf::IPv4, gr_params.families[0].afi);
    EXPECT_EQ(BgpAf::Vpn, gr_params.families[0].safi);
    EXPECT_EQ(0x80, gr_params.families[0].flags);

    EXPECT_EQ(BgpAf::IPv4, gr_params.families[1].afi);
    EXPECT_EQ(BgpAf::RTarget, gr_params.families[1].safi);
    EXPECT_EQ(0x80, gr_params.families[1].flags);

    EXPECT_EQ(BgpAf::L2Vpn, gr_params.families[2].afi);
    EXPECT_EQ(BgpAf::EVpn, gr_params.families[2].safi);
    EXPECT_EQ(0x80, gr_params.families[2].flags);

    EXPECT_EQ(BgpAf::IPv4, gr_params.families[3].afi);
    EXPECT_EQ(BgpAf::ErmVpn, gr_params.families[3].safi);
    EXPECT_EQ(0x80, gr_params.families[3].flags);

    cap = result->opt_params[5]->capabilities[0];
    EXPECT_EQ(BgpProto::OpenMessage::Capability::LongLivedGracefulRestart,
              cap->code);
    BgpProto::OpenMessage::Capability::LLGR llgr_params =
        BgpProto::OpenMessage::Capability::LLGR();
    BgpProto::OpenMessage::Capability::LLGR::Decode(&llgr_params,
            result->opt_params[5]->capabilities);

    EXPECT_EQ(4U, llgr_params.families.size());

    EXPECT_EQ(BgpAf::IPv4, llgr_params.families[0].afi);
    EXPECT_EQ(BgpAf::Vpn, llgr_params.families[0].safi);
    EXPECT_EQ(0x80, llgr_params.families[0].flags);
    EXPECT_EQ(0x0000FFu, llgr_params.families[0].time);

    EXPECT_EQ(BgpAf::IPv4, llgr_params.families[1].afi);
    EXPECT_EQ(BgpAf::RTarget, llgr_params.families[1].safi);
    EXPECT_EQ(0x80, llgr_params.families[1].flags);
    EXPECT_EQ(0x00FF00U, llgr_params.families[1].time);

    EXPECT_EQ(BgpAf::L2Vpn, llgr_params.families[2].afi);
    EXPECT_EQ(BgpAf::EVpn, llgr_params.families[2].safi);
    EXPECT_EQ(0x80, llgr_params.families[2].flags);
    EXPECT_EQ(0xFF0000U, llgr_params.families[2].time);

    EXPECT_EQ(BgpAf::IPv4, llgr_params.families[3].afi);
    EXPECT_EQ(BgpAf::ErmVpn, llgr_params.families[3].safi);
    EXPECT_EQ(0x80, llgr_params.families[3].flags);
    EXPECT_EQ(0xFFFFFFu, llgr_params.families[3].time);

    delete result;
}

TEST_F(BgpProtoTest, Notification) {
    BgpProto::Notification notification;
    notification.error = BgpProto::Notification::MsgHdrErr;
    notification.subcode = BgpProto::Notification::BadMsgLength;
    char notif_data[] = { 0x1, 0x2, 0x3 };
    notification.data = string(notif_data, sizeof(notif_data));
    uint8_t data[256];
    int res = BgpProto::Encode(&notification, data, 256);
    EXPECT_NE(-1, res);
    if (detail::debug_) {
        for (int i = 0; i < res; i++) {
            printf("%02x ", data[i]);
        }
        printf("\n");
    }

    const BgpProto::Notification *result;
    result = static_cast<const BgpProto::Notification *>(BgpProto::Decode(data, res));
    EXPECT_TRUE(result != NULL);
    if (result) {
        EXPECT_EQ(notification.error, result->error);
        EXPECT_EQ(notification.subcode, result->subcode);
        EXPECT_EQ(notification.data, result->data);
    }
    delete result;
}

TEST_F(BgpProtoTest, Update) {
    BgpProto::Update update;
    BgpMessageTest::GenerateUpdateMessage(&update, BgpAf::IPv4, BgpAf::Unicast);
    uint8_t data[256];

    int res = BgpProto::Encode(&update, data, 256, NULL, false);
    EXPECT_NE(-1, res);
    if (detail::debug_) {
        for (int i = 0; i < res; i++) {
            printf("%02x ", data[i]);
        }
        printf("\n");
    }

    const BgpProto::Update *result;
    result = static_cast<const BgpProto::Update *>(BgpProto::Decode(data, res, NULL, false));
    EXPECT_TRUE(result != NULL);
    if (result) {
        EXPECT_EQ(0, result->CompareTo(update));
        delete result;
    }
}

TEST_F(BgpProtoTest, L3VPNUpdate) {
    BgpProto::Update update;
    BgpMessageTest::GenerateUpdateMessage(&update, BgpAf::IPv4, BgpAf::Vpn);
    uint8_t data[256];

    int res = BgpProto::Encode(&update, data, 256, NULL, false);
    EXPECT_NE(-1, res);
    if (detail::debug_) {
        for (int i = 0; i < res; i++) {
            printf("%02x ", data[i]);
        }
        printf("\n");
    }

    const BgpProto::Update *result;
    result = static_cast<const BgpProto::Update *>(BgpProto::Decode(data, res, NULL, false));
    EXPECT_TRUE(result != NULL);
    if (result) {
        EXPECT_EQ(0, result->CompareTo(update));
        delete result;
    }
}

TEST_F(BgpProtoTest, 100PlusExtCommunities) {
    BgpProto::Update update;
    BgpMessageTest::GenerateUpdateMessage(&update, BgpAf::IPv4, BgpAf::Vpn);
    uint8_t data[BgpProto::kMaxMessageSize];

    ExtCommunitySpec *ext_community = NULL;
    for (size_t i = 0; i < update.path_attributes.size(); ++i) {
        BgpAttribute *attr = update.path_attributes[i];
        if (attr->code == BgpAttribute::ExtendedCommunities) {
            ext_community = static_cast<ExtCommunitySpec *>(attr);
            break;
        }
    }
    ASSERT_TRUE(ext_community != NULL);
    for (uint i = 0; i < 100; ++i) {
        uint64_t value = 0x0002fc00007a1200ULL;
        ext_community->communities.push_back(value + i);
    }

    int res = BgpProto::Encode(&update, data, BgpProto::kMaxMessageSize, NULL, false);
    EXPECT_NE(-1, res);
    if (detail::debug_) {
        for (int i = 0; i < res; i++) {
            printf("%02x ", data[i]);
        }
        printf("\n");
    }

    const BgpProto::Update *result;
    result = static_cast<const BgpProto::Update *>(BgpProto::Decode(data, res, NULL, false));
    EXPECT_TRUE(result != NULL);
    if (result) {
        EXPECT_EQ(0, result->CompareTo(update));
        delete result;
    } else {
        for (int i = 0; i < res; i++) {
            if (i % 16 == 0) {
                printf("\n");
            }
            printf("%02x ", data[i]);
        }
        printf("\n");
    }

}

TEST_F(BgpProtoTest, EvpnUpdate) {
    BgpProto::Update update;
    BgpMessageTest::GenerateUpdateMessage(&update, BgpAf::L2Vpn, BgpAf::EVpn);
    uint8_t data[256];

    int res = BgpProto::Encode(&update, data, 256, NULL, false);
    EXPECT_NE(-1, res);
    if (detail::debug_) {
        for (int i = 0; i < res; i++) {
            printf("%02x ", data[i]);
        }
        printf("\n");
    }

    const BgpProto::Update *result;
    result = static_cast<const BgpProto::Update *>(BgpProto::Decode(data, res, NULL, false));
    EXPECT_TRUE(result != NULL);
    if (result) {
        EXPECT_EQ(0, result->CompareTo(update));
        delete result;
    }
}

//
// Decode, parse and encode the cluster-list attribute
//
TEST_F(BgpProtoTest, ClusterList) {
    const uint8_t data[] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x34,     // Length
        0x02,           // Update
        0x00, 0x00,     // Withdrawn Routes Length
        0x00, 0x1d,     // Path Attribute Length
        0x40, 0x01, 0x01, 0x01, // ORIGIN
        0x40, 0x02, 0x04, 0x02, 0x01, 0x00, 0x01, // AS_PATH
        0x40, 0x05, 0x04, 0x00, 0x00, 0x00, 0x64, // LOCAL_PREF
        // CLUSTER_LIST
        0x80, 0x0a, 0x08, 0x0a, 0x0b, 0x0c, 0x0d, 0xca, 0xfe, 0xd0, 0xd0,
    };
    ParseErrorContext context;
    boost::scoped_ptr<const BgpProto::Update> update(
        static_cast<const BgpProto::Update *>(
            BgpProto::Decode(data, sizeof(data), &context, false)));
    ASSERT_TRUE(update.get() != NULL);

    const ClusterListSpec *clist_spec = static_cast<const ClusterListSpec *>(
        BgpFindAttribute(update.get(), BgpAttribute::ClusterList));
    ASSERT_TRUE(clist_spec != NULL);
    EXPECT_EQ(0x0a0b0c0dul, clist_spec->cluster_list[0]);
    EXPECT_EQ(0xcafed0d0Ul, clist_spec->cluster_list[1]);

    uint8_t buffer[4096];
    int res = BgpProto::Encode(update.get(), buffer, sizeof(buffer), NULL, false);
    EXPECT_EQ(sizeof(data), static_cast<size_t>(res));
    EXPECT_EQ(0, memcmp(buffer, data, sizeof(data)));
}

TEST_F(BgpProtoTest, OpenError) {

    uint8_t data[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                       0x00, 0x35, 0x01, 0x04, 0x1d, 0xfb, 0x00, 0xb4,
                       0xc0, 0xa8, 0x38, 0x65, 0x18, 0x02, 0x06, 0x01,
                       0x04, 0x00, 0x01, 0x00, 0x01, 0x02, 0x02, 0x80,
                       0x00, 0x02, 0x02, 0x02, 0x00, 0x02, 0x06, 0x41,
                       0x04, 0x00, 0x00, 0x1d, 0xfb };


    // marker error
    data[15] = 0xf1;
    ParseAndVerifyError(data, sizeof(data), BgpProto::Notification::MsgHdrErr,
            BgpProto::Notification::ConnNotSync, "BgpMarker", 0, 16);

    // msg length error
    data[15] = 0xff;
    data[17] = 15;
    ParseAndVerifyError(data, sizeof(data), BgpProto::Notification::MsgHdrErr,
            BgpProto::Notification::BadMsgLength, "BgpMsgLength", 16, 2);

    // msg type error
    data[17] = 0x35;
    data[18] = 10;;
    ParseAndVerifyError(data, sizeof(data), BgpProto::Notification::MsgHdrErr,
            BgpProto::Notification::BadMsgType, "BgpMsgType", 18, 1);

    // version error
    data[18] = 1;
    data[19] = 3;
    ParseAndVerifyError(data, sizeof(data), BgpProto::Notification::OpenMsgErr,
            BgpProto::Notification::UnsupportedVersion, "BgpOpenVersion", 19, 1);

    // hold time error
    data[19] = 4;
    data[23] = 1;
    ParseAndVerifyError(data, sizeof(data), BgpProto::Notification::OpenMsgErr,
            BgpProto::Notification::UnacceptableHoldTime, "BgpHoldTime", 22, 2);

    // optional parameter error
    data[23] = 0xb4;
    data[29] = 1;
    ParseAndVerifyError(data, sizeof(data), BgpProto::Notification::OpenMsgErr,
            BgpProto::Notification::UnsupportedOptionalParam,
            "BgpOpenOptParamChoice", 29, 1);
}

TEST_F(BgpProtoTest, UpdateError) {
    uint8_t data[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                       0x00, 0x7b, 0x02, 0x00, 0x07, 0x09, 0x01, 0x02,
                       0x14, 0x01, 0x02, 0x03, 0x00, 0x58, 0x50, 0x01,
                       0x00, 0x01, 0x02, 0x40, 0x03, 0x04, 0xab, 0xcd,
                       0xef, 0x01, 0x40, 0x06, 0x00, 0xc0, 0x07, 0x06,
                       0xfa, 0xce, 0xca, 0xfe, 0xba, 0xbe, 0x40, 0x02,
                       0x08, 0x01, 0x03, 0x00, 0x14, 0x00, 0x15, 0x00,
                       0x16, 0xc0, 0x08, 0x04, 0x87, 0x65, 0x43, 0x21,
                       0x90, 0x0e, 0x00, 0x15, 0x00, 0x01, 0x80, 0x0c,
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                       0x61, 0x62, 0x63, 0x64, 0x00, 0x14, 0x01, 0x02,
                       0x03, 0x90, 0x0f, 0x00, 0x06, 0x00, 0x01, 0x80,
                       0x09, 0x01, 0x02, 0xc0, 0x10, 0x08, 0x10, 0x20,
                       0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x04, 0x01,
                       0x0a, 0x01, 0x02 };
    const BgpProto::Update *result = static_cast<BgpProto::Update *>(
        BgpProto::Decode(&data[0], sizeof(data), NULL, false));
    EXPECT_TRUE(result != NULL);
    delete result;

    // Withdrawn routes error.
    data[19] = 0xff;
    ParseAndVerifyError(data, sizeof(data), BgpProto::Notification::UpdateMsgErr,
            BgpProto::Notification::MalformedAttributeList,
            "BgpUpdateWithdrawnRoutes", 19, 2);
    data[19] = 0x00;

    // Attributes length error.
    data[28] = 0xff;
    ParseAndVerifyError(data, sizeof(data), BgpProto::Notification::UpdateMsgErr,
            BgpProto::Notification::MalformedAttributeList,
            "BgpPathAttributeList", 28, 2);
    data[28] = 0x00;

    // Origin attribute length error.
    data[33] = 0x05;
    ParseAndVerifyError(data, sizeof(data), BgpProto::Notification::UpdateMsgErr,
            BgpProto::Notification::AttribLengthError,
            "BgpAttrOrigin", 30, 9);
    data[33] = 0x01;

    // Bad mp reach nlri nexthop address length.
    data[79] = 0x04;
    ParseAndVerifyError(data, sizeof(data), BgpProto::Notification::UpdateMsgErr,
            BgpProto::Notification::OptionalAttribError,
            "BgpPathAttributeMpNlriNextHopLength", 79, 1);
    data[79] = 0x0c;

    // Unknown well-known attribute.
    data[31] = 0x14;
    ParseAndVerifyError(data, sizeof(data), BgpProto::Notification::UpdateMsgErr,
            BgpProto::Notification::UnrecognizedWellKnownAttrib,
            "BgpAttrUnknown", 30, 5);

    // Unknown optional attribute.
    data[30] |= BgpAttribute::Optional;
    result = static_cast<BgpProto::Update *>(
        BgpProto::Decode(&data[0], sizeof(data), NULL, false));
    EXPECT_TRUE(result != NULL);
    BgpAttrUnknown *attr = static_cast<BgpAttrUnknown *>(result->path_attributes[0]);
    EXPECT_EQ(20, attr->code);
    EXPECT_EQ(1U, attr->value.size());
    delete result;
}

TEST_F(BgpProtoTest, UpdateAttrFlagsError) {
    // Good packet
    uint8_t data[] = {
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff,
            // Length = 161
            0x00, 0xa1,
            // Update Attribute Length 138
            0x02, 0x00, 0x00, 0x00, 0x8a,

            // Origin.. offset 23
            0x40, 0x01, 0x01, 0x02,

            // As Path.. offset 27
            0x40, 0x02, 0x08, 0x02, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03,

            // Nexthop.. offset 38
            0x40, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04,

            // MED.. offset 45
            0x80, 0x04, 0x04, 0x00, 0x00, 0x04, 0xd2,

            // Local Pref.. offset 52
            0x40, 0x05, 0x04, 0x00, 0x00, 0x00, 0x64,

            // Atomic Agg.. offset 59
            0x40, 0x06, 0x00,

            // Aggregator.. offset 62
            0xc0, 0x07, 0x06, 0x00, 0x01, 0x01, 0x02, 0x03, 0x04,

            // Communities.. offset 71
            0xc0, 0x08, 0x04, 0xff, 0xff, 0x00, 0x64,

            // Extended Communities.. offset 78
            0xc0, 0x10, 0x18, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02,
            0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x02, 0x00,
            0x01, 0x00, 0x00, 0x00, 0x04,

            // MP Reach NlRI.. offset 105
            0x80, 0x0e, 0x20, 0x00, 0x01, 0x80, 0x0c, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0xc0, 0xa8, 0x01, 0xf8, 0x00, 0x70, 0x00,
            0x01, 0x01, 0x00, 0x01, 0xc0, 0xa8, 0x0f, 0xf8, 0x00, 0xc9, 0xac,
            0xa8, 0x01,

            // MP UnReach NLRI.. offset 140
            0x80, 0x0f, 0x12, 0x00, 0x01, 0x80, 0x70, 0x80, 0x00, 0x00, 0x00,
            0x01, 0xc0, 0xa8, 0x0f, 0xf8, 0x00, 0xc9, 0xac, 0xa8, 0x02
    };


    struct {
        uint16_t offset;
        uint16_t len;
        string   attr;
        uint8_t  expected;
    } rules [] = {
        {23,  4, "BgpAttrOrigin",            BgpAttribute::Transitive},
        {27,  11, "AsPathSpec",              BgpAttribute::Transitive},
        {38,  7, "BgpAttrNextHop",           BgpAttribute::Transitive},
        {45,  7, "BgpAttrMultiExitDisc",     BgpAttribute::Optional},
        {52,  7, "BgpAttrLocalPref",         BgpAttribute::Transitive},
        {59,  3, "BgpAttrAtomicAggregate",   BgpAttribute::Transitive},
        {62,  9, "BgpAttrAggregator",        BgpAttribute::Optional|BgpAttribute::Transitive},
        {71,  7, "CommunitySpec",            BgpAttribute::Optional|BgpAttribute::Transitive},
        {78,  27, "ExtCommunitySpec",        BgpAttribute::Optional|BgpAttribute::Transitive},
        {105, 35, "BgpMpNlri",               BgpAttribute::Optional},
        {140, 21, "BgpMpNlri",               BgpAttribute::Optional},
    };

    uint16_t count = sizeof(rules)/sizeof(rules[0]);
    for (uint16_t i = 0; i < count; i++) {
        uint8_t old = data[rules[i].offset];
        for (uint8_t j = 0; j < 0xf; j++) {
            uint8_t poison = ((j << 4) & BgpAttribute::FLAG_MASK);
            if (poison == rules[i].expected)
                continue;
            data[rules[i].offset] = poison;
            char p[16];
            snprintf(p, sizeof(p), " %02x:%02x", rules[i].offset, poison);
            PROTO_DEBUG("Poison " << rules[i].attr <<  std::string(p));
            ParseAndVerifyError(data, sizeof(data), BgpProto::Notification::UpdateMsgErr,
                                BgpProto::Notification::AttribFlagsError,
                                rules[i].attr, rules[i].offset, rules[i].len);
        }
        data[rules[i].offset] = old;
    }
}

TEST_F(BgpProtoTest, LargeNotification) {
    BgpProto::Notification msg;
    msg.error = BgpProto::Notification::UpdateMsgErr;
    msg.subcode = BgpProto::Notification::AttribLengthError;
    msg.data = string(300, 'x');
    uint8_t buf[BgpProto::kMaxMessageSize];
    int result = BgpProto::Encode(&msg, buf, sizeof(buf));
    const int minMessageSize = BgpProto::kMinMessageSize;
    EXPECT_LT(minMessageSize, result);
}

TEST_F(BgpProtoTest, UpdateScale) {
    BgpProto::Update update;
    static const int kMaxRoutes = 500;

    BgpAttrOrigin *origin = new BgpAttrOrigin(BgpAttrOrigin::INCOMPLETE);
    update.path_attributes.push_back(origin);

    BgpAttrNextHop *nexthop = new BgpAttrNextHop(0xabcdef01);
    update.path_attributes.push_back(nexthop);

    BgpAttrAtomicAggregate *aa = new BgpAttrAtomicAggregate;
    update.path_attributes.push_back(aa);

    BgpAttr4ByteAggregator *agg = new BgpAttr4ByteAggregator(0xface, 0xcafebabe);
    update.path_attributes.push_back(agg);

    AsPath4ByteSpec *path4_spec = new AsPath4ByteSpec;
    AsPath4ByteSpec::PathSegment *ps3 = new AsPath4ByteSpec::PathSegment;
    ps3->path_segment_type = AsPath4ByteSpec::PathSegment::AS_SET;
    ps3->path_segment.push_back(20);
    ps3->path_segment.push_back(21);
    path4_spec->path_segments.push_back(ps3);
    update.path_attributes.push_back(path4_spec);

    As4PathSpec *path_spec4 = new As4PathSpec;
    As4PathSpec::PathSegment *ps4 = new As4PathSpec::PathSegment;
    ps4->path_segment_type = As4PathSpec::PathSegment::AS_SET;
    ps4->path_segment.push_back(20);
    ps4->path_segment.push_back(21);
    ps4->path_segment.push_back(22);
    path_spec4->path_segments.push_back(ps4);
    update.path_attributes.push_back(path_spec4);

    CommunitySpec *community = new CommunitySpec;
    community->communities.push_back(0x87654321);
    update.path_attributes.push_back(community);

    BgpMpNlri *mp_nlri = new BgpMpNlri(BgpAttribute::MPReachNlri);
    mp_nlri->flags = BgpAttribute::Optional;
    mp_nlri->afi = 1;
    mp_nlri->safi = 128;
    uint8_t nh[4] = {192,168,1,1};
    mp_nlri->nexthop.resize(12);
    copy(&nh[0], &nh[4], mp_nlri->nexthop.begin() + 8);
    for (int i = 0; i < kMaxRoutes; i++) {
        BgpProtoPrefix *prefix = new BgpProtoPrefix;
        int len = rand() % 12;
        prefix->prefixlen = len*8;
        for (int j = 0; j < len; j++)
            prefix->prefix.push_back(rand() % 256);
        mp_nlri->nlri.push_back(prefix);
    }
    update.path_attributes.push_back(mp_nlri);

    ExtCommunitySpec *ext_community = new ExtCommunitySpec;
    ext_community->communities.push_back(0x1020304050607080);
    update.path_attributes.push_back(ext_community);

    uint8_t data[4096];

    int res = BgpProto::Encode(&update, data, 4096, NULL, true);
    EXPECT_NE(-1, res);
    if (detail::debug_) {
        cout << res << " Bytes encoded" << endl;
        for (int i = 0; i < res; i++) {
            printf("%02x ", data[i]);
        }
        printf("\n");
    }

    const BgpProto::Update *result;
    result = static_cast<const BgpProto::Update *>(BgpProto::Decode(data, res, NULL, true));
    EXPECT_TRUE(result != NULL);
    if (result) {
        EXPECT_EQ(0, result->CompareTo(update));
        delete result;
    }
}

TEST_F(BgpProtoTest, KeepaliveError) {
    uint8_t data[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                       0x00, 0x13, 0x04 };

    // marker error
    data[15] = 0xf1;
    ParseAndVerifyError(data, sizeof(data), BgpProto::Notification::MsgHdrErr,
            BgpProto::Notification::ConnNotSync, "BgpMarker", 0, 16);

    // msg length error
    data[15] = 0xff;
    data[17] = 0;
    ParseAndVerifyError(data, sizeof(data), BgpProto::Notification::MsgHdrErr,
            BgpProto::Notification::BadMsgLength, "BgpMsgLength", 16, 2);
}

TEST_F(BgpProtoTest, RandomUpdate) {
    uint8_t data[BgpProto::kMaxMessageSize];
    int count = 10000;

    //
    // When running under memory profiling mode, this test can take a really
    // long time. Hence reduce the number of iterations
    //
    if (getenv("HEAPCHECK")) count = 100;
    for (int i = 0; i < count; i++) {
        BgpProto::Update update;
        BuildUpdateMessage::Generate(&update);
        int msglen = BgpProto::Encode(&update, data, sizeof(data), NULL, false);
        /*
         * Some message combinations will generate an update that is too
         * big. Ignore this for now since the test generates useful
         * permutations of attributes.
         */
        if (msglen == -1) {
            continue;
        }

        ParseErrorContext  err;
        std::auto_ptr<const BgpProto::Update> result(
            static_cast<const BgpProto::Update *>(
                BgpProto::Decode(data, msglen, &err, false)));
        EXPECT_TRUE(result.get() != NULL);
        if (result.get() != NULL) {
            EXPECT_EQ(0, result->CompareTo(update));
        } else {
            string repr(BgpProto::Notification::toString(
                (BgpProto::Notification::Code) err.error_code,
                err.error_subcode));
            printf("Decode: %s\n", repr.c_str());
            for (int x = 0; x < err.data_size; ++x) {
                if (x % 16 == 0) {
                    printf("\n");
                }
                printf("%02x ", err.data[x]);
            }

            printf("UPDATE: \n");
            for (int x = 0; x < msglen; ++x) {
                if (x % 16 == 0) {
                    printf("\n");
                }
                printf("%02x ", data[x]);
            }
            printf("\n");

        }
    }
}

TEST_F(BgpProtoTest, RandomError) {
    uint8_t data[4096];
    int count = 10000;

    //
    // When running under memory profiling mode, this test can take a really
    // long time. Hence reduce the number of iterations
    //
    if (getenv("HEAPCHECK")) count = 100;
    for (int i = 0; i < count; i++) {
        BgpProto::Update update;
        BuildUpdateMessage::Generate(&update);
        int msglen = BgpProto::Encode(&update, data, sizeof(data), NULL, false);
        if (msglen == -1) {
            continue;
        }
        if (detail::debug_) {
            cout << msglen << " Bytes encoded" << endl;
            for (int i = 0; i < msglen; i++) {
                printf("%02x ", data[i]);
            }
            printf("\n");
        }
        GenerateByteError(data, msglen);
    }
}

class EncodeLengthTest : public testing::Test {
  protected:

    int EncodeAndReadAttributeLength(BgpAttribute *spec) {
        BgpProto::Update update;
        EncodeOffsets offsets;
        update.path_attributes.push_back(spec);
        int msglen = BgpProto::Encode(&update, data_, sizeof(data_), &offsets, false);
        EXPECT_LT(0, msglen);

        int offset = offsets.FindOffset("BgpPathAttribute");
        EXPECT_LT(0, offset);
        if (offset <= 0) {
            return -1;
        }
        uint8_t flags = data_[offset + 2];
        uint attrlen = 0;
        if (flags & BgpAttribute::ExtendedLength) {
            attrlen = get_value(&data_[offset + 4], 2);
        } else {
            attrlen = get_value(&data_[offset + 4], 1);
        }
        return attrlen;
    }

    uint8_t data_[BgpProto::kMaxMessageSize];
};

static void BuildASPath(AsPathSpec::PathSegment *segment) {
    for (int i = rand() % 128; i > 0; i--) {
        segment->path_segment.push_back(i);
    }
}

TEST_F(EncodeLengthTest, AsPath) {
    const int count = 32;
    for (int i = 0; i < count; ++i) {
        AsPathSpec *spec = new AsPathSpec;
        for (int segments = rand() % 16; segments > 0; segments--) {
            AsPathSpec::PathSegment *segment = new AsPathSpec::PathSegment;
            BuildASPath(segment);
            spec->path_segments.push_back(segment);
        }
        uint encodeLen = spec->EncodeLength();
        int attrlen = EncodeAndReadAttributeLength(spec);
        EXPECT_EQ(static_cast<size_t>(attrlen), encodeLen);
    }
}

TEST_F(EncodeLengthTest, BgpMpNlri) {
    const int count = 32;
    for (int i = 0; i < count; ++i) {
        BgpMpNlri *mp_nlri = new BgpMpNlri;
        mp_nlri->flags = BgpAttribute::Optional;
        mp_nlri->code = BgpAttribute::MPReachNlri;
        mp_nlri->afi = BgpAf::IPv4;
        mp_nlri->safi = BgpAf::Vpn;
        if (rand() & 1) {
            boost::system::error_code err;
            Ip4Address addr = Ip4Address::from_string("127.0.0.1", err);
            const Ip4Address::bytes_type &bytes = addr.to_bytes();
            mp_nlri->nexthop.insert(
                mp_nlri->nexthop.begin(), bytes.begin(), bytes.end());
        } else {
            boost::system::error_code err;
            Ip6Address addr = Ip6Address::from_string(
                "2001:db8:85a3::8a2e:370:7334", err);
            const Ip6Address::bytes_type &bytes = addr.to_bytes();
            mp_nlri->nexthop.insert(
                mp_nlri->nexthop.begin(), bytes.begin(), bytes.end());
        }
        int len = rand() % 64;
        for (int i = 0; i < len; i++) {
            BgpProtoPrefix *prefix = new BgpProtoPrefix;
            int len = rand() % 16;
            prefix->prefixlen = len*8;
            for (int j = 0; j < len; j++) {
                prefix->prefix.push_back(rand() % 256);
            }
            mp_nlri->nlri.push_back(prefix);
        }

        uint encodeLen = mp_nlri->EncodeLength();
        int attrlen = EncodeAndReadAttributeLength(mp_nlri);
        EXPECT_EQ(static_cast<size_t>(attrlen), encodeLen);
    }
}

TEST_F(EncodeLengthTest, ExtendedCommunity) {
    /* Boundary condition at 32 Extended communities */
    {
        ExtCommunitySpec *spec = new ExtCommunitySpec;
        for (int j = 32; j > 0; j--) {
            spec->communities.push_back(8000000 + j);
        }
        uint encodeLen = spec->EncodeLength();
        int attrlen = EncodeAndReadAttributeLength(spec);
        EXPECT_EQ(static_cast<size_t>(attrlen), encodeLen);
    }

    const int count = 32;
    for (int i = 0; i < count; ++i) {
        ExtCommunitySpec *spec = new ExtCommunitySpec;
        for (int j = rand() % 128; j > 0; j--) {
            spec->communities.push_back(j);
        }
        uint encodeLen = spec->EncodeLength();
        int attrlen = EncodeAndReadAttributeLength(spec);
        EXPECT_EQ(static_cast<size_t>(attrlen), encodeLen);
    }
}

TEST_F(EncodeLengthTest, EdgeDiscovery) {
    const int count = 32;
    for (int i = 0; i < count; ++i) {
        EdgeDiscoverySpec *spec = new EdgeDiscoverySpec;
        for (int j = rand() % 64; j > 0; j--) {
            EdgeDiscoverySpec::Edge *edge = new EdgeDiscoverySpec::Edge;
            boost::system::error_code err;
            Ip4Address addr = Ip4Address::from_string("192.168.0.1", err);
            edge->SetIp4Address(addr);
            edge->SetLabels(10000, 20000);
            spec->edge_list.push_back(edge);
        }
        uint encodeLen = spec->EncodeLength();
        int attrlen = EncodeAndReadAttributeLength(spec);
        EXPECT_EQ(static_cast<size_t>(attrlen), encodeLen);
    }
}

static void BuildEdge(EdgeForwardingSpec::Edge *edge) {
    boost::system::error_code err;
    edge->SetInboundIp4Address(Ip4Address::from_string("10.1.1.1", err));
    edge->inbound_label = 10000;
    edge->SetOutboundIp4Address(Ip4Address::from_string("10.1.1.2", err));
    edge->outbound_label = 10001;
}

TEST_F(EncodeLengthTest, EdgeForwarding) {
    const int count = 32;
    for (int i = 0; i < count; ++i) {
        EdgeForwardingSpec *spec = new EdgeForwardingSpec;
        for (int j = rand() % 64; j > 0; j--) {
            EdgeForwardingSpec::Edge *edge = new EdgeForwardingSpec::Edge;
            BuildEdge(edge);
            spec->edge_list.push_back(edge);
        }
        uint encodeLen = spec->EncodeLength();
        int attrlen = EncodeAndReadAttributeLength(spec);
        EXPECT_EQ(static_cast<size_t>(attrlen), encodeLen);
    }
}

}  // namespace

int main(int argc, char **argv) {
    bgp_log_test::init();
    ControlNode::SetDefaultSchedulingPolicy();
    ::testing::InitGoogleTest(&argc, argv);

    //
    // Enable this debug flag when required during debugging
    //
    // detail::debug_ = false;
    return RUN_ALL_TESTS();
}
