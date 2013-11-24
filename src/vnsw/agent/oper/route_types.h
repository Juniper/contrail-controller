/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */


#ifndef vnsw_agent_route_types_hpp
#define vnsw_agent_route_types_hpp

#include <sys/types.h>
#include <sys/socket.h>
#include <route/route.h>
#include <route/table.h>

using namespace std;

class AgentRouteTable;
class AgentXmppChannel;
class VrfExport;
class LifetimeActor;
class LifetimeManager;
class RouteTableWalkerState;
class RouteInfo;

class RouteKey;
class RouteData;
class Inet4UnicastAgentRouteTable;
class Inet4MulticastAgentRouteTable;
class Layer2AgentRouteTable;
class Inet4UnicastRouteEntry;
class Inet4MulticastRouteEntry;
class Layer2RouteEntry;
class AgentPath;

/* Route Types */

class AgentRouteTableAPIS {
public:
    enum TableType {
        INET4_UNICAST = 0,
        INET4_MULTICAST,
        LAYER2,
        MAX
    };

    static AgentRouteTableAPIS *GetInstance() {
        static AgentRouteTableAPIS *instance_;
        if (!instance_) {
            instance_ = new AgentRouteTableAPIS();
        }
        return instance_;
    };
    ~AgentRouteTableAPIS() { };

    void CreateRouteTablesInVrf(DB *db, const string &name,
                                AgentRouteTable *table_list[]);
    //Createtable based on tabel type can create name for table and stores
    //in AgentRouteTabletree, so no need of GetInet4UcSuffix
    DBTableBase *CreateInet4UnicastTable(DB *db, const string &name) {
        return CreateRouteTable(db, name, AgentRouteTableAPIS::INET4_UNICAST);
    };
    DBTableBase *CreateInet4MulticastTable(DB *db, const string &name) {
        return CreateRouteTable(db, name, AgentRouteTableAPIS::INET4_MULTICAST);
    };
    DBTableBase *CreateLayer2Table(DB *db, const string &name) {
        return CreateRouteTable(db, name, AgentRouteTableAPIS::LAYER2);
    };
    string GetSuffix(AgentRouteTableAPIS::TableType type);
    string GetSuffix(int type) {
        return GetSuffix(TableType(type));
    };
    AgentRouteTable *GetRouteTable(TableType type) {
        return RouteTableTree[type];
    };
private:
    AgentRouteTableAPIS() : RouteTableTree() { };
   
    DBTableBase *CreateRouteTable(DB *db, const string &name, TableType type);
    //Still need to see better way currently it takes first table
    AgentRouteTable *RouteTableTree[MAX];
    DISALLOW_COPY_AND_ASSIGN(AgentRouteTableAPIS);
};

#endif
