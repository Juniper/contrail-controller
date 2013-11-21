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
    OperDB(Agent *agent);
    virtual ~OperDB();

    void CreateDBTables(DB *);
    void CreateDBClients();
    void Init();
    void CreateDefaultVrf();
    void Shutdown();

    MulticastHandler *multicast() const { return multicast_.get(); }
private:
    OperDB();
    static OperDB *singleton_;

    Agent *agent_;
    std::auto_ptr<MulticastHandler> multicast_;
    DISALLOW_COPY_AND_ASSIGN(OperDB);
};
#endif
