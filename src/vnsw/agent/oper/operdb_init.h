/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VNSW_OPERDB_INIT__
#define __VNSW_OPERDB_INIT__

#include <tbb/mutex.h>
#ifndef _LIBCPP_VERSION
#include <tbb/compat/condition_variable>
#endif

class DBEntryBase;

class OperDB {
public:
    typedef boost::function<void(void)> Callback;
    OperDB();
    virtual ~OperDB();

    static void CreateDBTables(DB *);
    static void CreateStaticObjects(Callback cb = NULL);
    static void Shutdown();

private:
    OperDB(Callback cb);
    void CreateDefaultVrf();
    void OnVrfCreate(DBEntryBase *entry);
    static OperDB *singleton_;

    DBTableBase::ListenerId vid_;
    Callback cb_;
    TaskTrigger *trigger_;
    DISALLOW_COPY_AND_ASSIGN(OperDB);
};
#endif
