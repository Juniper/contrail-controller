/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __DNS_CONFIG_PARSER__
#define __DNS_CONFIG_PARSER__

#include <list>
#include "base/util.h"

class DB;
struct DBRequest;
namespace pugi {
class xml_node;
}

// Convert an xml file into a set of IFMapTable requests.
class DnsConfigParser {
public:
    typedef std::list<DBRequest *> RequestList;

    explicit DnsConfigParser(DB *db);
    bool Parse(const std::string &content);

private:
    bool ParseConfig(const pugi::xml_node &root, bool add_change,
                     RequestList *requests) const;

    DB *db_;
    DISALLOW_COPY_AND_ASSIGN(DnsConfigParser);
};

#endif // __DNS_CONFIG_PARSER__
