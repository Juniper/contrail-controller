/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_ROUTING_POLICY_ROUTING_POLICY_H_
#define SRC_BGP_ROUTING_POLICY_ROUTING_POLICY_H_

#include <boost/scoped_ptr.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <tbb/atomic.h>

#include <map>
#include <string>

#include "base/lifetime.h"
#include "base/util.h"
#include "bgp/bgp_common.h"
#include "db/db_table_walker.h"

#include <sandesh/sandesh_trace.h>

class BgpAttr;
class BgpPath;
class BgpRoute;
class BgpServer;
class BgpTable;
class RoutingPolicyMgr;
class BgpRoutingPolicyConfig;
class RoutingInstance;
class RoutingPolicyMatch;
class RoutingPolicyAction;
class RoutingPolicyTerm;
class TaskTrigger;

// Routing Policy Manager
// This class implements the routing policy for control node.
// It maintains the list of routing policies configured on the system
// It provides API to lookup the routing policy by name.
//
// It provides two APIs to apply routing policy on the routes belonging to given
// routing instance.
// 1. ApplyRoutingPolicy
//    Apply routing policy on the routing instance passed as input.
//    To achieve this Routing policy manager walks all the routing tables of
//    the routing instance. On each route encountered during the walk, policy
//    is evaluated using EvaluateRoutingPolicy. EvaluateRoutingPolicy visits
//    all the BgpPath and apply all routing policies of that routing instance
//    on that BgpPath.
//
// 2. ExecuteRoutingPolicy
//     This function is called on the given route + path from two code path.
//     First one is from InsertPath. i.e. when new path is being added to a
//     route.
//     Second path is from DBTable walk context while applying the routing
//     policy on routing instance.  This function calls the operator() of the
//     policy in input to match and apply the action on successful match.
//
// To avoid multiple walk requests and cancels on the table,
// RoutingPolicyManager clubs multiple walk request in config update cycle into
// one table walk. This is achieved using RoutingPolicyWalkRequests,
// which stores a map of BgpTable vs RoutingPolicySyncState.
// RoutingPolicySyncState stores the walk id of the current walk and a boolean
// flag to indicate whether walk is requested again before the previous walk
// is completed.
//
// RoutingPolicyManager takes a delete reference of BgpServer.
//
// RoutingPolicy
//
// This represents one Routing Policy in operational data.
// Each routing policy has multiple policy terms (represented using PolicyTerm
// class) represented as ordered list using std::list.
//
// Routing Policy object takes lifetime reference of RoutingPolicyMgr.
//
// Reference to RoutingPolicy object is done using intrusive pointer which
// keeps track of ref_count.
// The DeleteActor of the RoutingPolicy uses this ref_count to allow/disallow
// delete of the RoutingPolicy object. On last dereference of the RoutingPolicy
// object, RetryDelete on the DeleteActor is triggered.
//
// RoutingPolicy is removed from the name-map in RoutingPolicyMgr  only in
// Destroy method of DeleteActor. It is possible that config might have revived
// the RoutingPolicy object with same name(with same or different config).
// In such case, RoutingPolicyMgr recreate the routing policy based on the
// new config object.
//
// RoutingPolicy object provided operator() to apply the policy on a
// BgpRoute+BgpPath. In this operator overload function, route is processed
// against each policy term till a terminal rule is encountered.
// A terminal rule is a term where action on successful match is to Reject or
// Accept the route. Return value gives the hint of result of policy apply.
// Result is represented as a pair with first element representing whether the
// match was for terminal rule and second element indicating whether there
// was a policy match.
//
// RoutingPolicy object is updated on config update function in UpdateConfig
// method. This method walks newly configured policy term list and existing
// policy term list and compares the PolicyTerm to see whether
//    a. Policy term matches (Done using operator==() function on PolicyTerm).
//       If no match is found, new PolicyTerm as per new config is created and
//       inserted in to ordered list of PolicyTerm.
//    b. New policy Term is inserted
//    c. Existing policy term is deleted
// At the end of the update, generation number of the policy term is updated to
// indicate config change to Routing Policy. Routing Instances referring to
// this policy is not triggered from this path. It is expected that
// BgpConfigListener infra would put the RoutingInstance to the change_list when
// the routing policy it is referring undergoes a change.
//
// Routing Instance
// Routing instance maintains an ordered list of routing policies that is refers.
// This is represented as std::list of pair<RoutingPolicyPtr, gen-id of policy>.
// Please note RoutingPolicyPtr is an intrusive pointer to RoutingPolicy object.
//
// While processing the routing policies on each routes belonging to this
// instance, each of routing policies are applied till terminal rule is hit.
// BgpRoute is update with all policy actions from the matching policy term.
// Policy action is stored in BgpPath (in flag ond/or in BgpAttr)
//
// Update of the routing instance checks whether the routing policy list on the
// instance has changed. This process checks for following
//   a. If a routing policy order is update
//   b. If new routing policy(ies) are added to the routing policy list
//   c. If existing routing policy(ies) are removed from the routing policy list
//   d. If the routing policies that it is referring has undergone config change
//      This is done by checking the generation number of routing policy that
//      is applied on the routing instance.
// In case the routing policy is updated on the routing instance
// (if either of (a) to (d) above is true), all the BgpTables belonging to the
// routing instance is walked to reapply the routing policy.
//
// BgpRoute/BgpPath
// BgpPath maintains original BgpAttribute that it received in a new field
// called as original_attr_. The path attribute attr_ represents the
// BgpAttribute after applying the policy.
// New flag is introduced to indicate if the path is rejected by routing policy.
//
// PolicyTerm
// Policy Term has two ordered lists
//      1. Match conditions
//      2. Actions
//
// PolicyTerm class provides accessor method to get list of matches and actions.
// It also provides compare function (operator==()) to check whether two policy
// terms matches/same. It would return true if each policy term has same match
// set(ordered list) and same action list(ordered).
//
// Policy action is represented as ordered list of action to be taken on
// successful match. The top of the action list indicates what should be done
// with route on successful match. e.g. Accept/Reject/NextTerm.
// Subsequent entry in this list are the update action to be taken on policy
// match.
//
// Match Condition
// RoutingPolicyMatch is the abstract base class to implement a match condition.
// This abstract class provides Match() function to compare the route + Path
// attribute with match condition. The derived class provides implementation
// to compare the route + attribute with match condition. Another method of this
// class is comparator function operator==(). This method compares whether two
// match conditions are same. This is done by comparing the typeid() (RTTI) and
// calling IsEqual pure virtual method if type ids are same.
//
// All match conditions inherit this RoutingPolicyMatch and provide
// implementation of pure virtual methods.
// In the current release, match is supported on Community and prefix.
//
// MatchCommunity implements match for community value
// MatchPrefix provides templetized implementation for matching prefix.
// Currently PrefixMatchInet and PrefixMatchInet6 implementation is supported
// to match inet and inet6 routes.
//
// Policy Action
// RoutingPolicyAction is the abstract base class for implementing the policy
// action.
// Action is represented as
//      RoutingPolicyNexTermAction,
//      RoutingPolicyRejectAction,
//      RoutingPolicyAcceptAction or
//      RoutingPolicyUpdateAction
// This represents NextTerm, Reject, accept and route update action respectively
// RoutingPolicyRejectAction & RoutingPolicyAcceptAction as Terminal actions.
// RoutingPolicyNexTermAction and RoutingPolicyAcceptAction may/may not have
// RoutingPolicyUpdateAction associated with it.
// RoutingPolicyUpdateAction represents the modification done to path attribute
// on successful match.
// RoutingPolicyAction provides comparator function to check whether two actions
// are same. This is done by comparing the typeid() of the object and invoking
// IsEqual when typeid() is same.
// RoutingPolicyUpdateAction which inherits the RoutingPolicyAction provides the
// pure virtual method operator() to apply update action on BgpAttr.
// Currently support for updating the local-pref and community list is supported
// for update action.
// UpdateCommunity supports add/remove/set list of
// community to incoming BgpAttr.
//
class PolicyTerm {
public:
    typedef std::vector<RoutingPolicyAction*> ActionList;
    typedef std::vector<RoutingPolicyMatch*> MatchList;
    PolicyTerm();
    ~PolicyTerm();
    bool terminal() const;
    bool ApplyTerm(const BgpRoute *route,
                   const BgpPath *path, BgpAttr *attr) const;
    void set_actions(const ActionList &actions) {
        actions_ = actions;
    }
    void set_matches(const MatchList &matches) {
        matches_ = matches;
    }
    const MatchList &matches() const {
        return matches_;
    }
    const ActionList &actions() const {
        return actions_;
    }
    bool operator==(const PolicyTerm &term) const;
private:
    MatchList matches_;
    ActionList actions_;
};

