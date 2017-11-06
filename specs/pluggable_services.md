
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

* Most of Python code relies on `if` statements to choose the implementation. Factory pattern should be used instead: the factory receives the configuration dictionary (see Sect. 3.3) and returns an instance for the desired engine or throws an error. Implementations register themselves with the factory.

* `GenDb::GenDbIf` is modelled after Cassandra (consistency levels and column families are good examples). Moreover, no implementation other than Cassandra-based one exists as of now. This stands for the `ConfigDbClient` as well.

We will also need a generic way (a Python/C++ data type, likely a class) to pass database-related settings (which could possible contain implementation-specific keys) across components.

It makes sense to have a separate directory under `database/` , `config-client-mgr/` per implementation, so that if someone prefers to keep theirs implementation off-tree, upstream pulls won't result in elaborate merge conflicts.

### Proposed configuration database interface

Below is proposed interface for the configuration database, expressed in Python. C++ version retains the semantics but provides just two methods: `object_read` and `object_list`, along with a few helper ones such as `fq_name_to_uuid()` and `uuid_to_fq_name()`.

```(python)
class VncGenConfigDb(object):

    # Object CRUD

    def object_create(self, obj_type, obj_uuid, obj_dict):
        """
        Create an object.

        :param obj_type: Object type, str
        :param obj_uuid: Object UUID, uuid.UUID
        :param obj_dict: A dictionary representation of an object.
        :return: (ok, result) where ok is True, False and result
                 is either str or (http_code, message).
        """

    def object_raw_read(self, obj_uuids, prop_names):
        """
        Reads selected properties of multiple objects.
        Objects returned are "incomplete" as they have "uuid" and
        (at least) requested properties, but don't have fq_name,
        references etc.

        :param obj_uuids: List of object UUIDs to read, [uuid.UUID]
        :param prop_names: List of properties names to read
        :return: List of dictionaries
        """

    def object_read(self, obj_type, obj_uuids, field_names=None):
        """
        Read a complete representation of an object. This includes
        type, uuid, fq_name, parent_type and all the properties and
        references.

        :param obj_type: Object type, str
        :param obj_uuids: List of object UUIDs, [uuid.UUID]
        :param field_names: If present, list of fields (properties,
               refs etc) to return
        :return: (ok, result) where ok is True, False and result
                 is either list of dictionaries or (http_code, message).
        """

    def object_count_children(self, obj_type, obj_uuid, child_type):
        """
        Count this object's children.

        :param obj_type: Object type, str
        :param obj_uuid: Object UUID, uuid.UUID
        :param child_type: The child's object type FQ name, [str]
        :return: (ok, count)
        """

    def object_update(self, obj_type, obj_uuid, new_obj_dict):
        """
        Update an object.

        :param obj_type: Object type, str
        :param obj_uuid: Object UUID, uuid.UUID
        :param new_obj_dict: A dictionary representation of an object.
        :return: (ok, result) where ok is True, False and result
                 is either str or (http_code, message).
        """

    def object_list(self, obj_type, parent_uuids=None, back_ref_uuids=None,
                    obj_uuids=None, count=False, filters=None):
        """
        List objects conforming to given criteria.

        If parent_uuids is not None, return these parents children only.
        If back_refs_uuids is not None, return only objects which these
        objects are referring to (such as VirtualMachineInterfaces in
        the given VirtualNetwork).
        If obj_uuids is not None, further restrict output to objects in
        the list.

        If no filters specified, grabs all resources of the given type.

        :param obj_type: Object type, str
        :param parent_uuids: Only return objects whose parents UUIDs are
                             in the list, [uuid.UUID].
        :param back_ref_uuids: Only return objects referenced by objects
                               in this list, [uuid.UUID].
        :param obj_uuids: List of object UUIDs to return, [uuid.UUID].
        :param count: If True, do not return actual results, only their count
        :param filters: If present, list of properties names to return
        :return: (ok, result) where ok is True, False and result is either a
                 number if count is True, or a list of (fq_name, obj_uuid)
                 tuples.
        """

    def object_delete(self, obj_type, obj_uuid):
        """
        Delete an object. This unlinks the object from its parent,
        removes references and relaxed back references.

        :param obj_type: Object type, str
        :param obj_uuid: Object UUID, uuid.UUID
        :return: (ok, result) where ok is True, False and result
                 is either str or (http_code, message).
        """

    # Properties collections

    def prop_collection_read(self, obj_type, obj_uuid, obj_fields, position):
        """
        Read a property which is a list or a map. Depending on position value
        yields a whole property or a single entry.

        :param obj_type: Object type, str
        :param obj_uuid: Object UUID, uuid.UUID
        :param obj_fields: List of properties names to get, [str]
        :param position: If present, a key to read from the property, str.
                         If not, the whole property is returned.
        :return: A dictionary (field => collection), where collection is a list
                 of (value, key) where key is either an index or a string key
                 within the property.
        """

    # FQName to UUID stuff

    def fq_name_to_uuid(self, obj_type, fq_name):
        """
        Resolves an object's fully qualified name to UUID.

        :param obj_type: Object type, str
        :param fq_name: Fully qualified name in the list form
        :return: The object's UUID, str
        """

    def uuid_to_fq_name(self, uuid):
        """
        Find a name for the object's UUID.

        :param uuid: Object UUID, uuid.UUID
        :return: FQName, str
        """

    def uuid_to_obj_type(self, uuid):
        """
        Find a type for the object's UUID

        :param uuid: Object UUID, uuid.UUID
        :return: Object type, str
        """

    # Object sharing

    def get_shared(self, obj_type, share_id='', share_type='global'):
        """
        Find all objects shared with a (share_type, share_id).

        TBD: What is this sharing?

        :param obj_type: Object type, str
        :param share_id: Share identifier, str
        :param share_type: Share type: global, domain etc, str
        :return: List of (obj_uuid, permissions) tuples, where permissions are octal
        """

    def set_shared(self, obj_type, obj_id, share_id='', share_type='global', rwx=7):
        """
        Share an object 'obj_id' with <share_type:share_id>. rwx indicate type of
        access (sharing) allowed.

        :param obj_type: Object type, str
        :param obj_id: Object UUID, uuid.UUID
        :param share_id: Share identifier, str
        :param share_type: Share type: global, domain etc, str
        :param rwx: Octal Unix permissions, int
        :return: None
        """

    def del_shared(self, obj_type, obj_id, share_id='', share_type='global'):
        """
        Deletes (unshares) an object.

        :param obj_type: Object type, str
        :param obj_id: Object UUID, uuid.UUID
        :param share_id: Share identifier, str
        :param share_type: Share type: global, domain etc, str
        :return: None
        """

    # References

    # It makes sense to have _{create,delete}_ref() public and in this interface
    # (one should also drop bch argument, of course).
    # ref_update() shouldn't really be part of the interface but rather a
    # convenience method defined somewhere in VncDb.

    def ref_relax_for_delete(self, obj_uuid, ref_uuid):
        """
        Relax a back reference. One can't normally delete objects which are
        referenced. If this is really needed, code relaxes the reference
        first. Relaxed referenced are cleaned upon deletion as normal ones,
        yet they don't prevent deletion per se.

        This is not the same as weak refs which (supposedly) are allowed
        to dangle. They are unused currently and used sparingly.

        :param obj_uuid: UUID for the object which holds a reference,
                         uuid.UUID.
        :param ref_uuid: Referenced object's UUID, uuid.UUID
        :return: None
        """

    def get_relaxed_refs(self, obj_uuid):
        """
        Return the object's relaxed back references.

        :param obj_uuid: Object UUID, uuid.UUID
        :return: None
        """

    def ref_update(self, obj_type, obj_uuid, ref_obj_type, ref_uuid,
                   ref_data, operation):
        """
        Update (add or delete) a reference.

        :param obj_type: Object type, str
        :param obj_uuid: Object UUID, uuid.UUID
        :param ref_obj_type: Referenced object type, str
        :param ref_uuid: Referenced object UUID, uuid.UUID
        :param ref_data: Arbitrary data to associate with the
                         reference, str
        :param operation: 'ADD' or 'DELETE', str
        :return: None
        """

    # This isn't really called from anywhere but it makes sense to include it for
    # the reasons outlined above.
    def _create_ref(self, bch, obj_type, obj_uuid, ref_obj_type, ref_uuid, ref_data):
        """
        Create a reference.

        :param bch: Cassandra batch operation, a leaky abstraction artifact.
        :param obj_type: Object type, str
        :param obj_uuid: Object UUID, uuid.UUID
        :param ref_obj_type: Referenced object type, str
        :param ref_uuid: Referenced object UUID, uuid.UUID
        :return: None
        """

    # TODO: Maybe also add _update_ref here? It's also unused, but it makes
    # the interface feel more complete.

    # Called from the database migration code, not implemented in VncRDBMS
    def _delete_ref(self, bch, obj_type, obj_uuid, ref_obj_type, ref_uuid):
        """
        Delete a reference.

        :param bch: Cassandra batch operation, a leaky abstraction artifact.
        :param obj_type: Object type, str
        :param obj_uuid: Object UUID, uuid.UUID
        :param ref_obj_type: Referenced object type, str
        :param ref_uuid: Referenced object UUID, uuid.UUID
        :return: None
        """

    # Misc

    def is_latest(self, id, tstamp):
        """
        Check if the given object was last updated at the given timestamp.
        A simple implementation can always return False, although it would
        results in a performance hit.

        :param id: Object UUID, uuid.UUID
        :param tstamp: Timestamp, ???
        :return: True if the object's last-modified date is the same
                 as tstamp, False otherwise.
        """

    def update_perms2(self, obj_uuid):
        """
        Insert new perms. Called on api-server startup when walking the DB.

        :param obj_uuid: Object UUID, uuid.UUID
        :return: None
        """

    # Useragent table - server-side storage for user data
    # Seems to be used from OpenStack plugins

    def useragent_kv_store(self, key, value):
        """
        Store a value in the user agent store.

        :param key: Unique key, str
        :param value: Value to store, str
        :return: None
        """

    def useragent_kv_retrieve(self, key):
        """
        Retrieve a previously stored value from the user agent store.

        :param key: Unique key, str
        :return: Previously stored value, str
        :raises: NoUserAgentKey if key is not found.
        """

    def useragent_kv_delete(self, key):
        """
        Delete a previously stored value from the user agent store.

        :param key: Unique key, str
        :return: None
        :raises: NoUserAgentKey if key is not found.
        """
```

