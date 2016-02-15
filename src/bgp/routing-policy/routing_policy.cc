/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-policy/routing_policy.h"

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include "base/task_annotations.h"
#include "base/task_trigger.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_table.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-policy/routing_policy_action.h"
#include "bgp/routing-policy/routing_policy_match.h"


class RoutingPolicyMgr::DeleteActor : public LifetimeActor {
public:
    explicit DeleteActor(RoutingPolicyMgr *manager)
        : LifetimeActor(manager->server_->lifetime_manager()),
          manager_(manager) {
    }
    virtual bool MayDelete() const {
        return true;
    }
    virtual void Shutdown() {
    }
    virtual void Destroy() {
        // memory is deallocated by BgpServer scoped_ptr.
        manager_->server_delete_ref_.Reset(NULL);
    }

private:
    RoutingPolicyMgr *manager_;
};

RoutingPolicyMgr::RoutingPolicyMgr(BgpServer *server) :
        server_(server),
        deleter_(new DeleteActor(this)),
        server_delete_ref_(this, server->deleter()),
        walk_trigger_(new TaskTrigger(
          boost::bind(&RoutingPolicyMgr::StartWalk, this),
          TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0)),
        trace_buf_(SandeshTraceBufferCreate("RoutingPolicyMgr", 500)) {
}

RoutingPolicyMgr::~RoutingPolicyMgr() {
}

void RoutingPolicyMgr::ManagedDelete() {
    deleter_->Delete();
}

LifetimeActor *RoutingPolicyMgr::deleter() {
    return deleter_.get();
}

bool RoutingPolicyMgr::deleted() {
    return deleter()->IsDeleted();
}

RoutingPolicy *RoutingPolicyMgr::CreateRoutingPolicy(
                                 const BgpRoutingPolicyConfig *config) {
    RoutingPolicy *policy = GetRoutingPolicy(config->name());

    if (policy) {
        if (policy->deleted()) {
            return NULL;
        }
        return policy;
    }

    policy = BgpObjectFactory::Create<RoutingPolicy>(
        config->name(), server_, this, config);
    routing_policies_.insert(std::make_pair(config->name(), policy));
    policy->ProcessConfig();

    return policy;
}

void RoutingPolicyMgr::UpdateRoutingPolicy(
                                   const BgpRoutingPolicyConfig *config) {
    CHECK_CONCURRENCY("bgp::Config");

    RoutingPolicy *policy = GetRoutingPolicy(config->name());
    if (policy && policy->deleted()) {
        return;
    } else if (!policy) {
        return;
    }

    policy->UpdateConfig(config);
}

//
// Concurrency: BGP Config task
//
// Trigger deletion of a particular routing-policy
//
void RoutingPolicyMgr::DeleteRoutingPolicy(const std::string &name) {
    CHECK_CONCURRENCY("bgp::Config");

    RoutingPolicy *policy = GetRoutingPolicy(name);

    if (policy && policy->deleted()) {
        return;
    } else if (!policy) {
        return;
    }

    policy->ClearConfig();

    policy->ManagedDelete();
}

//
// Concurrency: Called from BGP config task manager
//
void RoutingPolicyMgr::DestroyRoutingPolicy(RoutingPolicy *policy) {
    CHECK_CONCURRENCY("bgp::Config");

    const std::string name = policy->name();
    routing_policies_.erase(name);
    delete policy;

    if (deleted()) return;

    const BgpRoutingPolicyConfig *config
        = server()->config_manager()->FindRoutingPolicy(name);
    if (config) {
        CreateRoutingPolicy(config);
        return;
    }
}

// Given a routing instance re-evaluate routes/paths by applying routing policy
// Walks all the tables of the given routing instance and apply the policy
// This function puts the table into the walk request queue and triggers the
// task to start the actual walk
void RoutingPolicyMgr::ApplyRoutingPolicy(RoutingInstance *instance) {
    BOOST_FOREACH(RoutingInstance::RouteTableList::value_type &entry,
                  instance->GetTables()) {
        BgpTable *table = entry.second;
        if (table->IsRoutingPolicySupported())
            RequestWalk(table);
    }
    walk_trigger_->Set();
}

