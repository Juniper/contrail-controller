/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __testflow__
#define __testflow__
#include "pkt/flow_entry.h"
#include "test_pkt_util.h"
static uint32_t running_hash = 1;
class TestFlowPkt {
public:
    //Ingress flow
    TestFlowPkt(Address::Family family, const std::string &sip,
                const std::string &dip, uint16_t proto, uint32_t sport,
                uint32_t dport, std::string vrf, uint32_t ifindex) :
        family_(family), sip_(sip), dip_(dip), proto_(proto), sport_(sport),
        dport_(dport), ifindex_(ifindex), mpls_(0), hash_(0),
        allow_wait_for_idle_(true) {
        vrf_ = 
          Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf)->vrf_id();
        if (ifindex_) {
            nh_id_ =
                InterfaceTable::GetInstance()->
                FindInterface(ifindex_)->flow_key_nh()->id();
        } else {
            nh_id_ = GetActiveLabel(mpls_)->nexthop()->id();
        }
    };

    //Ingress flow
    TestFlowPkt(Address::Family family, const std::string &sip,
                const std::string &dip, uint16_t proto, uint32_t sport,
                uint32_t dport, const std::string &vrf, uint32_t ifindex,
                uint32_t hash):
        family_(family), sip_(sip), dip_(dip), proto_(proto),sport_(sport),
        dport_(dport), ifindex_(ifindex), mpls_(0), hash_(hash),
        allow_wait_for_idle_(true) {
         vrf_ = 
          Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf)->vrf_id();
         if (ifindex_) {
             nh_id_ =
                 InterfaceTable::GetInstance()->
                 FindInterface(ifindex_)->flow_key_nh()->id();
         } else {
             nh_id_ = GetActiveLabel(mpls_)->nexthop()->id();
         }
    };

    //Egress flow
    TestFlowPkt(Address::Family family, const std::string &sip,
                const std::string &dip, uint16_t proto, uint32_t sport,
                uint32_t dport, const std::string &vrf, const std::string &osip,
                uint32_t mpls) :
        family_(family), sip_(sip), dip_(dip), proto_(proto), sport_(sport),
        dport_(dport), ifindex_(0), mpls_(mpls), outer_sip_(osip), hash_(0),
        allow_wait_for_idle_(true) {
        vrf_ = 
         Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf)->vrf_id();
        if (ifindex_) {
            nh_id_ =
                InterfaceTable::GetInstance()->
                FindInterface(ifindex_)->flow_key_nh()->id();
        } else {
            nh_id_ = GetActiveLabel(mpls_)->nexthop()->id();
        }
    };

    //Egress flow
    TestFlowPkt(Address::Family family, const std::string &sip,
                const std::string &dip, uint16_t proto, uint32_t sport,
                uint32_t dport, std::string vrf, std::string osip,
                uint32_t mpls, uint32_t hash) :
        family_(family), sip_(sip), dip_(dip), proto_(proto), sport_(sport),
        dport_(dport), ifindex_(0), mpls_(mpls), hash_(hash),
        allow_wait_for_idle_(true) {
         vrf_ = 
          Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf)->vrf_id();
         if (ifindex_) {
             nh_id_ =
                 InterfaceTable::GetInstance()->
                 FindInterface(ifindex_)->flow_key_nh()->id();
         } else {
             nh_id_ = GetActiveLabel(mpls_)->nexthop()->id();
         }
     };

    void SendIngressFlow() {
        if (!hash_) {
            hash_ = running_hash;
            running_hash++;
            if (running_hash > 1000) {
                running_hash = 1;
            }
        }

        switch(proto_) {
        case IPPROTO_TCP:
            if (family_ == Address::INET) {
                TxTcpPacket(ifindex_, sip_.c_str(), dip_.c_str(), sport_,
                            dport_, false, hash_, vrf_);
            } else {
                TxTcp6Packet(ifindex_, sip_.c_str(), dip_.c_str(), sport_,
                             dport_, false, hash_, vrf_);
            }
            break;

        case IPPROTO_UDP:
            if (family_ == Address::INET) {
                TxUdpPacket(ifindex_, sip_.c_str(), dip_.c_str(), sport_,
                            dport_, hash_, vrf_);
            } else {
                TxUdp6Packet(ifindex_, sip_.c_str(), dip_.c_str(), sport_,
                             dport_, hash_, vrf_);
            }
            break;

        default:
            if (family_ == Address::INET) {
                TxIpPacket(ifindex_, sip_.c_str(), dip_.c_str(), proto_, hash_);
            } else {
                TxIp6Packet(ifindex_, sip_.c_str(), dip_.c_str(), proto_,
                            hash_);
            }
            break;
        }
    };

    void SendEgressFlow() {
        if (!hash_) {
            hash_ = running_hash;
            running_hash++;
            if (running_hash > 1000) {
                running_hash = 1;
            }
        }

        std::string self_server = Agent::GetInstance()->router_id().to_string();
        //Populate ethernet interface id
        uint32_t eth_intf_id = 
            EthInterfaceGet(Agent::GetInstance()->
                    fabric_interface_name().c_str())->id();
        switch(proto_) {
        case IPPROTO_TCP:
            if (family_ == Address::INET) {
                TxTcpMplsPacket(eth_intf_id, outer_sip_.c_str(),
                                self_server.c_str(), mpls_, sip_.c_str(),
                                dip_.c_str(), sport_, dport_, false, hash_);
            } else {
                TxTcp6MplsPacket(eth_intf_id, outer_sip_.c_str(), 
                                 self_server.c_str(), mpls_, sip_.c_str(),
                                 dip_.c_str(), sport_, dport_, false, hash_);
            }
            break;

        case IPPROTO_UDP:
            if (family_ == Address::INET) {
                TxUdpMplsPacket(eth_intf_id, outer_sip_.c_str(),
                                self_server.c_str(), mpls_, sip_.c_str(), 
                                dip_.c_str(), sport_, dport_, hash_);
            } else {
                TxUdp6MplsPacket(eth_intf_id, outer_sip_.c_str(), 
                                 self_server.c_str(), mpls_, sip_.c_str(), 
                                 dip_.c_str(), sport_, dport_, hash_);
            }
            break;

        default:
            if (family_ == Address::INET) {
                TxIpMplsPacket(eth_intf_id, outer_sip_.c_str(),
                               self_server.c_str(), mpls_, sip_.c_str(), 
                               dip_.c_str(), proto_, hash_);
            } else {
                TxIp6MplsPacket(eth_intf_id, outer_sip_.c_str(),
                                self_server.c_str(), mpls_, sip_.c_str(), 
                                dip_.c_str(), proto_, hash_);
            }
            break;
        }
    };

    bool FlowStatus(bool active) {
        FlowEntry * fe = FlowFetch();
        if (fe == NULL || fe->deleted()) {
            return !active;
        }

        return active;
    }

    FlowEntry* Send() {
        if (ifindex_) {
            SendIngressFlow();
        } else if (mpls_) {
            SendEgressFlow();
        } else {
            assert(0);
        }
        if (allow_wait_for_idle_) {
            client->WaitForIdle();
        }

        WAIT_FOR(1000, 3000, FlowStatus(true));

        //Get flow 
        FlowEntry * fe = FlowFetch();
        EXPECT_TRUE(fe != NULL);
        return fe;
    };

    void Delete() {
        FlowEntry * fe = FlowFetch();
        if (fe == NULL) {
            return;
        }

        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        FlowDeleteTask * task = new FlowDeleteTask(fe->key());
        scheduler->Enqueue(task);

        WAIT_FOR(1000, 3000, FlowStatus(false));
    };

    FlowEntry *FlowFetch() {
        uint16_t lookup_dport = dport_;
        if (proto_ == IPPROTO_ICMPV6) {
            lookup_dport = ICMP6_ECHO_REPLY;
        }
        FlowEntry *fe = FlowGet(vrf_, sip_, dip_, proto_, sport_, lookup_dport,
                                nh_id_);
        return fe;
    }

    void set_allow_wait_for_idle(bool allow) {
        allow_wait_for_idle_ = allow;
    }

