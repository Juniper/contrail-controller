# 1. Introduction
Running Contrail Analytics in constrained environments

# 2. Problem statement
Analytics uses cassandra as back-end database and in environments with limited
and/or shared CPU, disk, and IO resources issues have been observed with
cassandra not keeping up with the amount of inserts done by analytics and in
environments in which cassandra is on the same node as the controller it can
result in the whole controller being unstable or running very slow.

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
user configuration. By default, systemlog, objectlog, and flows will not be
sent to analytics and hence not stored into cassandra.

## 3.1 Alternatives considered
None

## 3.2 API schema changes
None

## 3.3 User workflow impact
By default, users will not be able to use contrail-logs or the analytics
REST API to query system and object logs. Users will have to look at the
respective daemons log files and/or syslog for debugging issues.

## 3.4 UI changes
By default, the UI systemlog and objectlog query pages will not return
any result.

## 3.5 Notification impact
As above, by default systemlog and objectlog messages will not be sent
to analytics

# 4. Implementation
As mentioned above, To reduce the amount of inserts in cassandra, we will
disable sending of systemlog, objectlog, and flows to the collector by default
in the sandesh client library on the generators. Currently only the
contrail-vrouter-agent sends flows to the collector and the sending can be
disabled by configuring the flow sample/export rate to be 0. We will change
the default flow sample/export rate to 0. A new configuration file parameter
and command line argument - SANDESH.enable_system_and_object_logs will be
added to the generators. Introspect command will be provided to enable/disable
the same.

On the collector, currently UVEs cannot be dropped similar to how systemlog,
objectlog, and flows due to 2 reasons:
1. UVEs need to be sent to redis and kafka for operational state
2. UVEs do not carry a severity or level.

UVEs will be changed to carry a default SYS_INFO severity and developers will
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
no flows will be sent to analytics.

# 7. Deprecations
None

# 8. Dependencies
None

# 9. Testing
## 9.1 Unit tests
1. Options tests will be modified to verify that the new option added
   - SANDESH.enable_system_and_object_logs works fine in the daemons.
2. Sandesh client library test will be added to verify that by default
   systemlog and objectlog messages are dropped.
3. Introspect test to verify that the option can be disabled/enabled.

## 9.2 Dev tests
Verify that no systemlog and objectlog are received on collector by default.

# 10. Documentation Impact
Documentation needs to mention that flows from contrail-vrouter-agent will not
be exported to analytics by default and by default system and object logs will
also not be exported to analytics.

# 11. References
None