class RoutingPolicy {
public:
    typedef boost::shared_ptr<PolicyTerm>  PolicyTermPtr;
    typedef std::list<PolicyTermPtr> RoutingPolicyTermList;
    typedef std::pair<bool, bool> PolicyResult;
    RoutingPolicy(std::string name, BgpServer *server,
                    RoutingPolicyMgr *mgr,
                    const BgpRoutingPolicyConfig *config);
    virtual ~RoutingPolicy();
    void ProcessConfig();
    void UpdateConfig(const BgpRoutingPolicyConfig *config);
    void ClearConfig();
    void Shutdown();

    bool MayDelete() const;
    void RetryDelete();
    void ManagedDelete();
    LifetimeActor *deleter();
    const LifetimeActor *deleter() const;
    bool deleted() const;

    const std::string &name() const { return name_; }
    const BgpRoutingPolicyConfig *config() const { return config_; }
    const RoutingPolicyMgr *manager() const { return mgr_; }

    BgpServer *server() { return server_; }
    const BgpServer *server() const { return server_; }

    RoutingPolicyTermList *terms() { return &terms_; }
    const RoutingPolicyTermList &terms() const { return terms_; }
    void add_term(PolicyTermPtr term) {
        terms_.push_back(term);
    }

    PolicyResult operator()(const BgpRoute *route,
                            const BgpPath *path, BgpAttr *attr) const;
    uint32_t generation() const { return generation_; }
    uint32_t refcount() const { return refcount_; }

private:
    friend class RoutingPolicyMgr;
    class DeleteActor;
    friend void intrusive_ptr_add_ref(RoutingPolicy *policy);
    friend void intrusive_ptr_release(RoutingPolicy *policy);

