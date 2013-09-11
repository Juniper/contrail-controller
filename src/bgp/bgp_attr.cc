/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_attr.h"
#include "base/util.h"
#include "bgp/bgp_proto.h"

BgpProtoPrefix::BgpProtoPrefix() : prefixlen(0), type(0) {
}

int BgpAttribute::CompareTo(const BgpAttribute &rhs) const {
    KEY_COMPARE(code, rhs.code);
    KEY_COMPARE(subcode, rhs.subcode);
    KEY_COMPARE(flags, rhs.flags);
    return 0;
}

std::string BgpAttribute::ToString() const {
    char repr[80];
    snprintf(repr, sizeof(repr), "<code: %d, flags: %02x>", code, flags);
    return std::string(repr);
}

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

BgpAttr::BgpAttr()
    : origin_(BgpAttrOrigin::INCOMPLETE), nexthop_(),
      med_(0), local_pref_(0), atomic_aggregate_(false),
      aggregator_as_num_(0), aggregator_address_() {
    refcount_ = 0;
}

BgpAttr::BgpAttr(BgpAttrDB *attr_db)
    : attr_db_(attr_db), origin_(BgpAttrOrigin::INCOMPLETE),
      nexthop_(), med_(0), local_pref_(0), atomic_aggregate_(false),
      aggregator_as_num_(0), aggregator_address_() {
    refcount_ = 0;
}

BgpAttr::BgpAttr(BgpAttrDB *attr_db, const BgpAttrSpec &spec)
    : attr_db_(attr_db), origin_(BgpAttrOrigin::INCOMPLETE),
      nexthop_(), med_(0),
      local_pref_(BgpAttrLocalPref::kDefault),
      atomic_aggregate_(false),
      aggregator_as_num_(0), aggregator_address_() {
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
      source_rd_(rhs.source_rd_),
      as_path_(rhs.as_path_), community_(rhs.community_),
      ext_community_(rhs.ext_community_),
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
    boost::hash_combine(hash, attr.source_rd_.ToString());

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

// Return a clone of attribute with updated source rd.
BgpAttrPtr BgpAttrDB::ReplaceSourceRdAndLocate(const BgpAttr *attr,
                                               RouteDistinguisher source_rd) {
    BgpAttr *clone = new BgpAttr(*attr);
    clone->set_source_rd(source_rd);
    return Locate(clone);
}

// Return a clone of attribute with updated nexthop.
BgpAttrPtr BgpAttrDB::UpdateNexthopAndLocate(const BgpAttr *attr, uint16_t afi,
                                             uint8_t safi, IpAddress &addr) {
    BgpAttr *clone = new BgpAttr(*attr);
    clone->set_nexthop(addr);
    return Locate(clone);
}
