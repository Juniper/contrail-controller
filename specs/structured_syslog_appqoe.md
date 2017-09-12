
# 1. Introduction
This blue print describes the design, features, and implementation of "structured syslog collector" in Contrail Collector to handle APPQOE messages like APPTRACK_APPQOE_PASSIVE_SLA_METRIC_REPORT, APPTRACK_APPQOE_ACTIVE_SLA_METRIC_REPORT, APPTRACK_APPQOE_SLA_METRIC_VIOLATION and APPTRACK_APPQOE_BEST_PATH_SELECTED.

# 2. Problem statement
"structured syslog collector" will receive APPQOE syslog messages from SRX/VSRX devices, parses them and pushes the relevant stats relating to the same into Cassandra, so that Application QOE related views can be made available to the customer.


# 3. Proposed solution
APPQOE syslog messages from SRX/VSRX devices contains additional data, which needs to be parsed to derive more meaningful & detailed information like ingress-jitter, egress-jitter, rtt-jitter, rtt, pkt-loss etc

sample APPQOE syslog messages:

<14>1 2017-10-12T21:52:56.976+05:30 rsubrahm-1srx RT_FLOW - APPQOE_PASSIVE_SLA_METRIC_REPORT [junos@2636.1.1.1.2.129 source-address="20.1.1.1" source-port="34421" destination-address="157.240.13.35" destination-port="443" apbr-profile="apbrProf1" apbr-rule="rule1" application="UNKNOWN" group-name="Root" service-name="junos-https" protocol-id="6" source-zone-name="trust" destination-zone-name="untrust" session-id-32="43" username="N/A" roles="N/A" routing-instance="RI1" destination-interface-name="gr-0/0/0.0" ip-dscp="0" sla-rule="4108306968" ingress-jitter="56" egress-jitter="453" rtt-jitter="397" rtt="1088" pkt-loss="0" bytes-from-client="5967" bytes-from-server="262576" packets-from-client="4108306768" packets-from-server="4108306736" monitoring-time="3204"]

<14>1 2017-10-12T21:53:03.614+05:30 rsubrahm-1srx RT_FLOW - APPQOE_ACTIVE_SLA_METRIC_REPORT [junos@2636.1.1.1.2.129source-address="7.1.1.0" source-port="35050" destination-address="7.1.1.1" destination-port="35050" application="UDP" protocol-id="17" destination-zone-name="untrust" routing-instance="default" destination-interface-name="gr-0/0/0.0" ip-dscp="0" ingress-jitter="785" egress-jitter="218" rtt-jitter="1790" rtt="1941" pkt-loss="0" bytes-from-client="19068" bytes-from-server="30592" packets-from-client="227" packets-from-server="239" monitoring-time="10" active-probe-params="PP1"]

<14>1 2017-10-12T21:52:53.783+05:30 rsubrahm-1srx RT_FLOW - APPQOE_SLA_METRIC_VIOLATION [junos@2636.1.1.1.2.129source-address="20.1.1.1" source-port="34421" destination-address="157.240.13.35" destination-port="443" apbr-profile="apbrProf1" apbr-rule="rule1" application="SSL" group-name="Root" service-name="junos-https" protocol-id="6" source-zone-name="trust" destination-zone-name="untrust" session-id-32="43" username="N/A" roles="N/A" routing-instance="RI1" destination-interface-name="gr-0/0/0.0" ip-dscp="0"sla-rule="SLA1" ingress-jitter="14" egress-jitter="23" rtt-jitter="9" rtt="598" pkt-loss="0" target-jitter-type="0" target-jitter="100" target-rtt="400" target-pkt-loss="1" violation-reason="1" jitter-violation-count="0" pkt-loss-violation-count="0" rtt-violation-count="3" violation-duration="0" bytes-from-client="1024" bytes-from-server="3738" packets-from-client="4108304016" packets-from-server="4108303984" monitoring-time="161"]

