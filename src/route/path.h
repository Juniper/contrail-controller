/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_path_h
#define ctrlplane_path_h

#include <boost/intrusive/list.hpp>

class Path {
public:
    Path() : time_stamp_usecs_(0) {
    }
    virtual ~Path() { }
    virtual std::string ToString() const = 0;

    const uint64_t time_stamp_usecs() const { return time_stamp_usecs_; }
    void set_time_stamp_usecs(uint64_t time_stamp_usecs) {
        time_stamp_usecs_ = time_stamp_usecs;
    }
private:
    friend class Route;
    boost::intrusive::list_member_hook<> node_;
    uint64_t time_stamp_usecs_;
    DISALLOW_COPY_AND_ASSIGN(Path);
};

#endif
