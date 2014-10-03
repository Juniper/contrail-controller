/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "base/proto.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "testing/gunit.h"
#include <boost/assign/list_of.hpp>
#include "net/bgp_af.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_proto.h"
#include "bgp_message_test.h"

using namespace std;

namespace {

class BgpProtoTest : public testing::Test {
protected:
    bool ParseAndVerifyError(const uint8_t *data, size_t size, int error,
            int subcode, std::string type, int offset, int err_size) {
        ParseErrorContext ec;
        const BgpProto::BgpMessage *result =
                BgpProto::Decode(&data[0], size, &ec);
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

        BgpProto::BgpMessage *msg = BgpProto::Decode(new_data, data_size);
        if (msg) delete msg;
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
        for (int len = rand() % 10; len > 0; len--) {
            community->communities.push_back(rand());
        }
        msg->path_attributes.push_back(community);
    }

    static void AddMpNlri(BgpProto::Update *msg) {
        static const int kMaxRoutes = 500;
        BgpMpNlri *mp_nlri = new BgpMpNlri;
        mp_nlri->flags = BgpAttribute::Optional|BgpAttribute::ExtendedLength;
        mp_nlri->code = BgpAttribute::MPReachNlri;
        mp_nlri->afi = 1;
        mp_nlri->safi = 128;
        for (int nh_len = rand() % 12; nh_len > 0; nh_len--) {
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
        for (int i = rand() % 10; i > 0; i--) {
            ext_community->communities.push_back(rand());
        }
        msg->path_attributes.push_back(ext_community);
    }

    static void AddPmsiTunnel(BgpProto::Update *msg) {
        PmsiTunnelSpec *pmsispec = new PmsiTunnelSpec;
        pmsispec->tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
        pmsispec->tunnel_type = PmsiTunnelSpec::IngressReplication;
        pmsispec->SetLabel(10000);
        boost::system::error_code ec;
        pmsispec->SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));
        msg->path_attributes.push_back(pmsispec);
    }

