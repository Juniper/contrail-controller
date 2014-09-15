/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_attr.h"
#include "base/util.h"
#include "bgp/bgp_proto.h"

int BgpAttrOrigin::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    KEY_COMPARE(origin, static_cast<const BgpAttrOrigin &>(rhs_attr).origin);
    return 0;
}

void BgpAttrOrigin::ToCanonical(BgpAttr *attr) {
    attr->set_origin(static_cast<BgpAttrOrigin::OriginType>(origin));
}

std::string BgpAttrOrigin::ToString() const {
    char repr[80];
    snprintf(repr, sizeof(repr), "ORIGIN <code: %d, flags: %02x> : %02x", 
             code, flags, origin);
    return std::string(repr);
}

int BgpAttrNextHop::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    KEY_COMPARE(nexthop,
            static_cast<const BgpAttrNextHop &>(rhs_attr).nexthop);
    return 0;
}

void BgpAttrNextHop::ToCanonical(BgpAttr *attr) {
    attr->set_nexthop(Ip4Address(nexthop));
}

std::string BgpAttrNextHop::ToString() const {
    char repr[80];
    snprintf(repr, sizeof(repr), "NEXTHOP <code: %d, flags: %02x> : %04x", 
             code, flags, nexthop);
    return std::string(repr);
}

int BgpAttrMultiExitDisc::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    KEY_COMPARE(med, static_cast<const BgpAttrMultiExitDisc &>(rhs_attr).med);
    return 0;
}
void BgpAttrMultiExitDisc::ToCanonical(BgpAttr *attr) {
    attr->set_med(med);
}

std::string BgpAttrMultiExitDisc::ToString() const {
    char repr[80];
    snprintf(repr, sizeof(repr), "MED <code: %d, flags: %02x> : %04x", 
             code, flags, med);
    return std::string(repr);
}

int BgpAttrLocalPref::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    KEY_COMPARE(local_pref,
            static_cast<const BgpAttrLocalPref &>(rhs_attr).local_pref);
    return 0;
}
void BgpAttrLocalPref::ToCanonical(BgpAttr *attr) {
    attr->set_local_pref(local_pref);
}

std::string BgpAttrLocalPref::ToString() const {
    char repr[80];
    snprintf(repr, sizeof(repr), "LOCAL_PREF <code: %d, flags: %02x> : %d", 
             code, flags, local_pref);
    return std::string(repr);
}

void BgpAttrAtomicAggregate::ToCanonical(BgpAttr *attr) {
    attr->set_atomic_aggregate(true);
}

std::string BgpAttrAtomicAggregate::ToString() const {
    char repr[80];
    snprintf(repr, sizeof(repr), "ATOMIC_AGGR <code: %d, flags: %02x>", 
             code, flags);
    return std::string(repr);
}

int BgpAttrAggregator::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    KEY_COMPARE(as_num,
            static_cast<const BgpAttrAggregator &>(rhs_attr).as_num);
    KEY_COMPARE(address,
            static_cast<const BgpAttrAggregator &>(rhs_attr).address);
    return 0;
}
void BgpAttrAggregator::ToCanonical(BgpAttr *attr) {
    attr->set_aggregator(as_num, Ip4Address(address));
}

std::string BgpAttrAggregator::ToString() const {
    char repr[80];
    snprintf(repr, sizeof(repr), 
             "Aggregator <code: %d, flags: %02x> : %02x:%04x", 
             code, flags, as_num, address);
    return std::string(repr);
}

int BgpAttrOriginatorId::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    KEY_COMPARE(originator_id,
        static_cast<const BgpAttrOriginatorId &>(rhs_attr).originator_id);
    return 0;
}

void BgpAttrOriginatorId::ToCanonical(BgpAttr *attr) {
    attr->set_originator_id(Ip4Address(originator_id));
}

