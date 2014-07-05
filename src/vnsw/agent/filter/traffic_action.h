/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_TRAFFIC_ACTION_H__
#define __AGENT_TRAFFIC_ACTION_H__

#include <vector>
#include <string>
#include <base/util.h>
#include <net/address.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <oper/agent_types.h>

class TrafficAction {
public:
    enum TrafficActionType {
      SIMPLE_ACTION = 1,
      MIRROR_ACTION = 2,
      VRF_TRANSLATE_ACTION = 3,
    };
    // Don't go beyond 31
    enum Action {
        ALERT = 1,
        DROP = 2,
        DENY = 3,
        LOG = 4,
        PASS = 5,
        REJECT = 6,
        MIRROR = 7,
        VRF_TRANSLATE = 8,
        TRAP = 28,
        IMPLICIT_DENY = 29,
        RESERVED = 30,
        UNKNOWN = 31,
    };
    static const uint32_t DROP_FLAGS = ((1 << DROP) | (1 << DENY) | 
                                        (1 << REJECT));
    static const uint32_t PASS_FLAGS = ((1 << PASS));
    static const uint32_t IMPLICIT_DENY_FLAGS = ((1 << IMPLICIT_DENY));

    TrafficAction() {};
    virtual ~TrafficAction() {};

    bool IsDrop() const;
    virtual Action GetAction() {return action_;};
    virtual TrafficActionType GetActionType() {return tact_type;};
    static std::string ActionToString(enum Action at);
    virtual void SetActionSandeshData(std::vector<ActionStr> &actions); 
    virtual bool Compare(const TrafficAction &rhs) const = 0;
    bool operator==(const TrafficAction &rhs) const {
        if (tact_type != rhs.tact_type) {
            return false;
        }
        if (action_ != rhs.action_) {
            return false;
        }
        return Compare(rhs);
    }

    TrafficActionType tact_type;
    Action action_;
};

class SimpleAction : public TrafficAction {
public:
    SimpleAction(Action action) {action_ = action; tact_type = SIMPLE_ACTION;};
    ~SimpleAction() {};
    Action GetAction() const {return action_;};
    virtual bool Compare(const TrafficAction &rhs) const {
        if (action_ != rhs.action_) {
            return false;
        }
        return true;
    }
private:
    DISALLOW_COPY_AND_ASSIGN(SimpleAction);
};

class MirrorAction : public TrafficAction {
public:
    MirrorAction(std::string analyzer_name, std::string vrf_name, 
                 IpAddress ip, uint16_t port, std::string encap) : 
                                            analyzer_name_(analyzer_name),
                                            vrf_name_(vrf_name), m_ip_(ip), 
                                            port_(port), encap_(encap) {
                                    tact_type = MIRROR_ACTION; 
                                    action_ = MIRROR;};
    ~MirrorAction();
                                    // {};
    IpAddress GetIp() {return m_ip_;};
    uint32_t  GetPort() {return port_;};
    std::string vrf_name() {return vrf_name_;};
    std::string GetAnalyzerName() {return analyzer_name_;};
    std::string GetEncap() {return encap_;};
    virtual void SetActionSandeshData(std::vector<ActionStr> &actions); 
    virtual bool Compare(const TrafficAction &rhs) const;
private:
    std::string analyzer_name_;
    std::string vrf_name_;
    //Action action_;
    IpAddress m_ip_;
    uint16_t port_;
    std::string encap_;
    DISALLOW_COPY_AND_ASSIGN(MirrorAction);
};

class VrfTranslateAction : public TrafficAction {
public:
    VrfTranslateAction(const std::string &vrf_name, bool ignore_acl):
        vrf_name_(vrf_name), ignore_acl_(ignore_acl) {
        tact_type = VRF_TRANSLATE_ACTION, action_ = VRF_TRANSLATE; };
    const std::string& vrf_name() const { return vrf_name_;}
    bool ignore_acl() const { return ignore_acl_;}
    virtual void SetActionSandeshData(std::vector<ActionStr> &actions);
    virtual bool Compare(const TrafficAction &rhs) const;
private:
    std::string vrf_name_;
    bool ignore_acl_;
};
#endif
