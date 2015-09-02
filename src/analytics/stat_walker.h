/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef _STAT_WALKER_H_
#define _STAT_WALKER_H_

#include <boost/function.hpp>
#include <string>
#include <map>
#include "db_handler.h"

/* This class provides a higher-level interface for DbHandler's StatTableInsert
 * It allows for nested stat strutures
 * The client calls "Push" to fill in a child node
 * The client calls "Pop" when the current node has no more children to fill
 * 
 * This class will call StatTableInsert with the right tags and attribs when 
 * "Pop" is called.
 */
class StatWalker {
public:
    typedef boost::function<void(
        const uint64_t& timestamp,
        const std::string& statName,
        const std::string& statAttr,
        const DbHandler::TagMap & attribs_tag,
        const DbHandler::AttribMap & attribs)> StatTableInsertFn ;

    struct TagVal {
        std::pair<std::string,DbHandler::Var> prefix;
        DbHandler::Var val;
    };
    typedef std::map<std::string,TagVal> TagMap;

    // Record a new child node
    //     name : local name of this node attribute in the parent struct
    //     tags : tags at this level. These tags will be inherited by children
    //     attribs : attribs at this level.  
    void Push(const std::string& name,
        const TagMap& tags,
        const DbHandler::AttribMap& attribs);

    // The current node's children have been completely processed.
    // Its time to call StatTableInsert
    void Pop(void);

    StatWalker(StatTableInsertFn fn, const uint64_t &timestamp,
               const std::string& statName, const TagMap& tags) :
            timestamp_(timestamp),
            stat_name_(statName),
            fn_(fn),
            top_tags_(tags)  {}
    ~StatWalker();
private:
    struct StatNode {
        std::string name;
        TagMap tags;
        DbHandler::AttribMap attribs;
    };

    // Utility function for aggregating StatWalker::TagMap into DbHandler::TagMap
    static void FillTag(DbHandler::TagMap *attribs_tag,
        const std::pair<const std::string, TagVal>* tag);

    const uint64_t timestamp_;
    const std::string stat_name_;
    const StatTableInsertFn fn_;
    const TagMap top_tags_;
    std::vector<StatNode> nodes_;
};

#endif

