/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __testflow__
#define __testflow__
#include "test_pkt_util.h"
class TestFlowPkt {
public:
    //Ingress flow
    TestFlowPkt(std::string sip, std::string dip, uint16_t proto, uint32_t sport,
                uint32_t dport, std::string vrf, uint32_t ifindex) : sip_(sip), 
                dip_(dip), proto_(proto), sport_(sport), dport_(dport), 
                ifindex_(ifindex), mpls_(0), hash_(0),
                allow_wait_for_idle_(true) {
        vrf_ = 
          Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf)->vrf_id();
        if (ifindex_) {
            nh_id_ =
                InterfaceTable::GetInstance()->
                FindInterface(ifindex_)->flow_key_nh()->id();
        } else {
            nh_id_ = GetActiveLabel(MplsLabel::VPORT_NH, mpls_)->nexthop()->id();
        }
    };

    //Ingress flow
    TestFlowPkt(std::string sip, std::string dip, uint16_t proto, uint32_t sport,
                uint32_t dport, std::string vrf, uint32_t ifindex, uint32_t hash):
                sip_(sip), dip_(dip), proto_(proto), sport_(sport), dport_(dport),
                ifindex_(ifindex), mpls_(0), hash_(hash),
                allow_wait_for_idle_(true) {
         vrf_ = 
          Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf)->vrf_id();
         if (ifindex_) {
             nh_id_ =
                 InterfaceTable::GetInstance()->
                 FindInterface(ifindex_)->flow_key_nh()->id();
         } else {
             nh_id_ = GetActiveLabel(MplsLabel::VPORT_NH, mpls_)->nexthop()->id();
         }
    };

    //Egress flow
    TestFlowPkt(std::string sip, std::string dip, uint16_t proto, uint32_t sport,
                uint32_t dport, std::string vrf, std::string osip, uint32_t mpls) :
                sip_(sip), dip_(dip), proto_(proto), sport_(sport), dport_(dport),
                ifindex_(0), mpls_(mpls), outer_sip_(osip), hash_(0),
                allow_wait_for_idle_(true) {
        vrf_ = 
         Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf)->vrf_id();
        if (ifindex_) {
            nh_id_ =
                InterfaceTable::GetInstance()->
                FindInterface(ifindex_)->flow_key_nh()->id();
        } else {
            nh_id_ = GetActiveLabel(MplsLabel::VPORT_NH, mpls_)->nexthop()->id();
        }
    };

    //Egress flow
    TestFlowPkt(std::string sip, std::string dip, uint16_t proto, uint32_t sport,
                uint32_t dport, std::string vrf, std::string osip, uint32_t mpls,
                uint32_t hash) : sip_(sip), dip_(dip), proto_(proto), sport_(sport),
                dport_(dport), ifindex_(0), mpls_(mpls), hash_(hash),
                allow_wait_for_idle_(true) {
         vrf_ = 
          Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf)->vrf_id();
         if (ifindex_) {
             nh_id_ =
                 InterfaceTable::GetInstance()->
                 FindInterface(ifindex_)->flow_key_nh()->id();
         } else {
             nh_id_ = GetActiveLabel(MplsLabel::VPORT_NH, mpls_)->nexthop()->id();
         }
     };

    void SendIngressFlow() {
        if (!hash_) {
            hash_ = rand() % 1000;
        }

        switch(proto_) {
        case IPPROTO_TCP:
            TxTcpPacket(ifindex_, sip_.c_str(), dip_.c_str(), sport_, dport_, 
                        false, hash_, vrf_);
            break;

        case IPPROTO_UDP:
            TxUdpPacket(ifindex_, sip_.c_str(), dip_.c_str(), sport_, dport_, 
                        hash_, vrf_);
            break;

        default:
            TxIpPacket(ifindex_, sip_.c_str(), dip_.c_str(), proto_, hash_);
            break;
        }
    };

    void SendEgressFlow() {
        if (!hash_) {
            hash_ = rand() % 65535;
        }

        std::string self_server = Agent::GetInstance()->router_id().to_string();
        //Populate ethernet interface id
        uint32_t eth_intf_id = 
            EthInterfaceGet(Agent::GetInstance()->
                    fabric_interface_name().c_str())->id();
        switch(proto_) {
        case IPPROTO_TCP:
            TxTcpMplsPacket(eth_intf_id, outer_sip_.c_str(), 
                            self_server.c_str(), mpls_, sip_.c_str(),
                            dip_.c_str(), sport_, dport_, false, hash_);
            break;

        case IPPROTO_UDP:
            TxUdpMplsPacket(eth_intf_id, outer_sip_.c_str(), 
                            self_server.c_str(), mpls_, sip_.c_str(), 
                            dip_.c_str(), sport_, dport_, hash_);
            break;

        default:
            TxIpMplsPacket(eth_intf_id, outer_sip_.c_str(), 
                           self_server.c_str(), mpls_, sip_.c_str(), 
                           dip_.c_str(), proto_, hash_);
            break;
        }
    };

    FlowEntry* Send() {
        if (ifindex_) {
            SendIngressFlow();
        } else if (mpls_) {
            SendEgressFlow();
        } else {
            assert(0);
        }
        if (allow_wait_for_idle_) {
            client->WaitForIdle(3);
        } else {
            WAIT_FOR(1000, 3000, FlowGet(vrf_, sip_, dip_, proto_,
                        sport_, dport_, nh_id_) != NULL);
        }
        //Get flow 
        FlowEntry *fe = FlowGet(vrf_, sip_, dip_, proto_, sport_, dport_,
                                nh_id_);
        EXPECT_TRUE(fe != NULL);
        return fe;
    };

    void Delete() {
        FlowKey key;
        key.nh = nh_id_;
        key.src.ipv4 = ntohl(inet_addr(sip_.c_str()));
        key.dst.ipv4 = ntohl(inet_addr(dip_.c_str()));
        key.src_port = sport_;
        key.dst_port = dport_;
        key.protocol = proto_;

        if (Agent::GetInstance()->pkt()->flow_table()->Find(key) == NULL) {
            return;
        }

        Agent::GetInstance()->pkt()->flow_table()->Delete(key, true);
        if (allow_wait_for_idle_) {
            client->WaitForIdle();
            EXPECT_TRUE(Agent::GetInstance()->pkt()->flow_table()->Find(key) == NULL);
        } else {
            FlowEntry *fe = FlowGet(vrf_, sip_, dip_, proto_, sport_, dport_, nh_id_);
            if (fe == NULL)
                return;
            EXPECT_TRUE(fe->deleted() == true);
        }
    };

    const FlowEntry *FlowFetch() {
        FlowEntry *fe = FlowGet(vrf_, sip_, dip_, proto_, sport_, dport_, nh_id_);
        return fe;
    }

    void set_allow_wait_for_idle(bool allow) {
        allow_wait_for_idle_ = allow;
    }

