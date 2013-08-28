/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __DB_IFMAP_AGENT_PARSER_H__
#define __DB_IFMAP_AGENT_PARSER_H__

#include <list>
#include <map>
#include <boost/function.hpp>
#include "db/db.h"
#include "ifmap/ifmap_object.h"
#include "ifmap/ifmap_table.h"

namespace pugi {
class xml_document;
class xml_node;
}  // namespace pugi

class IFMapAgentParser {
public:
    IFMapAgentParser(DB *db) : db_(db) {} ;

    typedef boost::function< IFMapObject *(const pugi::xml_node, DB *, 
                                               std::string *id_name) > NodeParseFn;
    typedef std::map<std::string, NodeParseFn> NodeParseMap;
    void NodeRegister(const std::string &node, NodeParseFn parser);
    void NodeClear();
    void ConfigParse(const pugi::xml_node config, uint64_t seq);
private:
    DB *db_;
    NodeParseMap node_map_;
    void NodeParse(pugi::xml_node &node, DBRequest::DBOperation oper, uint64_t seq);
    void LinkParse(pugi::xml_node &node, DBRequest::DBOperation oper, uint64_t seq);
};

#endif
