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

void IFMapMsgPropertyAdd(DB *db, const std::string &type, const std::string &id,
                         const std::string &metadata, AutogenProperty *content,
                         uint64_t sequence_number = 0);

void IFMapMsgPropertyDelete(DB *db, const std::string &type,
                            const std::string &id, const std::string &metadata);

IFMapNode *NodeLookup(DB *db, const std::string &type, const std::string &name);

}

#endif
