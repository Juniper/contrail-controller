# 1. Introduction:
Today, the control-node gets the user configuration using the IFMap protocol from the ifmap-server. We want to change this so that the control-node gets its configuration from the configuration database (e.g. cassandra).

# 2. Problem Statement:
The motivations for making this change are:

1. Today, the system has to deal with 2 variants: ifmap and casssandra. Removing ifmap will make the system simpler since the API-server will not have to populate ifmap-server. This will also lead the system to have one source of configuration data.
2. This change allows us to parallelize the receiving of data since working with cassandra facilitates various options of processing in parallel. In contrast, working with the ifmap protocol places a lot of rigidity.
3. Working with XML (format mandated by ifmap) is more cpu-intensive since its more verbose than Json (format used to store configuration in cassandra).

# 3. Proposed Solution:
We will add new modules to handle a rabbit-mq connection and a cassandra connection. In addtion, we will need to parse json input from cassandra.

We do not expect any change to schema, workflow or UI. The new logs for the new modules are TBD.

# 4. Implementation:
Work Items:
There will be 4 new modules:

a. Config-Client-Manager
This module will over-see the retrieval of user configuration. It will interact with the rabbit-mq client, the database-client and the parser that parses the configuration received from the database-client. It will coordinate the activities between the following 3 pieces.

b. RabbitMq-Client
This module will interact with the RabbitMq server. It will get information about the operation (Add/Delete/Change) and the configuration item on which the operation will apply.

c. Config-Cassandra-Client
This module will interact with the casssandra servers. It will perform connection-management and read requests actions for the cassandra database holding the user configuration.

d. Config-Cassandra-Parser
This module will parse the configuration received from the cassandra server.

We will add tests for all the new functionality, change existing tests to work with the new modules, add introspect functionality to peak into the new modules and also add/change introspect functionality that interacts with the new modules.

Changes in existing modules:
We will add functionality in the code that auto-generates the C++ code (tools/generateds) to parse Json data received via cassandra and create the same internal data-structures the the rest of the code expects. We will also add Json tests for schema. We will also remove all the xml and IFMap related code in tools/generateds. We will also change all the tests written for the schema. We will also remove all the existing functionality/tests/introspect code that deals with the ifmap-server.

# 5. Performance and scaling impact:
TBD

# 6. Upgrade:
The changes are only in the control-node. There should be no operational impact when we replace all control-nodes. In the case, where only a subset of control-nodes are upgraded, the data stored in the older-version and the newer-version control-nodes will be the same.

# 7. Deprecations
None

# 8. Dependencies
None

# 9. Testing:
TBD

# 10. Documentation Impact:
TBD

# 11. References
None
