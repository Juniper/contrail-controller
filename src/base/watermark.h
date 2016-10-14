/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

// watermark.h
// class to handle multiple level high/low watermarks
//
#ifndef __WATERMARK_H__
#define __WATERMARK_H__

#include <iostream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <set>

#include <tbb/atomic.h>
#include <tbb/concurrent_queue.h>
#include <tbb/mutex.h>
#include <tbb/spin_rw_mutex.h>

#include <base/task.h>
#include <base/time_util.h>

typedef boost::function<void (size_t)> WaterMarkChangeCallback;

struct WaterMark {
    WaterMark(size_t count, size_t level, WaterMarkChangeCallback cb) :
        count_(count),
        level_(level),
        cb_(cb) {
    }
    friend inline bool operator<(const WaterMark& lhs,
        const WaterMark& rhs);
    friend inline bool operator==(const WaterMark& lhs,
        const WaterMark& rhs);
    size_t count_;
    size_t level_;
    WaterMarkChangeCallback cb_;
};

inline bool operator<(const WaterMark& lhs, const WaterMark& rhs) {
    return lhs.count_ < rhs.count_;
}

inline bool operator==(const WaterMark& lhs, const WaterMark& rhs) {
    return lhs.count_ == rhs.count_;
}

typedef std::vector<WaterMark> WaterMarks;

class WaterMarksData {
public:
    WaterMarksData() {
        hwater_index_ = -1;
        lwater_index_ = -1;
        hwater_mark_set_ = false;
        lwater_mark_set_ = false;
    }

    ~WaterMarksData() {
        // do we need to destroy mutex?
    }

    void SetHighWaterMark(const WaterMarks &high_water) {
        tbb::mutex::scoped_lock lock(water_mutex_);
        // Eliminate duplicates and sort by converting to set
        std::set<WaterMark> hwater_set(high_water.begin(),
            high_water.end());
        // Update both high and low water mark indexes
        SetWaterMarkIndexes(-1, -1);
        high_water_ = WaterMarks(hwater_set.begin(), hwater_set.end());
        hwater_mark_set_ = true;
    }

    void SetHighWaterMark(const WaterMark& hwm_info) {
        tbb::mutex::scoped_lock lock(water_mutex_);
        // Eliminate duplicates and sort by converting to set
        std::set<WaterMark> hwater_set(high_water_.begin(),
            high_water_.end());
        hwater_set.insert(hwm_info);
        // Update both high and low water mark indexes
        SetWaterMarkIndexes(-1, -1);
        high_water_ = WaterMarks(hwater_set.begin(), hwater_set.end());
        hwater_mark_set_ = true;
    }

    void SetLowWaterMark(const WaterMarks &low_water) {
        tbb::mutex::scoped_lock lock(water_mutex_);
        // Eliminate duplicates and sort by converting to set
        std::set<WaterMark> lwater_set(low_water.begin(),
            low_water.end());
        // Update both high and low water mark indexes
        SetWaterMarkIndexes(-1, -1);
        low_water_ = WaterMarks(lwater_set.begin(), lwater_set.end());
        lwater_mark_set_ = true;
    }

    void SetLowWaterMark(const WaterMark& lwm_info) {
        tbb::mutex::scoped_lock lock(water_mutex_);
        // Eliminate duplicates and sort by converting to set
        std::set<WaterMark> lwater_set(low_water_.begin(),
            low_water_.end());
        lwater_set.insert(lwm_info);
        // Update both high and low water mark indexes
        SetWaterMarkIndexes(-1, -1);
        low_water_ = WaterMarks(lwater_set.begin(), lwater_set.end());
        lwater_mark_set_ = true;
    }

    void GetWaterMarkIndexes(int *hwater_index, int *lwater_index) const {
        *hwater_index = hwater_index_;
        *lwater_index = lwater_index_;
    }

    void SetWaterMarkIndexes(int hwater_index, int lwater_index) {
        hwater_index_ = hwater_index;
        lwater_index_ = lwater_index;
    }

    void ProcessHighWaterMarks(size_t count) {
        if (!hwater_mark_set_ || high_water_.size() == 0) {
            return;
        }
        // Are we crossing any new high water marks ? Assumption here is that
        // the vector is sorted in ascending order of the high water
        // mark counts. Upper bound finds first element that is greater than
        // count.
        WaterMarks::const_iterator ubound(std::upper_bound(
            high_water_.begin(), high_water_.end(),
            WaterMark(count, 0, NULL)));
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
        const WaterMark &wm_info(high_water_[nhwater_index]);
        assert(count >= wm_info.count_);
        wm_info.cb_(wm_info.level_);
    }

    void ProcessLowWaterMarks(size_t count) {
        if (!lwater_mark_set_ || low_water_.size() == 0) {
            return;
        }
        // Are we crossing any new low water marks ? Assumption here is that
        // the vector is sorted in ascending order of the low water
        // mark counts. Lower bound finds first element that is not less than
        // count.
        WaterMarks::const_iterator lbound(std::lower_bound(
            low_water_.begin(), low_water_.end(),
            WaterMark(count, 0, NULL)));
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
        const WaterMark &wm_info(low_water_[nlwater_index]);
        assert(count <= wm_info.count_);
        wm_info.cb_(wm_info.level_);
    }

private:
    WaterMarks high_water_;
    WaterMarks low_water_;
    int hwater_index_;
    int lwater_index_;
    mutable tbb::mutex water_mutex_;
    tbb::atomic<bool> hwater_mark_set_;
    tbb::atomic<bool> lwater_mark_set_;
};

#endif /* __WATERMARK_H__ */
