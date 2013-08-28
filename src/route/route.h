/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_route_h
#define ctrlplane_route_h

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>

#include "db/db_entry.h"
#include "route/path.h"

class Route : public DBEntry {
public:
    typedef boost::intrusive::member_hook<Path, 
            boost::intrusive::list_member_hook<>, 
            &Path::node_> PathListMember; 

    typedef boost::intrusive::list<Path, PathListMember> PathList;
    typedef bool (*Compare)(const Path &path1, const Path &path2);

    Route();

    virtual ~Route();

    virtual int CompareTo(const Route &rhs) const = 0;

    bool operator<(const Route &rhs) const {
        int res = CompareTo(rhs);
        return res < 0;
    }

    // Selected path
    const Path *front() const;

    // Insert a path
    void insert(const Path *path);

    // Remove a path
    void remove(const Path *path);

    // Sort paths based on compare function.
    void Sort(Compare compare, const Path *prev_front);

    const PathList &GetPathList() const {
        return path_;
    }

    PathList &GetPathList() {
        return path_;
    }

private:
    // Path List
    PathList path_;

    DISALLOW_COPY_AND_ASSIGN(Route);
};


#endif
