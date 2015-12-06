/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_ROUTING_POLICY_ROUTING_POLICY_H_
#define SRC_BGP_ROUTING_POLICY_ROUTING_POLICY_H_

#include <boost/scoped_ptr.hpp>
#include <boost/intrusive_ptr.hpp>

#include <map>
#include <string>

#include "base/lifetime.h"
#include "base/util.h"

#include <sandesh/sandesh_trace.h>

class BgpServer;
class RoutingPolicyMgr;
class BgpRoutingPolicyConfig;
class RoutingPolicyMatch;
class RoutingPolicyAction;
class RoutingPolicyTerm;
class BgpAttr;
class BgpRoute;

class PolicyTerm {
public:
    typedef std::vector<RoutingPolicyAction*> ActionList;
    typedef std::vector<RoutingPolicyMatch*> MatchList;
    PolicyTerm();
    ~PolicyTerm();
    bool terminal() const;
    bool ApplyTerm(const BgpRoute *route, BgpAttr *attr) const;
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
private:
    MatchList matches_;
    ActionList actions_;
};

class RoutingPolicy {
public:
    typedef std::vector<PolicyTerm*> RoutingPolicyTermList;
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

    RoutingPolicyTermList &terms() { return terms_; }
    const RoutingPolicyTermList &terms() const { return terms_; }
    void add_term(PolicyTerm *term) {
        terms_.push_back(term);
    }

    PolicyResult operator()(const BgpRoute *route, BgpAttr *attr) const;

private:
    friend class RoutingPolicyMgr;
    class DeleteActor;
    friend void intrusive_ptr_add_ref(RoutingPolicy *policy);
    friend void intrusive_ptr_release(RoutingPolicy *policy);

    PolicyTerm *BuildTerm(const RoutingPolicyTerm &term);
    std::string name_;
    BgpServer *server_;
    RoutingPolicyMgr *mgr_;
    const BgpRoutingPolicyConfig *config_;
    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<RoutingPolicy> manager_delete_ref_;

    // Updated when routing policy undergoes a change
    uint32_t refcount_;
    uint32_t generation_;
    RoutingPolicyTermList terms_;
};

inline void intrusive_ptr_add_ref(RoutingPolicy *policy) {
    policy->refcount_++;
}

inline void intrusive_ptr_release(RoutingPolicy *policy) {
    assert(policy->refcount_ != 0);
    if (--policy->refcount_ == 0) {
        if (policy->MayDelete())
            policy->RetryDelete();
    }
}

class RoutingPolicyMgr {
public:
    typedef std::map<std::string, RoutingPolicy*> RoutingPolicyList;
    typedef RoutingPolicyList::iterator name_iterator;
    typedef RoutingPolicyList::const_iterator const_name_iterator;

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

    RoutingPolicy::PolicyResult ApplyPolicy(const RoutingPolicy *policy,
                     const BgpRoute *route, BgpAttr *attr) const;

private:
    class DeleteActor;

    BgpServer *server_;
    RoutingPolicyList routing_policies_;
    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<RoutingPolicyMgr> server_delete_ref_;
    SandeshTraceBufferPtr trace_buf_;
};
#endif // SRC_BGP_ROUTING_POLICY_ROUTING_POLICY_H_
