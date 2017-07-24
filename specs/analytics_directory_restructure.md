# 1. Introduction
Contrail Analytics source code currently resides in the contrail-controller
repository on github. Contrail Analytics can be installed and deployed
as a standalone product independent of Contrail SDN controller product.
It is hence desirable to have the analytics source code in its own
repository on github.

# 2. Problem statement
Currently Contrail Analytics source code is part of the contrail-controller
github repository. Once Contrail Analytics is deployed as a standalone
product independent of Contrail SDN controller product, it might have
different release schedules and priorities compared to the Contrail SDN
controller product. It is thus desirable to have the analytics source
code in its own repository on github.

# 3. Proposed solution
Contrail Analytics source code is currently under the following
directories in the contrail-controller repo:
1. controller/src/analytics which contains the contrail-collector,
   contrail-snmp-collector, and contrail-topology source code
2. controller/src/query_engine which contains the contrail-query-engine
   source code
3. controller/src/opserver which contains the contrail-analytics-api,
   and contrail-alarm-gen source code

Proposal is to consolidate the analytics source code in a github source
repository - contrail-analytics and have it pulled under the
controller/src/analytics directory when repo sync is done. In addition:
1. Create controller/src/analytics/contrail-collector directory for the collector
   source code. Create controller/src/analytics/contrail-collector/ingest directory
   and move the different ingests under their respective directories, i.e.
   ingest/sandesh, ingest/protobuf, ingest/sflow, ingest/ipfix, ingest/syslog, etc.
2. Create controller/src/analytics/contrail-query-engine directory for the query
   engine source code.
3. Create controller/src/analytics/common for shared code between the analytics
   processes.
4. Create controller/src/analytics/opserver for contrail-analytics-api, and
   contrail-alarm-gen source code

## 3.1 Alternatives considered
None

## 3.2 API schema changes
None

## 3.3 User workflow impact
None

## 3.4 UI changes
None

## 3.5 Notification impact
None

# 4. Implementation
## 4.1 Work items
1. Create new github source repo - contrail-analytics
2. Move code under controller/src/analytics to ease movement to the new
   repo

# 5. Performance and scaling impact
## 5.1 API and control plane
None

## 5.2 Forwarding performance
NA

# 6. Upgrade
NA

# 7. Deprecations
NA

# 8. Dependencies
NA

# 9. Testing
CI package builds and tests should be sufficient

# 10. Documentation Impact
NA

# 11. References
NA
