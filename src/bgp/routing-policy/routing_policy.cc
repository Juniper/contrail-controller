/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-policy/routing_policy.h"

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include "base/task_annotations.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_server.h"
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

RoutingPolicy::PolicyResult RoutingPolicyMgr::ApplyPolicy(
                             const RoutingPolicy *policy, const BgpRoute *route,
                             const BgpAttr *in_attr, BgpAttr *out_attr) const {
    return (*policy)(route, in_attr, out_attr);
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
      manager_delete_ref_(this, mgr->deleter()), refcount_(0), generation_(0) {
}

RoutingPolicy::~RoutingPolicy() {
    STLDeleteValues(&terms_);
}

PolicyTerm *RoutingPolicy::BuildTerm(const RoutingPolicyTerm &cfg_term) {
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
    if (!cfg_term.match.prefix_match.prefix_to_match.empty()) {
        boost::system::error_code ec;
        Ip4Address ip4;
        int plen;
        ec = Ip4PrefixParse(cfg_term.match.prefix_match.prefix_to_match,
                            &ip4, &plen);
        if (ec.value() == 0) {
            PrefixMatchInet *prefix =
                new PrefixMatchInet(cfg_term.match.prefix_match.prefix_to_match,
                                 cfg_term.match.prefix_match.prefix_match_type);
            matches.push_back(prefix);
        } else {
            Ip6Address ip6;
            ec = Inet6PrefixParse(cfg_term.match.prefix_match.prefix_to_match,
                                  &ip6, &plen);
            if (ec.value() == 0) {
                PrefixMatchInet6 *prefix = new
                   PrefixMatchInet6(cfg_term.match.prefix_match.prefix_to_match,
                                 cfg_term.match.prefix_match.prefix_match_type);
                matches.push_back(prefix);
            }
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
            new UpdateCommunity(cfg_term.action.update.community_set, "remove");
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

    PolicyTerm *ret_term = NULL;
    if (!actions.empty() || !matches.empty()) {
        ret_term = new PolicyTerm();
        ret_term->set_actions(actions);
        ret_term->set_matches(matches);
    }

    return ret_term;
}

void RoutingPolicy::ProcessConfig() {
    BOOST_FOREACH(const RoutingPolicyTerm cfg_term, config_->terms()) {
        // Build each terms and insert to operational data
        PolicyTerm *term = BuildTerm(cfg_term);
        add_term(term);
    }
}

void RoutingPolicy::UpdateConfig(const BgpRoutingPolicyConfig *cfg) {
    CHECK_CONCURRENCY("bgp::Config");
    config_ = cfg;
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
                               const BgpAttr *in_attr, BgpAttr *out_attr) const {
    BOOST_FOREACH(PolicyTerm *term, terms()) {
        bool terminal = term->terminal();
        bool matched = term->ApplyTerm(route, in_attr, out_attr);
        if (terminal && matched) {
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

bool PolicyTerm::ApplyTerm(const BgpRoute *route,
                           const BgpAttr *in_attr, BgpAttr *out_attr) const {
    bool matched = true;
    BOOST_FOREACH(RoutingPolicyMatch *match, matches()) {
        if (!(*match)(route, in_attr)) {
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
                (*update)(in_attr, out_attr);
            }
        }
    }
    return matched;
}
