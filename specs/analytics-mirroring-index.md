# 1. Introduction
This blueprint describes support for:

Adding mirroring index flow in contrail Analytics
Capturing bytes/packets mirrored to a specific mirror index and have these stats available at Analytics node

# 2. Problem statement
For mirrored flows, current information in flow records ('flow -l') displays mirror index. Using mirror index, we could identify the mirror entry and ‘nh’ where the traffic gets mirrored. However, this flow entry (in flow -l) stays only till flow timer expiry. Although this information is received by Analytics node, it only has action ‘mirror’ to indicate this packet is mirrored, it does not have mirror index.

As part of network troubleshooting of virtual machine connectivity issue to other services/external network, we provision a mirror and mirrored packets are available at analyzer instance. If a packet is not correctly mirrored there will be no way to tell if the source VNF is not functioning correct or if mirroring is not working properly. To assist better troubleshoot, we need a way to match number of packets matching a network policy to the number of packets mirrored.

# 3. Proposed solution

To facilitate identifying mirrored flows and stats at analytics node, we need to enhance Sandesh Flow messages sent from vrouter agent to collector, to include mirror index and mirrored packet/byte counts.

Currently flow stats are read from shared memory by vrouter agent, and exported using Sandesh message to Analytics node. This message has action=’pass|mirror’, the only indication that the packets of this flow are mirrored. Along with this we should add three new fields mirror index, Mirrored byte count, mirrored packet count. In an ideal case the packet/byte count data in flow entry should match the mirrored packet/byte count (new). Having two fields will help giving more confidence on the stability of mirroring functionality.

Mirror index could be fetched from matched ACL data for the flow. ACL data includes analyzer name, using the analyzer name, we can get the mirror index. Mirrored packet/byte count should be collected at vrouter kernel module, when the packet is about to be mirrored. The capture stats can be added to same shared memory where flow stats are updated.

Other option for syncing mirror stats could be to introduce a new shared memory (to collect stats at kernel and read from vrouter agent), new Sandesh Mirror messages, and analytics tables to store collected info.

## 3.1 Alternatives considered
None

## 3.2 API schema changes
No Impact

## 3.3 User workflow impact
Support Users can use the mirror index to get the Mirror configuration and endpoint details. Having this information in flows at analytics DB for longer duration (than the flow timer expiry) helps in better support.

## 3.4 UI changes
None

## 3.5 Notification impact
None


# 4. Implementation
## 4.1 Work items
To add mirror index, mirrored packet and byte counts in flow stats:

At present number of bytes/packets using a flow is captured through /dev/flow device. Ageing task from FlowStatsCollector (flow_stats_collector.cc) is responsible for scan the flow table to collect the stats and send to collector using the format from flow.sandesh. Add mirror-index, mirrored packet and byte counts to ‘FlowLogData’ in flow.sandesh. From flow->data().match_p.action_info.mirror_l get AnalyzerName, using this get mirror-index from MirrorKSyncObject. Populate the index in FlowLogData. Mirrored packet/byte counts to be read from shared memory similar to existing flow packets and byte counts.

Enhancements to be done in applicable upstream components to carry/store this new fields to Analytics DB.

Capturing mirrored packet and byte counts in vrouter kernel module:

Add new struct members for ‘mirror_bytes’ and ‘mirror_packets’ as part of ‘vr_flow_stats’. Pass this reference of vr_flow_stats to vr_mirror method call. Add logic at vr_mirror to calculate bytes and packets and store in vr_flow_stats, as this is the place where a packet is sent to nh module for mirroring. This information to be captured after all existing mirror validations are done, just before handing the packet over to next hop.

Update the mirror stats collected in flow table shared memory.

# 5. Performance and scaling impact
## 5.1 API and control plane
None
## 5.2 Forwarding performance
None

# 6. Upgrade
No Impact

# 7. Deprecations
None

# 8. Dependencies
None

# 9. Testing
## 9.1 Unit tests
## 9.2 Dev tests
## 9.3 System tests

# 10. Documentation Impact
None

# 11. References
None
