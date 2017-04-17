# 1. Introduction
OpenContrail is deployed in a variety of environments. Analytics uses cassandra
as the back-end database and cassandra has certain requirements in terms of
the production hardware needed to deploy it. Some deployments do not have the
cassandra recommended CPU, disk, and IO resources. In these cases, it has
been observed that cassandra cannot keep up with the amount of data inserted
by analytics and it can result in slowness or instability of the whole system.

#2. Problem Statement
Analytics uses cassandra as back-end database and in deployments with limited
and/or shared CPU, disk, and IO resources issues have been observed with
cassandra not keeping up with the amount of inserts done by analytics.
For example, the deployment is using SAN (storage area network) or single
HDD (hard disk) for storing all cassandra data instead of the recommended
SSD (solid state disk). In some cases CPU is shared between cassandra,
analytics, and other processes that comprise OpenContrail controller.
In some cases, enough disk space is not available for cassandra or the
data is stored on the same partition as other system files. The blueprint
discusses changes to be done for running analytics in these constrained
environments.

# 3. Proposed solution
Analytics inserts data into cassandra that can broadly be classified into logs
(systemlog and objectlog), UVEs, alarms, statistics, and flows. The collector
receives data from the generators in the form of Sandesh messages. The
different kind of Sandesh messages sent by the generators to the collector are
systemlog, objectlog, UVEs, alarms, and flows. Systemlog, objectlog, and flow
messages carry a severity or level indicating the importance of the message.
Statistics can be currently sent as part of UVEs as well as part of objectlog.

The database tables are broadly classified into message tables, statistics
table, and flow tables. The collector inserts these Sandesh messages into
different database tables based on their types:
1. Systemlog - Message tables
2. Objectlog - Message tables, statistics table (if contain statistics)
3. UVEs - Message tables, statistics table (if contain statistics)
4. Alarm - Message tables
5. Flow - Flow tables

Currently the collector drops systemlog, objectlog, and flows based on the
severity or level of the messages and based on the cassandra load indicated
via the pending compaction tasks, analytics database disk usage percentage,
and based on the internal database module queue size in the collector.

To remediate these issues we have decided to reduce the amount of data inserted
into cassandra by default, and only enable additional inserts explicitly via
user configuration. By default, systemlog, and flows will not be sent to
analytics and hence not stored into cassandra.

## 3.1 Alternatives considered
None

## 3.2 API schema changes
None

## 3.3 User workflow impact
By default, users will not be able to use contrail-logs or the analytics
REST API to query system logs. Users will have to look at the respective
daemons log files and/or syslog for debugging issues.

## 3.4 UI changes
By default, the UI systemlog query pages will not return any result.

## 3.5 Notification impact
As above, by default systemlog messages will not be sent to analytics.

# 4. Implementation
To reduce the amount of inserts in cassandra, we will disable sending of
systemlog, and flows to the collector by default in the sandesh client
library on the generators. Currently only the contrail-vrouter-agent sends
flows to the collector and the sending can be disabled by configuring the
flow export rate to be 0. We will change the default flow export rate to 0.
Sending of systemlog messages to analytics can currently be rate limited
by configuring --DEFAULT.sandesh_send_rate_limit. We will change the default
for this parameter to 0 so that systemlogs are not sent to analytics. We
will also add --SANDESH.disable_object_logs parameter to the generators
to allow control of sending object logs. Introspect command will be
provided to enable/disable the same.

On the collector, currently UVEs cannot be dropped similar to how systemlog,
objectlog, and flows due to 2 reasons:
1. UVEs need to be sent to redis and kafka for operational state
2. UVEs do not carry a severity or level.

UVEs will be changed to carry a default SYS_NOTICE severity and developers will
be provided the ability to change the severity via the Send API. Collector will
be changed to allow UVEs to be dropped after writing to redis and kafka so that
they are not inserted into cassandra.

# 5. Performance and scaling impact
The scaling of analytics with respect to the number of generators per
collector is expected to improve since now each generator will be sending
less number of messages.

## 5.2 Forwarding performance
Not affected

# 6. Upgrade
The default flow export rate will be changed and so after upgrade by default,
no flows will be sent to analytics. Similarly, the default system log send
rate limit value will be changed and so after upgrade, no system logs will
be sent to analytics.

# 7. Deprecations
None

# 8. Dependencies
None

# 9. Testing
## 9.1 Unit tests
1. Options tests will be modified to verify that the new option added
   - SANDESH.disable_object_logs works fine in the daemons.
2. Sandesh client library test will be added to verify that by default
   systemlog messages are dropped.
3. Introspect test to verify that the option can be disabled/enabled.

## 9.2 Dev tests
Verify that no systemlog and flows are received on collector by default.

# 10. Documentation Impact
Documentation needs to mention that flows from contrail-vrouter-agent and
systemlogs from other daemons will not be exported to analytics by default.

# 11. References
None
