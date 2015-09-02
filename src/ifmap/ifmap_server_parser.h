/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __DB_IFMAP_PARSER_H__
#define __DB_IFMAP_PARSER_H__

#include <list>
#include <map>
#include <boost/function.hpp>

struct AutogenProperty;
class DB;
struct DBRequest;

namespace pugi {
class xml_document;
class xml_node;
}  // namespace pugi

class IFMapServerParser {
public:
    typedef boost::function<
                bool(const pugi::xml_node &, std::auto_ptr<AutogenProperty > *)
            > MetadataParseFn;
    typedef std::map<std::string, MetadataParseFn> MetadataParseMap;
    typedef std::list<struct DBRequest *> RequestList;

    // Called for each resultItem element in the IF-MAP notification.
    bool ParseResultItem(const pugi::xml_node &parent, bool add_change,
                         RequestList *list) const;

    void ParseResults(const pugi::xml_document &xdoc, RequestList *list) const;
    void MetadataRegister(const std:: string &metadata, MetadataParseFn parser);
    void MetadataClear(const std::string &module);
    void SetOrigin(struct DBRequest *result) const;

    bool Receive(DB *db, const char *data, size_t length,
                 uint64_t sequence_number);

    static IFMapServerParser *GetInstance(const std::string &module);
    static void DeleteInstance(const std::string &module);

private:
    typedef std::map<std::string, IFMapServerParser *> ModuleMap;
    static ModuleMap module_map_;

    bool ParseMetadata(const pugi::xml_node &node,
                       struct DBRequest *result) const;

    MetadataParseMap metadata_map_;
};

#endif