// On a given path of the route, apply the policy
RoutingPolicy::PolicyResult RoutingPolicyMgr::ExecuteRoutingPolicy(
                             const RoutingPolicy *policy, const BgpRoute *route,
                             const BgpPath *path, BgpAttr *attr) const {
    return (*policy)(route, path, attr);
}

//
// Concurrency: Called in the context of the DB partition task.
// On a given route, apply routing policy
// Walk through all the paths of the given route, and evaluate the result of the
// routing policy
//
bool RoutingPolicyMgr::EvaluateRoutingPolicy(DBTablePartBase *root,
                                             DBEntryBase *entry) {
    CHECK_CONCURRENCY("db::DBTable");

    BgpTable *table = static_cast<BgpTable *>(root->parent());
    BgpRoute *route = static_cast<BgpRoute *>(entry);
    const RoutingInstance *rtinstance = table->routing_instance();
    if (route->IsDeleted()) return true;

    bool sort_and_notify = false;
    const Path *prev_front = route->front();
    for (Route::PathList::iterator it = route->GetPathList().begin();
        it != route->GetPathList().end(); ++it) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        uint32_t old_flags = path->GetFlags();
        const BgpAttr *old_attr = path->GetAttr();
        rtinstance->ProcessRoutingPolicy(route, path);
        if ((sort_and_notify == false) &&
            (old_flags != path->GetFlags() || old_attr != path->GetAttr())) {
            sort_and_notify = true;
        }
    }

    if (sort_and_notify) {
        route->Sort(&BgpTable::PathSelection, prev_front);
        root->Notify(entry);
    }
    return true;
}

//
//
bool RoutingPolicyMgr::UpdateRoutingPolicyList(
                                        const RoutingPolicyConfigList &cfg_list,
                                        RoutingPolicyAttachList *oper_list) {
    CHECK_CONCURRENCY("bgp::Config");
    bool update_policy = false;
    // Number of routing policies is different
    if (oper_list->size() != cfg_list.size())
        update_policy = true;

    RoutingPolicyAttachList::iterator oper_it = oper_list->begin(), oper_next;
    RoutingPolicyConfigList::const_iterator config_it = cfg_list.begin();
    while (oper_it != oper_list->end() &&
           config_it != cfg_list.end()) {
        // Compare the configured routing policies on the routing-instance
        // with operational data.
        if (oper_it->first->name() == config_it->routing_policy_) {
            if (oper_it->second != oper_it->first->generation()) {
                // Policy content is updated
                oper_it->second = oper_it->first->generation();
                update_policy = true;
            }
            ++oper_it;
            ++config_it;
        } else {
            // Policy Order is updated or new policy is added
            // or policy is deleted
            RoutingPolicy *policy = GetRoutingPolicy(config_it->routing_policy_);
            if (policy) {
                *oper_it = std::make_pair(policy, policy->generation());
                ++oper_it;
                ++config_it;
                update_policy = true;
            } else {
                // points to routing policy that doesn't exists
                // will revisit in next config notification
                ++config_it;
            }
        }
    }
    for (oper_next = oper_it; oper_it != oper_list->end();
         oper_it = oper_next) {
        // Existing policy(ies) are removed
        ++oper_next;
        oper_list->erase(oper_it);
        update_policy = true;
    }
    for (; config_it != cfg_list.end(); ++config_it) {
        // new policy(ies) are added
        RoutingPolicy *policy = GetRoutingPolicy(config_it->routing_policy_);
        if (policy) {
            oper_list->push_back(std::make_pair(policy, policy->generation()));
        }
        update_policy = true;
    }

    return update_policy;
}

void
RoutingPolicyMgr::RequestWalk(BgpTable *table) {
    CHECK_CONCURRENCY("bgp::Config");
    RoutingPolicySyncState *state = NULL;
    RoutingPolicyWalkRequests::iterator loc = routing_policy_sync_.find(table);
    if (loc != routing_policy_sync_.end()) {
        // Accumulate the walk request till walk is started.
        // After the walk is started don't cancel/interrupt the walk
        // instead remember the request to walk again
        // Walk will restarted after completion of current walk
        // This situation is possible in cases where DBWalker yeilds or
        // config task requests for walk before the previous walk is finished
        if (loc->second->GetWalkerId() != DBTableWalker::kInvalidWalkerId) {
            // This will be reset when the walk actually starts
            loc->second->SetWalkAgain(true);
        }
        return;
    } else {
        state = new RoutingPolicySyncState();
        state->SetWalkerId(DBTableWalker::kInvalidWalkerId);
        routing_policy_sync_.insert(std::make_pair(table, state));
    }
}

