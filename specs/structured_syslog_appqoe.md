
# 1. Introduction
This blue print describes the design, features, and implementation of "structured syslog collector" in Contrail Collector to handle APPQOE messages like APPTRACK_APPQOE_PASSIVE_SLA_METRIC_REPORT, APPTRACK_APPQOE_ACTIVE_SLA_METRIC_REPORT, APPTRACK_APPQOE_SLA_METRIC_VIOLATION and APPTRACK_APPQOE_BEST_PATH_SELECTED.

# 2. Problem statement
"structured syslog collector" will receive APPQOE syslog messages from SRX/VSRX devices, parses them and pushes the relevant stats relating to the same into Cassandra, so that Application QOE related views can be made available to the customer.


# 3. Proposed solution
APPQOE syslog messages from SRX/VSRX devices contains additional data, which needs to be parsed to derive more meaningful & detailed information like ingress-jitter, egress-jitter, rtt-jitter, rtt, pkt-loss etc

sample APPQOE syslog messages:

<14>1 2011-11-30T00: 6:15.274 srx5800n0 RT_FLOW - APPTRACK_APPQOE_PASSIVE_SLA_METRIC_REPORT [junos@2636.1.1.1.2.129 service-name="icmp" source-address="1.1.1.1" source-port="3" destination-address="2.2.2.2" destination-port="23911" application="UNKNOWN" source-zone-name="lan" destination-zone-name="wan"  username="N/A" roles="N/A" rule-name="N/A" routing-instance="default" destination-interface-name="ge-0/0/4.0" protocol-id="1" ip-dscp="10" session-id-32="295" ingress-jitter="10" egress-jitter="5" rtt-jitter="10" rtt="10000" pkt-loss="2" application-group="1022" profile-name="apbr_prof1" bytes-from-client="84" bytes-from-server="84" packets-from-client="1" packets-from-server="1"  monitoring-time="4" sla-rule="sla1"]

<14>1 2011-11-30T00: 6:16.457 srx5800n0 RT_FLOW - APPTRACK_APPQOE_ACTIVE_SLA_METRIC_REPORT [junos@2636.1.1.1.2.129 source-address="1.1.1.1" source-port="3" destination-address="2.2.2.2" destination-port="23911" application="UNKNOWN"  destination-zone-name="wan"  routing-instance="default" destination-interface-name="ge-0/0/4.0" protocol-id="1" ip-dscp="10" ingress-jitter="10" egress-jitter="5" rtt-jitter="10" rtt="10000" pkt-loss="2" bytes-from-client="84" bytes-from-server="84" packets-from-client="1" packets-from-server="1"  monitoring-time="10" probe-params-name="voice_params"]

<14>1 2011-11-30T00: 6:16.848 srx5800n0 RT_FLOW - APPTRACK_APPQOE_SLA_METRIC_VIOLATION [junos@2636.1.1.1.2.129 service-name="icmp" source-address="1.1.1.1" source-port="3" destination-address="2.2.2.2" destination-port="23911" application="UNKNOWN" source-zone-name="lan" destination-zone-name="wan"  username="N/A" roles="N/A" rule-name="N/A" routing-instance="default" destination-interface-name="ge-0/0/4.0" protocol-id="1" ip-dscp="10" session-id-32="295" ingress-jitter="10" egress-jitter="5" rtt-jitter="10" rtt="10000" pkt-loss="2" application-group="1022" profile-name="apbr_prof1" target-jitter-type="1" target-jitter="2" target-rtt="4" target-pkt-loss="2" violation-reason="3" jitter-violation-count="2" pkt-loss-violation-count="1" rtt-violation-count="8"  violation-duration="10" bytes-from-client="84" bytes-from-server="84" packets-from-client="1" packets-from-server="1"  monitoring-time="4"  sla-rule="sla1"]

<14>1 2011-11-30T00: 6:17.925 srx5800n0 RT_FLOW - APPTRACK_APPQOE_BEST_PATH_SELECTED [junos@2636.1.1.1.2.129 service-name="icmp" source-address="1.1.1.1" source-port="3" destination-address="2.2.2.2" destination-port="23911" application="UNKNOWN" source-zone-name="lan" destination-zone-name="wan"  username="N/A" roles="N/A" rule-name="N/A" routing-instance="default" destination-interface-name="ge-0/0/4.0" protocol-id="1" ip-dscp="10" session-id-32="295" profile-name="apbr1" application-group="1022" bytes-from-client="84" bytes-from-server="84" packets-from-client="1" packets-from-server="1" elapsed-time="10" sla-rule="sla1" previous-interface="ge-0/0/2.0"]
 