private:
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
        EXPECT_TRUE(fe->data().source_vn == src_vn_);
        EXPECT_TRUE(fe->data().dest_vn == dest_vn_);

        if (true) {
            FlowEntry *rev = fe->reverse_flow_entry();
            EXPECT_TRUE(rev != NULL);
            EXPECT_TRUE(rev->data().source_vn == dest_vn_);
            EXPECT_TRUE(rev->data().dest_vn == src_vn_);
        }
    };

private:
    std::string src_vn_;
    std::string dest_vn_;
};

class ShortFlow : public FlowVerify {
public:
    ShortFlow() {}
    virtual ~ShortFlow() {};

    virtual void Verify(FlowEntry *fe) {
        EXPECT_TRUE(fe->is_flags_set(FlowEntry::ShortFlow) == true);
        EXPECT_TRUE((fe->data().match_p.action_info.action & (1 << TrafficAction::DROP)) != 0);
        FlowEntry *rev = fe->reverse_flow_entry();
        EXPECT_TRUE(rev->is_flags_set(FlowEntry::ShortFlow) == true);
        EXPECT_TRUE((rev->data().match_p.action_info.action & (1 << TrafficAction::DROP)) != 0);
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
        EXPECT_TRUE(rev->key().src.ipv4 == ntohl(inet_addr(nat_sip_.c_str())));
        EXPECT_TRUE(rev->key().dst.ipv4 == ntohl(inet_addr(nat_dip_.c_str())));
        EXPECT_TRUE(rev->key().src_port == nat_sport_);
        EXPECT_TRUE(rev->key().dst_port == nat_dport_ ||
                    rev->key().dst_port == fe->linklocal_src_port());
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
        EXPECT_TRUE(fe->data().nh_state_.get()->nh()->id() ==
                    forward_flow_rpf_nh_);
        EXPECT_TRUE(rev->data().nh_state_.get()->nh()->id() ==
                    reverse_flow_rpf_nh_);
    }
private:
    uint32_t forward_flow_rpf_nh_;
    uint32_t reverse_flow_rpf_nh_;
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
