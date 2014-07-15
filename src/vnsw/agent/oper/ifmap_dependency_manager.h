/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef AGENT_OPER_IFMAP_DEPENDENCY_MANAGER_H__
#define AGENT_OPER_IFMAP_DEPENDENCY_MANAGER_H__

#include <map>
#include <boost/function.hpp>
#include <boost/intrusive_ptr.hpp>

#include "db/db_entry.h"
#include "db/db_table.h"

class DB;
class DBGraph;
class IFMapDependencyTracker;
class IFMapNode;
class TaskTrigger;

class IFMapDependencyManager {
public:
    typedef boost::function<void(DBEntry *)> ChangeEventHandler;
    IFMapDependencyManager(DB *database, DBGraph *graph);
    virtual ~IFMapDependencyManager();

    /*
     * Initialize must be called after the ifmap tables are registered
     * via <schema>_Agent_ModuleInit.
     */
    void Initialize();

    /*
     * Unregister from all tables.
     */
    void Terminate();

    /*
     * Associate an IFMapNode with an object in the operational database.
     */
    void SetObject(IFMapNode *node, DBEntry *entry);

    /*
     * Reset the association between the IFMapNode and the operation DB
     * entry.
     */
    void ResetObject(IFMapNode *node);

    /*
     * Register a notification callback.
     */
    void Register(const std::string &type, ChangeEventHandler handler);

    /*
     * Unregister a notification callback.
     */
    void Unregister(const std::string &type);

private:
    /*
     * IFMapNodeState (DBState) should exist:
     * a) if the object is set
     * b) if the entry is on the change list.
     */
    class IFMapNodeState;
    friend void intrusive_ptr_add_ref(IFMapNodeState *state);
    friend void intrusive_ptr_release(IFMapNodeState *state);

    typedef boost::intrusive_ptr<IFMapNodeState> IFMapNodePtr;
    typedef std::vector<IFMapNodePtr> ChangeList;
    typedef std::map<std::string, DBTable::ListenerId> TableMap;
    typedef std::map<std::string, ChangeEventHandler> EventMap;

    bool ProcessChangeList();

    void NodeObserver(DBTablePartBase *root, DBEntryBase *db_entry);
    void LinkObserver(DBTablePartBase *root, DBEntryBase *db_entry);
    void ChangeListAdd(IFMapNode *node);

    void IFMapNodeSet(IFMapNode *node, DBEntry *object);
    void IFMapNodeReset(IFMapNode *node);
    IFMapNodeState *IFMapNodeGet(IFMapNode *node);

    DB *database_;
    DBGraph *graph_;
    std::auto_ptr<IFMapDependencyTracker> tracker_;
    std::auto_ptr<TaskTrigger> trigger_;
    TableMap table_map_;
    EventMap event_map_;
    ChangeList change_list_;
};

#endif