While the existing "structured syslog collector" receive and parses the syslogs, selected fields are written into the UVEs which will help aggregating the data as needed  

## 3.1 Alternatives considered
#### None

## 3.2 API schema changes

diff --git a/src/analytics/collector_uve.sandesh b/src/analytics/collector_uve.sandesh
index 4786365..942262d 100644
--- a/src/analytics/collector_uve.sandesh
+++ b/src/analytics/collector_uve.sandesh
@@ -410,6 +410,11 @@ struct AppMetrics{
       1: optional u64                     session_duration
       2: optional u64                     session_count
       3: optional u64                      total_bytes
+      4: optional u64                      input_bytes
+      5: optional u64                      output_bytes
+      6: optional u64                      rtt
+      7: optional u64                      loss_percentage
+      8: optional u64                      jitter
 }
 
 struct AppMetrics_P_{
@@ -483,3 +488,33 @@ struct AppTrackRecord{
 uve sandesh AppTrack {
      1: AppTrackRecord              data;
 }
+
+struct UveCPESlaMetrics{
+    1: optional u64                     txbps
+    2: optional u64                     rxbps
+    3: optional u64                     jitter
+    4: optional u64                     rtt
+    5: optional u64                     loss_percentage
+    6: optional u64                     latency
+}
+
+struct UveCPESlaMetrics_P_{
+    1:optional UveCPESlaMetrics value (tags="")
+    2:optional UveCPESlaMetrics staging
+}
+
+struct UveCPE {
+    1: string                           name (key="ObjectCPETable")
+    2: optional bool                    deleted
+    3: optional map <string, UveCPESlaMetrics>     sla_metrics_dial (hidden="yes",metric=”dial”)
+    4: optional map <string, UveCPESlaMetrics_P_>  avg_sla_metrics_dial_3m (mstats="1-sla_metrics_dial:DSAvg:",tags=".__key")
+    5: optional map <string, UveCPESlaMetrics_P_>  avg_sla_metrics_dial_1h (mstats="20-sla_metrics_dial:DSAvg:",tags=".__key"
+    6: optional map <string, UveCPESlaMetrics_P_>  avg_sla_metrics_dial_8h (mstats="160-sla_metrics_dial:DSAvg:",tags=".__key
+    7: optional map <string, UveCPESlaMetrics_P_>  avg_sla_metrics_dial_1d (mstats="480-sla_metrics_dial:DSAvg:",tags=".__key
+    8: optional map <string, UveCPESlaMetrics_P_>  avg_sla_metrics_dial_7d (mstats="3360-sla_metrics_dial:DSAvg:",tags=".__ke
+    9: optional map <string, UveCPESlaMetrics_P_>  avg_sla_metrics_dial_30d (mstats="14400-sla_metrics_dial:DSAvg:",tags=".__
+}(period="180")
+
+uve sandesh UveCPEData {
+     1: UveCPE              data;
+}

## 3.3 User workflow impact
#### N/A

## 3.4 UI changes
#### N/A

## 3.5 Notification impact
#### N/A


# 4. Implementation
## 4.1 Work items
#### Schema:
Create and update UVEs for QOE parameters with appropriate mappings

#### Collector:
Implement "structured syslog collector" changes to update UVE with relevant QOE fields

# 5. Performance and scaling impact
## 5.1 API and control plane
#### Scaling and performance for API and control plane

## 5.2 Forwarding performance
#### Scaling and performance for API and forwarding

# 6. Upgrade
#### N/A
#### N/A

# 7. Deprecations
#### N/A

# 8. Dependencies
#### N/A

# 9. Testing
## 9.1 Unit tests
Unit tests will be updated as needed

## 9.2 Dev tests

Testing of handling of APPQOE structured syslog collector using a structured syslog simulator

## 9.3 System tests


# 10. Documentation Impact

# 11. References

