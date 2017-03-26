
# 1. Introduction
This blue print describes the design, features, and implementation of "structured syslog collector" in Contrail Collector.

# 2. Problem statement
"structured syslog collector" will receive APPTRACK syslogs from SRX/VSRX devices and pushes the stats relating to the same into Cassandra, so that Application Visibility related views can be made available to the customer.
Apart from storing the syslog in Cassandra, syslog should be allowed to be forwarded to an external syslog receiver and/or over kafka if needed.

# 3. Proposed solution
 Structured syslogs from SRX/VSRX devices contains additional data, which needs to be parsed to derive more meaningful & detailed information like action, src_address, src_port, dst_address, dst_port, nat_src_address, nat_src_port, nat_dst_address, nat_dst_port, application, bytes-from-client, bytes-from-server etc

 sample structured syslog: <14>1 2011-11-30T00: 6:15.274 srx5800n0 RT_FLOW - APPTRACK_SESSION_CLOSE [junos@2636.1.1.1.2.26 reason="TCP RST" source-address="10.110.110.10" source-port="13175" destination-address="96.9.139.213" destination-port="48334" service-name="None" application="UNSPECIFIED-ENCRYPTED" nested-application="None" nat-source-address="10.110.110.10" nat-source-port="13175" destination-address="96.9.139.213" nat-destination-port="48334" src-nat-rule-name="None" dst-nat-rule-name="None" protocol-id="6" policy-name="dmz-out" source-zone-name="DMZ" destination-zone-name="Internet" session-id-32="44292" packets-from-client="7" bytes-from-client="1421" packets-from-server="6" bytes-from-server="1133" elapsed-time="4" username="Frank" roles="Engineering"]

 A new "structured syslog collector" needs to be implemented to receive the syslogs, which will parse the fields in the structured part of the syslog and the fields are written into the stattable using StatWalker

 Apart from the information available in the structured syslog, additional decorator information (like tenant, location, application group, application category) needs to be made available so that appropriate Application Visibility related views can be made available.

 Decorator information required is made available in the configDB as structured-syslog-hostname-record and structured-syslog-application-record

 The type of syslogs handled by "structured syslog collector" need to be configurable, hence structured-syslog-message objects are made available in configDB which describe the messages to be handled and actions to be taken on those messages.

 syslogs which needs to be forwarded to an external syslog receiver and/or over kafka are sent with or without decorator information. Based on the forwarder destination configuration in contrail-collector.conf, tcp connection with the destination syslog receiver will be established and/or kafka broker connection will be established and topic created at the startup of the "structured syslog collector".
## 3.1 Alternatives considered
#### None

## 3.2 API schema changes
The decorator information will be maintained in the configDB as structured-syslog-hostname-record and structured-syslog-application-record objects.
global-analytics-config is created under global-system-config, which will contain structured-syslog-config.

The structured-syslog-hostname-record and structured-syslog-application-record will be under structured-syslog-config which will be anchored under global-analytics-config.

structured-syslog-hostname-record and structured-syslog-application-record can be specific to each tenant as well. Hence structured-syslog-config can be anchored under project as well.

structured-syslog-message under structured-syslog-config describe the messages to be handled and actions to be taken on those messages.

## 3.3 User workflow impact
#### N/A

## 3.4 UI changes
#### N/A

## 3.5 Notification impact
#### N/A


# 4. Implementation
## 4.1 Work items
#### Schema:
Implement the XSD with the schema details for the structured-syslog-hostname-record, structured-syslog-application-record and structured-syslog-message
#### Collector:
Implement new "structured syslog collector" to receive structured syslogs (over TCP & UDP), parse the same and push data into statTable
Implement reading of decorator information from configDB and populate appropriate fields and push into statTable
Implement tcp client connection with remote syslog receiver and forward syslog with and without decoration
Implement kafka connection, topic creation and write syslog on required topic with and without decoration

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
Basic unit testing of structured_syslog_hostname_record and structured_syslog_application_record and utility functions in the collector are handled.

## 9.2 Dev tests

Testing of CRUD operations on structured_syslog_hostname_record, structured_syslog_application_record and structured-syslog-message

Testing of handling of structured syslog collector using a structured syslog simulator

## 9.3 System tests


# 10. Documentation Impact

# 11. References