    static void AddEdgeDiscovery(BgpProto::Update *msg) {
        EdgeDiscoverySpec *edspec = new EdgeDiscoverySpec;
        for (int i = rand() % 4; i > 0; i--) {
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
        for (int i = rand() % 4; i > 0; i--) {
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

    static void AddUnknown(BgpProto::Update *msg) {
        BgpAttrUnknown *unk = new BgpAttrUnknown;
        unk->flags = BgpAttribute::Optional;
        unk->code = (rand() % 236) + 20;
        for (int len = rand() % 8; len > 0; len--) {
            unk->value.push_back(rand());
        }
        msg->path_attributes.push_back(unk);
    }
};

std::vector<BuildUpdateMessage::BuildUpdateParam> BuildUpdateMessage::build_params_ =
        boost::assign::list_of
            (std::make_pair(&BuildUpdateMessage::AddOrigin, 1))
            (std::make_pair(&BuildUpdateMessage::AddNextHop, 1))
            (std::make_pair(&BuildUpdateMessage::AddMultiExitDisc, 5))
            (std::make_pair(&BuildUpdateMessage::AddLocalPref, 5))
            (std::make_pair(&BuildUpdateMessage::AddAtomicAggregate, 5))
            (std::make_pair(&BuildUpdateMessage::AddAggregator, 5))
            (std::make_pair(&BuildUpdateMessage::AddAsPath, 1))
            (std::make_pair(&BuildUpdateMessage::AddCommunity, 5))
            (std::make_pair(&BuildUpdateMessage::AddMpNlri, 5))
            (std::make_pair(&BuildUpdateMessage::AddExtCommunity, 5))
            (std::make_pair(&BuildUpdateMessage::AddPmsiTunnel, 5))
            (std::make_pair(&BuildUpdateMessage::AddEdgeDiscovery, 5))
            (std::make_pair(&BuildUpdateMessage::AddEdgeForwarding, 5))
            (std::make_pair(&BuildUpdateMessage::AddUnknown, 5));


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
    EXPECT_EQ(4, result->opt_params.size());
    for (size_t i = 0; i < result->opt_params.size(); i++) {
        EXPECT_EQ(1, result->opt_params[i]->capabilities.size());
    }
    const BgpProto::OpenMessage::Capability *cap = result->opt_params[0]->capabilities[0];
    EXPECT_EQ(1, cap->code);
    const unsigned char temp[] = {0, 1, 0, 1};
    EXPECT_EQ(cap->capability, vector<uint8_t>(temp, temp + 4));

    cap = result->opt_params[1]->capabilities[0];
    EXPECT_EQ(0x80, cap->code);
    EXPECT_EQ(0, cap->capability.size());

    cap = result->opt_params[2]->capabilities[0];
    EXPECT_EQ(0x02, cap->code);
    EXPECT_EQ(0, cap->capability.size());

    cap = result->opt_params[3]->capabilities[0];
    EXPECT_EQ(0x41, cap->code);
    const unsigned char temp2[] = {0, 0, 0x1d, 0xfb};
    EXPECT_EQ(cap->capability, vector<uint8_t>(temp2, temp2 + 4));
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

    int res = BgpProto::Encode(&update, data, 256);
    EXPECT_NE(-1, res);
    if (detail::debug_) {
        for (int i = 0; i < res; i++) {
            printf("%02x ", data[i]);
        }
        printf("\n");
    }

    const BgpProto::Update *result;
    result = static_cast<const BgpProto::Update *>(BgpProto::Decode(data, res));
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

    int res = BgpProto::Encode(&update, data, 256);
    EXPECT_NE(-1, res);
    if (detail::debug_) {
        for (int i = 0; i < res; i++) {
            printf("%02x ", data[i]);
        }
        printf("\n");
    }

    const BgpProto::Update *result;
    result = static_cast<const BgpProto::Update *>(BgpProto::Decode(data, res));
    EXPECT_TRUE(result != NULL);
    if (result) {
        EXPECT_EQ(0, result->CompareTo(update));
        delete result;
    }
}



TEST_F(BgpProtoTest, EvpnUpdate) {
    BgpProto::Update update;
    BgpMessageTest::GenerateUpdateMessage(&update, BgpAf::L2Vpn, BgpAf::EVpn);
    uint8_t data[256];

    int res = BgpProto::Encode(&update, data, 256);
    EXPECT_NE(-1, res);
    if (detail::debug_) {
        for (int i = 0; i < res; i++) {
            printf("%02x ", data[i]);
        }
        printf("\n");
    }

    const BgpProto::Update *result;
    result = static_cast<const BgpProto::Update *>(BgpProto::Decode(data, res));
    EXPECT_TRUE(result != NULL);
    if (result) {
        EXPECT_EQ(0, result->CompareTo(update));
        delete result;
    }
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
                       0x00, 0x72, 0x02, 0x00, 0x07, 0x09, 0x01, 0x02,
                       0x14, 0x01, 0x02, 0x03, 0x00, 0x4f, 0x10, 0x01,
                       0x00, 0x01, 0x02, 0x40, 0x03, 0x04, 0xab, 0xcd,
                       0xef, 0x01, 0x40, 0x06, 0x00, 0xc0, 0x07, 0x06,
                       0xfa, 0xce, 0xca, 0xfe, 0xba, 0xbe, 0x40, 0x02,
                       0x08, 0x01, 0x03, 0x00, 0x14, 0x00, 0x15, 0x00,
                       0x16, 0xc0, 0x08, 0x04, 0x87, 0x65, 0x43, 0x21,
                       0x90, 0x0e, 0x00, 0x0c, 0x00, 0x01, 0x80, 0x03,
                       0x61, 0x62, 0x64, 0x00, 0x14, 0x01, 0x02, 0x03,
                       0x90, 0x0f, 0x00, 0x06, 0x00, 0x01, 0x80, 0x09,
                       0x01, 0x02, 0xc0, 0x10, 0x08, 0x10, 0x20, 0x30,
                       0x40, 0x50, 0x60, 0x70, 0x80, 0x04, 0x01, 0x0a,
                       0x01, 0x02 };
    // withdrawn routes error
    data[19] = 0xff;
    ParseAndVerifyError(data, sizeof(data), BgpProto::Notification::UpdateMsgErr,
            BgpProto::Notification::MalformedAttributeList,
            "BgpUpdateWithdrawnRoutes", 19, 2);

    // attributes length error
    data[19] = 0;
    data[28] = 0xff;
    ParseAndVerifyError(data, sizeof(data), BgpProto::Notification::UpdateMsgErr,
            BgpProto::Notification::MalformedAttributeList,
            "BgpPathAttributeList", 28, 2);

    // Origin attribute length error
    data[28] = 0;
    data[33] = 5;
    ParseAndVerifyError(data, sizeof(data), BgpProto::Notification::UpdateMsgErr,
            BgpProto::Notification::AttribLengthError,
            "BgpAttrOrigin", 30, 9);

    // Unknown well-known attribute
    data[33] = 1;
    data[31] = 20;
    ParseAndVerifyError(data, sizeof(data), BgpProto::Notification::UpdateMsgErr,
            BgpProto::Notification::UnrecognizedWellKnownAttrib,
            "BgpAttrUnknown", 30, 5);

    // unknown optional attribute
    data[30] |= BgpAttribute::Optional;
    const BgpProto::Update *result =
            static_cast<BgpProto::Update *>(BgpProto::Decode(&data[0], sizeof(data)));
    EXPECT_TRUE(result != NULL);
    BgpAttrUnknown *attr = static_cast<BgpAttrUnknown *>(result->path_attributes[0]);
    EXPECT_EQ(20, attr->code);
    EXPECT_EQ(1, attr->value.size());
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

TEST_F(BgpProtoTest, UpdateScale) {
    BgpProto::Update update;
    static const int kMaxRoutes = 500;

    BgpAttrOrigin *origin = new BgpAttrOrigin(BgpAttrOrigin::INCOMPLETE);
    update.path_attributes.push_back(origin);

    BgpAttrNextHop *nexthop = new BgpAttrNextHop(0xabcdef01);
    update.path_attributes.push_back(nexthop);

    BgpAttrAtomicAggregate *aa = new BgpAttrAtomicAggregate;
    update.path_attributes.push_back(aa);

    BgpAttrAggregator *agg = new BgpAttrAggregator(0xface, 0xcafebabe);
    update.path_attributes.push_back(agg);

    AsPathSpec *path_spec = new AsPathSpec;
    AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
    ps->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    ps->path_segment.push_back(20);
    ps->path_segment.push_back(21);
    ps->path_segment.push_back(22);
    path_spec->path_segments.push_back(ps);
    update.path_attributes.push_back(path_spec);

    CommunitySpec *community = new CommunitySpec;
    community->communities.push_back(0x87654321);
    update.path_attributes.push_back(community);

    BgpMpNlri *mp_nlri = new BgpMpNlri(BgpAttribute::MPReachNlri);
    mp_nlri->flags = BgpAttribute::Optional|BgpAttribute::ExtendedLength;
    mp_nlri->afi = 1;
    mp_nlri->safi = 128;
    uint8_t nh[4] = {192,168,1,1};
    mp_nlri->nexthop.assign(&nh[0], &nh[4]);
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

    int res = BgpProto::Encode(&update, data, 4096);
    EXPECT_NE(-1, res);
    if (detail::debug_) {
        cout << res << " Bytes encoded" << endl;
        for (int i = 0; i < res; i++) {
            printf("%02x ", data[i]);
        }
        printf("\n");
    }

    const BgpProto::Update *result;
    result = static_cast<const BgpProto::Update *>(BgpProto::Decode(data, res));
    EXPECT_TRUE(result != NULL);
    if (result) {
        EXPECT_EQ(0, result->CompareTo(update));
        delete result;
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
        int res = BgpProto::Encode(&update, data, sizeof(data));
        EXPECT_NE(-1, res);
        if (detail::debug_) {
            cout << res << " Bytes encoded" << endl;
            for (int i = 0; i < res; i++) {
                printf("%02x ", data[i]);
            }
            printf("\n");
        }
        GenerateByteError(data, res);
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
