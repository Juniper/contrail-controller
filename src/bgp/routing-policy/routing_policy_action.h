/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_ROUTING_POLICY_ROUTING_POLICY_ACTION_H_
#define SRC_BGP_ROUTING_POLICY_ROUTING_POLICY_ACTION_H_

#include <stdint.h>
#include <string>
#include <typeinfo>
#include <vector>

class BgpAttr;

class RoutingPolicyAction {
public:
    virtual ~RoutingPolicyAction() {}
    // Whether the action is terminal (Accept/Reject)
    virtual bool terminal()  const = 0;
    virtual bool accept()  const = 0;
    virtual std::string ToString() const = 0;
    virtual bool operator==(const RoutingPolicyAction &action) const {
        if (typeid(*this) == typeid(action))
            return IsEqual(action);
        return false;
    }
    virtual bool IsEqual(const RoutingPolicyAction &action) const = 0;
};

class RoutingPolicyUpdateAction : public RoutingPolicyAction {
public:
    virtual ~RoutingPolicyUpdateAction() {}
    bool terminal()  const { return false; }
    bool accept() const { return true; }
    virtual void operator()(BgpAttr *out_attr) const = 0;
};

class RoutingPolicyAcceptAction : public RoutingPolicyAction {
public:
    virtual ~RoutingPolicyAcceptAction() {}
    bool terminal()  const { return true; }
    bool accept() const { return true; }
    std::string ToString() const {
        return "accept";
    }
    virtual bool IsEqual(const RoutingPolicyAction &action) const {
        return true;
    }
};

class RoutingPolicyRejectAction : public RoutingPolicyAction {
public:
    virtual ~RoutingPolicyRejectAction() {}
    bool terminal()  const { return true; }
    bool accept() const { return false; }
    std::string ToString() const {
        return "reject";
    }
    virtual bool IsEqual(const RoutingPolicyAction &action) const {
        return true;
    }
};

class RoutingPolicyNexTermAction : public RoutingPolicyAction {
public:
    virtual ~RoutingPolicyNexTermAction() {}
    bool terminal()  const { return false; }
    bool accept() const { return false; }
    std::string ToString() const {
        return "next-term";
    }
    virtual bool IsEqual(const RoutingPolicyAction &action) const {
        return true;
    }
};

class UpdateCommunity : public RoutingPolicyUpdateAction {
public:
    typedef std::vector<uint32_t> CommunityList;
    enum CommunityUpdateOp {
        ADD,
        REMOVE,
        SET
    };
    UpdateCommunity(const std::vector<std::string> communities, std::string op);
    virtual ~UpdateCommunity() {};
    virtual void operator()(BgpAttr *out_attr) const;
    std::string ToString() const;
    virtual bool IsEqual(const RoutingPolicyAction &community) const;
    const CommunityList &communities() const {
        return communities_;
    }
private:
    CommunityList communities_;
    CommunityUpdateOp op_;
};

class UpdateLocalPref : public RoutingPolicyUpdateAction {
public:
    UpdateLocalPref(uint32_t local_pref);
    virtual ~UpdateLocalPref() {}
    virtual void operator()(BgpAttr *out_attr) const;
    std::string ToString() const;
    virtual bool IsEqual(const RoutingPolicyAction &local_pref) const;
private:
    uint32_t local_pref_;
};

class UpdateMed : public RoutingPolicyUpdateAction {
public:
    UpdateMed(uint32_t med);
    virtual ~UpdateMed() {}
    virtual void operator()(BgpAttr *out_attr) const;
    std::string ToString() const;
    virtual bool IsEqual(const RoutingPolicyAction &med) const;
private:
    uint32_t med_;
};

#endif // SRC_BGP_ROUTING_POLICY_ROUTING_POLICY_ACTION_H_
