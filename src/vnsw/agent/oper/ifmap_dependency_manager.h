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
#include "ifmap/ifmap_dependency_tracker.h"

class Agent;
class DB;
class DBGraph;
class IFMapDependencyTracker;
class IFMapNode;
class TaskTrigger;
class IFMapDependencyManager;

//IFMapNodeState is a DBState for IFMapNode with listener ID
// of IFMapDependency Manager. DBState is created when the first
// time SetState is invoked. The caller of SetState gets intrusive
// pointer to IFMapNodeState  enabling the caller hold IFMapNode
// till reference is removed. Caller does not need to invoke
// clear state as it gets automatically cleared when references are
// removed. Optionally a DBEntry can be added to this IFMapNodeState
// to trigger the IFMapDependency tracker.
// IFMapDependency tracker also uses the same state, to ensure that
// across the traversal, IFMapNode is not removed.
class IFMapNodeState : public DBState {
  public:
    IFMapNodeState(IFMapDependencyManager *manager, IFMapNode *node)
            : manager_(manager), node_(node), object_(NULL),
            uuid_(boost::uuids::nil_uuid()), refcount_(0),
            notify_(true) {
    }

    IFMapNode *node() { return node_; }
    DBEntry *object() { return object_; }
    void set_object(DBEntry *object) {
        object_ = object;
    }

    void set_uuid(const boost::uuids::uuid &u) {
        uuid_ = u;
    }

    void set_notify(bool flag) {
        notify_ = flag;
    }

    bool notify() { return notify_;};

    boost::uuids::uuid uuid() { return uuid_; }

    void clear_object() {
        object_ = NULL;
    }

  private:
    friend void intrusive_ptr_add_ref(IFMapNodeState *state);
    friend void intrusive_ptr_release(IFMapNodeState *state);

    IFMapDependencyManager *manager_;
    IFMapNode *node_;
    DBEntry *object_;
    boost::uuids::uuid uuid_;
    int refcount_;
    bool notify_;
};


class IFMapDependencyManager {
public:
    typedef boost::intrusive_ptr<IFMapNodeState> IFMapNodePtr;
    typedef boost::function<void(IFMapNode *, DBEntry *)> ChangeEventHandler;

    struct Link {
        Link(const std::string &edge, const std::string &vertex, bool interest):
            edge_(edge), vertex_(vertex), vertex_interest_(interest) {
        }
        std::string edge_;
        std::string vertex_;
        bool vertex_interest_;
    };
    typedef std::vector<Link> Path;

    IFMapDependencyManager(DB *database, DBGraph *graph);
    virtual ~IFMapDependencyManager();

    /*
     * Initialize must be called after the ifmap tables are registered
     * via <schema>_Agent_ModuleInit.
     */
    void Initialize(Agent *agent);

    /*
     * Unregister from all tables.
     */
    void Terminate();

    void AddDependencyPath(const std::string &node, Path path);
    void InitializeDependencyRules(Agent *agent);
    /*
     * Register reactor-map for an IFMap node
     */
    void RegisterReactionMap(const char *node_name,
                             const IFMapDependencyTracker::ReactionMap &react);
    /*
     * Associate an IFMapNode with an object in the operational database.
     */
    void SetObject(IFMapNode *node, DBEntry *entry);

    /*
     * Add DBState to an IFMapNode
     */
    IFMapNodePtr SetState(IFMapNode *node);
    void SetNotify(IFMapNode *node, bool notfiy_flag);
    IFMapNodeState *IFMapNodeGet(IFMapNode *node);

    /*
     * Get DBEntry object set for an IFMapNode
     */
    DBEntry *GetObject(IFMapNode *node);

    /*
     * Register a notification callback.
     */
    void Register(const std::string &type, ChangeEventHandler handler);

    /*
     * Unregister a notification callback.
     */
    void Unregister(const std::string &type);

    IFMapDependencyTracker *tracker() const { return tracker_.get(); }
    void PropogateNodeChange(IFMapNode *node);
    void PropogateNodeAndLinkChange(IFMapNode *node);

private:
    /*
     * IFMapNodeState (DBState) should exist:
     * a) if the object is set
     * b) if the entry is on the change list.
     */
    friend void intrusive_ptr_add_ref(IFMapNodeState *state);
    friend void intrusive_ptr_release(IFMapNodeState *state);

    typedef std::vector<IFMapNodePtr> ChangeList;
    typedef std::map<std::string, DBTable::ListenerId> TableMap;
    typedef std::map<std::string, ChangeEventHandler> EventMap;

    bool ProcessChangeList();

    void NodeObserver(DBTablePartBase *root, DBEntryBase *db_entry);
    void LinkObserver(DBTablePartBase *root, DBEntryBase *db_entry);
    void ChangeListAdd(IFMapNode *node);

    void IFMapNodeReset(IFMapNode *node);

    DB *database_;
    DBGraph *graph_;
    std::auto_ptr<IFMapDependencyTracker> tracker_;
    std::auto_ptr<TaskTrigger> trigger_;
    TableMap table_map_;
    EventMap event_map_;
    ChangeList change_list_;
};

#endif
