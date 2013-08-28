/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __ctrlplane__bgp_config_parser__
#define __ctrlplane__bgp_config_parser__

#include <list>
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

    DB *db_;
    DISALLOW_COPY_AND_ASSIGN(BgpConfigParser);
};

#endif /* defined(__ctrlplane__bgp_config_parser__) */
