/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_aspath.h"

#include <sstream>

#include "bgp/bgp_proto.h"

using std::copy;
using std::ostringstream;
using std::string;

//
// Return the left most AS.
//
as_t AsPathSpec::AsLeftMost() const {
    if (path_segments.empty())
        return 0;
    if (path_segments[0]->path_segment_type == PathSegment::AS_SET)
        return 0;
    if (path_segments[0]->path_segment.empty())
        return 0;
    return (path_segments[0]->path_segment[0]);
}

//
// Return true if left most AS matches the input.
//
bool AsPathSpec::AsLeftMostMatch(as_t as) const {
    if (path_segments.empty())
        return false;
    if (path_segments[0]->path_segment.empty())
        return false;
    return (path_segments[0]->path_segment[0] == as);
}

int AsPathSpec::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    const AsPathSpec &rhs = static_cast<const AsPathSpec &>(rhs_attr);
    KEY_COMPARE(path_segments.size(), rhs.path_segments.size());

    for (size_t i = 0; i < path_segments.size(); i++) {
        int ret = path_segments[i]->CompareTo(*rhs.path_segments[i]);
        if (ret != 0) return ret;
    }
    return 0;
}

void AsPathSpec::ToCanonical(BgpAttr *attr) {
    attr->set_as_path(this);
}

string AsPathSpec::ToString() const {
    ostringstream oss;

    for (size_t i = 0; i < path_segments.size(); i++) {
        if (i != 0) oss << " ";
        switch (path_segments[i]->path_segment_type) {
        case AsPathSpec::PathSegment::AS_SET:
            oss << "{";
            for (size_t j = 0; j < path_segments[i]->path_segment.size(); j++) {
                if (j != 0) oss << " ";
                oss << path_segments[i]->path_segment[j];
            }
            oss << "}";
            break;
        case AsPathSpec::PathSegment::AS_SEQUENCE:
            for (size_t j = 0; j < path_segments[i]->path_segment.size(); j++) {
                if (j != 0) oss << " ";
                oss << path_segments[i]->path_segment[j];
            }
            break;
        default:
            break;
        }
    }

    return oss.str();
}

size_t AsPathSpec::EncodeLength() const {
    size_t sz = 0;
    for (size_t i = 0; i < path_segments.size(); i++) {
        sz += 2;
        sz += path_segments[i]->path_segment.size() * 2;
    }
    return sz;
}

//
// Check AsPathSpec for loops for the given as.
// Return true if the number of occurrences of as exceeds given max loop count.
//
bool AsPathSpec::AsPathLoop(as_t as, uint8_t max_loop_count) const {
    uint8_t loop_count = 0;
    for (size_t i = 0; i < path_segments.size(); ++i) {
        for (size_t j = 0; j < path_segments[i]->path_segment.size(); ++j) {
            if (path_segments[i]->path_segment[j] == as &&
                ++loop_count > max_loop_count) {
                return true;
            }
        }
    }
    return false;
}

//
// Create a new AsPathSpec by prepending the given asn at the beginning.
//
AsPathSpec *AsPathSpec::Add(as_t asn) const {
    AsPathSpec *new_spec = new AsPathSpec;
    PathSegment *ps = new PathSegment;
    ps->path_segment_type = PathSegment::AS_SEQUENCE;
    ps->path_segment.push_back(asn);
    int first = 0;
    int last = path_segments.size();
    if (last &&
        path_segments[0]->path_segment_type == PathSegment::AS_SEQUENCE &&
        path_segments[0]->path_segment.size() < 255) {
        copy(path_segments[0]->path_segment.begin(),
                path_segments[0]->path_segment.end(),
                back_inserter(ps->path_segment));
        new_spec->path_segments.push_back(ps);
        first++;
    } else {
        new_spec->path_segments.push_back(ps);
    }
    if (first == last)
        return new_spec;
    for (int i = first; i < last; i++) {
        PathSegment *ps = new PathSegment;
        *ps = *path_segments[i];
        new_spec->path_segments.push_back(ps);
    }
    return new_spec;
}

//
// Create a new AsPathSpec by replacing the old asn with given asn.
//
AsPathSpec *AsPathSpec::Replace(as_t old_asn, as_t asn) const {
    AsPathSpec *new_spec = new AsPathSpec(*this);
    for (size_t i = 0; i < new_spec->path_segments.size(); ++i) {
        PathSegment *ps = new_spec->path_segments[i];
        for (size_t j = 0; j < ps->path_segment.size(); ++j) {
            if (ps->path_segment[j] == old_asn)
                ps->path_segment[j] = asn;
        }
    }
    return new_spec;
}

//
// Create a new AsPathSpec by removing private asns. Stop looking for private
// asns when we encounter the first non-private asn or the peer asn.
// If all is true, remove all private asns not just the leftmost one. Also do
// not stop when we encounter the first non-private asn or the peer asn.
// If asn is non-zero then replace private asns instead of removing them.
// If peer asn is non-zero, do not remove/replace it.
//
AsPathSpec *AsPathSpec::RemovePrivate(bool all, as_t asn, as_t peer_asn) const {
    bool remove_replace_done = false;
    AsPathSpec *new_spec = new AsPathSpec;
    for (size_t i = 0; i < path_segments.size(); ++i) {
        PathSegment *ps = path_segments[i];
        PathSegment *new_ps = new PathSegment;

        // We've already removed/replaced the first private asn or have
        // decided not to remove/replace any private asns.
        // Copy the entire segment instead of copying one as_t at a time.
        if (remove_replace_done) {
            *new_ps = *ps;
            new_spec->path_segments.push_back(new_ps);
            continue;
        }

        // Examine each as_t in the path segment to build a modified version.
        // Do not remove/replace non-private asns and peer asn.
        // Otherwise remove/replace private asns as specified.
        new_ps->path_segment_type = ps->path_segment_type;
        for (size_t j = 0; j < ps->path_segment.size(); ++j) {
            if (remove_replace_done ||
                !AsIsPrivate(ps->path_segment[j]) ||
                ps->path_segment[j] == peer_asn) {
                new_ps->path_segment.push_back(ps->path_segment[j]);
            } else if (asn) {
                new_ps->path_segment.push_back(asn);
            }
            remove_replace_done = !all;
        }

        // Get rid of the new path segment if it's empty.
        // Otherwise add it to the new spec.
        if (new_ps->path_segment.empty()) {
            delete new_ps;
        } else {
            new_spec->path_segments.push_back(new_ps);
        }
    }
    return new_spec;
}

void AsPath::Remove() {
    aspath_db_->Delete(this);
}

AsPathDB::AsPathDB(BgpServer *server) {
}

