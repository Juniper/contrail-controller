/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __IFMAP__IFMAP_TEST_UTIL_H__
#define __IFMAP__IFMAP_TEST_UTIL_H__

#include <stdint.h>
#include <string>
class DB;
class DBGraph;
class IFMapNode;
class IFMapLink;
class IFMapTable;
struct DBRequest;
struct AutogenProperty;

namespace ifmap_test_util {

void IFMapLinkCommon(DBRequest *request,
                     const std::string &lhs, const std::string &lid,
                     const std::string &rhs, const std::string &rid,
                     const std::string &metadata, uint64_t sequence_number = 0);

void IFMapMsgLink(DB *db, const std::string &ltype, const std::string &lid,
                  const std::string &rtype, const std::string &rid,
                  const std::string &metadata, uint64_t sequence_number = 0);

void IFMapMsgUnlink(DB *db, const std::string &ltype, const std::string &lid,
                    const std::string &rtype, const std::string &rid,
                    const std::string &metadata);

void IFMapNodeCommon(IFMapTable *table, DBRequest *request,
                     const std::string &type, const std::string &id,
                     uint64_t sequence_number);

void IFMapMsgNodeAdd(DB *db, const std::string &type, const std::string &id,
                     uint64_t sequence_number = 0);

void IFMapMsgNodeDelete(DB *db, const std::string &type, const std::string &id);

void IFMapPropertyCommon(DBRequest *request, const std::string &type,
                         const std::string &id, const std::string &metadata,
                         AutogenProperty *content, uint64_t sequence_number);

void IFMapMsgPropertyAdd(DB *db, const std::string &type, const std::string &id,
                         const std::string &metadata, AutogenProperty *content,
                         uint64_t sequence_number = 0);

void IFMapMsgPropertyDelete(DB *db, const std::string &type,
                            const std::string &id, const std::string &metadata);

IFMapNode *IFMapNodeLookup(DB *db, const std::string &type,
                           const std::string &name);

void IFMapNodeNotify(DB *db, const std::string &type, const std::string &name);

IFMapLink *IFMapLinkLookup(DB *db, DBGraph *graph,
                           const std::string &ltype, const std::string &lid,
                           const std::string &rtype, const std::string &rid);

void IFMapLinkNotify(DB *db, DBGraph *graph,
                     const std::string &ltype, const std::string &lid,
                     const std::string &rtype, const std::string &rid);

}

#endif
