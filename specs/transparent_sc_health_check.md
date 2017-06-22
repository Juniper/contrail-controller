
# 1. Introduction
To provide health check support for transparent service chains.
This document describes the design and implementation details of Contrail
components to achieve this.

# 2. Problem statement
Support End to end health check for transparent service chains

# 3. Proposed solution

## 3.1 Configuration/API Model
This section provides details of configuration properties to configure health check for transparent service chain and objects where this configuration is made.

### 3.1.1 Schema
We need a way to identify transparent service chain in health check object. The new config objects to be defined and/or existing config objects to be modified in TBD

### 3.1.2 Schema Transformer
User configured service chain information needs to be translated to internal representation like vlan-tag/service-chain-address and passed to agent in service-health-check object as part of service-health-check-properties. This is still TBD

### 3.1.3 Vrouter Agent
Assumed agent will receive vlan-tag (to identify service-chain) in service-health-check-properties. This is still TBD.

### 3.1.3.1 Existing transparent service chain implementation details in vrouter-agent

Following diagram shows a sample service chain topology

<img src="images/transparent_sc_hc.png" alt="Transparent Service Chain Example" style="width:512px;height:256px;">

Let us assume that ping is initiated from 10.10.10.3 to 30.30.30.3. Let us trace the packet path for this. When ping is initiated we see the following flows

    Index                Source:Port/Destination:Port                      Proto(V)
    -----------------------------------------------------------------------------------
    36876<=>49828        10.10.10.3:51968                                    1 (7->5)
                         30.30.30.3:0
   (Gen: 1, K(nh):42, Action:F, Flags:, QOS:-1, S(nh):42,  Stats:11/1078,
    SPort 58877, TTL 0, Sinfo 7.0.0.0)

    49828<=>36876        30.30.30.3:51968                                    1 (5)
                         10.10.10.3:0
    (Gen: 1, K(nh):27, Action:F, Flags:, QOS:-1, S(nh):27,  Stats:11/1122,
     SPort 59861, TTL 0, Sinfo 6.0.0.0)

    223540<=>242904       10.10.10.3:51968                                    1 (2)
                          30.30.30.3:0
    (Gen: 1, K(nh):40, Action:F, Flags:, QOS:-1, S(nh):40,  Stats:11/1122,
     SPort 64457, TTL 0, Sinfo 4.0.0.0)

    242904<=>223540       30.30.30.3:51968                                    1 (1->2)
                          10.10.10.3:0
    (Gen: 1, K(nh):12, Action:F, Flags:, QOS:-1, S(nh):12,  Stats:11/1078,
     SPort 63308, TTL 0, Sinfo 5.0.0.0)


NH 42 = Left interface (IP 10.10.10.3)      
NH 27 = left interface of SI   
NH 40 = right interface of SI    
NH 12 = Right interface (IP 30.30.30.3)     

(1) Using the above flows, the packet from left-interface will be translated to left-internal-lvn1 vrf. This internal VRF will have destination route pointing to VLAN NH. The VLAN NH will have vlan-tag and left interface of SI. Based on this VLAN NH vrouter will send vlan-tagged packet to left interface of SI.    
(2) Packets coming out of right interface of SI will be classified to right-internal-rvn2 vrf. This classification is done based on (vlan-tag, interface) combination.    
(3) The destination route in right-internal-rvn2 vrf will point to interface/tunnel nh corresponding to right-interface. This will ensure that untagged packet is delivered to destination.   

### 3.1.3.2 Proposal for health check implementation in vrouter-agent

(1) Allocates a Link-local-ip and create route for link-local-ip in fabric-vrf pointing to VLAN NH containing vlan-tag (which comes in health-check configuration) and VMI (to which health check configuration is attached). This route is used only to setup flows.
(2) Health check packet destined to link-local-ip created above is initiated from health check script.
(3) Agent sets up NAT flow for this packet with action as VRF translate (From fabric to left-internal-lvn1 VRF),
    After DNAT, the natted destination ip will be looked in left-internal-lvn1 VRF which will point to VLAN NH.
(4) Reverse flow for the above NAT flow will also have VRF translate action (From left-internal-lvn1 to fabric-vrf). For reverse flow, reverse NAT will be done which converts service_ip to vhost0 ip which will be looked up in fabric-vrf.

