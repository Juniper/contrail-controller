//
// Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
//

#include <set>
#include <vector>
#include <tbb/atomic.h>
#include <boost/function.hpp>
#include <base/util.h>
#include "watermark.h"

WaterMarkTuple::WaterMarkTuple() :
    hwater_index_(-1),
    lwater_index_(-1) {
    hwater_mark_set_ = false;
    lwater_mark_set_ = false;
}

WaterMarkTuple::~WaterMarkTuple() {
}

void WaterMarkTuple::SetHighWaterMark(const WaterMarkInfos &high_water) {
    // Eliminate duplicates and sort by converting to set
    std::set<WaterMarkInfo> hwater_set(high_water.begin(),
        high_water.end());
    // Update both high and low water mark indexes
    SetWaterMarkIndexes(-1, -1);
    high_water_ = WaterMarkInfos(hwater_set.begin(), hwater_set.end());
    hwater_mark_set_ = true;
}

void WaterMarkTuple::SetHighWaterMark(const WaterMarkInfo& hwm_info) {
    // Eliminate duplicates and sort by converting to set
    std::set<WaterMarkInfo> hwater_set(high_water_.begin(),
        high_water_.end());
    hwater_set.insert(hwm_info);
    // Update both high and low water mark indexes
    SetWaterMarkIndexes(-1, -1);
    high_water_ = WaterMarkInfos(hwater_set.begin(), hwater_set.end());
    hwater_mark_set_ = true;
}

void WaterMarkTuple::ResetHighWaterMark() {
    // Update both high and low water mark indexes
    SetWaterMarkIndexes(-1, -1);
    high_water_.clear();
    hwater_mark_set_ = false;
}

WaterMarkInfos WaterMarkTuple::GetHighWaterMark() const {
    return high_water_;
}

void WaterMarkTuple::SetLowWaterMark(const WaterMarkInfos &low_water) {
    // Eliminate duplicates and sort by converting to set
    std::set<WaterMarkInfo> lwater_set(low_water.begin(),
        low_water.end());
    // Update both high and low water mark indexes
    SetWaterMarkIndexes(-1, -1);
    low_water_ = WaterMarkInfos(lwater_set.begin(), lwater_set.end());
    lwater_mark_set_ = true;
}

void WaterMarkTuple::SetLowWaterMark(const WaterMarkInfo& lwm_info) {
    // Eliminate duplicates and sort by converting to set
    std::set<WaterMarkInfo> lwater_set(low_water_.begin(),
        low_water_.end());
    lwater_set.insert(lwm_info);
    // Update both high and low water mark indexes
    SetWaterMarkIndexes(-1, -1);
    low_water_ = WaterMarkInfos(lwater_set.begin(), lwater_set.end());
    lwater_mark_set_ = true;
}

void WaterMarkTuple::ResetLowWaterMark() {
    // Update both high and low water mark indexes
    SetWaterMarkIndexes(-1, -1);
    low_water_.clear();
    lwater_mark_set_ = false;
}

WaterMarkInfos WaterMarkTuple::GetLowWaterMark() const {
    return low_water_;
}

void WaterMarkTuple::GetWaterMarkIndexes(int *hwater_index,
                                         int *lwater_index) const {
    *hwater_index = hwater_index_;
    *lwater_index = lwater_index_;
}

void WaterMarkTuple::SetWaterMarkIndexes(int hwater_index,
                                         int lwater_index) {
    hwater_index_ = hwater_index;
    lwater_index_ = lwater_index;
}

void WaterMarkTuple::ProcessWaterMarks(size_t in_count,
                                       size_t curr_count) {
    if (in_count < curr_count)
        ProcessLowWaterMarks(in_count);
    else
        ProcessHighWaterMarks(in_count);
}

bool WaterMarkTuple::AreWaterMarksSet() const {
    return hwater_mark_set_ || lwater_mark_set_;
}

void WaterMarkTuple::ProcessHighWaterMarks(size_t count) {
    if (!hwater_mark_set_ || high_water_.size() == 0) {
        return;
    }
    // Are we crossing any new high water marks ? Assumption here is that
    // the vector is sorted in ascending order of the high water
    // mark counts. Upper bound finds first element that is greater than
    // count.
    WaterMarkInfos::const_iterator ubound(std::upper_bound(
        high_water_.begin(), high_water_.end(),
        WaterMarkInfo(count, NULL)));
    // Get high and low water mark indexes
    int hwater_index, lwater_index;
    GetWaterMarkIndexes(&hwater_index, &lwater_index);
    // If the first element is greater than count, then we have not
    // yet crossed any water marks
    if (ubound == high_water_.begin()) {
        SetWaterMarkIndexes(-1, lwater_index);
        return;
    }
    int nhwater_index(ubound - high_water_.begin() - 1);
    if (hwater_index == nhwater_index) {
        return;
    }
    // Update the high and low water indexes
    SetWaterMarkIndexes(nhwater_index, nhwater_index + 1);
    const WaterMarkInfo &wm_info(high_water_[nhwater_index]);
    assert(count >= wm_info.count_);
    wm_info.cb_(count);
}

void WaterMarkTuple::ProcessLowWaterMarks(size_t count) {
    if (!lwater_mark_set_ || low_water_.size() == 0) {
        return;
    }
    // Are we crossing any new low water marks ? Assumption here is that
    // the vector is sorted in ascending order of the low water
    // mark counts. Lower bound finds first element that is not less than
    // count.
    WaterMarkInfos::const_iterator lbound(std::lower_bound(
        low_water_.begin(), low_water_.end(),
        WaterMarkInfo(count, NULL)));
    // If no element is not less than count we have not yet crossed
    // any low water marks
    if (lbound == low_water_.end()) {
        return;
    }
    int nlwater_index(lbound - low_water_.begin());
    // Get the high and low water indexes
    int hwater_index, lwater_index;
    GetWaterMarkIndexes(&hwater_index, &lwater_index);
    if (lwater_index == nlwater_index) {
        return;
    }
    // Update the high and low water indexes
    SetWaterMarkIndexes(nlwater_index - 1, nlwater_index);
    const WaterMarkInfo &wm_info(low_water_[nlwater_index]);
    assert(count <= wm_info.count_);
    wm_info.cb_(count);
}