private:
    class FlowDeleteTask : public Task {
    public:
        FlowDeleteTask(const FlowKey &key) :
        Task(TaskScheduler::GetInstance()->GetTaskId(kTaskFlowEvent), 0),
        key_(key) {}
        virtual bool Run() {
            FlowProto *proto = Agent::GetInstance()->pkt()->get_flow_proto();
            FlowEntry *flow = proto->Find(key_,
                                          proto->GetTable(0)->table_index());
            if (flow) {
                proto->DeleteFlowRequest(flow);
            }
            return true;
        }
        std::string Description() const { return "FlowDeleteTask"; }
    private:
        FlowKey key_;
    };

    Address::Family family_;
    std::string    sip_;
    std::string    dip_;
    uint16_t       proto_;
    uint32_t       sport_;
    uint32_t       dport_;
    uint32_t       vrf_;
    uint32_t       ifindex_;
    uint32_t       mpls_;
    std::string    outer_sip_;
    uint32_t       hash_; 
    bool           allow_wait_for_idle_;
    uint32_t       nh_id_;
};

class FlowVerify {
public:
    virtual ~FlowVerify() {};
    virtual void Verify(FlowEntry *fe) = 0;
};

class VerifyVn : public FlowVerify {
public:
    VerifyVn(std::string src_vn, std::string dest_vn):
        src_vn_(src_vn), dest_vn_(dest_vn) {};
    virtual ~VerifyVn() {};

