/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_message_builder.h"

#include "base/logging.h"
#include "base/parse_object.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_route.h"
#include "net/bgp_af.h"

BgpMessage::BgpMessage() {
}

BgpMessage::~BgpMessage() {
}

void BgpMessage::StartReach(const RibOutAttr *roattr, const BgpRoute *route) {
    BgpProto::Update update;
    const BgpAttr *attr = roattr->attr();

    BgpAttrOrigin *origin = new BgpAttrOrigin(attr->origin());
    update.path_attributes.push_back(origin);

    if ((route->Afi() == BgpAf::IPv4) && (route->Safi() == BgpAf::Unicast)) {
        BgpAttrNextHop *nh = new BgpAttrNextHop(attr->nexthop().to_v4().to_ulong());
        update.path_attributes.push_back(nh);
    }

    if (attr->med()) {
        BgpAttrMultiExitDisc *med = new BgpAttrMultiExitDisc(attr->med());
        update.path_attributes.push_back(med);
    }

    if (attr->local_pref()) {
        BgpAttrLocalPref *lp = new BgpAttrLocalPref(attr->local_pref());
        update.path_attributes.push_back(lp);
    }

    if (attr->atomic_aggregate()) {
        BgpAttrAtomicAggregate *aa = new BgpAttrAtomicAggregate;
        update.path_attributes.push_back(aa);
    }

    if (attr->aggregator_as_num()) {
        BgpAttrAggregator *agg = new BgpAttrAggregator(
                attr->aggregator_as_num(),
                attr->aggregator_adderess().to_v4().to_ulong());
        update.path_attributes.push_back(agg);
    }

    if (attr->as_path()) {
        AsPathSpec *path = new AsPathSpec(attr->as_path()->path());
        update.path_attributes.push_back(path);
    }

    if (attr->community() && attr->community()->communities().size()) {
        CommunitySpec *comm = new CommunitySpec;
        comm->communities = attr->community()->communities();
        update.path_attributes.push_back(comm);
    }

    if (attr->ext_community() && attr->ext_community()->communities().size()) {
        ExtCommunitySpec *ext_comm = new ExtCommunitySpec;
        const ExtCommunity::ExtCommunityList &v =
                attr->ext_community()->communities();
        for (ExtCommunity::ExtCommunityList::const_iterator it = v.begin();
                it != v.end(); ++it) {
            uint64_t value = get_value(it->data(), it->size());
            ext_comm->communities.push_back(value);
        }
        update.path_attributes.push_back(ext_comm);
    }

    std::vector<uint8_t> nh;

    route->BuildBgpProtoNextHop(nh, attr->nexthop());

    BgpMpNlri *nlri =
        new BgpMpNlri(BgpAttribute::MPReachNlri, route->Afi(), route->Safi(), nh);
    update.path_attributes.push_back(nlri);

    if (route) {
        BgpProtoPrefix *prefix = new BgpProtoPrefix;
        route->BuildProtoPrefix(prefix, roattr->label());
        nlri->nlri.push_back(prefix);
        num_reach_route_++;
    }

    datalen_ = BgpProto::Encode(&update, data_, sizeof(data_),
            &encode_offsets_);
    if (datalen_ <= 0) {
        BGP_LOG(BgpMessageBuilder, SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                "MP Reach Encoding failed", datalen_, route->ToString());
        assert(datalen_ > 0);
    }
}

void BgpMessage::StartUnreach(const BgpRoute *route) {
    BgpProto::Update update;

    BgpMpNlri *nlri =
        new BgpMpNlri(BgpAttribute::MPUnreachNlri, route->Afi(), route->Safi());
    update.path_attributes.push_back(nlri);

    if (route) {
        BgpProtoPrefix *prefix = new BgpProtoPrefix;
        route->BuildProtoPrefix(prefix, 0);
        nlri->nlri.push_back(prefix);
        num_unreach_route_++;
    }

    datalen_ = BgpProto::Encode(&update, data_, sizeof(data_),
            &encode_offsets_);
    if (datalen_ <= 0) {
        BGP_LOG(BgpMessageBuilder, SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                "MP Unreach Encoding failed", datalen_, route->ToString());
        assert(datalen_ > 0);
    }
    BGP_LOG(BgpMessageBuilder, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
            "Encoded Withdraw NLRI", datalen_, route->ToString());
}

void BgpMessage::Start(const RibOutAttr *roattr, const BgpRoute *route) {
    if (roattr->IsReachable()) {
        StartReach(roattr, route);
    } else {
        StartUnreach(route);
    }
}

bool BgpMessage::UpdateLength(const char *tag, int size, int delta) {
    int offset = encode_offsets_.FindOffset(tag);
    if (offset < 0) {
        return false;
    }
    int value = get_value(&data_[offset], size);
    value += delta;
    put_value(&data_[offset], size, value);
    return true;
}

bool BgpMessage::AddRoute(const BgpRoute *route, const RibOutAttr *roattr) {
    uint8_t *data = data_ + datalen_;
    size_t size = sizeof(data_) - datalen_;

    BgpMpNlri nlri;
    nlri.afi = route->Afi();
    nlri.safi = route->Safi();
    BgpProtoPrefix *prefix = new BgpProtoPrefix;
    uint32_t label = roattr ? roattr->label() : 0;
    route->BuildProtoPrefix(prefix, label);
    nlri.nlri.push_back(prefix);
    if (roattr->IsReachable()) {
        num_reach_route_++;
    } else {
        num_unreach_route_++;
    }

    int result = BgpProto::Encode(&nlri, data, size);
    if (result <= 0) return false;

    datalen_ += result;
    if (!UpdateLength("BgpMsgLength", 2, result)) {
        BGP_LOG(BgpMessageBuilder, SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                "Cannot find BGP message length", datalen_, route->ToString());
        assert(false);
        return false;
    }

    if (!UpdateLength("BgpPathAttribute", 2, result)) {
        BGP_LOG(BgpMessageBuilder, SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                "Cannot find BGP attributes length", datalen_,
                route->ToString());
        assert(false);
        return false;
    }

    if (!UpdateLength("MpReachUnreachNlri", 2, result)) {
        BGP_LOG(BgpMessageBuilder, SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                "Cannot find MP Reach/Unreach NLRI length", datalen_,
                route->ToString());
        assert(false);
        return false;
    }

    BGP_LOG(BgpMessageBuilder, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
            "Encoded Update NLRI", datalen_, route->ToString());
    return true;
}

void BgpMessage::Finish() {
}

const uint8_t *BgpMessage::GetData(IPeerUpdate *ipeer_update, size_t *lenp) {
    *lenp = datalen_;
    return data_;
}

Message *BgpMessageBuilder::Create(const BgpTable *table,
        const RibOutAttr *roattr, const BgpRoute *route) const {
    BgpMessage *msg = new BgpMessage();
    msg->Start(roattr, route);
    return msg;
}

BgpMessageBuilder BgpMessageBuilder::instance_;

BgpMessageBuilder::BgpMessageBuilder() {
}

BgpMessageBuilder *BgpMessageBuilder::GetInstance() {
    return &instance_;
}
