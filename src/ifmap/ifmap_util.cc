/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_util.h"

#include <boost/foreach.hpp>
#include <boost/tuple/tuple.hpp>
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_table.h"

using namespace std;
using boost::tie;

static pair<string, string> split(const string &str, char sep) {
    string left, right;
    size_t loc = str.find(sep);
    if (loc != string::npos) {
        left.append(str, 0, loc);
        right.append(str, loc + 1, str.size() - (loc + 1));
    }
    return make_pair(left, right);
}

static bool TypeMatchExpr(const DBGraphVertex *src, const DBGraphVertex *tgt,
                          const string &excl) {
    string v, vtype;
    tie(v, vtype) = split(excl, '=');
    string idname;
    if (v == "source") {
        const IFMapNode *srcnode = static_cast<const IFMapNode *>(src);
        idname = srcnode->table()->Typename();
    } else if (v == "target") {
        const IFMapNode *tgtnode = static_cast<const IFMapNode *>(tgt);
        idname = tgtnode->table()->Typename();
    } else {
        return false;
    }
    return (idname == vtype);
}
static bool TypeMatch(const DBGraphVertex *src, const DBGraphVertex *tgt,
                      const string &excl) {
    size_t comma = excl.find(',');
    if (comma != string::npos) {
        string left, right;
        tie(left, right) = split(excl, ',');
        return TypeMatchExpr(src, tgt, left) && TypeMatchExpr(src, tgt, right);
    }
    return TypeMatchExpr(src, tgt, excl);
}

bool IFMapTypenameFilter::VertexFilter(const DBGraphVertex *vertex) const {
    const IFMapNode *node = static_cast<const IFMapNode *>(vertex);
    BOOST_FOREACH(const string &excl, exclude_vertex) {
        if (node->table()->Typename() == excl) {
            return false;
        }
    }
    return true;
}

bool IFMapTypenameFilter::EdgeFilter(const DBGraphVertex *source,
                                     const DBGraphVertex *target,
                                     const DBGraphEdge *edge) const {
    BOOST_FOREACH(const string &excl, exclude_edge) {
        if (TypeMatch(source, target, excl)) {
            return false;
        }
    }
    return true;
}

// Return true if the node-type is in the white list
bool IFMapTypenameWhiteList::VertexFilter(const DBGraphVertex *vertex) const {
    const IFMapNode *node = static_cast<const IFMapNode *>(vertex);
    if (include_vertex.find(node->table()->Typename()) != include_vertex.end()) {
        return true;
    } else {
        return false;
    }
}

// The whitelist for links, 'include_edge', has strings formatted as:
//      "source=virtual-router,target=virtual-machine"
// Create a string of the same format from the source/target nodes and check if
// its exists in the whitelist. 
// Return true if the link is part of the white list; false otherwise.
bool IFMapTypenameWhiteList::EdgeFilter(const DBGraphVertex *source,
                                        const DBGraphVertex *target,
                                        const DBGraphEdge *edge) const {
    const IFMapNode *srcnode = static_cast<const IFMapNode *>(source);
    const IFMapNode *tgtnode = static_cast<const IFMapNode *>(target);

    string source_type = srcnode->table()->Typename();
    string target_type = tgtnode->table()->Typename();
    string check_string = "source=" + source_type + ",target=" + target_type;
    if (include_edge.find(check_string) != include_edge.end()) {
        return true;
    } else {
        return false;
    }
}