std::string BgpAttrOriginatorId::ToString() const {
    char repr[80];
    snprintf(repr, sizeof(repr), "OriginatorId <code: %d, flags: 0x%02x> : %s",
             code, flags, Ip4Address(originator_id).to_string().c_str());
    return std::string(repr);
}

int BgpMpNlri::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    const BgpMpNlri &rhs = static_cast<const BgpMpNlri &>(rhs_attr);
    KEY_COMPARE(afi, rhs.afi);
    KEY_COMPARE(safi, rhs.safi);
    KEY_COMPARE(nexthop, rhs.nexthop);
    KEY_COMPARE(nlri.size(), rhs.nlri.size());

    for (size_t i = 0; i < nlri.size(); i++) {
        KEY_COMPARE(nlri[i]->type, rhs.nlri[i]->type);
        KEY_COMPARE(nlri[i]->prefixlen, rhs.nlri[i]->prefixlen);
        KEY_COMPARE(nlri[i]->prefix, rhs.nlri[i]->prefix);
    }
    return 0;
}

void BgpMpNlri::ToCanonical(BgpAttr *attr) {
}

PmsiTunnelSpec::PmsiTunnelSpec()
    : BgpAttribute(PmsiTunnel, kFlags),
      tunnel_flags(0), tunnel_type(0), label(0) {
}

PmsiTunnelSpec::PmsiTunnelSpec(const BgpAttribute &rhs)
    : BgpAttribute(rhs) {
}

int PmsiTunnelSpec::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    const PmsiTunnelSpec &rhs =
        static_cast<const PmsiTunnelSpec &>(rhs_attr);
    KEY_COMPARE(this, &rhs);
    return 0;
}

void PmsiTunnelSpec::ToCanonical(BgpAttr *attr) {
    attr->set_pmsi_tunnel(this);
}

std::string PmsiTunnelSpec::ToString() const {
    std::ostringstream oss;
    oss << "PmsiTunnel <code: " << int(code);
    oss << ", flags: 0x" << std::hex << int(flags) << std::dec << ">";
    oss << " Tunnel Flags: 0x" << std::hex << int(tunnel_flags) << std::dec;
    oss << " Tunnel Type: " << int(tunnel_type);
    oss << " Label: " << GetLabel();
    oss << " Identifier: " << GetIdentifier().to_string();

    return oss.str();
}

uint32_t PmsiTunnelSpec::GetLabel() const {
    return label >> 4;
}

void PmsiTunnelSpec::SetLabel(uint32_t in_label) {
    label = in_label << 4 | 0x01;
}

Ip4Address PmsiTunnelSpec::GetIdentifier() const {
    if (identifier.size() < 4)
        return Ip4Address();
    return Ip4Address(get_value(&identifier[0], 4));
}

void PmsiTunnelSpec::SetIdentifier(Ip4Address in_identifier) {
    identifier.resize(4, 0);
    const Ip4Address::bytes_type &bytes = in_identifier.to_bytes();
    std::copy(bytes.begin(), bytes.begin() + 4, identifier.begin());
}

PmsiTunnel::PmsiTunnel(const PmsiTunnelSpec &pmsi_spec)
    : pmsi_spec_(pmsi_spec) {
    refcount_ = 0;
    tunnel_flags = pmsi_spec_.tunnel_flags;
    tunnel_type = pmsi_spec_.tunnel_type;
    label = pmsi_spec_.GetLabel();
    identifier = pmsi_spec_.GetIdentifier();
}

EdgeDiscoverySpec::EdgeDiscoverySpec()
    : BgpAttribute(McastEdgeDiscovery, kFlags) {
}

EdgeDiscoverySpec::EdgeDiscoverySpec(const BgpAttribute &rhs)
    : BgpAttribute(rhs) {
}

EdgeDiscoverySpec::EdgeDiscoverySpec(const EdgeDiscoverySpec &rhs)
    : BgpAttribute(BgpAttribute::McastEdgeDiscovery, kFlags) {
    for (size_t i = 0; i < rhs.edge_list.size(); i++) {
        Edge *edge = new Edge;
        *edge = *rhs.edge_list[i];
        edge_list.push_back(edge);
    }
}

