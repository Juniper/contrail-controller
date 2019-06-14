/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_aspath.h"

#include <boost/assign/list_of.hpp>
#include <sstream>

#include "bgp/bgp_proto.h"

using boost::assign::list_of;
using std::copy;
using std::ostringstream;
using std::string;
using std::vector;

//
// Return the left most AS.
//
as2_t AsPathSpec::AsLeftMost() const {
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
bool AsPathSpec::AsLeftMostMatch(as2_t as) const {
    if (path_segments.empty())
        return false;
    if (path_segments[0]->path_segment.empty())
        return false;
    return (path_segments[0]->path_segment[0] == as);
}

//
// Return the left most public AS.
//
as2_t AsPathSpec::AsLeftMostPublic() const {
    for (size_t i = 0; i < path_segments.size(); ++i) {
        PathSegment *ps = path_segments[i];
        for (size_t j = 0; j < path_segments[i]->path_segment.size(); ++j) {
            if (!AsIsPrivate(ps->path_segment[j]))
                return ps->path_segment[j];
        }
    }
    return 0;
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
bool AsPathSpec::AsPathLoop(as2_t as, uint8_t max_loop_count) const {
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
AsPathSpec *AsPathSpec::Add(as2_t asn) const {
    vector<as2_t> asn_list = list_of(asn);
    return Add(asn_list);
}

//
// Create a new AsPathSpec by prepending the given vector of asns at the
// beginning.
//
AsPathSpec *AsPathSpec::Add(const vector<as_t> &asn_list) const {
    AsPathSpec *new_spec = new AsPathSpec;
    PathSegment *ps = new PathSegment;
    ps->path_segment_type = PathSegment::AS_SEQUENCE;
    for (vector<as_t>::const_iterator it = asn_list.begin(); it !=
                      asn_list.end(); it++) {
        if ((*it) <= AS2_MAX)
            ps->path_segment.push_back((as2_t)(*it));
    }
    size_t first = 0;
    size_t last = path_segments.size();
    if (last &&
        path_segments[0]->path_segment_type == PathSegment::AS_SEQUENCE &&
        path_segments[0]->path_segment.size() + asn_list.size() <= 255) {
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
    for (size_t idx = first; idx < last; ++idx) {
        PathSegment *ps = new PathSegment;
        *ps = *path_segments[idx];
        new_spec->path_segments.push_back(ps);
    }
    return new_spec;
}

//
// Create a new AsPathSpec by prepending the given vector of asns at the
// beginning.
//
AsPathSpec *AsPathSpec::Add(const vector<as2_t> &asn_list) const {
    AsPathSpec *new_spec = new AsPathSpec;
    PathSegment *ps = new PathSegment;
    ps->path_segment_type = PathSegment::AS_SEQUENCE;
    ps->path_segment = asn_list;
    size_t first = 0;
    size_t last = path_segments.size();
    if (last &&
        path_segments[0]->path_segment_type == PathSegment::AS_SEQUENCE &&
        path_segments[0]->path_segment.size() + asn_list.size() <= 255) {
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
    for (size_t idx = first; idx < last; ++idx) {
        PathSegment *ps = new PathSegment;
        *ps = *path_segments[idx];
        new_spec->path_segments.push_back(ps);
    }
    return new_spec;
}

//
// Create a new AsPathSpec by replacing the old asn with given asn.
//
AsPathSpec *AsPathSpec::Replace(as2_t old_asn, as2_t asn) const {
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
// Create a new AsPathSpec by removing private asns.  Stop looking for private
// asns when we encounter the first public asn or the peer asn.
// If all is true, remove all private asns i.e. do not stop when we encounter
// the first public asn or the peer asn.
// If asn is non-zero, replace private asns instead of removing them. Use the
// nearest public asn as the replacement value.  If we haven't found a public
// asn, then use the given asn.
// If peer asn is non-zero, do not remove/replace it.
//
AsPathSpec *AsPathSpec::RemovePrivate(bool all, as2_t asn, as2_t peer_asn) const {
    bool remove_replace_done = false;
    AsPathSpec *new_spec = new AsPathSpec;
    for (size_t i = 0; i < path_segments.size(); ++i) {
        PathSegment *ps = path_segments[i];
        PathSegment *new_ps = new PathSegment;

        // We've already removed/replaced all private asns that we can.
        // Copy the entire segment instead of copying one as2_t at a time.
        if (remove_replace_done) {
            *new_ps = *ps;
            new_spec->path_segments.push_back(new_ps);
            continue;
        }

        // Examine each as2_t in the path segment to build a modified version.
        // Note down that we're done removing/replacing when we see a public
        // asn or the peer asn.
        new_ps->path_segment_type = ps->path_segment_type;
        for (size_t j = 0; j < ps->path_segment.size(); ++j) {
            if (remove_replace_done ||
                !AsIsPrivate(ps->path_segment[j]) ||
                ps->path_segment[j] == peer_asn) {
                new_ps->path_segment.push_back(ps->path_segment[j]);
                remove_replace_done = !all;
                if (asn && !AsIsPrivate(ps->path_segment[j])) {
                    asn = ps->path_segment[j];
                }
            } else if (asn) {
                new_ps->path_segment.push_back(asn);
            }
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

//
// Return the left most AS.
//
as_t AsPath4ByteSpec::AsLeftMost() const {
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
bool AsPath4ByteSpec::AsLeftMostMatch(as_t as) const {
    if (path_segments.empty())
        return false;
    if (path_segments[0]->path_segment.empty())
        return false;
    return (path_segments[0]->path_segment[0] == as);
}

//
// Return the left most public AS.
//
as_t AsPath4ByteSpec::AsLeftMostPublic() const {
    for (size_t i = 0; i < path_segments.size(); ++i) {
        PathSegment *ps = path_segments[i];
        for (size_t j = 0; j < path_segments[i]->path_segment.size(); ++j) {
            if (!AsIsPrivate(ps->path_segment[j]))
                return ps->path_segment[j];
        }
    }
    return 0;
}

int AsPath4ByteSpec::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    const AsPath4ByteSpec &rhs = static_cast<const AsPath4ByteSpec &>(rhs_attr);
    KEY_COMPARE(path_segments.size(), rhs.path_segments.size());

    for (size_t i = 0; i < path_segments.size(); i++) {
        int ret = path_segments[i]->CompareTo(*rhs.path_segments[i]);
        if (ret != 0) return ret;
    }
    return 0;
}

void AsPath4ByteSpec::ToCanonical(BgpAttr *attr) {
    attr->set_aspath_4byte(this);
}

string AsPath4ByteSpec::ToString() const {
    ostringstream oss;

    for (size_t i = 0; i < path_segments.size(); i++) {
        if (i != 0) oss << " ";
        switch (path_segments[i]->path_segment_type) {
        case AsPath4ByteSpec::PathSegment::AS_SET:
            oss << "{";
            for (size_t j = 0; j < path_segments[i]->path_segment.size(); j++) {
                if (j != 0) oss << " ";
                oss << path_segments[i]->path_segment[j];
            }
            oss << "}";
            break;
        case AsPath4ByteSpec::PathSegment::AS_SEQUENCE:
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

size_t AsPath4ByteSpec::EncodeLength() const {
    size_t sz = 0;
    for (size_t i = 0; i < path_segments.size(); i++) {
        sz += 2;
        sz += path_segments[i]->path_segment.size() * 4;
    }
    return sz;
}

//
// Check AsPath4ByteSpec for loops for the given as.
// Return true if the number of occurrences of as exceeds given max loop count.
//
bool AsPath4ByteSpec::AsPathLoop(as_t as, uint8_t max_loop_count) const {
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
// Create a new AsPath4ByteSpec by prepending the given asn at the beginning.
//
AsPath4ByteSpec *AsPath4ByteSpec::Add(as_t asn) const {
    vector<as_t> asn_list = list_of(asn);
    return Add(asn_list);
}

//
// Create a new AsPath4ByteSpec by prepending the given vector of asns at the
// beginning.
//
AsPath4ByteSpec *AsPath4ByteSpec::Add(const vector<as_t> &asn_list) const {
    AsPath4ByteSpec *new_spec = new AsPath4ByteSpec;
    PathSegment *ps = new PathSegment;
    ps->path_segment_type = PathSegment::AS_SEQUENCE;
    ps->path_segment = asn_list;
    size_t first = 0;
    size_t last = path_segments.size();
    if (last &&
        path_segments[0]->path_segment_type == PathSegment::AS_SEQUENCE &&
        path_segments[0]->path_segment.size() + asn_list.size() <= 255) {
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
    for (size_t idx = first; idx < last; ++idx) {
        PathSegment *ps = new PathSegment;
        *ps = *path_segments[idx];
        new_spec->path_segments.push_back(ps);
    }
    return new_spec;
}

//
// Create a new AsPath4ByteSpec by replacing the old asn with given asn.
//
AsPath4ByteSpec *AsPath4ByteSpec::Replace(as_t old_asn, as_t asn) const {
    AsPath4ByteSpec *new_spec = new AsPath4ByteSpec(*this);
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
// Create a new AsPath4ByteSpec by removing private asns.  Stop looking for private
// asns when we encounter the first public asn or the peer asn.
// If all is true, remove all private asns i.e. do not stop when we encounter
// the first public asn or the peer asn.
// If asn is non-zero, replace private asns instead of removing them. Use the
// nearest public asn as the replacement value.  If we haven't found a public
// asn, then use the given asn.
// If peer asn is non-zero, do not remove/replace it.
//
AsPath4ByteSpec *AsPath4ByteSpec::RemovePrivate(bool all, as_t asn, as_t peer_asn) const {
    bool remove_replace_done = false;
    AsPath4ByteSpec *new_spec = new AsPath4ByteSpec;
    for (size_t i = 0; i < path_segments.size(); ++i) {
        PathSegment *ps = path_segments[i];
        PathSegment *new_ps = new PathSegment;

        // We've already removed/replaced all private asns that we can.
        // Copy the entire segment instead of copying one as_t at a time.
        if (remove_replace_done) {
            *new_ps = *ps;
            new_spec->path_segments.push_back(new_ps);
            continue;
        }

        // Examine each as_t in the path segment to build a modified version.
        // Note down that we're done removing/replacing when we see a public
        // asn or the peer asn.
        new_ps->path_segment_type = ps->path_segment_type;
        for (size_t j = 0; j < ps->path_segment.size(); ++j) {
            if (remove_replace_done ||
                !AsIsPrivate(ps->path_segment[j]) ||
                ps->path_segment[j] == peer_asn) {
                new_ps->path_segment.push_back(ps->path_segment[j]);
                remove_replace_done = !all;
                if (asn && !AsIsPrivate(ps->path_segment[j])) {
                    asn = ps->path_segment[j];
                }
            } else if (asn) {
                new_ps->path_segment.push_back(asn);
            }
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

void AsPath4Byte::Remove() {
    aspath_db_->Delete(this);
}

AsPath4ByteDB::AsPath4ByteDB(BgpServer *server) {
}

//
// Return the left most AS.
//
as_t As4PathSpec::AsLeftMost() const {
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
bool As4PathSpec::AsLeftMostMatch(as_t as) const {
    if (path_segments.empty())
        return false;
    if (path_segments[0]->path_segment.empty())
        return false;
    return (path_segments[0]->path_segment[0] == as);
}

//
// Return the left most public AS.
//
as_t As4PathSpec::AsLeftMostPublic() const {
    for (size_t i = 0; i < path_segments.size(); ++i) {
        PathSegment *ps = path_segments[i];
        for (size_t j = 0; j < path_segments[i]->path_segment.size(); ++j) {
            if (!AsIsPrivate(ps->path_segment[j]))
                return ps->path_segment[j];
        }
    }
    return 0;
}

int As4PathSpec::CompareTo(const BgpAttribute &rhs_attr) const {
    int ret = BgpAttribute::CompareTo(rhs_attr);
    if (ret != 0) return ret;
    const As4PathSpec &rhs = static_cast<const As4PathSpec &>(rhs_attr);
    KEY_COMPARE(path_segments.size(), rhs.path_segments.size());

    for (size_t i = 0; i < path_segments.size(); i++) {
        int ret = path_segments[i]->CompareTo(*rhs.path_segments[i]);
        if (ret != 0) return ret;
    }
    return 0;
}

void As4PathSpec::ToCanonical(BgpAttr *attr) {
    attr->set_as4_path(this);
}

string As4PathSpec::ToString() const {
    ostringstream oss;

    for (size_t i = 0; i < path_segments.size(); i++) {
        if (i != 0) oss << " ";
        switch (path_segments[i]->path_segment_type) {
        case As4PathSpec::PathSegment::AS_SET:
            oss << "{";
            for (size_t j = 0; j < path_segments[i]->path_segment.size(); j++) {
                if (j != 0) oss << " ";
                oss << path_segments[i]->path_segment[j];
            }
            oss << "}";
            break;
        case As4PathSpec::PathSegment::AS_SEQUENCE:
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

size_t As4PathSpec::EncodeLength() const {
    size_t sz = 0;
    for (size_t i = 0; i < path_segments.size(); i++) {
        sz += 2;
        sz += path_segments[i]->path_segment.size() * 4;
    }
    return sz;
}

//
// Check As4PathSpec for loops for the given as.
// Return true if the number of occurrences of as exceeds given max loop count.
//
bool As4PathSpec::AsPathLoop(as_t as, uint8_t max_loop_count) const {
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
// Create a new As4PathSpec by prepending the given asn at the beginning.
//
As4PathSpec *As4PathSpec::Add(as_t asn) const {
    vector<as_t> asn_list = list_of(asn);
    return Add(asn_list);
}

//
// Create a new As4PathSpec by prepending the given vector of asns at the
// beginning.
//
As4PathSpec *As4PathSpec::Add(const vector<as_t> &asn_list) const {
    As4PathSpec *new_spec = new As4PathSpec;
    PathSegment *ps = new PathSegment;
    ps->path_segment_type = PathSegment::AS_SEQUENCE;
    ps->path_segment = asn_list;
    size_t first = 0;
    size_t last = path_segments.size();
    if (last &&
        path_segments[0]->path_segment_type == PathSegment::AS_SEQUENCE &&
        path_segments[0]->path_segment.size() + asn_list.size() <= 255) {
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
    for (size_t idx = first; idx < last; ++idx) {
        PathSegment *ps = new PathSegment;
        *ps = *path_segments[idx];
        new_spec->path_segments.push_back(ps);
    }
    return new_spec;
}

//
// Create a new As4PathSpec by replacing the old asn with given asn.
//
As4PathSpec *As4PathSpec::Replace(as_t old_asn, as_t asn) const {
    As4PathSpec *new_spec = new As4PathSpec(*this);
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
// Create a new As4PathSpec by removing private asns.  Stop looking for private
// asns when we encounter the first public asn or the peer asn.
// If all is true, remove all private asns i.e. do not stop when we encounter
// the first public asn or the peer asn.
// If asn is non-zero, replace private asns instead of removing them. Use the
// nearest public asn as the replacement value.  If we haven't found a public
// asn, then use the given asn.
// If peer asn is non-zero, do not remove/replace it.
//
As4PathSpec *As4PathSpec::RemovePrivate(bool all, as_t asn, as_t peer_asn) const {
    bool remove_replace_done = false;
    As4PathSpec *new_spec = new As4PathSpec;
    for (size_t i = 0; i < path_segments.size(); ++i) {
        PathSegment *ps = path_segments[i];
        PathSegment *new_ps = new PathSegment;

        // We've already removed/replaced all private asns that we can.
        // Copy the entire segment instead of copying one as_t at a time.
        if (remove_replace_done) {
            *new_ps = *ps;
            new_spec->path_segments.push_back(new_ps);
            continue;
        }

        // Examine each as_t in the path segment to build a modified version.
        // Note down that we're done removing/replacing when we see a public
        // asn or the peer asn.
        new_ps->path_segment_type = ps->path_segment_type;
        for (size_t j = 0; j < ps->path_segment.size(); ++j) {
            if (remove_replace_done ||
                !AsIsPrivate(ps->path_segment[j]) ||
                ps->path_segment[j] == peer_asn) {
                new_ps->path_segment.push_back(ps->path_segment[j]);
                remove_replace_done = !all;
                if (asn && !AsIsPrivate(ps->path_segment[j])) {
                    asn = ps->path_segment[j];
                }
            } else if (asn) {
                new_ps->path_segment.push_back(asn);
            }
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

void As4Path::Remove() {
    aspath_db_->Delete(this);
}

As4PathDB::As4PathDB(BgpServer *server) {
}
