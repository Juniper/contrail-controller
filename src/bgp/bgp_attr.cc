/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_attr.h"

#include <algorithm>
#include <string>

#include "bgp/bgp_server.h"
#include "bgp/extended-community/mac_mobility.h"
#include "net/bgp_af.h"

using std::sort;

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
    if (v6_nexthop.is_unspecified()) {
        attr->set_nexthop(Ip4Address(nexthop));
    } else {
        attr->set_nexthop(v6_nexthop);
    }
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
    snprintf(repr, sizeof(repr), "MED <code: %d, flags: %02x> : %d",
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
             "Aggregator <code: %d, flags: %02x> : %d:%08x",
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

int ClusterListSpec::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    KEY_COMPARE(cluster_list,
        static_cast<const ClusterListSpec &>(rhs_attr).cluster_list);
    return 0;
}

void ClusterListSpec::ToCanonical(BgpAttr *attr) {
    attr->set_cluster_list(this);
}

std::string ClusterListSpec::ToString() const {
    std::stringstream repr;
    repr << "CLUSTER_LIST <code: " << std::dec << code;
    repr << ", flags: 0x" << std::hex << flags << "> :";
    for (std::vector<uint32_t>::const_iterator iter = cluster_list.begin();
         iter != cluster_list.end(); ++iter) {
        repr << " " << Ip4Address(*iter).to_string();
    }
    repr << std::endl;
    return repr.str();
}

ClusterList::ClusterList(ClusterListDB *cluster_list_db,
    const ClusterListSpec &spec)
    : cluster_list_db_(cluster_list_db),
      spec_(spec) {
    refcount_ = 0;
}

void ClusterList::Remove() {
    cluster_list_db_->Delete(this);
}