EdgeDiscoverySpec::~EdgeDiscoverySpec() {
    STLDeleteValues(&edge_list);
}

Ip4Address EdgeDiscoverySpec::Edge::GetIp4Address() const {
    return Ip4Address(get_value(&address[0], 4));
}

void EdgeDiscoverySpec::Edge::SetIp4Address(Ip4Address addr) {
    address.resize(4, 0);
    const Ip4Address::bytes_type &addr_bytes = addr.to_bytes();
    std::copy(addr_bytes.begin(), addr_bytes.begin() + 4, address.begin());
}

void EdgeDiscoverySpec::Edge::GetLabels(
    uint32_t *first_label, uint32_t *last_label) const {
    *first_label = labels[0];
    *last_label = labels[1];
}

void EdgeDiscoverySpec::Edge::SetLabels(
    uint32_t first_label, uint32_t last_label) {
    labels.push_back(first_label);
    labels.push_back(last_label);
}

int EdgeDiscoverySpec::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    const EdgeDiscoverySpec &rhs =
        static_cast<const EdgeDiscoverySpec &>(rhs_attr);
    KEY_COMPARE(this, &rhs);
    return 0;
}

void EdgeDiscoverySpec::ToCanonical(BgpAttr *attr) {
    attr->set_edge_discovery(this);
}

std::string EdgeDiscoverySpec::ToString() const {
    std::ostringstream oss;
    oss << "EdgeDiscovery <code: " << int(code);
    oss << ", flags: 0x" << std::hex << int(flags) << std::dec << ">";
    int idx = 0;
    for (EdgeList::const_iterator it = edge_list.begin();
         it != edge_list.end(); ++it, ++idx) {
        const Edge *edge = *it;
        uint32_t first_label, last_label;
        edge->GetLabels(&first_label, &last_label);
        oss << " Edge[" << idx << "] = (" << edge->GetIp4Address() << ", ";
        oss << first_label << "-" << last_label << ")";
    }

    return oss.str();
}

EdgeDiscovery::Edge::Edge(const EdgeDiscoverySpec::Edge *spec_edge) {
    address = spec_edge->GetIp4Address();
    uint32_t first_label, last_label;
    spec_edge->GetLabels(&first_label, &last_label);
    label_block = new LabelBlock(first_label, last_label);
}

EdgeDiscovery::EdgeDiscovery(const EdgeDiscoverySpec &edspec)
    : edspec_(edspec) {
    refcount_ = 0;
    for (EdgeDiscoverySpec::EdgeList::const_iterator it =
         edspec_.edge_list.begin(); it != edspec_.edge_list.end(); ++it) {
        Edge *edge = new Edge(*it);
        edge_list.push_back(edge);
    }
}

EdgeDiscovery::~EdgeDiscovery() {
    STLDeleteValues(&edge_list);
}

EdgeForwardingSpec::EdgeForwardingSpec()
    : BgpAttribute(McastEdgeForwarding, kFlags) {
}

EdgeForwardingSpec::EdgeForwardingSpec(const BgpAttribute &rhs)
    : BgpAttribute(rhs) {
}

EdgeForwardingSpec::EdgeForwardingSpec(const EdgeForwardingSpec &rhs)
    : BgpAttribute(BgpAttribute::McastEdgeForwarding, kFlags) {
    for (size_t i = 0; i < rhs.edge_list.size(); i++) {
        Edge *edge = new Edge;
        *edge = *rhs.edge_list[i];
        edge_list.push_back(edge);
    }
}

EdgeForwardingSpec:: ~EdgeForwardingSpec() {
    STLDeleteValues(&edge_list);
}

Ip4Address EdgeForwardingSpec::Edge::GetInboundIp4Address() const {
    return Ip4Address(get_value(&inbound_address[0], 4));
}

