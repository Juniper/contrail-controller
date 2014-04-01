/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/rtarget/rtarget_prefix.h"


using namespace std;
using boost::system::error_code;

RTargetPrefix::RTargetPrefix()
  : as_(0), rtarget_(RouteTarget::null_rtarget) {
}

RTargetPrefix::RTargetPrefix(const BgpProtoPrefix &prefix) {
    if (prefix.prefixlen == 0) {
        as_ = 0;
        rtarget_ =  RouteTarget::null_rtarget;
        return;
    }

    as_ = get_value(&prefix.prefix[0], 4);
    RouteTarget::bytes_type bt = { { 0 } };
    std::copy(prefix.prefix.begin() + 4, prefix.prefix.end(), bt.begin());
    rtarget_ = RouteTarget(bt);
}

void RTargetPrefix::BuildProtoPrefix(BgpProtoPrefix *prefix) const {
    prefix->prefix.clear();
    if (as_ == 0 && rtarget_ == RouteTarget::null_rtarget) {
        prefix->prefixlen = 0;
        return;
    }

    prefix->prefixlen = 96;
    prefix->prefix.resize(12);
    put_value(&prefix->prefix[0], 4, as_);
    put_value(&prefix->prefix[4], 8, rtarget_.GetExtCommunityValue());
}

// as:rtarget
RTargetPrefix RTargetPrefix::FromString(const string &str, error_code *errorp) {
    RTargetPrefix prefix;

    size_t pos = str.find(':');
    if (pos == string::npos) {
        if (errorp != NULL)
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        return prefix;
    }

    as4_t as;
    string asstr = str.substr(0, pos);
    stringToInteger(asstr, as);

    string rtargetstr(str, pos + 1);
    error_code rtarget_err;
    RouteTarget rtarget;
    rtarget = RouteTarget::FromString(rtargetstr, &rtarget_err);
    if (rtarget_err != 0) {
        if (errorp != NULL)
            *errorp = rtarget_err;
        return prefix;
    }

    prefix.rtarget_ = rtarget;
    prefix.as_ = as;
    return prefix;
}

string RTargetPrefix::ToString() const {
    return (integerToString(as_) + ":" + rtarget_.ToString());
}

int RTargetPrefix::CompareTo(const RTargetPrefix &rhs) const {
    if (as_ < rhs.as_) {
        return -1;
    }
    if (as_ > rhs.as_) {
        return 1;
    }
    if (rtarget_ < rhs.rtarget_) {
        return -1;
    }
    if (rtarget_ > rhs.rtarget_) {
        return 1;
    }
    return 0;
}

