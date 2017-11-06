
# 1. Introduction
Make it possible to use databases other than Cassandra, message brokers other than RabbitMQ, and distributed coordination services other than ZooKeeper with OpenContrail.

# 2. Problem statement
OpenContrail comes with a set of external infrastructure dependencies, such as a database (Cassandra), a message broker (RabbitMQ) and a distributed coordination service (ZooKeeper). Users thus need to accept indirect costs of running these services on premises (administration, maintenance, updates etc).

Many enterprises standardize on some infrastructure stack to cut these costs down. This is especially true for public clouds where a managed database and a message queue are usually provided. Thus, it makes sense for OpenContrail to re-use these infrastructure choices rather than enforcing its own.

The work is already undergoing to let users choose between two configuration databases: Cassandra and RDBMS (MySQL). The scope of this blueprint is to:

* Amend this work so more databases can be easily added in future

* Extend it to the two remaining dependencies, namely RabbitMQ and ZooKeeper.

Analytics is out of the scope of this blueprint. Analytics comes with its own set of the external dependencies (Kafka and Redis are major ones), but we feel it's feasible to see the shape of analytics by the end of OC 5.0 release cycle before doing any changes to it.


# 3. Proposed solution
The idea is to have generic C++/Python interfaces for all three backends which users can configure at their discretion. Some of these interfaces already exist in OpenContrail in some form while the others will need to be implemented from scratch.

It should be emphasized that this blueprint is about restructuring the code to implement appropriate abstractions, not about adding support for any new database, message broker or distributed coordination service. In particular, this means simpler testing procedures.

## Database

There are two database abstraction interfaces already, namely:

* `cfg_common.VncObjectDbClient` for Python

* `GenDb::GenDbIf` for C++

Still, they should be improved to make the abstraction clearer:

* Stop using Cassandra-only concepts (such as column families, `get_one_col()` method etc) in `schema-transformer` and other config node services.

* Make the database interface strictly defined. Currently, `api-server` can use either `VncRDBMSClient` or `VncServerCassandraClient` which inherits `VncCassandra`. None of these two has a common ancestor class. Moreover, some of the methods required are defined at `VncRDBMSClient`/`VncCassandraLevel` level while the others are defined at `VncServerCassandraClient` level which stays one level higher in the hierarchy. We want a single interface class, say `VncGenDb`, providing all relevant methods, and inherited by all implementations.

* Most of Python code relies on `if` statements to choose the implementation. Factory pattern should be used instead: the factory receives the configuration dictionary (see Sect. 3) and returns an instance for the desired engine or throws an error. Implementations register themselves with the factory.

* `GenDb::GenDbIf` is modelled after Cassandra (consistency levels and column families are good examples). Moreover, no implementation other than Cassandra-based one exists as of now. This stands for the `ConfigDbClient` as well.

We will also need a generic way (a Python/C++ data type, likely a class) to pass database-related settings (which could possible contain implementation-specific keys) across components.

It makes sense to have a separate directory under `database/` , `config-client-mgr/` per implementation, so that if someone prefers to keep theirs implementation off-tree, upstream pulls won't result in elaborate merge conflicts.

## Message broker

Currently, no message broker abstraction exists in Python or C++ part. We need to define the most basic interface for the publish-subscribe system (create queue/notify/consume/ack) with multiple queues (that's what you call "exchanges" in RabbitMQ). No distribution modes other than simple fanout need to be supported.

* For Python, `cfg_common.VncKombuClient` can serve as a starting point.

* In C++, we can have this internal to `config-client-mgr`, as there are no other users for the message bus now. The interface should be kept generic as well, so we can factor it out if necessary.

Implementation considerations are the same as for databases above.

## Distributed Coordination Service

The situation is the same as with message broker, except it is used in the Python parts only (if ever).

The generic interface must provide CRUD method to manage data nodes stored in the service, and also shortcut for master election. Again, `cfg_common.VncZkClient` seems to be a good start.

Implementation considerations are the same as for databases and message brokers above.

## 3.1 Alternatives considered
Keep things as they are. Requires no effort, but doesn't help to solve end-user problems summarized in Sect. 2

## 3.2 API schema changes
None

## 3.3 User workflow impact
A user should be able to choose a supported implementation via configuration files. The approach is to have a dedicated configuration file for these purposes and to "include" it via a `--conf-file` command-line switch the same way we do for authentication. We don't expect users to switch between many different implementations in the runtime. As the bulk of Contrail is statically linked, it should be straightforward to select a few supported implementations compile-time. The suggested configuration file format is as follows:

```
[DATABASE]
engine=cassandra
endpoints=ip1:port1,ip2:port2,...

[MESSAGE_BROKER]
engine=rabbitmq
endpoints=ip1:port1,ip2:port2,...

[DISTRIBUTED_COORDINATION]
engine=zookeeper
endpoints=ip1:port1,ip2:port2,...
```

Engine-specific options could be added if necessary.

## 3.4 UI changes
#### Describe any UI changes
None

## 3.5 Notification impact
Wherever an UVE names a specific technology (such as "RabbitMQ connection"), a generic name should be used instead.


# 4. Implementation
## 4.1 Work items
To be figured at the prototyping stage. The rough outline is given in the Sect. 3

# 5. Performance and scaling impact
## 5.1 API and control plane
No impact. Exact figures of course depends on the specific backend used, but having them pluggable doesn't include a performance tax. Abstraction layers are already in place, and switching them occurs once at startup.

## 5.2 Forwarding performance
No impact. It's a control plane-only feature.

# 6. Upgrade
Upgrading from one backend to another (say, from Cassandra to RDBMS) is left out of the scope for this blueprint.

# 7. Deprecations
None

# 8. Dependencies
This change has no dependencies.

# 9. Testing
## 9.1 Unit tests
Existing tests for the config node services and for the control node services need to be expanded to cover new functionality. This can be done on top of the mock "database" which stores its contents in memory in a dictionary. The same goes for the two remaining services. Having these tests in place is a pre-requisite for the feature to be considered complete, which should happen by 01/30/2018.
## 9.2 Dev tests
None
## 9.3 System tests
Having the code work with a third-party backend (at the implementor's discretion) in the test lab. Should be completed by 02/15/2018.

# 10. Documentation Impact
To be figured out.

# 11. References
None