Ip4Address EdgeForwardingSpec::Edge::GetOutboundIp4Address() const {
    return Ip4Address(get_value(&outbound_address[0], 4));
}

void EdgeForwardingSpec::Edge::SetInboundIp4Address(Ip4Address addr) {
    inbound_address.resize(4, 0);
    const Ip4Address::bytes_type &addr_bytes = addr.to_bytes();
    std::copy(addr_bytes.begin(), addr_bytes.begin() + 4,
        inbound_address.begin());
}

void EdgeForwardingSpec::Edge::SetOutboundIp4Address(Ip4Address addr) {
    outbound_address.resize(4, 0);
    const Ip4Address::bytes_type &addr_bytes = addr.to_bytes();
    std::copy(addr_bytes.begin(), addr_bytes.begin() + 4,
        outbound_address.begin());
}

int EdgeForwardingSpec::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    const EdgeForwardingSpec &rhs =
        static_cast<const EdgeForwardingSpec &>(rhs_attr);
    KEY_COMPARE(this, &rhs);
    return 0;
}

void EdgeForwardingSpec::ToCanonical(BgpAttr *attr) {
    attr->set_edge_forwarding(this);
}

std::string EdgeForwardingSpec::ToString() const {
    std::ostringstream oss;
    oss << "EdgeForwarding <code: " << int(code);
    oss << ", flags: 0x" << std::hex << int(flags) << std::dec << ">";
    int idx = 0;
    for (EdgeList::const_iterator it = edge_list.begin();
         it != edge_list.end(); ++it, ++idx) {
        const Edge *edge = *it;
        oss << " Edge[" << idx << "] = (";
        oss << "InAddress=" << edge->GetInboundIp4Address() << ", ";
        oss << "InLabel=" << edge->inbound_label << ", ";
        oss << "OutAddress=" << edge->GetOutboundIp4Address() << ", ";
        oss << "OutLabel=" << edge->outbound_label << ")";
    }

    return oss.str();
}

EdgeForwarding::Edge::Edge(const EdgeForwardingSpec::Edge *spec_edge) {
    inbound_address = spec_edge->GetInboundIp4Address();
    outbound_address = spec_edge->GetOutboundIp4Address();
    inbound_label = spec_edge->inbound_label;
    outbound_label = spec_edge->outbound_label;
}

EdgeForwarding::EdgeForwarding(const EdgeForwardingSpec &efspec)
    : efspec_(efspec) {
    refcount_ = 0;
    for (EdgeForwardingSpec::EdgeList::const_iterator it =
         efspec_.edge_list.begin(); it != efspec_.edge_list.end(); ++it) {
        Edge *edge = new Edge(*it);
        edge_list.push_back(edge);
    }
}

EdgeForwarding::~EdgeForwarding() {
    STLDeleteValues(&edge_list);
}

int BgpAttrOList::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    KEY_COMPARE(olist.get(),
            static_cast<const BgpAttrOList &>(rhs_attr).olist.get());
    return 0;
}

void BgpAttrOList::ToCanonical(BgpAttr *attr) {
    attr->set_olist(olist);
}

std::string BgpAttrOList::ToString() const {
    char repr[80];
    snprintf(repr, sizeof(repr), "OList <subcode: %d> : %p",
             subcode, olist.get());
    return std::string(repr);
}

int BgpAttrLabelBlock::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    KEY_COMPARE(label_block.get(),
            static_cast<const BgpAttrLabelBlock &>(rhs_attr).label_block.get());
    return 0;
}

void BgpAttrLabelBlock::ToCanonical(BgpAttr *attr) {
    attr->set_label_block(label_block);
}

std::string BgpAttrLabelBlock::ToString() const {
    char repr[80];
    snprintf(repr, sizeof(repr), "LabelBlock <subcode: %d> : %d-%d",
             subcode, label_block->first(), label_block->last());
    return std::string(repr);
}

