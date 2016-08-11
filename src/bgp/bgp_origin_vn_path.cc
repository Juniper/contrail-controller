/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_origin_vn_path.h"


#include <algorithm>
#include <string>

#include "bgp/bgp_proto.h"
#include "bgp/origin-vn/origin_vn.h"

using std::string;
using std::vector;

string OriginVnPathSpec::ToString() const {
    char repr[80];
    snprintf(repr, sizeof(repr), "OriginVnPath <code: %d, flags: %02x> : %zu",
             code, flags, origin_vns.size());
    return string(repr);
}

int OriginVnPathSpec::CompareTo(const BgpAttribute &rhs) const {
    int ret = BgpAttribute::CompareTo(rhs);
    if (ret != 0)
        return ret;
    KEY_COMPARE(origin_vns,
        static_cast<const OriginVnPathSpec &>(rhs).origin_vns);
    return 0;
}

void OriginVnPathSpec::ToCanonical(BgpAttr *attr) {
    attr->set_origin_vn_path(this);
}

size_t OriginVnPathSpec::EncodeLength() const {
    return origin_vns.size() * sizeof(uint64_t);
}

OriginVnPath::OriginVnPath(OriginVnPathDB *ovnpath_db,
    const OriginVnPathSpec spec)
    : ovnpath_db_(ovnpath_db) {
    refcount_ = 0;
    for (vector<uint64_t>::const_iterator it = spec.origin_vns.begin();
         it < spec.origin_vns.end(); ++it) {
        OriginVnValue value;
        put_value(value.data(), value.size(), *it);
        origin_vns_.push_back(value);
    }
}

void OriginVnPath::Remove() {
    ovnpath_db_->Delete(this);
}

void OriginVnPath::Prepend(const OriginVnValue &value) {
    OriginVnList::iterator it = origin_vns_.begin();
    origin_vns_.insert(it, value);
}

bool OriginVnPath::Contains(const OriginVnValue &val) const {
    OriginVn in_origin_vn(val);
    int in_vn_index = in_origin_vn.IsGlobal() ? in_origin_vn.vn_index() : 0;
    for (OriginVnList::const_iterator it = origin_vns_.begin();
         it != origin_vns_.end(); ++it) {
        if (*it == val)
            return true;
        if (in_vn_index == 0)
            continue;
        OriginVn origin_vn(*it);
        if (origin_vn.vn_index() == in_vn_index)
            return true;
    }
    return false;
}

int OriginVnPath::CompareTo(const OriginVnPath &rhs) const {
    KEY_COMPARE(origin_vns_.size(), rhs.origin_vns_.size());

    OriginVnList::const_iterator it1, it2;
    for (it1 = origin_vns_.begin(), it2 = rhs.origin_vns_.begin();
         it1 < origin_vns_.end(); ++it1, ++it2) {
        if (*it1 < *it2) {
            return -1;
        }
        if (*it1 > *it2) {
            return 1;
        }
    }
    return 0;
}

OriginVnPathDB::OriginVnPathDB(BgpServer *server) {
}

OriginVnPathPtr OriginVnPathDB::PrependAndLocate(const OriginVnPath *ovnpath,
    const OriginVnPath::OriginVnValue &value) {
    OriginVnPath *clone;
    if (ovnpath) {
        clone = new OriginVnPath(*ovnpath);
    } else {
        clone = new OriginVnPath(this);
    }
    clone->Prepend(value);
    return Locate(clone);
}
