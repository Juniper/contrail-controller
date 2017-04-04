/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_CONFIG_PARSER_H_
#define SRC_BGP_BGP_CONFIG_PARSER_H_

#include <list>
#include <string>

#include "base/util.h"

class DB;
struct DBRequest;
namespace pugi {
class xml_node;
}

// Convert an xml file into a set of IFMapTable requests.
class BgpConfigParser {
public:
    typedef std::list<DBRequest *> RequestList;

    explicit BgpConfigParser(DB *db);

    bool Parse(const std::string &content);

    static std::string session_uuid(const std::string &left,
                                    const std::string &right,
                                    int index);

private:
    bool ParseConfig(const pugi::xml_node &root, bool add_change,
                     RequestList *requests) const;
    bool ParseRoutingInstance(const pugi::xml_node &parent, bool add_change,
                              RequestList *requests) const;
    bool ParseVirtualNetwork(const pugi::xml_node &parent, bool add_change,
                              RequestList *requests) const;
    bool ParseRoutingPolicy(const pugi::xml_node &parent, bool add_change,
                              RequestList *requests) const;
    bool ParseRouteAggregate(const pugi::xml_node &parent, bool add_change,
                             RequestList *requests) const;
    bool ParseGlobalSystemConfig(const pugi::xml_node &parent, bool add_change,
                                 RequestList *requests) const;
    bool ParseGlobalQosConfig(const pugi::xml_node &parent, bool add_change,
                              RequestList *requests) const;

    DB *db_;
    DISALLOW_COPY_AND_ASSIGN(BgpConfigParser);
};

#endif  // SRC_BGP_BGP_CONFIG_PARSER_H_