int BgpAttrSourceRd::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    KEY_COMPARE(source_rd,
            static_cast<const BgpAttrSourceRd &>(rhs_attr).source_rd);
    return 0;
}

void BgpAttrSourceRd::ToCanonical(BgpAttr *attr) {
    attr->set_source_rd(source_rd);
}

std::string BgpAttrSourceRd::ToString() const {
    return source_rd.ToString();
}

int BgpAttrEsi::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    KEY_COMPARE(esi, static_cast<const BgpAttrEsi &>(rhs_attr).esi);
    return 0;
}

void BgpAttrEsi::ToCanonical(BgpAttr *attr) {
    attr->set_esi(esi);
}

std::string BgpAttrEsi::ToString() const {
    return esi.ToString();
}

int BgpAttrParams::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    KEY_COMPARE(params, static_cast<const BgpAttrParams &>(rhs_attr).params);
    return 0;
}

void BgpAttrParams::ToCanonical(BgpAttr *attr) {
    attr->set_params(params);
}

std::string BgpAttrParams::ToString() const {
    char repr[80];
    snprintf(repr, sizeof(repr), "Params <subcode: %d> : 0x%016lx",
             subcode, params);
    return std::string(repr);
}

BgpAttr::BgpAttr()
    : origin_(BgpAttrOrigin::INCOMPLETE), nexthop_(),
      med_(0), local_pref_(0), atomic_aggregate_(false),
      aggregator_as_num_(0), params_(0) {
    refcount_ = 0;
}

BgpAttr::BgpAttr(BgpAttrDB *attr_db)
    : attr_db_(attr_db), origin_(BgpAttrOrigin::INCOMPLETE),
      nexthop_(), med_(0), local_pref_(0), atomic_aggregate_(false),
      aggregator_as_num_(0), params_(0) {
    refcount_ = 0;
}

BgpAttr::BgpAttr(BgpAttrDB *attr_db, const BgpAttrSpec &spec)
    : attr_db_(attr_db), origin_(BgpAttrOrigin::INCOMPLETE),
      nexthop_(), med_(0),
      local_pref_(BgpAttrLocalPref::kDefault),
      atomic_aggregate_(false),
      aggregator_as_num_(0), aggregator_address_(), params_(0) {
    refcount_ = 0;
    for (std::vector<BgpAttribute *>::const_iterator it = spec.begin();
         it < spec.end(); it++) {
        (*it)->ToCanonical(this);
    }
}

BgpAttr::BgpAttr(const BgpAttr &rhs) 
    : attr_db_(rhs.attr_db_), origin_(rhs.origin_), nexthop_(rhs.nexthop_),
      med_(rhs.med_), local_pref_(rhs.local_pref_),
      atomic_aggregate_(rhs.atomic_aggregate_),
      aggregator_as_num_(rhs.aggregator_as_num_),
      aggregator_address_(rhs.aggregator_address_),
      originator_id_(rhs.originator_id_),
      source_rd_(rhs.source_rd_), esi_(rhs.esi_), params_(rhs.params_),
      as_path_(rhs.as_path_),
      community_(rhs.community_),
      ext_community_(rhs.ext_community_),
      pmsi_tunnel_(rhs.pmsi_tunnel_),
      edge_discovery_(rhs.edge_discovery_),
      edge_forwarding_(rhs.edge_forwarding_),
      label_block_(rhs.label_block_), olist_(rhs.olist_) {
    refcount_ = 0; 
}

void BgpAttr::set_as_path(const AsPathSpec *spec) {
    if (spec) {
        as_path_ = attr_db_->server()->aspath_db()->Locate(*spec);
    } else {
        as_path_ = NULL;
    }
}

void BgpAttr::set_community(const CommunitySpec *comm) {
    if (comm) {
        community_ = attr_db_->server()->comm_db()->Locate(*comm);
    } else {
        community_ = NULL;
    }
}