bool
RoutingPolicyMgr::StartWalk() {
    CHECK_CONCURRENCY("bgp::Config");

    // For each member table, start a walker to replicate
    for (RoutingPolicyWalkRequests::iterator it = routing_policy_sync_.begin();
         it != routing_policy_sync_.end(); ++it) {
        if (it->second->GetWalkerId() != DBTableWalker::kInvalidWalkerId) {
            // Walk is in progress.
            continue;
        }
        BgpTable *table = it->first;
        DB *db = server()->database();
        DBTableWalker::WalkId id = db->GetWalker()->WalkTable(table, NULL,
            boost::bind(&RoutingPolicyMgr::EvaluateRoutingPolicy, this, _1, _2),
            boost::bind(&RoutingPolicyMgr::WalkDone, this, _1));
        it->second->SetWalkerId(id);
        it->second->SetWalkAgain(false);
    }
    return true;
}

void
RoutingPolicyMgr::WalkDone(DBTableBase *dbtable) {
    CHECK_CONCURRENCY("db::DBTable");
    tbb::mutex::scoped_lock lock(mutex_);
    BgpTable *table = static_cast<BgpTable *>(dbtable);
    RoutingPolicyWalkRequests::iterator loc = routing_policy_sync_.find(table);
    assert(loc != routing_policy_sync_.end());
    RoutingPolicySyncState *policy_sync_state = loc->second;
    if (policy_sync_state->WalkAgain()) {
        policy_sync_state->SetWalkerId(DBTableWalker::kInvalidWalkerId);
        walk_trigger_->Set();
        return;
    }
    delete policy_sync_state;
    routing_policy_sync_.erase(loc);
}

class RoutingPolicy::DeleteActor : public LifetimeActor {
public:
    DeleteActor(BgpServer *server, RoutingPolicy *parent)
            : LifetimeActor(server->lifetime_manager()), parent_(parent) {
    }
    virtual bool MayDelete() const {
        return parent_->MayDelete();
    }
    virtual void Shutdown() {
        parent_->Shutdown();
    }
    virtual void Destroy() {
        parent_->mgr_->DestroyRoutingPolicy(parent_);
    }

private:
    RoutingPolicy *parent_;
};

RoutingPolicy::RoutingPolicy(std::string name, BgpServer *server,
                                 RoutingPolicyMgr *mgr,
                                 const BgpRoutingPolicyConfig *config)
    : name_(name), server_(server), mgr_(mgr), config_(config),
      deleter_(new DeleteActor(server, this)),
      manager_delete_ref_(this, mgr->deleter()), generation_(0) {
    refcount_ = 0;
}

RoutingPolicy::~RoutingPolicy() {
    terms_.clear();
}

