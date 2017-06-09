# 1. Introduction
Analytics API RBAC provides ability to access UVE and query information based
on the permissions of the user for the UVE or queried object.

# 2. Problem statement
Currently Analytics API supports authenticated access for cloud-admin role.
However to display network monitoring for tenant pages in the UI, analytics
API needs to support RBAC similar to config API so that the tenants can only
view information about the networks for which they have the read permissions.

# 3. Proposed solution
Analytics API will map query and UVE objects to configuration objects on
which RBAC rules are applied so that read permissions can be verified using
VNC API. Following steps will be needed:

1. For statistics queries, annotations will be added to sandesh file so that
   indices/tags on statistics queries can be associated with objects/UVEs.
   These can then be added to the schema and used by contrail-analytics-api
   to determine the object level read permissions.
2. For flow and log queries, we will evaluate the object read permissions for
   each AND term in the where query.
3. For UVEs list queries (e.g. analytics/uve/virtual-networks/),
   contrail-analytics-api will get list of UVEs which have read permissions
   for given token. Similarly, for UVE query for specific
   resource (e.g. analytics/uves/virtual-network/vn1), contrail-analytics-api
   will check the object level read permissions using VNC API.

## 3.1 Alternatives considered
None

## 3.2 API schema changes
None

## 3.3 User workflow impact
Tenants will not be able to view system-logs and and flow-logs.

## 3.4 UI changes
1. UI needs to use VNC API to get list of networks to display in the
   network monitoring page based on the tenant user.
2. System logs and flows tabs need to be greyed out for tenant user.

## 3.5 Notification impact
None

# 4. Implementation
* Currently VNC API provides obj_perms function which takes object
  UUID and token, and returns the permissions.
* A non-admin user will be able to see only non-global UVEs. There is an
  existing list of UVEs with a global flag indicating if it is a global UVE.
  Here is the list of UVEs which will be visible to a tenant user to start
  with:

        * virtual_network
        * virtual_machine
        * virtual_machine_interface
        * service_instance

  More will be added to this list as and when backend support is added.

* UVEs list queries (e.g. analytics/uve/virtual-networks)

  There is an already existing mapping(_OBJECT_TABLES) from uve-type
  (virtual-network in the example) to corresponding uve table (ObjectVNTable in
  the example). An extra field indicating corredponding config object type
  (virtual_network for this example) will be added to that. For a uve list
  query, analytics-api will pass config object type and user token to ApiServer
  to get a list of objects that the user has permissions for.
* System Logs and Flow Logs

  These are displayed for cloud-admin roles only
* Object Logs

  A reverse mapping of object log type to config object type will be created
  using the mapping in previous section. For an object-log query, using ths
  mapping, get corresponding config object type. ApiServer will use this
  object type and user token, to return list of objects with read permissions.
  'ObjectId' will be the only where clause supported for non-admin users. This
  clause can be used to filter the list of config objects returned by
  ApiServer.
* Statistics

  Supporting stats tables for tenants will require changes in sandesh files.
  A new annotation (uve\_type) will be added for every member that is used as
  index in stats tables. This uve\_type will be added to the stats table schema.
  During query, analytics-api will retrieve uve_type for each tag. It will be
  used to get corresponding config object type using the mapping table defined
  in previous section. For every tag, list of permitted objects will be
  retrieved using config-type and user token.

  Here is an example of the changes in sandesh file. A stat table in
  virtual_network.sandesh is specified like this:

          optional list<InterVnStats>        vn_stats (tags=".other_vn,.vrouter")

  It will be changed to:

          optional list<InterVnStats>        vn_stats (tags=".other_vn,.vrouter",uve_type=".other_vn:virtual_network"))

  Corresponding stats table schema will also change from:

          {
                datatype: "string",
                index: true,
                name: "vn_stats.other_vn",
                suffixes: null
          },
          {
                datatype: "string",
                index: true,
                name: "vn_stats.vrouter",
                suffixes: null
          }

  to:

          {
                uve_type: "virtual-network",
                datatype: "string",
                index: true,
                name: "vn_stats.other_vn",
                suffixes: null
          },
          {
                uve_type: "vrouter",
                datatype: "string",
                index: true,
                name: "vn_stats.vrouter",
                suffixes: null
          }

* Alarms

  Alarms are binned based on alarm-key (same as object type e.g. Virtual-network
  ) and uve-key. As mentioned in previous section, there is already a list of
  non-global UVEs. This list is used as a filter (tablefilt) for non-admin
  users. For every non-global UVE type, get list of permitted uves from
  ApiServer. Now, for every alarm, dsplay if uve-key is in permitted uves.
  Discard all other alarms

* Alarm/uve-stream

  As mentioned in previous section, there is a list of object-log types which
  are supported for non-admin users. This list is passed as ‘tablefilt’ to
  UveStreamer. In addition, ContrailConfig structure (which has details of owner
  and all the domains/tenants with which that UVE is shared) is checked in every
  UVE to check whether the user has 'read' permissions.

# 5. Performance and scaling impact
## 5.1 API and control plane
API server will have to make additional calls per invocation of obj_perms
API from VNC API for each analytics API call to verify the object level read
permissions.

## 5.2 Forwarding performance
Not affected

# 6. Upgrade
We will not change the ```DEFAULTS.aaa_mode``` during upgrade if it exists in
```/etc/contrail/contrail-analytics-api.conf```. Same provisioning flag will be
used for ApiServer as well as analytics-api.

# 7. Deprecations
None

# 8. Dependencies
None

# 9. Testing
## 9.1 Unit tests
Opserver systemless tests will be added.
## 9.2 Dev tests
## 9.3 System tests

# 10. Documentation Impact
```/etc/contrail/contrail-analytics-api.conf``` - section ```DEFAULTS```,
parameter ```aaa_mode``` will now support ```rbac``` as one of the values.

# 11. References
RBAC - https://github.com/Juniper/contrail-controller/wiki/RBAC