void BgpAttr::set_ext_community(ExtCommunityPtr comm) {
    ext_community_ = comm;
}

void BgpAttr::set_ext_community(const ExtCommunitySpec *extcomm) {
    if (extcomm) {
        ext_community_ = attr_db_->server()->extcomm_db()->Locate(*extcomm);
    } else {
        ext_community_ = NULL;
    }
}

void BgpAttr::set_pmsi_tunnel(const PmsiTunnelSpec *pmsi_spec) {
    if (pmsi_spec) {
        pmsi_tunnel_.reset(new PmsiTunnel(*pmsi_spec));
    }
}

void BgpAttr::set_edge_discovery(const EdgeDiscoverySpec *edspec) {
    if (edspec) {
        edge_discovery_.reset(new EdgeDiscovery(*edspec));
    }
}

void BgpAttr::set_edge_forwarding(const EdgeForwardingSpec *efspec) {
    if (efspec) {
        edge_forwarding_.reset(new EdgeForwarding(*efspec));
    }
}

void BgpAttr::set_label_block(LabelBlockPtr label_block) {
    label_block_ = label_block;
}

void BgpAttr::set_olist(BgpOListPtr olist) {
    olist_ = olist;
}

// TODO: Return the left-most AS number in the path.
uint32_t BgpAttr::neighbor_as() const {
    return 0;
}

void BgpAttr::Remove() {
    attr_db_->Delete(this);
}

int BgpAttr::CompareTo(const BgpAttr &rhs) const {
    KEY_COMPARE(origin_, rhs.origin_);
    KEY_COMPARE(nexthop_, rhs.nexthop_);
    KEY_COMPARE(med_, rhs.med_);
    KEY_COMPARE(local_pref_, rhs.local_pref_);
    KEY_COMPARE(atomic_aggregate_, rhs.atomic_aggregate_);
    KEY_COMPARE(aggregator_as_num_, rhs.aggregator_as_num_);
    KEY_COMPARE(aggregator_address_, rhs.aggregator_address_);
    KEY_COMPARE(originator_id_, rhs.originator_id_);
    KEY_COMPARE(pmsi_tunnel_.get(), rhs.pmsi_tunnel_.get());
    KEY_COMPARE(edge_discovery_.get(), rhs.edge_discovery_.get());
    KEY_COMPARE(edge_forwarding_.get(), rhs.edge_forwarding_.get());
    KEY_COMPARE(esi_, rhs.esi_);
    KEY_COMPARE(params_, rhs.params_);
    KEY_COMPARE(source_rd_, rhs.source_rd_);
    KEY_COMPARE(label_block_.get(), rhs.label_block_.get());
    KEY_COMPARE(olist_.get(), rhs.olist_.get());

    if (as_path_.get() == NULL || rhs.as_path_.get() == NULL) {
        KEY_COMPARE(as_path_.get(), rhs.as_path_.get());
    } else {
        int ret = as_path_->CompareTo(*rhs.as_path_);
        if (ret != 0) return ret;
    }

    if (community_.get() == NULL || rhs.community_.get() == NULL) {
        KEY_COMPARE(community_.get(), rhs.community_.get());
    } else {
        int ret = community_->CompareTo(*rhs.community_);
        if (ret != 0) return ret;
    }

    if (ext_community_.get() == NULL || rhs.ext_community_.get() == NULL) {
        KEY_COMPARE(ext_community_.get(), rhs.ext_community_.get());
    } else {
        int ret = ext_community_->CompareTo(*rhs.ext_community_);
        if (ret != 0) return ret;
    }

    return 0;
}

