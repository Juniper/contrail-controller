/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_TRAFFIC_ACTION_H__
#define __AGENT_TRAFFIC_ACTION_H__

#include <vector>
#include <string>
#include <base/util.h>
#include <net/address.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/nil_generator.hpp>

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <oper/qos_config.h>

#include <agent_types.h>

class TrafficAction {
public:
    enum TrafficActionType {
      SIMPLE_ACTION = 1,
      MIRROR_ACTION = 2,
      VRF_TRANSLATE_ACTION = 3,
      LOG_ACTION = 4,
      ALERT_ACTION = 5,
      QOS_ACTION = 6
    };
    // Don't go beyond 31
    enum Action {
        ALERT = 1,
        DENY = 3,
        LOG = 4,
        PASS = 5,
        MIRROR = 7,
        VRF_TRANSLATE = 8,
        APPLY_QOS = 9,
        TRAP = 28,
        IMPLICIT_DENY = 29,
        RESERVED = 30,
        UNKNOWN = 31,
    };
    static const std::string kActionLogStr;
    static const std::string kActionAlertStr;
    static const uint32_t DROP_FLAGS = ((1 << DENY));
    static const uint32_t PASS_FLAGS = ((1 << PASS));
    static const uint32_t IMPLICIT_DENY_FLAGS = ((1 << IMPLICIT_DENY));

    TrafficAction() {}
    TrafficAction(Action action, TrafficActionType type) : action_(action),
        action_type_(type) {}
    virtual ~TrafficAction() {}

    bool IsDrop() const;
    Action action() const {return action_;}
    TrafficActionType action_type() const {return action_type_;}
    static std::string ActionToString(enum Action at);
    virtual void SetActionSandeshData(std::vector<ActionStr> &actions); 
    virtual bool Compare(const TrafficAction &rhs) const = 0;
    bool operator==(const TrafficAction &rhs) const {
        if (action_type_ != rhs.action_type_) {
            return false;
        }
        if (action_ != rhs.action_) {
            return false;
        }
        return Compare(rhs);
    }
private:
    Action action_;
    TrafficActionType action_type_;
};

class SimpleAction : public TrafficAction {
public:
    SimpleAction(Action action) : TrafficAction(action, SIMPLE_ACTION) {}
    ~SimpleAction() {}
    virtual bool Compare(const TrafficAction &rhs) const {
        if (action() != rhs.action()) {
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
         TrafficAction(MIRROR, MIRROR_ACTION), analyzer_name_(analyzer_name),
         vrf_name_(vrf_name), m_ip_(ip), port_(port), encap_(encap) {}
    ~MirrorAction();
    IpAddress GetIp() {return m_ip_;}
    uint32_t  GetPort() {return port_;}
    std::string vrf_name() {return vrf_name_;}
    std::string GetAnalyzerName() {return analyzer_name_;}
    std::string GetEncap() {return encap_;}
    virtual void SetActionSandeshData(std::vector<ActionStr> &actions); 
    virtual bool Compare(const TrafficAction &rhs) const;
private:
    std::string analyzer_name_;
    std::string vrf_name_;
    IpAddress m_ip_;
    uint16_t port_;
    std::string encap_;
    DISALLOW_COPY_AND_ASSIGN(MirrorAction);
};

class VrfTranslateAction : public TrafficAction {
public:
    VrfTranslateAction(const std::string &vrf_name, bool ignore_acl):
        TrafficAction(VRF_TRANSLATE, VRF_TRANSLATE_ACTION),
        vrf_name_(vrf_name), ignore_acl_(ignore_acl) {}
    const std::string& vrf_name() const { return vrf_name_;}
    bool ignore_acl() const { return ignore_acl_;}
    virtual void SetActionSandeshData(std::vector<ActionStr> &actions);
    virtual bool Compare(const TrafficAction &rhs) const;
private:
    std::string vrf_name_;
    bool ignore_acl_;
};

class QosConfigAction : public TrafficAction {
public:
    QosConfigAction() :
        TrafficAction(APPLY_QOS, QOS_ACTION), name_(""), qos_config_ref_(NULL) {}
    QosConfigAction(const std::string &qos_config_name):
        TrafficAction(APPLY_QOS, QOS_ACTION),
        name_(qos_config_name), qos_config_ref_(NULL) {}
    virtual ~QosConfigAction() { }
    const std::string& name() const {
        return name_;
    }

    void set_qos_config_ref(const AgentQosConfig *config) {
        qos_config_ref_ = config;
    }
    const AgentQosConfig* qos_config_ref() const {
        return qos_config_ref_.get();
    }

    virtual bool Compare(const TrafficAction &rhs) const;
    virtual void SetActionSandeshData(std::vector<ActionStr> &actions);
private:
    std::string name_;
    AgentQosConfigConstRef qos_config_ref_;
};

class LogAction : public TrafficAction {
public:
    LogAction() : TrafficAction(LOG, LOG_ACTION) {}
    ~LogAction() {}
    virtual bool Compare(const TrafficAction &rhs) const {
        if (action() != rhs.action()) {
            return false;
        }
        return true;
    }
private:
    DISALLOW_COPY_AND_ASSIGN(LogAction);
};

class AlertAction : public TrafficAction {
public:
    AlertAction() : TrafficAction(ALERT, ALERT_ACTION) {}
    ~AlertAction() {}
    virtual bool Compare(const TrafficAction &rhs) const {
        if (action() != rhs.action()) {
            return false;
        }
        return true;
    }
private:
    DISALLOW_COPY_AND_ASSIGN(AlertAction);
};
#endif