    virtual void Verify(FlowEntry *fe) {
        EXPECT_TRUE(VnMatch(fe->data().source_vn_list, src_vn_));
        EXPECT_TRUE(VnMatch(fe->data().dest_vn_list, dest_vn_));

        if (true) {
            FlowEntry *rev = fe->reverse_flow_entry();
            EXPECT_TRUE(rev != NULL);
            EXPECT_TRUE(VnMatch(rev->data().source_vn_list, dest_vn_));
            EXPECT_TRUE(VnMatch(rev->data().dest_vn_list, src_vn_));
        }
    };

private:
    std::string src_vn_;
    std::string dest_vn_;
};

class VerifyQosAction : public FlowVerify {
public:
    VerifyQosAction(std::string qos_action1,
                    std::string qos_action2):
    qos_action1_(qos_action1), qos_action2_(qos_action2) {}
    ~VerifyQosAction() {};

    virtual void Verify(FlowEntry *fe) {
        FlowEntry *rev = fe->reverse_flow_entry();
        EXPECT_TRUE(QosConfigGetByIndex(fe->data().qos_config_idx)->name() ==
                    qos_action1_);
        EXPECT_TRUE(QosConfigGetByIndex(rev->data().qos_config_idx)->name()==
                    qos_action2_);
    }

    std::string qos_action1_;
    std::string qos_action2_;
};

class ShortFlow : public FlowVerify {
public:
    ShortFlow() {}
    virtual ~ShortFlow() {};

    virtual void Verify(FlowEntry *fe) {
        EXPECT_TRUE(fe->is_flags_set(FlowEntry::ShortFlow) == true);
        EXPECT_TRUE((fe->data().match_p.action_info.action & (1 << TrafficAction::DENY)) != 0);
        FlowEntry *rev = fe->reverse_flow_entry();
        EXPECT_TRUE(rev->is_flags_set(FlowEntry::ShortFlow) == true);
        EXPECT_TRUE((rev->data().match_p.action_info.action & (1 << TrafficAction::DENY)) != 0);
    }
private:
};


class VerifyFlowAction : public FlowVerify {
public:
    VerifyFlowAction(TrafficAction::Action action):action_(action) {}
    virtual ~VerifyFlowAction() {}
    virtual void Verify(FlowEntry *fe) {
        EXPECT_TRUE((fe->data().match_p.action_info.action & (1 << action_)) != 0);
        FlowEntry *rev = fe->reverse_flow_entry();
        EXPECT_TRUE((rev->data().match_p.action_info.action & (1 << action_)) != 0);
    }
private:
    TrafficAction::Action action_;
};

class VerifySrcDstVrf : public FlowVerify {
public:
    VerifySrcDstVrf(std::string fwd_flow_src_vrf, std::string fwd_flow_dest_vrf,
                    std::string rev_flow_src_vrf, std::string rev_flow_dest_vrf):
        fwd_flow_src_vrf_(fwd_flow_src_vrf),
        fwd_flow_dest_vrf_(fwd_flow_dest_vrf),
        rev_flow_src_vrf_(rev_flow_src_vrf),
        rev_flow_dest_vrf_(rev_flow_dest_vrf) {};

    virtual ~VerifySrcDstVrf() {};

