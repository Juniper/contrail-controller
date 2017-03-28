/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_message_builder.h"

#include <vector>

#include "bgp/bgp_log.h"
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_server.h"
#include "net/bgp_af.h"

using std::auto_ptr;

BgpMessage::BgpMessage() : table_(NULL), datalen_(0) {
}

BgpMessage::~BgpMessage() {
}

bool BgpMessage::StartReach(const RibOut *ribout, const RibOutAttr *roattr,
                            const BgpRoute *route) {
    BgpProto::Update update;
    const BgpAttr *attr = roattr->attr();

    BgpAttrOrigin *origin = new BgpAttrOrigin(attr->origin());
    update.path_attributes.push_back(origin);

    if ((route->Afi() == BgpAf::IPv4) && (route->Safi() == BgpAf::Unicast)) {
        BgpAttrNextHop *nh =
            new BgpAttrNextHop(attr->nexthop().to_v4().to_ulong());
        update.path_attributes.push_back(nh);
    }

    if (attr->med()) {
        BgpAttrMultiExitDisc *med = new BgpAttrMultiExitDisc(attr->med());
        update.path_attributes.push_back(med);
    }

    if (ribout->peer_type() == BgpProto::IBGP) {
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

    if (!attr->originator_id().is_unspecified()) {
        BgpAttrOriginatorId *originator_id =
            new BgpAttrOriginatorId(attr->originator_id().to_ulong());
        update.path_attributes.push_back(originator_id);
    }

    if (attr->cluster_list()) {
        ClusterListSpec *clist =
            new ClusterListSpec(attr->cluster_list()->cluster_list());
        update.path_attributes.push_back(clist);
    }

    if (attr->as_path()) {
        AsPathSpec *path = new AsPathSpec(attr->as_path()->path());
        update.path_attributes.push_back(path);
    }

    if (attr->edge_discovery()) {
        EdgeDiscoverySpec *edspec =
            new EdgeDiscoverySpec(attr->edge_discovery()->edge_discovery());
        update.path_attributes.push_back(edspec);
    }

    if (attr->edge_forwarding()) {
        EdgeForwardingSpec *efspec =
            new EdgeForwardingSpec(attr->edge_forwarding()->edge_forwarding());
        update.path_attributes.push_back(efspec);
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

    if (attr->origin_vn_path() && attr->origin_vn_path()->origin_vns().size()) {
        OriginVnPathSpec *ovnpath_spec = new OriginVnPathSpec;
        const OriginVnPath::OriginVnList &v =
            attr->origin_vn_path()->origin_vns();
        for (OriginVnPath::OriginVnList::const_iterator it = v.begin();
             it != v.end(); ++it) {
            uint64_t value = get_value(it->data(), it->size());
            ovnpath_spec->origin_vns.push_back(value);
        }
        update.path_attributes.push_back(ovnpath_spec);
    }

    if (attr->pmsi_tunnel()) {
        PmsiTunnelSpec *pmsi_spec =
            new PmsiTunnelSpec(attr->pmsi_tunnel()->pmsi_tunnel());
        update.path_attributes.push_back(pmsi_spec);
    }

    std::vector<uint8_t> nh;

    route->BuildBgpProtoNextHop(nh, attr->nexthop());

    BgpMpNlri *nlri = new BgpMpNlri(
        BgpAttribute::MPReachNlri, route->Afi(), route->Safi(), nh);
    update.path_attributes.push_back(nlri);

    BgpProtoPrefix *prefix = new BgpProtoPrefix;
    route->BuildProtoPrefix(prefix, attr, roattr->label(), roattr->l3_label());
    nlri->nlri.push_back(prefix);

    int result =
        BgpProto::Encode(&update, data_, sizeof(data_), &encode_offsets_);
    if (result <= 0) {
        BGP_LOG_WARNING_STR(BgpMessageSend, BGP_LOG_FLAG_ALL,
            "Error encoding reach message for route " << route->ToString() <<
            " in table " << (table_ ? table_->name() : "unknown"));
        table_->server()->increment_message_build_error();
        return false;
    }

    num_reach_route_++;
    datalen_ = result;
    return true;
}

bool BgpMessage::StartUnreach(const BgpRoute *route) {
    BgpProto::Update update;

    BgpMpNlri *nlri =
        new BgpMpNlri(BgpAttribute::MPUnreachNlri, route->Afi(), route->Safi());
    update.path_attributes.push_back(nlri);

    BgpProtoPrefix *prefix = new BgpProtoPrefix;
    route->BuildProtoPrefix(prefix);
    nlri->nlri.push_back(prefix);

    int result =
        BgpProto::Encode(&update, data_, sizeof(data_), &encode_offsets_);
    if (result <= 0) {
        BGP_LOG_WARNING_STR(BgpMessageSend, BGP_LOG_FLAG_ALL,
            "Error encoding unreach message for route " << route->ToString() <<
            " in table " << (table_ ? table_->name() : "unknown"));
        table_->server()->increment_message_build_error();
        return false;
    }

    num_unreach_route_++;
    datalen_ = result;
    return true;
}

void BgpMessage::Reset() {
    Message::Reset();
    table_ = NULL;
    encode_offsets_.ClearOffsets();
    datalen_ = 0;
}

bool BgpMessage::Start(const RibOut *ribout, bool cache_repr,
    const RibOutAttr *roattr, const BgpRoute *route) {
    Reset();
    table_ = ribout->table();

    if (roattr->IsReachable()) {
        return StartReach(ribout, roattr, route);
    } else {
        return StartUnreach(route);
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
    if (roattr) {
        route->BuildProtoPrefix(prefix, roattr->attr(), roattr->label());
    } else {
        route->BuildProtoPrefix(prefix);
    }
    nlri.nlri.push_back(prefix);

    int result = BgpProto::Encode(&nlri, data, size);
    if (result <= 0) {
        return false;
    }

    datalen_ += result;
    if (roattr->IsReachable()) {
        num_reach_route_++;
    } else {
        num_unreach_route_++;
    }

    if (!UpdateLength("BgpMsgLength", 2, result)) {
        assert(false);
        return false;
    }

    if (!UpdateLength("BgpPathAttribute", 2, result)) {
        assert(false);
        return false;
    }

    if (!UpdateLength("MpReachUnreachNlri", 2, result)) {
        assert(false);
        return false;
    }

    return true;
}

void BgpMessage::Finish() {
}

const uint8_t *BgpMessage::GetData(IPeerUpdate *peer, size_t *lenp,
    const string **msg_str) {
    *lenp = datalen_;
    return data_;
}

Message *BgpMessageBuilder::Create() const {
    return new BgpMessage;
}

BgpMessageBuilder::BgpMessageBuilder() {
}
