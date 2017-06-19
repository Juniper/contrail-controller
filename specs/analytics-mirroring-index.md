# 1. Introduction
This blueprint describes support for:

Adding mirroring index flow in Contrail Analytics

Capturing bytes/packets mirrored to a specific mirror index and have these
stats available at Analytics node

# 2. Problem statement

For mirrored flows, current information in flow records ('flow -l') displays
mirror index. Using mirror index, we could identify the mirror entry and ‘nh’
where the traffic gets mirrored. However, this flow entry (in flow -l) stays
only till flow timer expiry. Although this information is received by Analytics
node, it only has action ‘mirror’ to indicate this packet is mirrored, it does
not have mirror index.

As part of network troubleshooting of virtual machine connectivity issue to
other services/external network, we provision a mirror and mirrored packets are
available at analyzer instance. If a packet is not correctly mirrored there will
be no way to tell if the source VNF is not functioning correct or if mirroring
is not working properly. To assist better troubleshoot, we need a way to compare
number of packets matching a network policy to the number of packets mirrored.

# 3. Proposed solution

To facilitate identifying mirrored flows and stats at analytics node, we need to
enhance Sandesh Flow messages sent from vrouter agent to collector, to include
mirror index and mirrored packet/byte counts.

Currently flow stats are read from shared memory by vrouter agent, and exported
using Sandesh message to Analytics node. This message has action=’pass|mirror’,
the only indication that the packets of this flow are mirrored. Along with this
we should add six new fields mirror_index, mirror_bytes, mirror_packets,
sec_mirror_index, sec_mirror_bytes, sec_mirror_packets (as current framework
supports provisioning of two mirrors). In an ideal case the packet/byte count
data in flow entry should match the mirrored packet/byte count (new). Having
two fields will help giving more confidence on the stability of mirroring
functionality.

Mirror index will be fetched from matched ACL data for the flow. ACL data
includes analyzer name, using the analyzer name we will get the mirror index.
Mirrored packet/byte count should be collected at vrouter kernel module, when
the packet is about to be mirrored. The capture stats can be added to same
shared memory where flow stats are updated.

Since the framework supports provisioning of two mirrors, stats from both the
mirrors (if configured) will be captured.


## 3.1 Alternatives considered
Other option for syncing mirror stats could be to introduce a new shared memory
(to collect stats at kernel and read from vrouter agent), new Sandesh Mirror
messages, and analytics tables to store collected info.

## 3.2 API schema changes
With addition of new fields in Sandesh flow from vrouter agent to collector here
is a sample message:

<pre><code>
&lt;FlowLogData&gt;
  &lt;flowuuid type="string"
  identifier="1"&gt;ed6bdc5b-6ebc-48ca-b8fc-d483fdbd89fc&lt;/flowuuid&gt;
  &lt;direction_ing type="byte" identifier="2"&gt;1&lt;/direction_ing&gt;
  &lt;sourcevn type="string"
identifier="3"&gt;default-domain:demo:net&lt;/sourcevn&gt;
  &lt;sourceip type="ipaddr" identifier="4"&gt;11.0.0.4&lt;/sourceip&gt;
  &lt;destvn type="string"
identifier="5"&gt;default-domain:demo:net&lt;/destvn&gt;
  &lt;destip type="ipaddr" identifier="6"&gt;11.0.0.3&lt;/destip&gt;
  &lt;protocol type="byte" identifier="7"&gt;1&lt;/protocol&gt;
  &lt;sport type="i16" identifier="8"&gt;-31743&lt;/sport&gt;
  &lt;dport type="i16" identifier="9"&gt;0&lt;/dport&gt;
  &lt;tcp_flags type="u16" identifier="11"&gt;0&lt;/tcp_flags&gt;
  &lt;vm type="string"
identifier="12"&gt;500514ed-5550-41f6-ba7f-2663e89c3417&lt;/vm&gt;
  &lt;reverse_uuid type="string"
  identifier="16"&gt;e341e705-f995-4aee-818e-02cf72c285f7&lt;/reverse_uuid&gt;
  &lt;setup_time type="i64"
