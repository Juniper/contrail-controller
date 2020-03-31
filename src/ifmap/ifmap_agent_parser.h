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

    enum ConfigMsgType {
        UPDATE,
        DEL,
        MAX
    };

    IFMapAgentParser(DB *db) : db_(db) { reset_statistics(); } ;

    typedef boost::function< IFMapObject *(const pugi::xml_node, DB *,
                                               std::string *id_name) > NodeParseFn;
    typedef std::map<std::string, NodeParseFn> NodeParseMap;
    void NodeRegister(const std::string &node, NodeParseFn parser);
    void NodeClear();
    void ConfigParse(const pugi::xml_node &config, uint64_t seq);
    uint64_t node_updates() { return nodes_processed_[UPDATE]; }
    uint64_t node_deletes() { return nodes_processed_[DEL]; }
    uint64_t link_updates() { return links_processed_[UPDATE]; }
    uint64_t link_deletes() { return links_processed_[DEL]; }
    uint64_t node_update_parse_errors() { return node_parse_errors_[UPDATE]; }
    uint64_t node_delete_parse_errors() { return node_parse_errors_[DEL]; }
    uint64_t link_update_parse_errors() { return link_parse_errors_[UPDATE]; }
    uint64_t link_delete_parse_errors() { return link_parse_errors_[DEL]; }
    void reset_statistics() {
        nodes_processed_[UPDATE] = 0;
        nodes_processed_[DEL] = 0;
        links_processed_[UPDATE] = 0;
        links_processed_[DEL] = 0;
        node_parse_errors_[UPDATE] = 0;
        node_parse_errors_[DEL] = 0;
        link_parse_errors_[UPDATE] = 0;
        link_parse_errors_[DEL] = 0;
    }
private:
    DB *db_;
    NodeParseMap node_map_;
    uint64_t nodes_processed_[MAX];
    uint64_t links_processed_[MAX];
    uint64_t node_parse_errors_[MAX];
    uint64_t link_parse_errors_[MAX];
    void NodeParse(pugi::xml_node &node, DBRequest::DBOperation oper, uint64_t seq);
    void LinkParse(pugi::xml_node &node, DBRequest::DBOperation oper, uint64_t seq);
};

#endif