<14>1 2017-10-12T21:52:56.976+05:30 rsubrahm-1srx RT_FLOW - APPQOE_BEST_PATH_SELECTED [junos@2636.1.1.1.2.129 source-address="20.1.1.1" source-port="34421" destination-address="157.240.13.35" destination-port="443" apbr-profile="apbrProf1" apbr-rule="rule1" application="UNKNOWN" group-name="Root" service-name="junos-https" protocol-id="6" source-zone-name="trust" destination-zone-name="untrust" session-id-32="43" username="N/A" roles="N/A" routing-instance="RI1" destination-interface-name="gr-0/0/0.0" ip-dscp="0" sla-rule="SLA1" elapsed-time="2" bytes-from-client="6367" bytes-from-server="262576" packets-from-client="98" packets-from-server="190" previous-interface="gr-0/0/0.0"]

 
While the existing "structured syslog collector" receive and parses the syslogs, selected fields are written into the app level, link level and tenant level SDWAN UVEs which will help aggregating the data as needed.

structured-syslog-sla-profile configDB object is introduced, which will be used to decorate the UVE with traffic-type and sampling-percentage information  

structured-syslog-hostname-record is updated to have a list (structured-syslog-linkmap) to store a mapping between overlay link names and underlay link names, so that throughput related stats for an overlay link can be accounted against underlay as well. 

## 3.1 Alternatives considered
#### None

## 3.2 API schema changes
diff --git a/src/schema/structured_syslog_config.xsd b/src/schema/structured_syslog_config.xsd
index 963d8ec..eda0669 100644
--- a/src/schema/structured_syslog_config.xsd
+++ b/src/schema/structured_syslog_config.xsd
@@ -51,6 +51,18 @@
     </xsd:restriction>
 </xsd:simpleType>
 
+<xsd:complexType name="StructuredSyslogLinkType">
+    <xsd:all>
+        <xsd:element name="overlay" type="xsd:string"/>
+        <xsd:element name="underlay" type="xsd:string"/>
+    </xsd:all>
+</xsd:complexType>
+
+<xsd:complexType name="StructuredSyslogLinkmap">
+    <xsd:all>
+        <xsd:element name="links" type="StructuredSyslogLinkType" maxOccurs="unbounded"/>
+    </xsd:all>
+</xsd:complexType>
 
 <xsd:element name="structured-syslog-message" type="ifmap:IdentityType"
      description="structured syslog messages to be handled"/>
@@ -123,6 +135,11 @@
 <!--#IFMAP-SEMANTICS-IDL Property('structured-syslog-device',
                                   'structured-syslog-hostname-record',
                                   'optional', 'CRUD', 'device id') -->
+<xsd:element name="structured-syslog-linkmap" type="StructuredSyslogLinkmap" />
+<!--#IFMAP-SEMANTICS-IDL Property('structured-syslog-linkmap',
+                                  'structured-syslog-hostname-record',
+                                  'optional', 'CRUD', 'overlay to
+                                   underlay mapping') -->
 
 <xsd:element name="structured-syslog-application-record"
                     type="ifmap:IdentityType"
@@ -161,5 +178,23 @@
                                   'optional', 'CRUD', 'service-tags
                                    corresponding to applications') -->
 