    void Verify(FlowEntry *fe) {
        Agent *agent = Agent::GetInstance();
        const VrfEntry *src_vrf =
            agent->vrf_table()->FindVrfFromName(fwd_flow_src_vrf_);
        EXPECT_TRUE(src_vrf != NULL);

        const VrfEntry *dest_vrf =
            agent->vrf_table()->FindVrfFromName(fwd_flow_dest_vrf_);
        EXPECT_TRUE(dest_vrf != NULL);

        EXPECT_TRUE(fe->data().vrf == src_vrf->vrf_id());
        EXPECT_TRUE(fe->data().dest_vrf == dest_vrf->vrf_id());

        FlowEntry *rev = fe->reverse_flow_entry();
        EXPECT_TRUE(rev != NULL);

        src_vrf =
            agent->vrf_table()->FindVrfFromName(rev_flow_src_vrf_);
        EXPECT_TRUE(src_vrf != NULL);

        dest_vrf =
            agent->vrf_table()->FindVrfFromName(rev_flow_dest_vrf_);
        EXPECT_TRUE(dest_vrf != NULL);

        EXPECT_TRUE(rev->data().vrf == src_vrf->vrf_id());
        EXPECT_TRUE(rev->data().dest_vrf == dest_vrf->vrf_id());
    }

private:
    std::string fwd_flow_src_vrf_;
    std::string fwd_flow_dest_vrf_;
    std::string rev_flow_src_vrf_;
    std::string rev_flow_dest_vrf_;
};

class VerifyVrf : public FlowVerify {
public:
    VerifyVrf(std::string src_vrf, std::string dest_vrf):
        src_vrf_(src_vrf), dest_vrf_(dest_vrf) {};
    virtual ~VerifyVrf() {};

    void Verify(FlowEntry *fe) {
        const VrfEntry *src_vrf = 
            Agent::GetInstance()->vrf_table()->FindVrfFromName(src_vrf_);
        EXPECT_TRUE(src_vrf != NULL);

        const VrfEntry *dest_vrf = 
            Agent::GetInstance()->vrf_table()->FindVrfFromName(dest_vrf_);
        EXPECT_TRUE(dest_vrf != NULL);

        EXPECT_TRUE(fe->data().vrf == src_vrf->vrf_id());

        if (true) {
            FlowEntry *rev = fe->reverse_flow_entry();
            EXPECT_TRUE(rev != NULL);
            EXPECT_TRUE(rev->data().vrf == dest_vrf->vrf_id());
        }
    };

private:
    std::string src_vrf_;
    std::string dest_vrf_;
};

class VerifyDestVrf : public FlowVerify {
public:
    VerifyDestVrf(std::string src_vrf, std::string dest_vrf):
        src_vrf_(src_vrf), dest_vrf_(dest_vrf) {};
    virtual ~VerifyDestVrf() {};

    void Verify(FlowEntry *fe) {
        const VrfEntry *src_vrf =
            Agent::GetInstance()->vrf_table()->FindVrfFromName(src_vrf_);
        EXPECT_TRUE(src_vrf != NULL);

        const VrfEntry *dest_vrf =
            Agent::GetInstance()->vrf_table()->FindVrfFromName(dest_vrf_);
        EXPECT_TRUE(dest_vrf != NULL);

        EXPECT_TRUE(fe->data().flow_source_vrf == dest_vrf->vrf_id());

        if (true) {
            FlowEntry *rev = fe->reverse_flow_entry();
            EXPECT_TRUE(rev != NULL);
            EXPECT_TRUE(rev->data().flow_source_vrf == src_vrf->vrf_id());
        }
    };

private:
    std::string src_vrf_;
    std::string dest_vrf_;
};

struct VerifyNat : public FlowVerify {
public:
    VerifyNat(std::string nat_sip, std::string nat_dip, uint32_t proto,
              uint32_t nat_sport, uint32_t nat_dport):
              nat_sip_(nat_sip), nat_dip_(nat_dip), nat_sport_(nat_sport),
              nat_dport_(nat_dport) { };
    virtual ~VerifyNat() { };

    void Verify(FlowEntry *fe) {
        FlowEntry *rev = fe->reverse_flow_entry();
        EXPECT_TRUE(rev != NULL);
        EXPECT_TRUE(rev->key().src_addr == Ip4Address::from_string(nat_sip_));
        EXPECT_TRUE(rev->key().dst_addr == Ip4Address::from_string(nat_dip_));
        EXPECT_TRUE(rev->key().src_port == nat_sport_);
        uint32_t dport = fe->linklocal_src_port();
        if (dport == 0) {
            dport = fe->key().src_port;
        }
        EXPECT_TRUE(rev->key().dst_port == nat_dport_ ||
                    rev->key().dst_port == dport);
    };

private:
    std::string nat_sip_;
    std::string nat_dip_;
    uint32_t    nat_sport_;
    uint32_t    nat_dport_;
};

