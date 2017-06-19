# 1. Introduction
This blueprint describes support for:

Adding Mirror analyzer in Contrail Analytics

Capturing bytes/packets mirrored to a specific mirror analyzer and have these
stats available at Analytics node

Support to capture mirror stats for mirroring enabled on interface

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
mirror analyzer and mirrored packet/byte counts. As mirror index is internal to
code, it is not exported to analytics.

Currently flow stats are read from shared memory by vrouter agent, and exported
using Sandesh message to Analytics node. This message has action=’pass|mirror’,
the only indication that the packets of this flow are mirrored. Along with this
we should add six new fields analyzer_name, mirror_bytes, mirror_packets,
sec_analyzer_name, sec_mirror_bytes, sec_mirror_packets (as current framework
supports provisioning of two mirrors). In an ideal case the packet/byte count
data in flow entry should match the mirrored packet/byte count (new). Having
two fields will help giving more confidence on the stability of mirroring
functionality.

ACL data includes analyzer name index. Mirrored packet/byte count should be
collected at vrouter kernel module, when the packet is about to be mirrored.
The capture stats will be added to same shared memory where flow stats are
updated.

Since the framework supports provisioning of two mirrors, stats from both the
mirrors (if configured) will be captured.

Not all flows are exported to analytics due to sampling feature. Mirror stats
from interface mirroring will be supported to help with this. 

## 3.1 Alternatives considered
Other option for syncing mirror stats could be to introduce a new shared memory
(to collect stats at kernel and read from vrouter agent), new Sandesh Mirror
messages, and analytics tables to store collected info.

## 3.2 API schema changes

### 3.2.1 UVE enhancements
For interface mirroring stats collection, following UVEs are enahanced to
display mirror stats:
virtual-machine-interface:
http://<analytics-server>:8081/analytics/uves/virtual-machine-interface/<intf-name>
...
if_stats": {
            "in_bytes": 126,
            "in_pkts": 3,
            "mir_bytes": 207,
            "mir_pkts": 3,
            "out_bytes": 126,
            "out_pkts": 3
        }
...
Note: The difference in the bytes is due to Juniper header added to mirrored
packet.

vrouter:
http://<analytics-server>:8081/analytics/uves/vrouter/<vrouter-name>?flat
Two new fields are added under 'VrouterStatsAgent'.
...
"VrouterStatsAgent": {
    ...
    "mir_tpkts": 24663,
    "mir_bytes": 1702195,
    ...
}

## 3.3 User workflow impact
Support Users can use the mirror analyzer to get the Mirror configuration and
endpoint details. Having this information in flows at analytics DB for
longer duration (than the flow timer expiry) helps in better support.

This mirror analyzer should be used to refer analyzer_name (new field) in the
flow record to get bytes and packet counts mirrored to a specific analyzer.

## 3.4 UI changes

### 3.4.1 Dashboard changes
With new fields in flow record table, the 'query tab', flow record' is modifed
to include new fields in select query list. Following six fields will be part of
select list: agg-mir-packets, agg-mir-bytes, agg-sec-mir-packets,
agg-sec-mir-bytes, analyzer_name, sec_analyzer_name
Reference URL: https://<dashboard-ip>:8143/#p=query_flow_record

### 3.4.2 CLI Changes

Changes to 'flow -l' display when a flow is mirrored:

Sample flow entry for network where mirroring is enabled is shown below.

792<=>940          11.0.0.4:5779                                       1 (1)
                   192.168.122.1:0
(Gen: 1, K(nh):13, Action:F, Flags:, QOS:-1, S(nh):13,  Stats:1/98,  
Mirror Index:1, MStats:1/162 (1),  SPort 54475, TTL 0, Sinfo 4.0.0.0)

Mirror Index, is an existing entry. MStats, is a newentry. MStats corresponds to
primary mirror analyzer stats, SMStats corresponds to stats of secondary mirror
analyzer.


Changes to 'flow --get <flow-index>':

When interface mirroring is enabled, mirror stats are colelcted for the
interface as well. These stats are shown as part of 'flow --get' output along
with existing interface stats. Sample outout is shown below:

flow --get 1604
Flow Index:                   1604
Flow Generation ID:           2
Reverse Flow Index:           2348
VRF:                          1
Destination VRF:              1
Flow Source:                  [192.168.122.1]:6123
Flow Destination:             [11.0.0.3]:0
Flow Protocol:                ICMP
Flow Action:                  FORWARD
Expected Source:              NextHop(Index, VRF, Type): 9, 1, ENCAP
                              Ingress Interface(Index, VRF, OS): vif0/3, 1, vgw
                              Interface Statistics(Out, In, Errors): 24548, 17, 0
Source Information:           VRF: 1
                              Layer 3 Route Information
                              Matching Route: 0.0.0.0/0
                              NextHop(Index, VRF, Type): 9, 1, ENCAP
                              Ingress Interface(Index, VRF, OS): vif0/3, 1, vgw
                              Interface Statistics(Out, In, Errors): 24548, 17, 0
Destination Information:      VRF: 1
                              Layer 3 Route Information
                              Matching Route: 11.0.0.3/32
                              NextHop(Index, VRF, Type): 21, 1, ENCAP
                              Egress Interface(Index, VRF, OS): vif0/5, 1, tap15928739-41
                              Interface Statistics(Out, In, Errors): 24542, 24532, 0
                              Mirror Statistics(Index, Bytes, Packets): 0, 1693570, 24538

                              Layer 2 Route Information
                              DestinationMAC: 2:15:92:87:39:41
                              NextHop(Index, VRF, Type): 24, 1, ENCAP
                              Egress Interface(Index, VRF, OS): vif0/5, 1, tap15928739-41
                              Interface Statistics(Out, In, Errors): 24542, 24532, 0
                              Mirror Statistics(Index, Bytes, Packets): 0, 1693570, 24538

Flow Flags:
UDP Source Port:              51969

Flow Statistics:              1/84
System Wide Packet Drops:     24560
                              Reverse Path Failures: 0
                              Flow Block Drops: 9

'Mirror Statistics' entry above is a new change.


Changes to 'vif --list' and 'vif --get':

Along with existing interface statistics, mirror stats are also displayed.
Sample 'vif --get' output is shown below:
$ sudo vif --get 5
Vrouter Interface Table

Flags: P=Policy, X=Cross Connect, S=Service Chain, Mr=Receive Mirror
       Mt=Transmit Mirror, Tc=Transmit Checksum Offload, L3=Layer 3, L2=Layer 2
       D=DHCP, Vp=Vhost Physical, Pr=Promiscuous, Vnt=Native Vlan Tagged
       Mnp=No MAC Proxy, Dpdk=DPDK PMD Interface, Rfl=Receive Filtering Offload,
Mon=Interface is Monitored
       Uuf=Unknown Unicast Flood, Vof=VLAN insert/strip offload, Df=Drop New
Flows, L=MAC Learning Enabled
       Proxy=MAC Requests Proxied Always, Er=Etree Root

vif0/5      OS: tap15928739-41
            Type:Virtual HWaddr:00:00:5e:00:01:00 IPaddr:0
            Vrf:1 Flags:PMrMtL3L2DEr QOS:-1 Ref:5 Mirror index 0
            RX packets:24558  bytes:1031660 errors:0
            TX packets:24568  bytes:1032080 errors:0
            Mirror packets:24564  bytes:1695364 errors:0
            Drops:5
            Ingress Mirror Metadata: 3 17 64 65 66 61 75 6c 74 2d 64
                                     6f 6d 61 69 6e 3a 64 65 6d 6f 3a
                                     6e 65 74 ff 0
            Egress Mirror Metadata: 4 17 64 65 66 61 75 6c 74 2d 64 6f
                                    6d 61 69 6e 3a 64 65 6d 6f 3a 6e
                                    65 74 ff 0

'Mirror packets' entry above is a new change. 

Configured mirror analyzer instances can be inspected using info from 
contrail-vrouter-agent introspect call.
e.g. http://<compute-ip>:8085/agent.xml#Snh_MirrorEntryReq


## 3.5 Notification impact
With addition of new fields in Sandesh flow from vrouter agent to collector here
is a sample message:

<pre><code>
<FlowLogData>
  <flowuuid type="string"
  identifier="1">ed6bdc5b-6ebc-48ca-b8fc-d483fdbd89fc</flowuuid>
  <direction_ing type="byte" identifier="2">1</direction_ing>
  <sourcevn type="string" identifier="3">default-domain:demo:net</sourcevn>
  <sourceip type="ipaddr" identifier="4">11.0.0.4</sourceip>
  <destvn type="string" identifier="5">default-domain:demo:net</destvn>
  <destip type="ipaddr" identifier="6">11.0.0.3</destip>
  <protocol type="byte" identifier="7">1</protocol>
  <sport type="i16" identifier="8">-31743</sport>
  <dport type="i16" identifier="9">0</dport>
  <tcp_flags type="u16" identifier="11">0</tcp_flags>
  <vm type="string" identifier="12">500514ed-5550-41f6-ba7f-2663e89c3417</vm>
  <reverse_uuid type="string"
  identifier="16">e341e705-f995-4aee-818e-02cf72c285f7</reverse_uuid>
  <setup_time type="i64" identifier="17">1494846979256107</setup_time>
  <bytes type="i64" identifier="23">98</bytes>
  <packets type="i64" identifier="24">1</packets>
  <diff_bytes type="i64" identifier="26">98</diff_bytes>
  <diff_packets type="i64" identifier="27">1</diff_packets>
  <action type="string" identifier="28">pass|mirror</action>
  <sg_rule_uuid type="string"
  identifier="29">4cc31bfc-a1ac-4ae2-85ab-cc16ef5371be</sg_rule_uuid>
  <nw_ace_uuid type="string"
  identifier="30">00000000-0000-0000-0000-000000000001</nw_ace_uuid>
  <vrouter_ip type="string" identifier="31">172.16.0.26</vrouter_ip>
  <other_vrouter_ip type="string" identifier="32">172.16.0.26</other_vrouter_ip>
  <underlay_proto type="u16" identifier="33">0</underlay_proto>
  <underlay_source_port type="u16" identifier="34">0</underlay_source_port>
  <vmi_uuid type="string"
  identifier="35">6195ad01-b804-4706-ab37-bfe60d4b87ee</vmi_uuid>
  <drop_reason type="string" identifier="36">UNKNOWN</drop_reason>
  <forward_flow type="bool" identifier="37">false</forward_flow>
  <b><analyzer_name type="byte" identifier="38">test</analyzer_name>
  <mirror_bytes type="i64" identifier="39">98</mirror_bytes>
  <mirror_packets type="i64" identifier="40">1</mirror_packets>
  <sec_analyzer_name type="byte" identifier="41">test2</sec_analyzer_name>
  <sec_mirror_bytes type="i64" identifier="42">98</sec_mirror_bytes>
  <sec_mirror_packets type="i64" identifier="43">1</sec_mirror_packets></b>
</FlowLogData>
</code></pre>

# 4. Implementation
## 4.1 Work items
To add mirror analyzer, mirrored packet and byte counts in flow stats:

At present number of bytes/packets using a flow is captured through /dev/flow
device. Ageing task from FlowStatsCollector (flow_stats_collector.cc) is
responsible for scan the flow table to collect the stats and send to collector
using the format from flow.sandesh. Add analyzer_name, mirror_bytes,
mirror_packets, sec_analyzer_name, sec_mirror_bytes, sec_mirror_packets to
‘FlowLogData’ in flow.sandesh. From flow->data().match_p.action_info.mirror_l
get AnalyzerName. Populate analyzer in FlowLogData. Mirrored packet/byte counts 
to be read from shared memory similar to existing flow packets and byte counts.

Enhancements to be done in applicable upstream components to carry/store this
new fields to Analytics DB.

Capturing mirrored packet and byte counts in vrouter kernel module:

Add new struct members for 'mirror_bytes', 'mirror_packets', 'sec_mirror_bytes',
'sec_mirror_packets' as part of ‘vr_flow_stats’. Pass this reference of
vr_flow_stats to vr_mirror method call. Add logic at vr_mirror to calculate bytes
and packets and store in vr_flow_stats, as this is the place where a packet is
sent to nh module for mirroring. This information to be captured after all
existing mirror validations are done, just before handing the packet over to
next hop.

Update the mirror stats collected in flow table shared memory.

Interface mirroring stats collection:

Modifications to vif_mirror procedure to get the bytes and packet couts for the
packet that is mirrored. Enahancements to provide the stats to UVEs are done in
vr_interface.c. interface.sandesh is modifed to include mirror stats in virtual
machine interface UVE and vrouter UVE.

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