    PolicyTermPtr BuildTerm(const RoutingPolicyTerm &term);
    std::string name_;
    BgpServer *server_;
    RoutingPolicyMgr *mgr_;
    const BgpRoutingPolicyConfig *config_;
    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<RoutingPolicy> manager_delete_ref_;

    // Updated when routing policy undergoes a change
    tbb::atomic<uint32_t> refcount_;
    uint32_t generation_;
    RoutingPolicyTermList terms_;
};

inline void intrusive_ptr_add_ref(RoutingPolicy *policy) {
    policy->refcount_.fetch_and_increment();
    return;
}

inline void intrusive_ptr_release(RoutingPolicy *policy) {
    int prev = policy->refcount_.fetch_and_decrement();
    if (prev == 1) {
        if (policy->MayDelete())
            policy->RetryDelete();
    }
}

// RoutingPolicySyncState
// This class holds the information of the TableWalk requests posted from
// config sync.
class RoutingPolicySyncState {
public:
    RoutingPolicySyncState() : id_(DBTable::kInvalidId), walk_again_(false) {
    }

    DBTableWalker::WalkId GetWalkerId() {
        return id_;
    }

    void SetWalkerId(DBTableWalker::WalkId id) {
        id_ = id;
    }

    void SetWalkAgain(bool walk) {
        walk_again_ = walk;
    }

