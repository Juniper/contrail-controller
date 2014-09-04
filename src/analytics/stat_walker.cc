/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "stat_walker.h"
#include "viz_message.h"

using std::string;
using std::vector;
using std::make_pair;

void
StatWalker::Push(const std::string& name,
        const TagMap& tags,
        const DbHandler::AttribMap& attribs) {

    string prename("");
    for (vector<StatNode>::const_iterator ni = nodes_.begin();
            ni != nodes_.end(); ni++) {
        const StatNode& ancestor = *ni;
        if (!prename.empty()) {
            prename.append(".");
        }
        prename.append(ancestor.name);
    }

    StatNode sn;
    sn.name = name;

    if (!prename.empty()) {
        prename.append(".");
    }
    prename.append(sn.name);

    // For both tag names and attribute names, we need to convert
    // from local name to fully-qualified name
    for (TagMap::const_iterator ti = tags.begin();
            ti != tags.end(); ti++) {
        std::string tname;

        VIZD_ASSERT(ti->first.find('.') == string::npos);
        tname = prename + "." + ti->first;

        // For prefixes, the tag prefix name is already fully-qualified
        sn.tags.insert(make_pair(tname, ti->second));
    }
    for (DbHandler::AttribMap::const_iterator ai = attribs.begin();
            ai != attribs.end(); ai++) {
        std::string aname;

        VIZD_ASSERT(ai->first.find('.') == string::npos);
        aname = prename + "." + ai->first;
        sn.attribs.insert(make_pair(aname, ai->second));
    }
    nodes_.push_back(sn);
}

void
StatWalker::FillTag(DbHandler::TagMap *attribs_tag,
        const std::pair<const std::string, TagVal>* ti) {
    DbHandler::Var pval, sval;
    std::string pname, sname;
    if (ti->second.prefix.second.type == DbHandler::INVALID) {
        // There is no prefix supplied
        pname = ti->first;
        pval = ti->second.val;
    } else {
        // Use the prefix
        pname = ti->second.prefix.first;
        pval = ti->second.prefix.second;
        sname = ti->first;
        sval = ti->second.val;
    }

    DbHandler::TagMap::iterator di = attribs_tag->find(pname);
    if (di == attribs_tag->end()) {
        // This 1st level tag is absent
        DbHandler::AttribMap amap;
        if (sval.type != DbHandler::INVALID) {
            amap.insert(make_pair(sname, sval));
        }
        attribs_tag->insert(make_pair(pname, make_pair(pval, amap)));
    } else {
        // The 1st level tag is already present
        // Add the 2nd level tag if needed
        DbHandler::AttribMap &amap = di->second.second;
        if (sval.type != DbHandler::INVALID) {
            if (amap.find(sname) == amap.end()) {
                amap.insert(make_pair(sname, sval));
            }
        }
    }
}


void
StatWalker::Pop(void) {
    VIZD_ASSERT(!nodes_.empty());
    DbHandler::AttribMap attribs = nodes_.back().attribs;
    DbHandler::TagMap attribs_tag;

    for (TagMap::const_iterator ti = top_tags_.begin();
            ti != top_tags_.end(); ti++) {
        FillTag(&attribs_tag, &(*ti));
    }
    
    string prename("");
    for (vector<StatNode>::const_iterator ni = nodes_.begin();
            ni != nodes_.end(); ni++) {

        const StatNode& ancestor = *ni;
        if (!prename.empty()) {
            prename.append(".");
        }
        prename.append(ancestor.name);

        for (TagMap::const_iterator ti = ancestor.tags.begin();
                ti != ancestor.tags.end(); ti++) {
            FillTag(&attribs_tag, &(*ti));
        }
    }

    // Take the final tags and also insert them as attribs
    // We may get duplicates ; the last value read will get used
    for (DbHandler::TagMap::const_iterator fi = attribs_tag.begin();
         fi != attribs_tag.end(); fi++) {
        attribs.insert(make_pair(fi->first, fi->second.first));
        attribs.insert(fi->second.second.begin(), fi->second.second.end());
    }
    fn_(timestamp_, stat_name_, prename, attribs_tag, attribs);
    nodes_.pop_back();
}

StatWalker::~StatWalker() {
    VIZD_ASSERT(nodes_.empty());
}
