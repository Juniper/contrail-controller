
# 1. Introduction
Today, the contrail-analytics services get the user configuration from
contrail-api through REST interface. This spec provides details about the new
scheme, where the analytics services would read the user configuration directly
from the config database (cassandra).

# 2. Problem statement
Today, the contrail-analytics services periodically poll the user configuration
from the contrail-api through REST interface. This scheme has the following
limitations.

1. Analytics services do not react immediately to the user configuration
   changes and the delay depends on the poll duration.
2. The analytics service reads all the user configuration it is interested in
   during every poll interval and compares with its local data store to
   identify the CREATE, UPDATE and DELETE operations. This could be an
   expensive operation depending on the number of config objects.
3. Periodic polling of user configuration may increase the load on the
   contrail-api service.

# 3. Proposed solution
Upon start, the analytics service would read the configuration from config
database (cassandra) and listen for configuration notifications (CREATE,
UPDATE, DELETE) from Rabbitmq message bus.

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

## 4.1 Analytics Python services
Analytics python services will reuse the following classes/modules in
cfgm_common package.

1. class VncAmqpHandle defined in vnc_amqp.py to connect to RabbitMq server and
   receive/handle config notifications.
2. class VncObjectDBClient defined in vnc_object_db.py to connect to Cassandra.
3. class DBBase defined in vnc_db.py - Base class for all config objects.

## 4.2 Analytics C++ services
Analytics C++ services will reuse most the existing implementation for
control-node.

Please refer to [Control node config from cassandra - Implementation](https://github.com/Juniper/contrail-controller/blob/master/specs/control_node_config_from_cassandra.md#4-implementation)

# 5. Performance and scaling impact
The new scheme (push) will have better performance and scale over the existing
scheme (pull) for handling the config changes in the Analytics daemons.

# 6. Upgrade
None

# 7. Deprecations
Deprecate [API_SERVER] section in the config section for the following
Analytics services.

1. contrail-collector
2. contrail-alarm-gen
3. contrail-snmp-collector
4. contrail-topology

# 8. Dependencies
None

# 9. Testing
TBD

# 10. Documentation Impact
None

# 11. References
1. [Control node config from Cassandra spec](https://github.com/Juniper/contrail-controller/blob/master/specs/control_node_config_from_cassandra.md)
2. [Python RabbitMq/Cassandra client code](https://github.com/Juniper/contrail-controller/tree/master/src/config/common)
3. [C++ RabbitMq/Cassandra client code](https://github.com/Juniper/contrail-controller/tree/master/src/ifmap/client)