class VerifyEcmp : public FlowVerify {
public:
    VerifyEcmp(bool fwd_ecmp, bool rev_ecmp):
        fwd_flow_is_ecmp_(fwd_ecmp), rev_flow_is_ecmp_(rev_ecmp) { };
    virtual ~VerifyEcmp() { };
    void Verify(FlowEntry *fe) {
        FlowEntry *rev = fe->reverse_flow_entry();
        EXPECT_TRUE(rev != NULL);

        if (fwd_flow_is_ecmp_) {
            EXPECT_TRUE(fe->is_flags_set(FlowEntry::EcmpFlow) == true);
            EXPECT_TRUE(fe->data().component_nh_idx != (uint32_t) -1);
        } else {
            EXPECT_TRUE(fe->is_flags_set(FlowEntry::EcmpFlow) == false);
            EXPECT_TRUE(fe->data().component_nh_idx == (uint32_t) -1);
        }

        if (rev_flow_is_ecmp_) {
            EXPECT_TRUE(rev->is_flags_set(FlowEntry::EcmpFlow) == true);
            EXPECT_TRUE(rev->data().component_nh_idx != (uint32_t) -1);
        } else {
            EXPECT_TRUE(rev->is_flags_set(FlowEntry::EcmpFlow) == false);
            EXPECT_TRUE(rev->data().component_nh_idx == (uint32_t) -1);
        }
    };

private:
    bool fwd_flow_is_ecmp_;
    bool rev_flow_is_ecmp_;
};

class VerifyAction : public FlowVerify {
public:
    VerifyAction(uint32_t action, uint32_t rev_action):
        action_(action), rev_action_(rev_action) { };
    virtual ~VerifyAction() { };
    void Verify(FlowEntry *fe) {
        FlowEntry *rev = fe->reverse_flow_entry();
        EXPECT_TRUE(rev != NULL);
        EXPECT_TRUE(fe->match_p().action_info.action == action_);
        EXPECT_TRUE(rev->match_p().action_info.action == rev_action_);
    };

private:
    uint32_t action_;
    uint32_t rev_action_;
};

class VerifyRpf : public FlowVerify {
public:
    VerifyRpf(uint32_t forward_flow_nh, uint32_t reverse_flow_nh):
        forward_flow_rpf_nh_(forward_flow_nh),
        reverse_flow_rpf_nh_(reverse_flow_nh) {};
    virtual ~VerifyRpf() {};
    void Verify(FlowEntry *fe) {
        FlowEntry *rev = fe->reverse_flow_entry();
        EXPECT_TRUE(rev != NULL);
        EXPECT_TRUE(fe->data().rpf_nh->id() == forward_flow_rpf_nh_);
        EXPECT_TRUE(rev->data().rpf_nh->id() == reverse_flow_rpf_nh_);
    }
private:
    uint32_t forward_flow_rpf_nh_;
    uint32_t reverse_flow_rpf_nh_;
};

class VerifyRpfEnable : public FlowVerify {
public:
    VerifyRpfEnable(bool fwd_rpf_enable, bool rev_rpf_enable):
        fwd_rpf_enable_(fwd_rpf_enable),
        rev_rpf_enable_(rev_rpf_enable){};
    virtual ~VerifyRpfEnable() {};
    void Verify(FlowEntry *fe) {
        FlowEntry *rev = fe->reverse_flow_entry();
        EXPECT_TRUE(rev != NULL);
        EXPECT_TRUE(fe->data().enable_rpf == fwd_rpf_enable_);
        EXPECT_TRUE(rev->data().enable_rpf == rev_rpf_enable_);
    }
private:
    bool fwd_rpf_enable_;
    bool rev_rpf_enable_;
};


struct TestFlow {
    ~TestFlow() {
        for (int i = 0; i < 10; i++) {
            if (action_[i]) {
                delete action_[i];
            }
        }
    };

    void Verify(FlowEntry *fe) {
        EXPECT_TRUE(fe != NULL);
        if (fe == NULL) {
            return;
        }
        for (int i = 0; i < 10; i++) {
            if (action_[i]) {
                action_[i]->Verify(fe);
            }
        }
    };

    FlowEntry* Send() {
        return pkt_.Send();
    };

    void Delete() {
        pkt_.Delete();
    };

    TestFlowPkt pkt_;
    FlowVerify* action_[10];
};

void CreateFlow(TestFlow *tflow, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        FlowEntry *fe = tflow->Send();
        tflow->Verify(fe);
        tflow = tflow + 1;
    }
}

void DeleteFlow(TestFlow *tflow, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        tflow->Delete();
    }
}
#endif