## Message broker

Currently, no message broker abstraction exists in Python or C++ part. We need to define the most basic interface for the publish-subscribe system (create queue/notify/consume/ack) with multiple queues (that's what you call "exchanges" in RabbitMQ). No distribution modes other than simple fanout need to be supported.

* For Python, `cfg_common.VncKombuClient` can serve as a starting point.

* In C++, we can have this internal to `config-client-mgr`, as there are no other users for the message bus now. The interface should be kept generic as well, so we can factor it out if necessary.

Implementation considerations are the same as for databases above.

### Proposed Message broker interface

Below is the proposed message broker interface in Python. C++ version retains the semantics. It is closely modeled after the existing `cfg_common.VncKombuClient`.

```(python)
class MessageBroker(object):

    def connect(self, **conn_params):
        """
        Connect to the message broker.

        :param conn_params: Connection-specific parameter values.
        :return: None
        """

    def shutdown(self):
        """
        Shutdown the connection.
        :return: None
        """

    def create_exchange(self, exchange_name):
        """
        Create exchange point in the broker.

        Most brokers have notion of an 'exhange' as a named point where messages can go.
        This method creates an exchange or does nothing if it's already present.

        :param exchange_name: Exchange name, implementation-specific.
        :return: None
        """

    def publish(self, message):
        """
        Publish a message.

        :param message: A dict-like object storing the message.
        :return: A message id (broker-specific and opaque).
        """

    def consume(self):
        """
        Consume a message.

        Return a message (dict-like object) if there are any messages ready.
        Return None if not.

        :return: None
        """

    def ack(self, message):
        """
        Acknowledge a message.

        Send message acknowledgment to the broker.

        :param message: A message (dict-like object) to acknowledge.
        :return: None
        """
```

## Distributed Coordination Service

The situation is the same as with message broker, except it is used in the Python parts only (if ever).

The generic interface must provide CRUD method to manage data nodes stored in the service, and also shortcut for master election. Again, `cfg_common.ZookeeperClient` seems to be a good start.

Implementation considerations are the same as for databases and message brokers above.

### Proposed Distributed Coordination Service interface

Below is the proposed Distributed Coordination Service interface in Python. C++ version retains the semantics. It is closely modeled after the existing `cfg_common.ZookeeperClient`.

```(python)
class DistributedCoordinationService(object):

    def connect(self, **params):
        """
        Connect to the service.

        :param conn_params: Connection-specific parameter values.
        """

    def create_node(self, path, value=None):
        """
        Create node.
        :param path: Path to the node to create.
        :param value: Node contents.
        """

    def delete_node(self, path, recursive=False):
        """
        Delete a node.
        :param path: Path to the node to delete.
        :param recursive: If set to True, delete child nodes as well.
        """

    def read_node(self, path, include_timestamp=False):
        """
        Read a node's value.

        :param path: Path to the node to read
        :param include_timestamp: Include timestamp in the return value.
        :return: (value, ts) if include_timestamp is True, value otherwise.
        """

    def get_children(self, path):
        """
        Get the node's children.

        :param path: Node path.
        :return: List of the children nodes.
        """

    def exists(self, path):
        """
        Check if given node exists.

        :param path: Node path.
        :return: True if the node exists, False if not.
        """
```

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