    bool WalkAgain() {
        return walk_again_;
    }

private:
    DBTableWalker::WalkId id_;
    bool walk_again_;
    DISALLOW_COPY_AND_ASSIGN(RoutingPolicySyncState);
};

class RoutingPolicyMgr {
public:
    typedef std::map<std::string, RoutingPolicy*> RoutingPolicyList;
    typedef RoutingPolicyList::iterator name_iterator;
    typedef RoutingPolicyList::const_iterator const_name_iterator;
    typedef std::map<BgpTable *, RoutingPolicySyncState *> RoutingPolicyWalkRequests;

    explicit RoutingPolicyMgr(BgpServer *server);
    virtual ~RoutingPolicyMgr();
    SandeshTraceBufferPtr trace_buffer() const { return trace_buf_; }

    name_iterator name_begin() { return routing_policies_.begin(); }
    name_iterator name_end() { return routing_policies_.end(); }
    name_iterator name_lower_bound(const std::string &name) {
        return routing_policies_.lower_bound(name);
    }
    const_name_iterator name_cbegin() { return routing_policies_.begin(); }
    const_name_iterator name_cend() { return routing_policies_.end(); }
    const_name_iterator name_clower_bound(const std::string &name) {
        return routing_policies_.lower_bound(name);
    }

    RoutingPolicy *GetRoutingPolicy(const std::string &name) {
        name_iterator it;
        if ((it = routing_policies_.find(name)) != routing_policies_.end()) {
            return it->second;
        }
        return NULL;
    }
    const RoutingPolicy *GetRoutingPolicy(const std::string &name) const {
        const_name_iterator it;
        if ((it = routing_policies_.find(name)) != routing_policies_.end()) {
            return it->second;
        }
        return NULL;
    }

    virtual RoutingPolicy *CreateRoutingPolicy(
                             const BgpRoutingPolicyConfig *config);
    void UpdateRoutingPolicy(const BgpRoutingPolicyConfig *config);
    virtual void DeleteRoutingPolicy(const std::string &name);


    bool deleted();
    void ManagedDelete();

    void DestroyRoutingPolicy(RoutingPolicy *policy);

    size_t count() const { return routing_policies_.size(); }
    BgpServer *server() { return server_; }
    const BgpServer *server() const { return server_; }
    LifetimeActor *deleter();

    RoutingPolicy::PolicyResult ExecuteRoutingPolicy(const RoutingPolicy *policy,
               const BgpRoute *route, const BgpPath *path, BgpAttr *attr) const;


    // Update the routing policy list on attach point
    bool UpdateRoutingPolicyList(const RoutingPolicyConfigList &cfg_list,
                                       RoutingPolicyAttachList *oper_list);

    // RoutingInstance is updated with new set of policies. This function
    // applies that policy on each route of this routing instance
    void ApplyRoutingPolicy(RoutingInstance *instance);

    bool StartWalk();
    void RequestWalk(BgpTable *table);
    void WalkDone(DBTableBase *dbtable);
    bool EvaluateRoutingPolicy(DBTablePartBase *root, DBEntryBase *entry);

private:
    class DeleteActor;

    BgpServer *server_;
    RoutingPolicyList routing_policies_;
    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<RoutingPolicyMgr> server_delete_ref_;
    // Mutex to protect routing_policy_sync_ from multiple DBTable tasks.
    tbb::mutex mutex_;
    RoutingPolicyWalkRequests routing_policy_sync_;
    boost::scoped_ptr<TaskTrigger> walk_trigger_;
    SandeshTraceBufferPtr trace_buf_;
};
#endif // SRC_BGP_ROUTING_POLICY_ROUTING_POLICY_H_
