
# 1. Introduction
This blue print describes the design, features, and implementation of "structured syslog collector" in Contrail Collector to handle APPQOE messages like APPTRACK_APPQOE_PASSIVE_SLA_METRIC_REPORT, APPTRACK_APPQOE_ACTIVE_SLA_METRIC_REPORT, APPTRACK_APPQOE_SLA_METRIC_VIOLATION and APPTRACK_APPQOE_BEST_PATH_SELECTED.

# 2. Problem statement
"structured syslog collector" will receive APPQOE syslog messages from SRX/VSRX devices, parses them and pushes the relevant stats relating to the same into Cassandra, so that Application QOE related views can be made available to the customer.


# 3. Proposed solution
APPQOE syslog messages from SRX/VSRX devices contains additional data, which needs to be parsed to derive more meaningful & detailed information like ingress-jitter, egress-jitter, rtt-jitter, rtt, pkt-loss etc

sample APPQOE syslog messages:

<14>1 2011-11-30T00: 6:15.274 srx5800n0 RT_FLOW - APPTRACK_APPQOE_PASSIVE_SLA_METRIC_REPORT [junos@2636.1.1.1.2.129 service-name="icmp" source-address="1.1.1.1" source-port="3" destination-address="2.2.2.2" destination-port="23911" application="UNKNOWN" source-zone-name="lan" destination-zone-name="wan"  username="N/A" roles="N/A" rule-name="N/A" routing-instance="default" destination-interface-name="ge-0/0/4.0" protocol-id="1" ip-dscp=“10” session-id-32="295" ingress-jitter=“10” egress-jitter=“5” rtt-jitter=“10” rtt=“10000” pkt-loss=“2” application-group=“1022” profile-name=“apbr_prof1” bytes-from-client="84" bytes-from-server="84" packets-from-client="1" packets-from-server="1"  monitoring-time="4" sla-rule="sla1"]

<14>1 2011-11-30T00: 6:16.457 srx5800n0 RT_FLOW - APPTRACK_APPQOE_ACTIVE_SLA_METRIC_REPORT [junos@2636.1.1.1.2.129 source-address="1.1.1.1" source-port="3" destination-address="2.2.2.2" destination-port="23911" application="UNKNOWN"  destination-zone-name="wan"  routing-instance="default" destination-interface-name="ge-0/0/4.0" protocol-id="1" ip-dscp=“10” ingress-jitter=“10” egress-jitter=“5” rtt-jitter=“10” rtt=“10000” pkt-loss=“2” bytes-from-client="84" bytes-from-server="84" packets-from-client="1" packets-from-server="1"  monitoring-time=“10” probe-params-name="voice_params"]

<14>1 2011-11-30T00: 6:16.848 srx5800n0 RT_FLOW - APPTRACK_APPQOE_SLA_METRIC_VIOLATION [junos@2636.1.1.1.2.129 service-name="icmp" source-address="1.1.1.1" source-port="3" destination-address="2.2.2.2" destination-port="23911" application="UNKNOWN" source-zone-name="lan" destination-zone-name="wan"  username="N/A" roles="N/A" rule-name="N/A" routing-instance="default" destination-interface-name="ge-0/0/4.0" protocol-id="1" ip-dscp=“10” session-id-32="295" ingress-jitter=“10” egress-jitter=“5” rtt-jitter=“10” rtt=“10000” pkt-loss=“2” application-group=“1022” profile-name=“apbr_prof1” target-jitter-type=“1” target-jitter=“2” target-rtt=“4” target-pkt-loss=“2” violation-reason=“3” jitter-violation-count=“2” pkt-loss-violation-count=“1” rtt-violation-count=“8”  violation-duration="10" bytes-from-client="84" bytes-from-server="84" packets-from-client="1" packets-from-server="1"  monitoring-time="4"  sla-rule="sla1"]

<14>1 2011-11-30T00: 6:17.925 srx5800n0 RT_FLOW - APPTRACK_APPQOE_BEST_PATH_SELECTED [junos@2636.1.1.1.2.129 service-name="icmp" source-address="1.1.1.1" source-port="3" destination-address="2.2.2.2" destination-port="23911" application="UNKNOWN" source-zone-name="lan" destination-zone-name="wan"  username="N/A" roles="N/A" rule-name="N/A" routing-instance="default" destination-interface-name="ge-0/0/4.0" protocol-id="1" ip-dscp=“10” session-id-32="295" profile-name="apbr1" application-group=“1022” bytes-from-client="84" bytes-from-server="84" packets-from-client="1" packets-from-server="1" elapsed-time="10" sla-rule="sla1" previous-interface=“ge-0/0/2.0”]
 
While the existing "structured syslog collector" receive and parses the syslogs, selected fields are written into the UVEs which will help aggregating the data as needed  

structured-syslog-message objects in configDB will have new flag to indicate if new fields in the messages is to be written into UVEs

## 3.1 Alternatives considered
#### None

## 3.2 API schema changes
structured-syslog-message which describes the messages to be handled and actions to be taken on those messages will have a new flag to indicate QOE fields in the message is to be written into UVEs  

## 3.3 User workflow impact
#### N/A

## 3.4 UI changes
#### N/A

## 3.5 Notification impact
#### N/A


# 4. Implementation
## 4.1 Work items
#### Schema:
Update the XSD with the new flag under structured-syslog-message

Create and update UVEs for QOE parameters with appropriate mappings

#### Collector:
Implement "structured syslog collector" changes to update UVE with relevant QOE fields based on structured-syslog-message flag

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

Testing of CRUD operations on structured-syslog-message

Testing of handling of APPQOE structured syslog collector using a structured syslog simulator

## 9.3 System tests


# 10. Documentation Impact

# 11. References

