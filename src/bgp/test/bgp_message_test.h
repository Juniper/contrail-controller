/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef BGP_MESSAGE_TEST_H_
#define BGP_MESSAGE_TEST_H_

using namespace std;
class BgpMessageTest {
public:
    static void GenerateOpenMessage(BgpProto::OpenMessage *open) {
        open->as_num = 10458;
        open->holdtime = 30;
        open->identifier = 1;
        uint8_t cap_value[] = {0x0, 0x1, 0x0, 0x1};
        BgpProto::OpenMessage::Capability *cap =
                new BgpProto::OpenMessage::Capability(
                        BgpProto::OpenMessage::Capability::MpExtension,
                        cap_value, 4);
        BgpProto::OpenMessage::OptParam *opt_param =
                new BgpProto::OpenMessage::OptParam;

        opt_param->capabilities.push_back(cap);

        cap = new BgpProto::OpenMessage::Capability;
        cap->code = 0x80;
        opt_param->capabilities.push_back(cap);

        cap = new BgpProto::OpenMessage::Capability;
        cap->code = BgpProto::OpenMessage::Capability::RouteRefresh;
        opt_param->capabilities.push_back(cap);

        cap = new BgpProto::OpenMessage::Capability;
        cap->code = BgpProto::OpenMessage::Capability::AS4Support;
        cap->capability.push_back('a');
        cap->capability.push_back('b');
        cap->capability.push_back('c');
        cap->capability.push_back('d');
        opt_param->capabilities.push_back(cap);
        open->opt_params.push_back(opt_param);
    }

    static void GenerateUpdateMessage(BgpProto::Update *update, uint16_t afi, 
                                      uint8_t safi) {
        char p[] = {0x1, 0x2, 0x3};
        BgpProtoPrefix *prefix = new BgpProtoPrefix;
        prefix->prefixlen = 9;
        prefix->prefix = vector<uint8_t>(p, p + 2);
        update->withdrawn_routes.push_back(prefix);
        prefix = new BgpProtoPrefix;
        prefix->prefixlen = 20;
        prefix->prefix = vector<uint8_t>(p, p + 3);
            update->withdrawn_routes.push_back(prefix);

        BgpAttrOrigin *origin = new BgpAttrOrigin(BgpAttrOrigin::INCOMPLETE);
        update->path_attributes.push_back(origin);

        BgpAttrNextHop *nexthop = new BgpAttrNextHop(0xabcdef01);
        update->path_attributes.push_back(nexthop);

        BgpAttrAtomicAggregate *aa = new BgpAttrAtomicAggregate;
        update->path_attributes.push_back(aa);

        BgpAttrAggregator *agg = new BgpAttrAggregator(0xface, 0xcafebabe);
        update->path_attributes.push_back(agg);

        AsPathSpec *path_spec = new AsPathSpec;
        AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
        ps->path_segment_type = AsPathSpec::PathSegment::AS_SET;
        ps->path_segment.push_back(20);
        ps->path_segment.push_back(21);
        ps->path_segment.push_back(22);
        path_spec->path_segments.push_back(ps);
        update->path_attributes.push_back(path_spec);

        CommunitySpec *community = new CommunitySpec;
        community->communities.push_back(0x87654321);
        update->path_attributes.push_back(community);

        BgpMpNlri *mp_nlri = new BgpMpNlri(BgpAttribute::MPReachNlri);
        mp_nlri->afi = afi;
        mp_nlri->safi = safi;
        uint8_t nh[3] = {192,168,1};
        mp_nlri->nexthop.assign(&nh[0], &nh[3]);
        prefix = new BgpProtoPrefix;
        if (afi == BgpAf::L2Vpn && safi == BgpAf::EVpn) {
            prefix->type = 2;
            prefix->prefixlen = 24;
        } else {
            prefix->prefixlen = 20;
        }
        prefix->prefix = vector<uint8_t>(p, p + 3);
        mp_nlri->nlri.push_back(prefix);
        update->path_attributes.push_back(&mp_nlri[0]);

        mp_nlri = new BgpMpNlri(BgpAttribute::MPUnreachNlri);
        mp_nlri->afi = afi;
        mp_nlri->safi = safi;
        prefix = new BgpProtoPrefix;
        if (afi == BgpAf::L2Vpn && safi == BgpAf::EVpn) {
            prefix->type = 2;
            prefix->prefixlen = 16;
        } else {
            prefix->prefixlen = 9;
        }
        prefix->prefix = vector<uint8_t>(p, p + 2);
        mp_nlri->nlri.push_back(prefix);
        update->path_attributes.push_back(mp_nlri);

        ExtCommunitySpec *ext_community = new ExtCommunitySpec;
        ext_community->communities.push_back(0x1020304050607080);
        update->path_attributes.push_back(ext_community);

        BgpProtoPrefix *nlri = new BgpProtoPrefix;
        nlri->prefixlen = 4;
        nlri->prefix = vector<uint8_t>(p, p + 1);
        update->nlri.push_back(nlri);
        nlri = new BgpProtoPrefix;
        nlri->prefixlen = 10;
        nlri->prefix = vector<uint8_t>(p, p + 2);
        update->nlri.push_back(nlri);
    }

    static void GenerateWithdrawMessage(BgpProto::Update *withdraw) {
        char p[] = {0x1, 0x2, 0x3};

        BgpProtoPrefix *prefix = new BgpProtoPrefix;
        prefix->prefixlen = 4;
        prefix->prefix = vector<uint8_t>(p, p + 1);
        withdraw->withdrawn_routes.push_back(prefix);
        prefix = new BgpProtoPrefix;
        prefix->prefixlen = 10;
        prefix->prefix = vector<uint8_t>(p, p + 2);
        withdraw->withdrawn_routes.push_back(prefix);

        BgpMpNlri *mp_nlri = new BgpMpNlri(BgpAttribute::MPUnreachNlri);
        mp_nlri->afi = 1;
        mp_nlri->safi = 128;
        prefix = new BgpProtoPrefix;
        prefix->prefixlen = 20;
        prefix->prefix = vector<uint8_t>(p, p + 3);
        mp_nlri->nlri.push_back(prefix);
        withdraw->path_attributes.push_back(mp_nlri);
    }

    static void GenerateEmptyUpdateMessage(BgpProto::Update *update) {
        BgpMpNlri *mp_nlri = new BgpMpNlri(BgpAttribute::MPUnreachNlri);
        mp_nlri->afi = 1;
        mp_nlri->safi = 128;
        update->path_attributes.push_back(mp_nlri);
    }
};


#endif /* BGP_MESSAGE_TEST_H_ */