ClusterListDB::ClusterListDB(BgpServer *server) {
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

size_t BgpMpNlri::EncodeLength() const {
    size_t sz = 2 /* safi */ + 1 /* afi */ +
                1 /* NlriNextHopLength */ +
                1 /* Reserved */;
    sz += nexthop.size();
    for (std::vector<BgpProtoPrefix*>::const_iterator iter = nlri.begin();
         iter != nlri.end(); ++iter) {
        size_t bytes = 0;
        if (afi == BgpAf::L2Vpn &&
            (safi == BgpAf::EVpn || safi == BgpAf::ErmVpn)) {
            bytes = (*iter)->prefixlen;
        } else {
            bytes = ((*iter)->prefixlen + 7) / 8;
        }
        sz += 1 + bytes;
    }
    return sz;
}

PmsiTunnelSpec::PmsiTunnelSpec()
    : BgpAttribute(PmsiTunnel, kFlags),
      tunnel_flags(0), tunnel_type(0), label(0) {
}

PmsiTunnelSpec::PmsiTunnelSpec(const BgpAttribute &rhs)
    : BgpAttribute(rhs), tunnel_flags(0), tunnel_type(0), label(0) {
}

int PmsiTunnelSpec::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    const PmsiTunnelSpec &rhs = static_cast<const PmsiTunnelSpec &>(rhs_attr);
    KEY_COMPARE(tunnel_flags, rhs.tunnel_flags);
    KEY_COMPARE(tunnel_type, rhs.tunnel_type);
    KEY_COMPARE(label, rhs.label);
    KEY_COMPARE(identifier, rhs.identifier);
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

uint32_t PmsiTunnelSpec::GetLabel(bool is_vni) const {
    return (is_vni ? label : (label >> 4));
}

void PmsiTunnelSpec::SetLabel(uint32_t in_label, bool is_vni) {
    label = (is_vni ? in_label : (in_label << 4 | 0x01));
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

std::string PmsiTunnelSpec::GetTunnelTypeString() const {
    switch (tunnel_type) {
    case RsvpP2mpLsp:
        return "RsvpP2mpLsp";
    case LdpP2mpLsp:
        return "LdpP2mpLsp";
    case PimSsmTree:
        return "PimSsmTree";
    case PimSmTree:
        return "PimSmTree";
    case BidirPimTree:
        return "BidirPimTree";
    case IngressReplication:
        return "IngressReplication";
    case MldpMp2mpLsp:
        return "MldpMp2mpLsp";
    case AssistedReplicationContrail:
        return "AssistedReplication";
    default:
        break;
    }
    std::ostringstream oss;
    oss << "Unknown(" << int(tunnel_type) << ")";
    return oss.str();
}

std::string PmsiTunnelSpec::GetTunnelArTypeString() const {
    switch (tunnel_flags & AssistedReplicationType) {
    case RegularNVE:
        return "RegularNVE";
    case ARReplicator:
        return "ARReplicator";
    case ARLeaf:
        return "ARLeaf";
    default:
        break;
    }
    return "Unknown";
}

std::vector<std::string> PmsiTunnelSpec::GetTunnelFlagsStrings() const {
    std::vector<std::string> flags;
    if (tunnel_flags & LeafInfoRequired) {
        flags.push_back("LeafInfoRequired");
    }
    if (tunnel_flags & EdgeReplicationSupported) {
        flags.push_back("EdgeReplicationSupported");
    }
    if (flags.empty()) {
        flags.push_back("None");
    }
    return flags;
}

PmsiTunnel::PmsiTunnel(PmsiTunnelDB *pmsi_tunnel_db,
    const PmsiTunnelSpec &pmsi_spec)
    : pmsi_tunnel_db_(pmsi_tunnel_db),
      pmsi_spec_(pmsi_spec) {
    refcount_ = 0;
    tunnel_flags_ = pmsi_spec_.tunnel_flags;
    tunnel_type_ = pmsi_spec_.tunnel_type;
    label_ = pmsi_spec_.label;
    identifier_ = pmsi_spec_.GetIdentifier();
}

void PmsiTunnel::Remove() {
    pmsi_tunnel_db_->Delete(this);
}

PmsiTunnelDB::PmsiTunnelDB(BgpServer *server) {
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

struct EdgeDiscoverySpecEdgeCompare {
    int operator()(const EdgeDiscoverySpec::Edge *lhs,
                   const EdgeDiscoverySpec::Edge *rhs) const {
        KEY_COMPARE(lhs->address, rhs->address);
        KEY_COMPARE(lhs->labels, rhs->labels);
        return 0;
    }
};

int EdgeDiscoverySpec::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    const EdgeDiscoverySpec &rhs =
            static_cast<const EdgeDiscoverySpec &>(rhs_attr);
    ret = STLSortedCompare(edge_list.begin(), edge_list.end(),
                           rhs.edge_list.begin(), rhs.edge_list.end(),
                           EdgeDiscoverySpecEdgeCompare());
    return ret;
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

size_t EdgeDiscoverySpec::EncodeLength() const {
    size_t sz = 0;
    for (EdgeList::const_iterator iter = edge_list.begin();
         iter != edge_list.end(); ++iter) {
        sz += 2; /* AddressLen + LabelLen */
        sz += (*iter)->address.size();
        sz += (*iter)->labels.size() * sizeof(uint32_t);
    }
    return sz;
}

EdgeDiscovery::Edge::Edge(const EdgeDiscoverySpec::Edge *spec_edge) {
    address = spec_edge->GetIp4Address();
    uint32_t first_label, last_label;
    spec_edge->GetLabels(&first_label, &last_label);
    label_block = new LabelBlock(first_label, last_label);
}

bool EdgeDiscovery::Edge::operator<(const Edge &rhs) const {
    BOOL_KEY_COMPARE(address, rhs.address);
    BOOL_KEY_COMPARE(label_block->first(), rhs.label_block->first());
    BOOL_KEY_COMPARE(label_block->last(), rhs.label_block->last());
    return false;
}

EdgeDiscovery::EdgeDiscovery(EdgeDiscoveryDB *edge_discovery_db,
    const EdgeDiscoverySpec &edspec)
    : edge_discovery_db_(edge_discovery_db),
      edspec_(edspec) {
    refcount_ = 0;
    for (EdgeDiscoverySpec::EdgeList::const_iterator it =
         edspec_.edge_list.begin(); it != edspec_.edge_list.end(); ++it) {
        Edge *edge = new Edge(*it);
        edge_list.push_back(edge);
    }
    sort(edge_list.begin(), edge_list.end(), EdgeDiscovery::EdgeCompare());
}

EdgeDiscovery::~EdgeDiscovery() {
    STLDeleteValues(&edge_list);
}

struct EdgeDiscoveryEdgeCompare {
    int operator()(const EdgeDiscovery::Edge *lhs,
                   const EdgeDiscovery::Edge *rhs) const {
        KEY_COMPARE(*lhs, *rhs);
        return 0;
    }
};

int EdgeDiscovery::CompareTo(const EdgeDiscovery &rhs) const {
    int result = STLSortedCompare(edge_list.begin(), edge_list.end(),
                                  rhs.edge_list.begin(), rhs.edge_list.end(),
                                  EdgeDiscoveryEdgeCompare());
    return result;
}

void EdgeDiscovery::Remove() {
    edge_discovery_db_->Delete(this);
}

EdgeDiscoveryDB::EdgeDiscoveryDB(BgpServer *server) {
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

struct EdgeForwardingSpecEdgeCompare {
    int operator()(const EdgeForwardingSpec::Edge *lhs,
                   const EdgeForwardingSpec::Edge *rhs) const {
        KEY_COMPARE(lhs->inbound_address, rhs->inbound_address);
        KEY_COMPARE(lhs->outbound_address, rhs->outbound_address);
        KEY_COMPARE(lhs->inbound_label, rhs->inbound_label);
        KEY_COMPARE(lhs->outbound_label, rhs->outbound_label);
        return 0;
    }
};

int EdgeForwardingSpec::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    const EdgeForwardingSpec &rhs =
            static_cast<const EdgeForwardingSpec &>(rhs_attr);
    ret = STLSortedCompare(edge_list.begin(), edge_list.end(),
                           rhs.edge_list.begin(), rhs.edge_list.end(),
                           EdgeForwardingSpecEdgeCompare());
    return ret;
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

size_t EdgeForwardingSpec::EncodeLength() const {
    size_t sz = 0;
    for (EdgeList::const_iterator iter = edge_list.begin();
         iter != edge_list.end(); ++iter) {
        sz += 1 /* address len */ + 8 /* 2 labels */;
        sz += (*iter)->inbound_address.size();
        sz += (*iter)->outbound_address.size();
    }

    return sz;
}

EdgeForwarding::Edge::Edge(const EdgeForwardingSpec::Edge *spec_edge) {
    inbound_address = spec_edge->GetInboundIp4Address();
    outbound_address = spec_edge->GetOutboundIp4Address();
    inbound_label = spec_edge->inbound_label;
    outbound_label = spec_edge->outbound_label;
}

bool EdgeForwarding::Edge::operator<(const Edge &rhs) const {
    BOOL_KEY_COMPARE(inbound_address, rhs.inbound_address);
    BOOL_KEY_COMPARE(outbound_address, rhs.outbound_address);
    BOOL_KEY_COMPARE(inbound_label, rhs.inbound_label);
    BOOL_KEY_COMPARE(outbound_label, rhs.outbound_label);
    return false;
}

EdgeForwarding::EdgeForwarding(EdgeForwardingDB *edge_forwarding_db,
    const EdgeForwardingSpec &efspec)
    : edge_forwarding_db_(edge_forwarding_db),
      efspec_(efspec) {
    refcount_ = 0;
    for (EdgeForwardingSpec::EdgeList::const_iterator it =
         efspec_.edge_list.begin(); it != efspec_.edge_list.end(); ++it) {
        Edge *edge = new Edge(*it);
        edge_list.push_back(edge);
    }
    sort(edge_list.begin(), edge_list.end(), EdgeForwarding::EdgeCompare());
}

EdgeForwarding::~EdgeForwarding() {
    STLDeleteValues(&edge_list);
}

struct EdgeForwardingEdgeCompare {
    int operator()(const EdgeForwarding::Edge *lhs,
                   const EdgeForwarding::Edge *rhs) const {
        KEY_COMPARE(*lhs, *rhs);
        return 0;
    }
};

int EdgeForwarding::CompareTo(const EdgeForwarding &rhs) const {
    int result = STLSortedCompare(edge_list.begin(), edge_list.end(),
                                  rhs.edge_list.begin(), rhs.edge_list.end(),
                                  EdgeForwardingEdgeCompare());
    return result;
}

void EdgeForwarding::Remove() {
    edge_forwarding_db_->Delete(this);
}

EdgeForwardingDB::EdgeForwardingDB(BgpServer *server) {
}

bool BgpOListElem::operator<(const BgpOListElem &rhs) const {
    BOOL_KEY_COMPARE(address, rhs.address);
    BOOL_KEY_COMPARE(label, rhs.label);
    BOOL_KEY_COMPARE(encap, rhs.encap);
    return false;
}

int BgpOListSpec::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    return 0;
}

void BgpOListSpec::ToCanonical(BgpAttr *attr) {
    if (subcode == BgpAttribute::OList) {
        attr->set_olist(this);
    } else if (subcode == BgpAttribute::LeafOList) {
        attr->set_leaf_olist(this);
    } else {
        assert(false);
    }
}

std::string BgpOListSpec::ToString() const {
    char repr[80];
    snprintf(repr, sizeof(repr), "OList <subcode: %d> : %p",
             subcode, this);
    return std::string(repr);
}

BgpOList::BgpOList(BgpOListDB *olist_db, const BgpOListSpec &olist_spec)
    : olist_db_(olist_db),
      olist_spec_(olist_spec) {
    refcount_ = 0;
    for (BgpOListSpec::Elements::const_iterator it =
         olist_spec_.elements.begin(); it != olist_spec_.elements.end(); ++it) {
        BgpOListElem *elem = new BgpOListElem(*it);
        sort(elem->encap.begin(), elem->encap.end());
        elements_.push_back(elem);
    }
    sort(elements_.begin(), elements_.end(), BgpOListElemCompare());
}

BgpOList::~BgpOList() {
    STLDeleteValues(&elements_);
}

struct BgpOListElementCompare {
    int operator()(const BgpOListElem *lhs, const BgpOListElem *rhs) const {
        KEY_COMPARE(*lhs, *rhs);
        return 0;
    }
};

int BgpOList::CompareTo(const BgpOList &rhs) const {
    KEY_COMPARE(olist().subcode, rhs.olist().subcode);
    int result = STLSortedCompare(elements().begin(), elements().end(),
                                  rhs.elements().begin(), rhs.elements().end(),
                                  BgpOListElementCompare());
    return result;
}

void BgpOList::Remove() {
    olist_db_->Delete(this);
}

BgpOListDB::BgpOListDB(BgpServer *server) {
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
    char repr[80];
    snprintf(repr, sizeof(repr), "SourceRd <subcode: %d> : %s",
             subcode, source_rd.ToString().c_str());
    return std::string(repr);
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
    char repr[80];
    snprintf(repr, sizeof(repr), "Esi <subcode: %d> : %s",
             subcode, esi.ToString().c_str());
    return std::string(repr);
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
    snprintf(repr, sizeof(repr), "Params <subcode: %d> : 0x%016jx",
             subcode, params);
    return std::string(repr);
}

BgpAttr::BgpAttr()
    : attr_db_(NULL), origin_(BgpAttrOrigin::INCOMPLETE), nexthop_(),
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
      cluster_list_(rhs.cluster_list_),
      community_(rhs.community_),
      ext_community_(rhs.ext_community_),
      origin_vn_path_(rhs.origin_vn_path_),
      pmsi_tunnel_(rhs.pmsi_tunnel_),
      edge_discovery_(rhs.edge_discovery_),
      edge_forwarding_(rhs.edge_forwarding_),
      label_block_(rhs.label_block_),
      olist_(rhs.olist_),
      leaf_olist_(rhs.leaf_olist_) {
    refcount_ = 0;
}

void BgpAttr::set_as_path(AsPathPtr aspath) {
    as_path_ = aspath;
}

void BgpAttr::set_as_path(const AsPathSpec *spec) {
    if (spec) {
        as_path_ = attr_db_->server()->aspath_db()->Locate(*spec);
    } else {
        as_path_ = NULL;
    }
}

void BgpAttr::set_cluster_list(const ClusterListSpec *spec) {
    if (spec) {
        cluster_list_ = attr_db_->server()->cluster_list_db()->Locate(*spec);
    } else {
        cluster_list_ = NULL;
    }
}

void BgpAttr::set_community(CommunityPtr comm) {
    community_ = comm;
}

void BgpAttr::set_community(const CommunitySpec *comm) {
    if (comm) {
        community_ = attr_db_->server()->comm_db()->Locate(*comm);
    } else {
        community_ = NULL;
    }
}

void BgpAttr::set_ext_community(ExtCommunityPtr extcomm) {
    ext_community_ = extcomm;
}

void BgpAttr::set_ext_community(const ExtCommunitySpec *extcomm) {
    if (extcomm) {
        ext_community_ = attr_db_->server()->extcomm_db()->Locate(*extcomm);
    } else {
        ext_community_ = NULL;
    }
}

void BgpAttr::set_origin_vn_path(OriginVnPathPtr ovnpath) {
    origin_vn_path_ = ovnpath;
}

void BgpAttr::set_origin_vn_path(const OriginVnPathSpec *spec) {
    if (spec) {
        origin_vn_path_ = attr_db_->server()->ovnpath_db()->Locate(*spec);
    } else {
        origin_vn_path_ = NULL;
    }
}

void BgpAttr::set_pmsi_tunnel(const PmsiTunnelSpec *pmsi_spec) {
    if (pmsi_spec) {
        pmsi_tunnel_ = attr_db_->server()->pmsi_tunnel_db()->Locate(*pmsi_spec);
    } else {
        pmsi_tunnel_ = NULL;
    }
}

void BgpAttr::set_edge_discovery(const EdgeDiscoverySpec *edspec) {
    if (edspec) {
        edge_discovery_ =
            attr_db_->server()->edge_discovery_db()->Locate(*edspec);
    } else {
        edge_discovery_ = NULL;
    }
}

void BgpAttr::set_edge_forwarding(const EdgeForwardingSpec *efspec) {
    if (efspec) {
        edge_forwarding_ =
            attr_db_->server()->edge_forwarding_db()->Locate(*efspec);
    } else {
        edge_forwarding_ = NULL;
    }
}

void BgpAttr::set_label_block(LabelBlockPtr label_block) {
    label_block_ = label_block;
}

void BgpAttr::set_olist(const BgpOListSpec *olist_spec) {
    if (olist_spec) {
        olist_ = attr_db_->server()->olist_db()->Locate(*olist_spec);
    } else {
        olist_ = NULL;
    }
}

void BgpAttr::set_leaf_olist(const BgpOListSpec *leaf_olist_spec) {
    if (leaf_olist_spec) {
        leaf_olist_ = attr_db_->server()->olist_db()->Locate(*leaf_olist_spec);
    } else {
        leaf_olist_ = NULL;
    }
}

Address::Family BgpAttr::nexthop_family() const {
    if (nexthop_.is_v6()) {
        return Address::INET6;
    } else {
        return Address::INET;
    }
}

as_t BgpAttr::neighbor_as() const {
    return (as_path_.get() ? as_path_->neighbor_as() : 0);
}

uint32_t BgpAttr::sequence_number() const {
    if (!ext_community_)
        return 0;
    for (ExtCommunity::ExtCommunityList::const_iterator it =
         ext_community_->communities().begin();
         it != ext_community_->communities().end(); ++it) {
        if (ExtCommunity::is_mac_mobility(*it)) {
            MacMobility mm(*it);
            return mm.sequence_number();
        }
    }
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
    KEY_COMPARE(leaf_olist_.get(), rhs.leaf_olist_.get());
    KEY_COMPARE(as_path_.get(), rhs.as_path_.get());
    KEY_COMPARE(cluster_list_.get(), rhs.cluster_list_.get());
    KEY_COMPARE(community_.get(), rhs.community_.get());
    KEY_COMPARE(ext_community_.get(), rhs.ext_community_.get());
    KEY_COMPARE(origin_vn_path_.get(), rhs.origin_vn_path_.get());
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
        boost::hash_range(hash, attr.olist_->elements().begin(),
                          attr.olist_->elements().end());
    }
    if (attr.leaf_olist_) {
        boost::hash_range(hash, attr.leaf_olist_->elements().begin(),
                          attr.leaf_olist_->elements().end());
    }

    if (attr.as_path_) boost::hash_combine(hash, *attr.as_path_);
    if (attr.community_) boost::hash_combine(hash, *attr.community_);
    if (attr.ext_community_) boost::hash_combine(hash, *attr.ext_community_);
    if (attr.origin_vn_path_) boost::hash_combine(hash, *attr.origin_vn_path_);

    return hash;
}

BgpAttrDB::BgpAttrDB(BgpServer *server) : server_(server) {
}

// Return a clone of attribute with updated aspath.
BgpAttrPtr BgpAttrDB::ReplaceAsPathAndLocate(const BgpAttr *attr,
                                             AsPathPtr aspath) {
    BgpAttr *clone = new BgpAttr(*attr);
    clone->set_as_path(aspath);
    return Locate(clone);
}

// Return a clone of attribute with updated community.
BgpAttrPtr BgpAttrDB::ReplaceCommunityAndLocate(const BgpAttr *attr,
                                                CommunityPtr community) {
    BgpAttr *clone = new BgpAttr(*attr);
    clone->set_community(community);
    return Locate(clone);
}

// Return a clone of attribute with updated extended community.
BgpAttrPtr BgpAttrDB::ReplaceExtCommunityAndLocate(const BgpAttr *attr,
                                                   ExtCommunityPtr extcomm) {
    BgpAttr *clone = new BgpAttr(*attr);
    clone->set_ext_community(extcomm);
    return Locate(clone);
}

// Return a clone of attribute with updated origin vn path.
BgpAttrPtr BgpAttrDB::ReplaceOriginVnPathAndLocate(const BgpAttr *attr,
                                                   OriginVnPathPtr ovnpath) {
    BgpAttr *clone = new BgpAttr(*attr);
    clone->set_origin_vn_path(ovnpath);
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
    const RouteDistinguisher &source_rd) {
    BgpAttr *clone = new BgpAttr(*attr);
    clone->set_source_rd(source_rd);
    return Locate(clone);
}

// Return a clone of attribute with updated esi.
BgpAttrPtr BgpAttrDB::ReplaceEsiAndLocate(const BgpAttr *attr,
                                          const EthernetSegmentId &esi) {
    BgpAttr *clone = new BgpAttr(*attr);
    clone->set_esi(esi);
    return Locate(clone);
}

// Return a clone of attribute with updated olist.
BgpAttrPtr BgpAttrDB::ReplaceOListAndLocate(const BgpAttr *attr,
    const BgpOListSpec *olist_spec) {
    assert(olist_spec->subcode == BgpAttribute::OList);
    BgpAttr *clone = new BgpAttr(*attr);
    clone->set_olist(olist_spec);
    return Locate(clone);
}

// Return a clone of attribute with updated leaf olist.
BgpAttrPtr BgpAttrDB::ReplaceLeafOListAndLocate(const BgpAttr *attr,
    const BgpOListSpec *leaf_olist_spec) {
    assert(leaf_olist_spec->subcode == BgpAttribute::LeafOList);
    BgpAttr *clone = new BgpAttr(*attr);
    clone->set_leaf_olist(leaf_olist_spec);
    return Locate(clone);
}

// Return a clone of attribute with updated pmsi tunnel.
BgpAttrPtr BgpAttrDB::ReplacePmsiTunnelAndLocate(const BgpAttr *attr,
    const PmsiTunnelSpec *pmsi_spec) {
    BgpAttr *clone = new BgpAttr(*attr);
    clone->set_pmsi_tunnel(pmsi_spec);
    return Locate(clone);
}

// Return a clone of attribute with updated nexthop.
BgpAttrPtr BgpAttrDB::ReplaceNexthopAndLocate(const BgpAttr *attr,
    const IpAddress &addr) {
    BgpAttr *clone = new BgpAttr(*attr);
    clone->set_nexthop(addr);
    return Locate(clone);
}