std::size_t hash_value(BgpAttr const &attr) {
    size_t hash = 0;

    boost::hash_combine(hash, attr.origin_);
    boost::hash_combine(hash, attr.nexthop_.to_string());
    boost::hash_combine(hash, attr.med_);
    boost::hash_combine(hash, attr.local_pref_);
    boost::hash_combine(hash, attr.atomic_aggregate_);
    boost::hash_combine(hash, attr.aggregator_as_num_);
    boost::hash_combine(hash, attr.aggregator_address_.to_string());
    boost::hash_combine(hash, attr.originator_id_.to_string());
    boost::hash_combine(hash, attr.params_);
    boost::hash_combine(hash, attr.source_rd_.ToString());
    boost::hash_combine(hash, attr.esi_.ToString());

    if (attr.label_block_) {
        boost::hash_combine(hash, attr.label_block_->first());
        boost::hash_combine(hash, attr.label_block_->last());
    }

    if (attr.olist_) {
        boost::hash_range(hash, attr.olist_->elements.begin(),
                          attr.olist_->elements.end());
    }

    if (attr.as_path_) boost::hash_combine(hash, *attr.as_path_);
    if (attr.community_) boost::hash_combine(hash, *attr.community_);
    if (attr.ext_community_) boost::hash_combine(hash, *attr.ext_community_);

    return hash;
}

BgpAttrDB::BgpAttrDB(BgpServer *server) : server_(server) {
}

// Return a clone of attribute with updated extended community.
BgpAttrPtr BgpAttrDB::ReplaceExtCommunityAndLocate(const BgpAttr *attr, 
                                                   ExtCommunityPtr extcomm) {
    BgpAttr *clone = new BgpAttr(*attr);
    clone->set_ext_community(extcomm);
    return Locate(clone);
}

// Return a clone of attribute with updated local preference.
BgpAttrPtr BgpAttrDB::ReplaceLocalPreferenceAndLocate(const BgpAttr *attr, 
                                                      uint32_t local_pref) {
    BgpAttr *clone = new BgpAttr(*attr);
    clone->set_local_pref(local_pref);
    return Locate(clone);
}

// Return a clone of attribute with updated originator id.
BgpAttrPtr BgpAttrDB::ReplaceOriginatorIdAndLocate(const BgpAttr *attr,
                                                   Ip4Address originator_id) {
    BgpAttr *clone = new BgpAttr(*attr);
    clone->set_originator_id(originator_id);
    return Locate(clone);
}

// Return a clone of attribute with updated source rd.
BgpAttrPtr BgpAttrDB::ReplaceSourceRdAndLocate(const BgpAttr *attr,
                                               RouteDistinguisher source_rd) {
    BgpAttr *clone = new BgpAttr(*attr);
    clone->set_source_rd(source_rd);
    return Locate(clone);
}

// Return a clone of attribute with updated esi.
BgpAttrPtr BgpAttrDB::ReplaceEsiAndLocate(const BgpAttr *attr,
                                          EthernetSegmentId esi) {
    BgpAttr *clone = new BgpAttr(*attr);
    clone->set_esi(esi);
    return Locate(clone);
}

// Return a clone of attribute with updated olist.
BgpAttrPtr BgpAttrDB::ReplaceOListAndLocate(const BgpAttr *attr,
                                            BgpOListPtr olist) {
    BgpAttr *clone = new BgpAttr(*attr);
    clone->set_olist(olist);
    return Locate(clone);
}

// Return a clone of attribute with updated pmsi tunnel.
BgpAttrPtr BgpAttrDB::ReplacePmsiTunnelAndLocate(const BgpAttr *attr,
                                                 PmsiTunnelSpec *pmsi_spec) {
    BgpAttr *clone = new BgpAttr(*attr);
    clone->set_pmsi_tunnel(pmsi_spec);
    return Locate(clone);
}

// Return a clone of attribute with updated nexthop.
BgpAttrPtr BgpAttrDB::UpdateNexthopAndLocate(const BgpAttr *attr, uint16_t afi,
                                             uint8_t safi, IpAddress &addr) {
    BgpAttr *clone = new BgpAttr(*attr);
    clone->set_nexthop(addr);
    return Locate(clone);
}
