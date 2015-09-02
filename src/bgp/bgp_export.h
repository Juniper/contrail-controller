/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_EXPORT_H_
#define SRC_BGP_BGP_EXPORT_H_

#include <memory>

class BgpTable;
class DBEntryBase;
class DBTablePartBase;
class RibOut;
class RibPeerSet;

class BgpExport {
public:
    explicit BgpExport(RibOut *ribout);

    void Export(DBTablePartBase *root, DBEntryBase *db_entry);

    // Create new route advertisements in order to sync a peers that has
    // just joined the table.
    bool Join(DBTablePartBase *root, const RibPeerSet &mjoin,
              DBEntryBase *db_entry);

    // Process a route refresh: re-advertise routes to the given set of peers.
    bool Refresh(DBTablePartBase *root, const RibPeerSet &mgroup,
                 DBEntryBase *db_entry);

    // Cleanup the advertisement bits on update entries.
    bool Leave(DBTablePartBase *root, const RibPeerSet &mleave,
               DBEntryBase *db_entry);

private:
    RibOut *ribout_;
};

#endif  // SRC_BGP_BGP_EXPORT_H_
