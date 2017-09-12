
# 1. Introduction
This blue print describes the design, features, and implementation of "structured syslog collector" in Contrail Collector to handle APPQOE messages like APPTRACK_APPQOE_PASSIVE_SLA_METRIC_REPORT, APPTRACK_APPQOE_ACTIVE_SLA_METRIC_REPORT, APPTRACK_APPQOE_SLA_METRIC_VIOLATION and APPTRACK_APPQOE_BEST_PATH_SELECTED.
And also add tenant level metrics and traffic type related metrics.

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
index 4786365..61cee1b 100644
--- a/src/analytics/collector_uve.sandesh
+++ b/src/analytics/collector_uve.sandesh
@@ -409,7 +409,17 @@ response sandesh DatabaseWritesStatusResponse {
 struct AppMetrics{
       1: optional u64                     session_duration
       2: optional u64                     session_count
-      3: optional u64                      total_bytes
+      3: optional u64                     total_bytes
+      4: optional u64                     input_bytes
+      5: optional u64                     output_bytes
+      6: optional u64                     rtt
+      7: optional u64                     loss_percentage
+      8: optional u64                     jitter
+      9: optional u64                     session_switch_count
+     10: optional u64                     violation_duration
+     11: optional u64                     jitter_violation_count
+     12: optional u64                     rtt_violation_count
+     13: optional u64                     pkt_loss_violation_count
+     14: optional u64                     score
 }
 
 struct AppMetrics_P_{
@@ -418,7 +428,7 @@ struct AppMetrics_P_{
 }
 
 /*  UVE key -> TENANT::SITE::DEVICE_ID
-*   Three maps:
+*   Six maps:
 *     1. app_metrics_sla
 *        If app-group is unknown, map key => NESTED-APP(APP/CATEGORY)::DEPARTMENT::SLA_PROFILE
 *         Else, key is => APP-GROUP(NESTED-APP:APP/APP-CATEGORY)::DEPARTMENT::SLA_PROFILE
@@ -428,6 +438,12 @@ struct AppMetrics_P_{
 *     3. app_metrics_user
 *        If app-group is unknown, map key => NESTED-APP(APP/APP-CATEGORY)::DEPARTMENT::USER
 *         Else, key is => APP-GROUP(NESTED-APP:APP/APP-CATEGORY)::DEPARTMENT::USER
+*     4. app_metrics_traffic_type_sla
+*         key is => TRAFFIC-TYPE(NESTED-APP:APP/APP-CATEGORY)::DEPARTMENT::SLA_PROFILE
+*     5. app_metrics_traffic_type_link
+*         key is => TRAFFIC-TYPE(NESTED-APP:APP/APP-CATEGORY)::DEPARTMENT::LINK
+*     6. app_metrics_traffic_type_user
+*         key is => TRAFFIC-TYPE(NESTED-APP:APP/APP-CATEGORY)::DEPARTMENT::USER
 */
 
 struct AppTrackRecord{
@@ -478,8 +494,136 @@ struct AppTrackRecord{
     39: optional map<string, AppMetrics_P_>  avg_app_metrics_user_1d (mstats="480-app_metrics_user:DSAvg:",tags=".__key")
     40: optional map<string, AppMetrics_P_>  avg_app_metrics_user_7d (mstats="3360-app_metrics_user:DSAvg:",tags=".__key")
     41: optional map<string, AppMetrics_P_>  avg_app_metrics_user_30d (mstats="14400-app_metrics_user:DSAvg:",tags=".__key")
+
+    /* app_metrics_traffic_type_sla  */
+    42: optional map<string, AppMetrics>      app_metrics_traffic_type_sla (hidden="yes",metric="diff")
+    43: optional map<string, AppMetrics_P_>   sum_app_metrics_traffic_type_sla_3m (mstats="1-app_metrics_traffic_type_sla:DSSum:",tags=".__key")
+    44: optional map<string, AppMetrics_P_>   sum_app_metrics_traffic_type_sla_1h (mstats="20-app_metrics_traffic_type_sla:DSSum:",tags=".__key")
+    45: optional map<string, AppMetrics_P_>   sum_app_metrics_traffic_type_sla_8h (mstats="160-app_metrics_traffic_type_sla:DSSum:",tags=".__key")
+    46: optional map<string, AppMetrics_P_>   sum_app_metrics_traffic_type_sla_1d (mstats="480-app_metrics_traffic_type_sla:DSSum:",tags=".__key")
+    47: optional map<string, AppMetrics_P_>   sum_app_metrics_traffic_type_sla_7d (mstats="3360-app_metrics_traffic_type_sla:DSSum:",tags=".__key")
+    48: optional map<string, AppMetrics_P_>   sum_app_metrics_traffic_type_sla_30d (mstats="14400-app_metrics_traffic_type_sla:DSSum:",tags=".__key")
+    49: optional map<string, AppMetrics_P_>  avg_app_metrics_traffic_type_sla_3m (mstats="1-app_metrics_traffic_type_sla:DSAvg:",tags=".__key")
+    50: optional map<string, AppMetrics_P_>  avg_app_metrics_traffic_type_sla_1h (mstats="20-app_metrics_traffic_type_sla:DSAvg:",tags=".__key")
+    51: optional map<string, AppMetrics_P_>  avg_app_metrics_traffic_type_sla_8h (mstats="160-app_metrics_traffic_type_sla:DSAvg:",tags=".__key")
+    52: optional map<string, AppMetrics_P_>  avg_app_metrics_traffic_type_sla_1d (mstats="480-app_metrics_traffic_type_sla:DSAvg:",tags=".__key")
+    53: optional map<string, AppMetrics_P_>  avg_app_metrics_traffic_type_sla_7d (mstats="3360-app_metrics_traffic_type_sla:DSAvg:",tags=".__key")
+    54: optional map<string, AppMetrics_P_>  avg_app_metrics_traffic_type_sla_30d (mstats="14400-app_metrics_traffic_type_sla:DSAvg:",tags=".__key")
+
+    /* app_metrics_traffic_type_link */
+    55: optional map<string, AppMetrics>     app_metrics_traffic_type_link (hidden="yes",metric="diff")
+    56: optional map<string, AppMetrics_P_>  sum_app_metrics_traffic_type_link_3m (mstats="1-app_metrics_traffic_type_link:DSSum:",tags=".__key")
+    57: optional map<string, AppMetrics_P_>  sum_app_metrics_traffic_type_link_1h (mstats="20-app_metrics_traffic_type_link:DSSum:",tags=".__key")
+    58: optional map<string, AppMetrics_P_>  sum_app_metrics_traffic_type_link_8h (mstats="160-app_metrics_traffic_type_link:DSSum:",tags=".__key")
+    59: optional map<string, AppMetrics_P_>  sum_app_metrics_traffic_type_link_1d (mstats="480-app_metrics_traffic_type_link:DSSum:",tags=".__key")
+    60: optional map<string, AppMetrics_P_>  sum_app_metrics_traffic_type_link_7d (mstats="3360-app_metrics_traffic_type_link:DSSum:",tags=".__key")
+    61: optional map<string, AppMetrics_P_>  sum_app_metrics_traffic_type_link_30d (mstats="14400-app_metrics_traffic_type_link:DSSum:",tags=".__key")
+    62: optional map<string, AppMetrics_P_>  avg_app_metrics_traffic_type_link_3m (mstats="1-app_metrics_traffic_type_link:DSAvg:",tags=".__key")
+    63: optional map<string, AppMetrics_P_>  avg_app_metrics_traffic_type_link_1h (mstats="20-app_metrics_traffic_type_link:DSAvg:",tags=".__key")
+    64: optional map<string, AppMetrics_P_>  avg_app_metrics_traffic_type_link_8h (mstats="160-app_metrics_traffic_type_link:DSAvg:",tags=".__key")
+    65: optional map<string, AppMetrics_P_>  avg_app_metrics_traffic_type_link_1d (mstats="480-app_metrics_traffic_type_link:DSAvg:",tags=".__key")
+    66: optional map<string, AppMetrics_P_>  avg_app_metrics_traffic_type_link_7d (mstats="3360-app_metrics_traffic_type_link:DSAvg:",tags=".__key")
+    67: optional map<string, AppMetrics_P_>  avg_app_metrics_traffic_type_link_30d (mstats="14400-app_metrics_traffic_type_link:DSAvg:",tags=".__key")
+
+    /* app_metrics_traffic_type_user */
+    68: optional map<string, AppMetrics>     app_metrics_traffic_type_user (hidden="yes",metric="diff")
+    69: optional map<string, AppMetrics_P_>  sum_app_metrics_traffic_type_user_3m (mstats="1-app_metrics_traffic_type_user:DSSum:",tags=".__key")
+    70: optional map<string, AppMetrics_P_>  sum_app_metrics_traffic_type_user_1h (mstats="20-app_metrics_traffic_type_user:DSSum:",tags=".__key")
+    71: optional map<string, AppMetrics_P_>  sum_app_metrics_traffic_type_user_8h (mstats="160-app_metrics_traffic_type_user:DSSum:",tags=".__key")
+    72: optional map<string, AppMetrics_P_>  sum_app_metrics_traffic_type_user_1d (mstats="480-app_metrics_traffic_type_user:DSSum:",tags=".__key")
+    73: optional map<string, AppMetrics_P_>  sum_app_metrics_traffic_type_user_7d (mstats="3360-app_metrics_traffic_type_user:DSSum:",tags=".__key")
+    74: optional map<string, AppMetrics_P_>  sum_app_metrics_traffic_type_user_30d (mstats="14400-app_metrics_traffic_type_user:DSSum:",tags=".__key")
+    75: optional map<string, AppMetrics_P_>  avg_app_metrics_traffic_type_user_3m (mstats="1-app_metrics_traffic_type_user:DSAvg:",tags=".__key")
+    76: optional map<string, AppMetrics_P_>  avg_app_metrics_traffic_type_user_1h (mstats="20-app_metrics_traffic_type_user:DSAvg:",tags=".__key")
+    77: optional map<string, AppMetrics_P_>  avg_app_metrics_traffic_type_user_8h (mstats="160-app_metrics_traffic_type_user:DSAvg:",tags=".__key")
+    78: optional map<string, AppMetrics_P_>  avg_app_metrics_traffic_type_user_1d (mstats="480-app_metrics_traffic_type_user:DSAvg:",tags=".__key")
+    79: optional map<string, AppMetrics_P_>  avg_app_metrics_traffic_type_user_7d (mstats="3360-app_metrics_traffic_type_user:DSAvg:",tags=".__key")
+    80: optional map<string, AppMetrics_P_>  avg_app_metrics_traffic_type_user_30d (mstats="14400-app_metrics_traffic_type_user:DSAvg:",tags=".__key")
 }(period="180")
 
 uve sandesh AppTrack {
      1: AppTrackRecord              data;
 }
+
+
+/*  UVE key -> REGION::TENANT
+*   Two maps:
+*     1. tenant_metrics_sla
+*         key is => SLA_PROFILE
+*     2. tenant_metrics_traffic_type
+*         key is => TRAFFIC_TYPE
+*/
+struct TenantMetricRecord{
+    1: string                           name (key="ObjectCPETable")
+    2: optional bool                    deleted
+
+    /* tenant_metrics_sla  */
+    3: optional map<string, AppMetrics>      tenant_metrics (hidden="yes",metric="diff")
+    4: optional map<string, AppMetrics_P_>   sum_tenant_metrics_sla_3m (mstats="1-tenant_metrics_sla:DSSum:",tags=".__key")
+    5: optional map<string, AppMetrics_P_>   sum_tenant_metrics_sla_1h (mstats="20-tenant_metrics_sla:DSSum:",tags=".__key")
+    6: optional map<string, AppMetrics_P_>   sum_tenant_metrics_sla_8h (mstats="160-tenant_metrics_sla:DSSum:",tags=".__key")
+    7: optional map<string, AppMetrics_P_>   sum_tenant_metrics_sla_1d (mstats="480-tenant_metrics_sla:DSSum:",tags=".__key")
+    8: optional map<string, AppMetrics_P_>   sum_tenant_metrics_sla_7d (mstats="3360-tenant_metrics_sla:DSSum:",tags=".__key")
+    9: optional map<string, AppMetrics_P_>   sum_tenant_metrics_sla_30d (mstats="14400-tenant_metrics_sla:DSSum:",tags=".__key")
+    10: optional map<string, AppMetrics_P_>  avg_tenant_metrics_sla_3m (mstats="1-tenant_metrics_sla:DSAvg:",tags=".__key")
+    11: optional map<string, AppMetrics_P_>  avg_tenant_metrics_sla_1h (mstats="20-tenant_metrics_sla:DSAvg:",tags=".__key")
+    12: optional map<string, AppMetrics_P_>  avg_tenant_metrics_sla_8h (mstats="160-tenant_metrics_sla:DSAvg:",tags=".__key")
+    13: optional map<string, AppMetrics_P_>  avg_tenant_metrics_sla_1d (mstats="480-tenant_metrics_sla:DSAvg:",tags=".__key")
+    14: optional map<string, AppMetrics_P_>  avg_tenant_metrics_sla_7d (mstats="3360-tenant_metrics_sla:DSAvg:",tags=".__key")
+    15: optional map<string, AppMetrics_P_>  avg_tenant_metrics_sla_30d (mstats="14400-tenant_metrics_sla:DSAvg:",tags=".__key")
+
+    /* tenant_metrics_traffic_type  */
+    16: optional map<string, AppMetrics>      tenant_metrics (hidden="yes",metric="diff")
+    17: optional map<string, AppMetrics_P_>   sum_tenant_metrics_traffic_type_3m (mstats="1-tenant_metrics_traffic_type:DSSum:",tags=".__key")
+    18: optional map<string, AppMetrics_P_>   sum_tenant_metrics_traffic_type_1h (mstats="20-tenant_metrics_traffic_type:DSSum:",tags=".__key")
+    19: optional map<string, AppMetrics_P_>   sum_tenant_metrics_traffic_type_8h (mstats="160-tenant_metrics_traffic_type:DSSum:",tags=".__key")
+    20: optional map<string, AppMetrics_P_>   sum_tenant_metrics_traffic_type_1d (mstats="480-tenant_metrics_traffic_type:DSSum:",tags=".__key")
+    21: optional map<string, AppMetrics_P_>   sum_tenant_metrics_traffic_type_7d (mstats="3360-tenant_metrics_traffic_type:DSSum:",tags=".__key")
+    22: optional map<string, AppMetrics_P_>   sum_tenant_metrics_traffic_type_30d (mstats="14400-tenant_metrics_traffic_type:DSSum:",tags=".__key")
+    23: optional map<string, AppMetrics_P_>  avg_tenant_metrics_traffic_type_3m (mstats="1-tenant_metrics_traffic_type:DSAvg:",tags=".__key")
+    24: optional map<string, AppMetrics_P_>  avg_tenant_metrics_traffic_type_1h (mstats="20-tenant_metrics_traffic_type:DSAvg:",tags=".__key")
+    25: optional map<string, AppMetrics_P_>  avg_tenant_metrics_traffic_type_8h (mstats="160-tenant_metrics_traffic_type:DSAvg:",tags=".__key")
+    26: optional map<string, AppMetrics_P_>  avg_tenant_metrics_traffic_type_1d (mstats="480-tenant_metrics_traffic_type:DSAvg:",tags=".__key")
+    27: optional map<string, AppMetrics_P_>  avg_tenant_metrics_traffic_type_7d (mstats="3360-tenant_metrics_traffic_type:DSAvg:",tags=".__key")
+    28: optional map<string, AppMetrics_P_>  avg_tenant_metrics_traffic_type_30d (mstats="14400-tenant_metrics_traffic_type:DSAvg:",tags=".__key")
+}(period="180")
+
+uve sandesh TenantMetric {
+     1: TenantMetricRecord          data;
+}
+
+struct UveCPESlaMetrics{
+    1: optional u64                     txbps
+    2: optional u64                     rxbps
+    3: optional u64                     jitter
+    4: optional u64                     rtt
+    5: optional u64                     loss_percentage
+    6: optional u64                     latency
+    7: optional u64                     score
+}
+
+struct UveCPESlaMetrics_P_{
+    1:optional UveCPESlaMetrics value (tags="")
+    2:optional UveCPESlaMetrics staging
+}
+
+/*  UVE key -> TENANT::SITE::DEVICE_ID
+*   1 map:
+*     sla_metrics_dial
+*         key is => LINK::APP(APP-GROUP)
+*/
+struct UveCPE {
+    1: string                           name (key="ObjectCPETable")
+    2: optional bool                    deleted
+    3: optional map <string, UveCPESlaMetrics>     sla_metrics_dial (hidden="yes",metric=”dial”)
+    4: optional map <string, UveCPESlaMetrics_P_>  avg_sla_metrics_dial_3m (mstats="1-sla_metrics_dial:DSAvg:",tags=".__key")
+    5: optional map <string, UveCPESlaMetrics_P_>  avg_sla_metrics_dial_1h (mstats="20-sla_metrics_dial:DSAvg:",tags=".__key")
+    6: optional map <string, UveCPESlaMetrics_P_>  avg_sla_metrics_dial_8h (mstats="160-sla_metrics_dial:DSAvg:",tags=".__key")
+    7: optional map <string, UveCPESlaMetrics_P_>  avg_sla_metrics_dial_1d (mstats="480-sla_metrics_dial:DSAvg:",tags=".__key")
+    8: optional map <string, UveCPESlaMetrics_P_>  avg_sla_metrics_dial_7d (mstats="3360-sla_metrics_dial:DSAvg:",tags=".__key")
+    9: optional map <string, UveCPESlaMetrics_P_>  avg_sla_metrics_dial_30d (mstats="14400-sla_metrics_dial:DSAvg:",tags=".__key")
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

