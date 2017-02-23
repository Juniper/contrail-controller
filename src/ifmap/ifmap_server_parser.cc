/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdint.h>
#include "ifmap/ifmap_server_parser.h"

#include <pugixml/pugixml.hpp>
#include "db/db.h"
#include "ifmap/ifmap_server_table.h"
#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_log_types.h"

using namespace std;
using namespace pugi;

IFMapServerParser::ModuleMap IFMapServerParser::module_map_;

IFMapServerParser *IFMapServerParser::GetInstance(const string &module) {
    ModuleMap::iterator loc = module_map_.find(module);
    if (loc != module_map_.end()) {
        return loc->second;
    }
    IFMapServerParser *parser = new IFMapServerParser();
    module_map_.insert(make_pair(module, parser));
    return parser;
}

void IFMapServerParser::DeleteInstance(const string &module) {
    ModuleMap::iterator loc = module_map_.find(module);
    if (loc == module_map_.end()) {
        return;
    }

    delete loc->second;
    module_map_.erase(loc);
}

void IFMapServerParser::MetadataRegister(const string &metadata,
                                   MetadataParseFn parser) {
    pair<MetadataParseMap::iterator, bool> result =
            metadata_map_.insert(make_pair(metadata, parser));
    assert(result.second);
}

void IFMapServerParser::MetadataClear(const string &module) {
    metadata_map_.clear();
    DeleteInstance(module);
}
