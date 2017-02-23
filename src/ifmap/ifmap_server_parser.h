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

    void MetadataRegister(const std:: string &metadata, MetadataParseFn parser);
    void MetadataClear(const std::string &module);
    static IFMapServerParser *GetInstance(const std::string &module);
    static void DeleteInstance(const std::string &module);

private:
    typedef std::map<std::string, IFMapServerParser *> ModuleMap;
    static ModuleMap module_map_;
    MetadataParseMap metadata_map_;
};

#endif
