/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_rtarget_group_h
#define ctrlplane_rtarget_group_h

#include <list>

#include "bgp/bgp_table.h"
#include "bgp/rtarget/rtarget_address.h"

// RouteTarget Group for a given RouteTarget
// Contains two lists of tables 
//       1. Tables that imports the route belonging to this RouteTarget
//       2. Tables to which route needs to be exported
class RtGroup {
public:
    typedef std::list<BgpTable *> RtGroupMemberList;

    RtGroup(const RouteTarget &rt) : rt_(rt) {
    }

    RtGroupMemberList &GetImportTables() {
        return import_list_;
    }

    RtGroupMemberList &GetExportTables() {
        return export_list_;
    }

    void AddImportTable(BgpTable *tbl) {
        import_list_.push_back(tbl);
        import_list_.sort();
        import_list_.unique();
    }

    void AddExportTable(BgpTable *tbl) {
        export_list_.push_back(tbl);
        export_list_.sort();
        export_list_.unique();
    }

    void RemoveImportTable(BgpTable *tbl) {
        import_list_.remove(tbl);
    }

    void RemoveExportTable(BgpTable *tbl) {
        export_list_.remove(tbl);
    }

    bool empty() {
        return import_list_.empty() && export_list_.empty();
    }

    const RouteTarget &rt() {
        return rt_;
    }
private:
    RtGroupMemberList import_list_;
    RtGroupMemberList export_list_;
    RouteTarget rt_;
    DISALLOW_COPY_AND_ASSIGN(RtGroup);
};

#endif