identifier="17"&gt;1494846979256107&lt;/setup_time&gt;
  &lt;bytes type="i64" identifier="23"&gt;98&lt;/bytes&gt;
  &lt;packets type="i64" identifier="24"&gt;1&lt;/packets&gt;
  &lt;diff_bytes type="i64" identifier="26"&gt;98&lt;/diff_bytes&gt;
  &lt;diff_packets type="i64" identifier="27"&gt;1&lt;/diff_packets&gt;
  &lt;action type="string" identifier="28"&gt;pass|mirror&lt;/action&gt;
  &lt;sg_rule_uuid type="string"
  identifier="29"&gt;4cc31bfc-a1ac-4ae2-85ab-cc16ef5371be&lt;/sg_rule_uuid&gt;
  &lt;nw_ace_uuid type="string"
  identifier="30"&gt;00000000-0000-0000-0000-000000000001&lt;/nw_ace_uuid&gt;
  &lt;vrouter_ip type="string" identifier="31"&gt;172.16.0.26&lt;/vrouter_ip&gt;
  &lt;other_vrouter_ip type="string"
identifier="32"&gt;172.16.0.26&lt;/other_vrouter_ip&gt;
  &lt;underlay_proto type="u16" identifier="33"&gt;0&lt;/underlay_proto&gt;
  &lt;underlay_source_port type="u16"
identifier="34"&gt;0&lt;/underlay_source_port&gt;
  &lt;vmi_uuid type="string"
  identifier="35"&gt;6195ad01-b804-4706-ab37-bfe60d4b87ee&lt;/vmi_uuid&gt;
  &lt;drop_reason type="string" identifier="36"&gt;UNKNOWN&lt;/drop_reason&gt;
  &lt;forward_flow type="bool" identifier="37"&gt;false&lt;/forward_flow&gt;
  <b>&lt;mirror_index type="byte" identifier="38"&gt;1&lt;/mirror_index&gt;
  &lt;mirror_bytes type="i64" identifier="39"&gt;98&lt;/mirror_bytes&gt;
  &lt;mirror_packets type="i64" identifier="40"&gt;1&lt;/mirror_packets&gt;
  &lt;sec_mirror_index type="byte" identifier="41"&gt;1&lt;/sec_mirror_index&gt;
  &lt;sec_mirror_bytes type="i64" identifier="42"&gt;98&lt;/sec_mirror_bytes&gt;
  &lt;sec_mirror_packets type="i64"
identifier="43"&gt;1&lt;/sec_mirror_packets&gt;</b>
&lt;/FlowLogData&gt;
</code></pre>

## 3.3 User workflow impact
Support Users can use the mirror index to get the Mirror configuration and
endpoint details. Having this information in flows at analytics DB for
longer duration (than the flow timer expiry) helps in better support.

This mirror index should be used to refer mirror_index (new field) in the
flow record to get bytes and packet counts mirrored to a specific analyzer.

## 3.4 UI changes
There are no changes done as part of this feature. However, configured mirror
indexes for an analyzer instance can be inspected using info from
contrail-vrouter-agent introspect call.
e.g. http://&lt;compute-ip&gt;:8085/agent.xml#Snh_MirrorEntryReq


## 3.5 Notification impact
None


# 4. Implementation
## 4.1 Work items
To add mirror index, mirrored packet and byte counts in flow stats:

At present number of bytes/packets using a flow is captured through /dev/flow
device. Ageing task from FlowStatsCollector (flow_stats_collector.cc) is
responsible for scan the flow table to collect the stats and send to collector
using the format from flow.sandesh. Add mirror_index, mirror_bytes,
mirror_packets, sec_mirror_index, sec_mirror_bytes, sec_mirror_packets to
‘FlowLogData’ in flow.sandesh. From flow->data().match_p.action_info.mirror_l
get AnalyzerName, using this get mirror-index from MirrorKSyncObject.
Populate the index in FlowLogData. Mirrored packet/byte counts to be read
from shared memory similar to existing flow packets and byte counts.

Enhancements to be done in applicable upstream components to carry/store this
new fields to Analytics DB.

Capturing mirrored packet and byte counts in vrouter kernel module:

Add new struct members for ‘mirror_bytes’, ‘mirror_packets’, 'sec_mirror_bytes',
'sec_mirror_packets' as part of ‘vr_flow_stats’. Pass this reference of
vr_flow_stats to vr_mirror method call. Add logic at vr_mirror to calculate bytes
and packets and store in vr_flow_stats, as this is the place where a packet is
sent to nh module for mirroring. This information to be captured after all
existing mirror validations are done, just before handing the packet over to
next hop.

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
Documentation to be updated to reflect new fields visible as part of flow record
data.

# 11. References
None



