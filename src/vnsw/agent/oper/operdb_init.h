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
class GlobalVrouter;
class PathPreferenceModule;

class OperDB {
public:
    OperDB(Agent *agent);
    virtual ~OperDB();

    void CreateDBTables(DB *);
    void RegisterDBClients();
    void Init();
    void CreateDefaultVrf();
    void DeleteRoutes();
    void Shutdown();

    Agent *agent() const { return agent_; }
    MulticastHandler *multicast() const { return multicast_.get(); }
    GlobalVrouter *global_vrouter() const { return global_vrouter_.get(); }
    PathPreferenceModule *route_preference_module() const {
        return route_preference_module_.get();
    }

private:
    OperDB();
    static OperDB *singleton_;

    Agent *agent_;
    std::auto_ptr<MulticastHandler> multicast_;
    std::auto_ptr<GlobalVrouter> global_vrouter_;
    std::auto_ptr<PathPreferenceModule> route_preference_module_;
    DISALLOW_COPY_AND_ASSIGN(OperDB);
};
#endif
