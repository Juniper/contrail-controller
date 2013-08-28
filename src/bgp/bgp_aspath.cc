/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_aspath.h"

#include "base/util.h"
#include "bgp/bgp_proto.h"

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

std::string AsPathSpec::ToString() const {
    char repr[1024];
    snprintf(repr, sizeof(repr), 
             "AS_PATH: %d", (uint32_t)path_segments.size());

    for (size_t i = 0; i < path_segments.size(); i++) {
        char type[10];
        snprintf(type, sizeof(type), 
                 " [ %1d %3d ", path_segments[i]->path_segment_type, 
                 (uint32_t)path_segments[i]->path_segment.size());
        strcat(repr, type);
        for (size_t j = 0; j < path_segments[i]->path_segment.size(); j++) {
            char path[10];
            snprintf(path, sizeof(path), 
                     " %d ", (uint32_t)path_segments[i]->path_segment[j]);
            strcat(repr, path);
        }
        snprintf(type, sizeof(type), "]");
        strcat(repr, type);
    }

    return std::string(repr);
}

bool AsPathSpec::AsPathLoop(as_t as) const {
    for (size_t i = 0; i < path_segments.size(); i++)
        for (size_t j = 0; j < path_segments[i]->path_segment.size(); j++)
            if (path_segments[i]->path_segment[j] == as)
                return true;
    return false;
}

// Create a new AsPathSpec by prepending the given asn at the beginning
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
        std::copy(path_segments[0]->path_segment.begin(),
                path_segments[0]->path_segment.end(),
                back_inserter(ps->path_segment));
        new_spec->path_segments.push_back(ps);
        first++;
    } else {
        new_spec->path_segments.push_back(ps);
    }
    if (first == last) return new_spec;
    for (int i = first; i < last; i++) {
        PathSegment *ps = new PathSegment;
        *ps = *path_segments[i];
        new_spec->path_segments.push_back(ps);
    }
    return new_spec;
}

void AsPath::Remove() {
    aspath_db_->Delete(this);
}

AsPathDB::AsPathDB(BgpServer *server) : server_(server) {
}

