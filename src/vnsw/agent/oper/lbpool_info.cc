/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "lbpool_info.h"

#include <sstream>
#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

LBPoolInfo::LBPoolInfo()
        : vip_uuid_(boost::uuids::nil_uuid()) {
    pool_.Clear();
    vip_.Clear();
}

LBPoolInfo::~LBPoolInfo() {
}

template <typename Type>
static int compare(const Type &lhs, const Type &rhs) {
    if (lhs < rhs) {
        return -1;
    }
    if (rhs < lhs) {
        return 1;
    }
    return 0;
}

#define COMPARE_PROPERTY(Xname) \
    do {                        \
        int res = compare(lhs.Xname, rhs.Xname);    \
        if (res != 0) {         \
            return res;         \
        }                       \
    } while(0)

static int CompareMember(const autogen::LoadbalancerMemberType &lhs,
                         const autogen::LoadbalancerMemberType &rhs) {
    COMPARE_PROPERTY(admin_state);
    COMPARE_PROPERTY(protocol_port);
    COMPARE_PROPERTY(weight);
    COMPARE_PROPERTY(address);
    return 0;
}

static int CompareMonitor(
    const autogen::LoadbalancerHealthmonitorType &lhs,
    const autogen::LoadbalancerHealthmonitorType &rhs) {
    COMPARE_PROPERTY(admin_state);
    COMPARE_PROPERTY(monitor_type);
    COMPARE_PROPERTY(delay);
    COMPARE_PROPERTY(timeout);
    COMPARE_PROPERTY(max_retries);
    COMPARE_PROPERTY(http_method);
    COMPARE_PROPERTY(url_path);
    COMPARE_PROPERTY(expected_codes);
    return 0;
}

#undef COMPARE_PROPERTY

template <class MapType, class Comparator>
int MapCompare(const MapType &lhs, const MapType &rhs, Comparator comp) {
    typename MapType::const_iterator iter1 = lhs.begin();
    typename MapType::const_iterator iter2 = rhs.begin();

    while (iter1 != lhs.end() && iter2 != rhs.end()) {
        if (iter1->first < iter2->first) {
            return -1;
        }
        if (iter2->first < iter1->first) {
            return 1;
        }
        int result = comp(iter1->second, iter2->second);
        if (result) {
            return result;
        }
        ++iter1;
        ++iter2;
    }

    if (iter1 != lhs.end()) {
        return 1;
    }
    if (iter2 != rhs.end()) {
        return -1;
    }
    return 0;
}

template <class MapType, class Comparator>
void MapDiff(const MapType &lhs, const MapType &rhs, Comparator comp,
             std::stringstream *ss) {
    typename MapType::const_iterator iter1 = lhs.begin();
    typename MapType::const_iterator iter2 = rhs.begin();

    while (iter1 != lhs.end() && iter2 != rhs.end()) {
        if (iter1->first < iter2->first) {
            *ss << " -" << iter1->first;
            ++iter1;
            continue;
        }
        if (iter2->first < iter1->first) {
            *ss << " +" << iter2->first;
            ++iter2;
            continue;
        }

        int result = comp(iter1->second, iter2->second);
        if (result) {
            *ss << " ^" << iter1->first;
        }

        ++iter1;
        ++iter2;
    }

    for (; iter1 != lhs.end(); ++iter1) {
        *ss << " -" << iter1->first;
    }
    for (; iter2 != rhs.end(); ++iter2) {
        *ss << " +" << iter2->first;
    }
}

int CustomAttributesCompare(const std::vector<autogen::KeyValuePair> &lhs,
                            const std::vector<autogen::KeyValuePair> &rhs) {
    autogen::KeyValuePairs::const_iterator iter1 = lhs.begin();
    autogen::KeyValuePairs::const_iterator iter2 = rhs.begin();
    int ret = 0;

    while (iter1 != lhs.end() && iter2 != rhs.end()) {
        if ((ret = iter1->key.compare(iter2->key)) != 0) {
            return ret;
        }
        if ((ret = iter1->value.compare(iter2->value)) != 0) {
            return ret;
        }
        ++iter1;
        ++iter2;
    }

    if (iter1 != lhs.end()) {
        return 1;
    }
    if (iter2 != rhs.end()) {
        return -1;
    }
    return 0;
}

void CustomAttributesDiff(const std::vector<autogen::KeyValuePair> &lhs,
                          const std::vector<autogen::KeyValuePair> &rhs,
                          std::stringstream *ss) {
    autogen::KeyValuePairs::const_iterator iter1 = lhs.begin();
    autogen::KeyValuePairs::const_iterator iter2 = rhs.begin();

    while (iter1 != lhs.end()) {
        *ss << " -" << iter1->key << ": " << iter1->value;
        ++iter1;
    }
    while (iter2 != rhs.end()) {
        *ss << " +" << iter2->key << ": " << iter2->value;
        ++iter2;
    }
}

#define COMPARE_PROPERTY(Xname) \
    do {                        \
        int res = compare(Xname, rhs.Xname);    \
        if (res != 0) {         \
            return res;         \
        }                       \
    } while(0)

int LBPoolInfo::CompareTo(const LBPoolInfo &rhs) const {
    COMPARE_PROPERTY(pool_.admin_state);
    COMPARE_PROPERTY(pool_.protocol);
    COMPARE_PROPERTY(pool_.loadbalancer_method);

    COMPARE_PROPERTY(vip_.address);
    COMPARE_PROPERTY(vip_.admin_state);
    COMPARE_PROPERTY(vip_.protocol);
    COMPARE_PROPERTY(vip_.protocol_port);
    COMPARE_PROPERTY(vip_.connection_limit);
    COMPARE_PROPERTY(vip_.persistence_cookie_name);
    COMPARE_PROPERTY(vip_.persistence_type);

    int result = MapCompare(members_, rhs.members_, CompareMember);
    if (result) {
        return result;
    }

    result = MapCompare(monitors_, rhs.monitors_, CompareMonitor);
    if (result) {
        return result;
    }

    result = CustomAttributesCompare(custom_attributes_, rhs.custom_attributes_);
    return result;
}

#undef COMPARE_PROPERTY

#define DIFF_PROPERTY(Xss, Xname)\
    do {                \
        if (compare(Xname, current->Xname)) {                           \
            Xss << " " << #Xname << " -" << Xname << " +" << current->Xname; \
        }               \
    } while (0)

std::string LBPoolInfo::DiffString(
    const LBPoolInfo *current) const {
    std::stringstream ss;
    if (current == NULL) {
        ss << "previous: NULL";
        return ss.str();
    }

    DIFF_PROPERTY(ss, pool_.admin_state);
    DIFF_PROPERTY(ss, pool_.protocol);
    DIFF_PROPERTY(ss, pool_.loadbalancer_method);

    DIFF_PROPERTY(ss, vip_.address);
    DIFF_PROPERTY(ss, vip_.admin_state);
    DIFF_PROPERTY(ss, vip_.protocol);
    DIFF_PROPERTY(ss, vip_.protocol_port);
    DIFF_PROPERTY(ss, vip_.connection_limit);
    DIFF_PROPERTY(ss, vip_.persistence_cookie_name);
    DIFF_PROPERTY(ss, vip_.persistence_type);

    ss << " Members:";
    MapDiff(current->members_, members_, CompareMember, &ss);
    ss << " Monitors:";
    MapDiff(current->monitors_, monitors_, CompareMonitor, &ss);
    ss << " Custom Attributes:";
    CustomAttributesDiff(current->custom_attributes_, custom_attributes_, &ss);

    return ss.str();
}

#undef DIFF_PROPERTY