+<xsd:element name="structured-syslog-sla-profile"
+                    type="ifmap:IdentityType"
+                    description="mapping sla-profile to sla params"/>
+<xsd:element name="structured-syslog-config-structured-syslog-sla-profile"
+                    description="mapping sla-profile to sla params"/>
+<!--#IFMAP-SEMANTICS-IDL
+    Link('structured-syslog-config-structured-syslog-sla-profile',
+         'structured-syslog-config', 'structured-syslog-sla-profile',
+         ['has'], 'optional', 'CRUD', 'List of
+         structured-syslog-sla-profile that are applicable to objects
+         anchored under structured-syslog-config.')-->
+<xsd:element name="structured-syslog-sla-params" type="xsd:string" />
+<!--#IFMAP-SEMANTICS-IDL Property('structured-syslog-sla-params',
+                                  'structured-syslog-sla-profile',
+                                  'required', 'CRUD', 'The sla
+                                   params like sampling %age and
+                                   traffic type') -->
+
 </xsd:schema>


diff --git a/src/analytics/collector_uve.sandesh b/src/analytics/collector_uve.sandesh
index 4786365..95d8837 100644
--- a/src/analytics/collector_uve.sandesh
+++ b/src/analytics/collector_uve.sandesh
@@ -483,3 +483,169 @@ struct AppTrackRecord{
 uve sandesh AppTrack {
      1: AppTrackRecord              data;
 }
+
+/*
+ * UVE definition for SDWAN metrics from structured syslog messages.
+*/
+
+struct SDWANMetrics_diff {
+      1: optional u64                     session_duration
+      2: optional u64                     session_count
+      3: optional u64                     total_bytes
+      4: optional u64                     input_bytes
+      5: optional u64                     output_bytes
+      6: optional u64                     total_pkts
+      7: optional u64                     input_pkts
+      8: optional u64                     output_pkts
+      9: optional u64                     sla_violation_duration
+     10: optional u64                     sla_violation_count
+     11: optional u64                     session_switch_count
+     12: optional u64                     jitter_violation_count
+     13: optional u64                     rtt_violation_count
+     14: optional u64                     pkt_loss_violation_count
+     15: optional u64                     minor_alarms_raised
+     16: optional u64                     major_alarms_raised
+     17: optional u64                     critical_alarms_raised
+     18: optional u64                     total_alarms_raised
+     19: optional u64                     total_alarms_cleared
+}
+
+struct SDWANMetrics_dial {
+      1: optional u64                     txbps
+      2: optional u64                     rxbps
+      3: optional u64                     rtt
+      4: optional u64                     latency
+      5: optional u64                     pkt_loss
+      6: optional u64                     rtt_jitter
+      7: optional u64                     egress_jitter
+      8: optional u64                     ingress_jitter
+      9: optional u64                     sampling_percentage
+     10: optional u64                     score
+}
+
+struct SDWANMetrics_diff_P_ {
+      1: optional SDWANMetrics_diff value (tags="")
+      2: optional SDWANMetrics_diff staging
+}
+
+struct SDWANMetrics_dial_P_ {
+      1: optional SDWANMetrics_dial value (tags="")
+      2: optional SDWANMetrics_dial staging
+}
+
+/*  UVE key -> TENANT::SITE::DEVICE_ID
+*   Eight maps:
+*     1. app_metrics_diff_sla
+*        TRAFFIC-TYPE(NESTED-APP:APP/APP-CATEGORY)::DEPARTMENT::SLA_PROFILE
+*     2. app_metrics_dial_sla
+*        TRAFFIC-TYPE(NESTED-APP:APP/APP-CATEGORY)::DEPARTMENT::SLA_PROFILE
+*     3. app_metrics_diff_link
+*        TRAFFIC-TYPE(NESTED-APP:APP/APP-CATEGORY)::DEPARTMENT::LINK
+*     4. app_metrics_dial_link
+*        TRAFFIC-TYPE(NESTED-APP:APP/APP-CATEGORY)::DEPARTMENT::LINK
+*     5. app_metrics_diff_user
+*        TRAFFIC-TYPE(NESTED-APP:APP/APP-CATEGORY)::DEPARTMENT::USER
+*     6. app_metrics_dial_user
+*        TRAFFIC-TYPE(NESTED-APP:APP/APP-CATEGORY)::DEPARTMENT::USER
+*     7. link_metrics_diff_traffic_type
+*        LINK::SLA_PROFILE::TRAFFIC_TYPE
+*     8. link_metrics_dial_traffic_type
+*        LINK::SLA_PROFILE::TRAFFIC_TYPE
+*/
+
+struct SDWANMetricsRecord {
+    1: string                           name (key="ObjectCPETable")
+    2: optional bool                    deleted
+
+    /* app_metrics_diff_sla  */
+    3: optional map<string, SDWANMetrics_diff>      app_metrics_diff_sla (hidden="yes",metric="diff")
+    4: optional map<string, SDWANMetrics_diff_P_>   app_metrics_diff_sla_3m (mstats="1-app_metrics_diff_sla:DSSum:",tags=".__
+    5: optional map<string, SDWANMetrics_diff_P_>   app_metrics_diff_sla_1h (mstats="20-app_metrics_diff_sla:DSSum:",tags="._
+    6: optional map<string, SDWANMetrics_diff_P_>   app_metrics_diff_sla_8h (mstats="160-app_metrics_diff_sla:DSSum:",tags=".
+    7: optional map<string, SDWANMetrics_diff_P_>   app_metrics_diff_sla_1d (mstats="480-app_metrics_diff_sla:DSSum:",tags=".
+
+    /* app_metrics_dial_sla  */
+    8: optional map<string, SDWANMetrics_dial>      app_metrics_dial_sla (hidden="yes",metric="dial")
+    9: optional map<string, SDWANMetrics_dial_P_>   app_metrics_dial_sla_3m (mstats="1-app_metrics_dial_sla:DSAvg:",tags=".__
+   10: optional map<string, SDWANMetrics_dial_P_>   app_metrics_dial_sla_1h (mstats="20-app_metrics_dial_sla:DSAvg:",tags="._
+   11: optional map<string, SDWANMetrics_dial_P_>   app_metrics_dial_sla_8h (mstats="160-app_metrics_dial_sla:DSAvg:",tags=".
+   12: optional map<string, SDWANMetrics_dial_P_>   app_metrics_dial_sla_1d (mstats="480-app_metrics_dial_sla:DSAvg:",tags=".
+
+    /* app_metrics_diff_link  */
+   13: optional map<string, SDWANMetrics_diff>      app_metrics_diff_link (hidden="yes",metric="diff")
+   14: optional map<string, SDWANMetrics_diff_P_>   app_metrics_diff_link_3m (mstats="1-app_metrics_diff_link:DSSum:",tags=".
+   15: optional map<string, SDWANMetrics_diff_P_>   app_metrics_diff_link_1h (mstats="20-app_metrics_diff_link:DSSum:",tags="
+   16: optional map<string, SDWANMetrics_diff_P_>   app_metrics_diff_link_8h (mstats="160-app_metrics_diff_link:DSSum:",tags=
+   17: optional map<string, SDWANMetrics_diff_P_>   app_metrics_diff_link_1d (mstats="480-app_metrics_diff_link:DSSum:",tags=
+
+    /* app_metrics_dial_link  */
+   18: optional map<string, SDWANMetrics_dial>      app_metrics_dial_link (hidden="yes",metric="dial")
+   19: optional map<string, SDWANMetrics_dial_P_>   app_metrics_dial_link_3m (mstats="1-app_metrics_dial_link:DSAvg:",tags=".
+   20: optional map<string, SDWANMetrics_dial_P_>   app_metrics_dial_link_1h (mstats="20-app_metrics_dial_link:DSAvg:",tags="
+   21: optional map<string, SDWANMetrics_dial_P_>   app_metrics_dial_link_8h (mstats="160-app_metrics_dial_link:DSAvg:",tags=
+   22: optional map<string, SDWANMetrics_dial_P_>   app_metrics_dial_link_1d (mstats="480-app_metrics_dial_link:DSAvg:",tags=
+
+    /* app_metrics_diff_user  */
+   23: optional map<string, SDWANMetrics_diff>      app_metrics_diff_user (hidden="yes",metric="diff")
+   24: optional map<string, SDWANMetrics_diff_P_>   app_metrics_diff_user_3m (mstats="1-app_metrics_diff_user:DSSum:",tags=".
+   25: optional map<string, SDWANMetrics_diff_P_>   app_metrics_diff_user_1h (mstats="20-app_metrics_diff_user:DSSum:",tags="
+   26: optional map<string, SDWANMetrics_diff_P_>   app_metrics_diff_user_8h (mstats="160-app_metrics_diff_user:DSSum:",tags=
+   27: optional map<string, SDWANMetrics_diff_P_>   app_metrics_diff_user_1d (mstats="480-app_metrics_diff_user:DSSum:",tags=
+
+    /* app_metrics_dial_user  */
+   28: optional map<string, SDWANMetrics_dial>      app_metrics_dial_user (hidden="yes",metric="dial")
+   29: optional map<string, SDWANMetrics_dial_P_>   app_metrics_dial_user_3m (mstats="1-app_metrics_dial_user:DSAvg:",tags=".
+   30: optional map<string, SDWANMetrics_dial_P_>   app_metrics_dial_user_1h (mstats="20-app_metrics_dial_user:DSAvg:",tags="
+   31: optional map<string, SDWANMetrics_dial_P_>   app_metrics_dial_user_8h (mstats="160-app_metrics_dial_user:DSAvg:",tags=
+   32: optional map<string, SDWANMetrics_dial_P_>   app_metrics_dial_user_1d (mstats="480-app_metrics_dial_user:DSAvg:",tags=
+    /* link_metrics_diff_traffic_type  */
+   33: optional map<string, SDWANMetrics_diff>      link_metrics_diff_traffic_type (hidden="yes",metric="diff")
+   34: optional map<string, SDWANMetrics_diff_P_>   link_metrics_diff_traffic_type_3m (mstats="1-link_metrics_diff_traffic_ty
+   35: optional map<string, SDWANMetrics_diff_P_>   link_metrics_diff_traffic_type_1h (mstats="20-link_metrics_diff_traffic_t
+   36: optional map<string, SDWANMetrics_diff_P_>   link_metrics_diff_traffic_type_8h (mstats="160-link_metrics_diff_traffic_
+   37: optional map<string, SDWANMetrics_diff_P_>   link_metrics_diff_traffic_type_1d (mstats="480-link_metrics_diff_traffic_
+
+    /* link_metrics_dial_traffic_type  */
+   38: optional map<string, SDWANMetrics_dial>      link_metrics_dial_traffic_type (hidden="yes",metric="dial")
+   39: optional map<string, SDWANMetrics_dial_P_>   link_metrics_dial_traffic_type_3m (mstats="1-link_metrics_dial_traffic_ty
+   40: optional map<string, SDWANMetrics_dial_P_>   link_metrics_dial_traffic_type_1h (mstats="20-link_metrics_dial_traffic_t
+   41: optional map<string, SDWANMetrics_dial_P_>   link_metrics_dial_traffic_type_8h (mstats="160-link_metrics_dial_traffic_
+   42: optional map<string, SDWANMetrics_dial_P_>   link_metrics_dial_traffic_type_1d (mstats="480-link_metrics_dial_traffic_
+}(period="180")
+
+uve sandesh SDWANMetrics {
+     1: SDWANMetricsRecord              data;
+}
+
+
+/*  UVE key -> REGION::TENANT
+*   Two maps:
+*     1. tenant_metrics_diff_sla
+*         key is => SITE::SLA_PROFILE::TRAFFIC_TYPE
+*     2. tenant_metrics_dial_sla
+*         key is => SITE::SLA_PROFILE::TRAFFIC_TYPE
+*/
+struct SDWANTenantMetricsRecord {
+    1: string                           name (key="ObjectCPETable")
+    2: optional bool                    deleted
+
+    /* tenant_metrics_diff_sla  */
+    3: optional map<string, SDWANMetrics_diff>      tenant_metrics_diff_sla (hidden="yes",metric="diff")
+    4: optional map<string, SDWANMetrics_diff_P_>   tenant_metrics_diff_sla_3m (mstats="1-tenant_metrics_diff_sla:DSSum:",tag
+    5: optional map<string, SDWANMetrics_diff_P_>   tenant_metrics_diff_sla_1h (mstats="20-tenant_metrics_diff_sla:DSSum:",ta
+    6: optional map<string, SDWANMetrics_diff_P_>   tenant_metrics_diff_sla_8h (mstats="160-tenant_metrics_diff_sla:DSSum:",t
+    7: optional map<string, SDWANMetrics_diff_P_>   tenant_metrics_diff_sla_1d (mstats="480-tenant_metrics_diff_sla:DSSum:",t
+
+    /* tenant_metrics_dial_sla  */
+    8: optional map<string, SDWANMetrics_dial>      tenant_metrics_dial_sla (hidden="yes",metric="dial")
+    9: optional map<string, SDWANMetrics_dial_P_>   tenant_metrics_dial_sla_3m (mstats="1-tenant_metrics_dial_sla:DSAvg:",tag
+   10: optional map<string, SDWANMetrics_dial_P_>   tenant_metrics_dial_sla_1h (mstats="20-tenant_metrics_dial_sla:DSAvg:",ta
+   11: optional map<string, SDWANMetrics_dial_P_>   tenant_metrics_dial_sla_8h (mstats="160-tenant_metrics_dial_sla:DSAvg:",t
+   12: optional map<string, SDWANMetrics_dial_P_>   tenant_metrics_dial_sla_1d (mstats="480-tenant_metrics_dial_sla:DSAvg:",t
+
+}(period="180")
+
+uve sandesh SDWANTenantMetrics {
+     1: SDWANTenantMetricsRecord          data;
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
Create and update SDWAN UVEs for QOE parameters with appropriate mappings

#### Collector:
Implement "structured syslog collector" changes to update SDWAN UVE with relevant QOE fields

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