RoutingPolicy::PolicyTermPtr RoutingPolicy::BuildTerm(const RoutingPolicyTerm &cfg_term) {
    PolicyTerm::ActionList actions;
    PolicyTerm::MatchList matches;
    // Build the Match object
    if (!cfg_term.match.community_match.empty()) {
        std::vector<std::string> communities_to_match =
            boost::assign::list_of(cfg_term.match.community_match);
        MatchCommunity *community =
         new MatchCommunity(communities_to_match);
        matches.push_back(community);
    }

    if (!cfg_term.match.protocols_match.empty()) {
        MatchProtocol *protocol =
         new MatchProtocol(cfg_term.match.protocols_match);
        matches.push_back(protocol);
    }

    if (!cfg_term.match.prefixes_to_match.empty()) {
        PrefixMatchConfigList inet_prefix_list;
        PrefixMatchConfigList inet6_prefix_list;
        BOOST_FOREACH(PrefixMatchConfig match, cfg_term.match.prefixes_to_match) {
            boost::system::error_code ec;
            Ip4Address ip4;
            int plen;
            ec = Ip4PrefixParse(match.prefix_to_match, &ip4, &plen);
            if (ec.value() == 0) {
                inet_prefix_list.push_back(match);
            } else {
                Ip6Address ip6;
                ec = Inet6PrefixParse(match.prefix_to_match, &ip6, &plen);
                if (ec.value() == 0) {
                    inet6_prefix_list.push_back(match);
                }
            }
        }
        if (!inet_prefix_list.empty()) {
            PrefixMatchInet *prefix = new PrefixMatchInet(inet_prefix_list);
            matches.push_back(prefix);
        } 
        if (!inet6_prefix_list.empty()) {
            PrefixMatchInet6 *prefix = new PrefixMatchInet6(inet6_prefix_list);
            matches.push_back(prefix);
        }
    }

    // Build the Action object
    if (cfg_term.action.action == RoutingPolicyActionConfig::REJECT) {
        RoutingPolicyRejectAction *action = new RoutingPolicyRejectAction();
        actions.push_back(action);
    } else if (cfg_term.action.action == RoutingPolicyActionConfig::NEXT_TERM) {
        RoutingPolicyNexTermAction *action = new RoutingPolicyNexTermAction();
        actions.push_back(action);
    } else if (cfg_term.action.action == RoutingPolicyActionConfig::ACCEPT) {
        RoutingPolicyAcceptAction *action = new RoutingPolicyAcceptAction();
        actions.push_back(action);
    }

    if (!cfg_term.action.update.community_set.empty()) {
        UpdateCommunity *set_comm =
            new UpdateCommunity(cfg_term.action.update.community_set, "set");
        actions.push_back(set_comm);
    }

    if (!cfg_term.action.update.community_remove.empty()) {
        UpdateCommunity *remove_comm =
         new UpdateCommunity(cfg_term.action.update.community_remove, "remove");
        actions.push_back(remove_comm);
    }

    if (!cfg_term.action.update.community_add.empty()) {
        UpdateCommunity *add_comm =
            new UpdateCommunity(cfg_term.action.update.community_add, "add");
        actions.push_back(add_comm);
    }

    if (cfg_term.action.update.local_pref) {
        UpdateLocalPref *local_pref =
            new UpdateLocalPref(cfg_term.action.update.local_pref);
        actions.push_back(local_pref);
    }

    if (cfg_term.action.update.med) {
        UpdateMed *med =
            new UpdateMed(cfg_term.action.update.med);
        actions.push_back(med);
    }

    PolicyTermPtr ret_term;
    if (!actions.empty() || !matches.empty()) {
        ret_term = PolicyTermPtr(new PolicyTerm());
        ret_term->set_actions(actions);
        ret_term->set_matches(matches);
    }

    return ret_term;
}

void RoutingPolicy::ProcessConfig() {
    BOOST_FOREACH(const RoutingPolicyTerm cfg_term, config_->terms()) {
        // Build each terms and insert to operational data
        PolicyTermPtr term = BuildTerm(cfg_term);
        if (term)
            add_term(term);
    }
}

//
// Reprogram policy terms based on new config.
// If the policy term has changed (number of terms got updated, or new term is
// added or earlier term is deleted or existing term is updated), increment the
// generation number to indicate the update
//
void RoutingPolicy::UpdateConfig(const BgpRoutingPolicyConfig *cfg) {
    CHECK_CONCURRENCY("bgp::Config");
    config_ = cfg;
    bool update_policy = false;
    if (terms()->size() != config_->terms().size())
        update_policy = true;

    RoutingPolicyTermList::iterator oper_it = terms()->begin(), oper_next;
    BgpRoutingPolicyConfig::RoutingPolicyTermList::const_iterator
        config_it = config_->terms().begin();
    while (oper_it != terms()->end() && config_it != config_->terms().end()) {
        PolicyTermPtr term = BuildTerm(*config_it);
        if (**oper_it == *term) {
            ++oper_it;
            ++config_it;
        } else {
            if (term) {
                *oper_it = term;
                update_policy = true;
                ++oper_it;
                ++config_it;
            } else {
                ++config_it;
            }
        }
    }
    for (oper_next = oper_it; oper_it != terms()->end(); oper_it = oper_next) {
        ++oper_next;
        terms()->erase(oper_it);
        update_policy = true;
    }
    for (; config_it != config_->terms().end(); ++config_it) {
        PolicyTermPtr term = BuildTerm(*config_it);
        if (term)
            add_term(term);
        update_policy = true;
    }

    if (update_policy) generation_++;
}

