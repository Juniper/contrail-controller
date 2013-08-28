/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_origin_vn.h"

#include "bgp/bgp_proto.h"

OriginVnDB::OriginVnDB(BgpServer *server) : server_(server) {
}

int OriginVnSpec::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    KEY_COMPARE(origin_vn,
        static_cast<const OriginVnSpec &>(rhs_attr).origin_vn);
    return 0;
}

void OriginVnSpec::ToCanonical(BgpAttr *attr) {
    attr->set_origin_vn(this);
}

std::string OriginVnSpec::ToString() const {
    return origin_vn;
}

void OriginVn::Remove() {
    origin_vn_db_->Delete(this);
}