void RoutingPolicy::ClearConfig() {
    CHECK_CONCURRENCY("bgp::Config");
    config_ = NULL;
}

void RoutingPolicy::ManagedDelete() {
    deleter_->Delete();
}

void RoutingPolicy::Shutdown() {
    CHECK_CONCURRENCY("bgp::Config");
    ClearConfig();
}

bool RoutingPolicy::MayDelete() const {
    return (refcount_ == 0);
}

LifetimeActor *RoutingPolicy::deleter() {
    return deleter_.get();
}

const LifetimeActor *RoutingPolicy::deleter() const {
    return deleter_.get();
}

bool RoutingPolicy::deleted() const {
    return deleter()->IsDeleted();
}

//
// Attempt to enqueue a delete for the RoutingPolicy.
//
void RoutingPolicy::RetryDelete() {
    if (!deleter_->IsDeleted())
        return;
    deleter_->RetryDelete();
}

RoutingPolicy::PolicyResult RoutingPolicy::operator()(const BgpRoute *route,
                                  const BgpPath *path, BgpAttr *attr) const {
    BOOST_FOREACH(PolicyTermPtr term, terms()) {
        bool terminal = term->terminal();
        bool matched = term->ApplyTerm(route, path, attr);
        if (matched && terminal) {
            return std::make_pair(terminal,
                                  (*term->actions().begin())->accept());
        }
    }
    return std::make_pair(false, true);
}

PolicyTerm::PolicyTerm() {
}

PolicyTerm::~PolicyTerm() {
    STLDeleteValues(&actions_);
    STLDeleteValues(&matches_);
}

bool PolicyTerm::terminal() const {
    if (!actions().empty())
        return (*actions().begin())->terminal();
    return false;
}

bool PolicyTerm::ApplyTerm(const BgpRoute *route, const BgpPath *path,
                           BgpAttr *attr) const {
    bool matched = true;
    BOOST_FOREACH(RoutingPolicyMatch *match, matches()) {
        if (!(*match)(route, path, attr)) {
            matched = false;
            break;
        }
    }
    if (matched) {
        bool first = true;
        BOOST_FOREACH(RoutingPolicyAction *action, actions()) {
            // First action defines what to do with the route
            // accept/reject/next-term
            if (first) {
                if (action->terminal()) {
                    if (!action->accept()) {
                        // out_attr is unaltered
                        break;
                    }
                }
                first = false;
            } else {
                RoutingPolicyUpdateAction *update =
                    static_cast<RoutingPolicyUpdateAction *>(action);
                (*update)(attr);
            }
        }
    }
    return matched;
}

// Compare two terms
bool PolicyTerm::operator==(const PolicyTerm &rhs) const {
    // Different number of match conditions
    if (matches().size() != rhs.matches().size()) return false;
    // Different number of actions conditions
    if (actions().size() != rhs.actions().size()) return false;

    for (MatchList::const_iterator rhs_matches_cit = rhs.matches().begin(),
         lhs_matches_cit = matches().begin();
         lhs_matches_cit != matches().end();
         lhs_matches_cit++,rhs_matches_cit++) {
        // Walk the list of match conditions and compare each match
        if (**rhs_matches_cit == **lhs_matches_cit)
            continue;
        else
            return false;
    }

    for (ActionList::const_iterator lhs_actions_cit = actions().begin(),
         rhs_actions_cit = rhs.actions().begin();
         lhs_actions_cit != actions().end();
         lhs_actions_cit++,rhs_actions_cit++) {
        // Walk the list of actions and compare each action
        if (**rhs_actions_cit == **lhs_actions_cit)
            continue;
        else
            return false;
    }
    return true;
}
