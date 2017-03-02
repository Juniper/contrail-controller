#1. Introduction
Analytics API RBAC provides ability to access UVE and query information based
on the permissions of the user for the UVE or queried object.

#2. Problem statement
Currently Analytics API supports authenticated access for cloud-admin role.
However to display network monitoring for tenant pages in the UI, analytics
API needs to support RBAC similar to config API so that the tenants can only
view information about the networks for which they have the read permissions.

#3. Proposed solution
Analytics API will map query and UVE objects to configuration objects on
which RBAC rules are applied so that read permissions can be verified using
VNC API.

Analytics API RBAC will be implemented in 2 phases:

## Phase 1:
1. All statistics, flow, and log queries are allowed only for cloud-admin
   role and hence for tenant, the query pages need to be greyed out in the
   UI.
2. All UVEs list queries (e.g. analytics/uves/virtual-networks/) are allowed
   only for cloud-admin role. For displaying appropriate networks on the
   monitoring page, UI will have to get the list of networks with read
   permissions for the user using the VNC API. VM and VMI information are
   obtained from the VN UVEs and hence no other change should be needed
   on the UI.
3. UVE query for specific resource (e.g. analytics/uves/virtual-network/vn1),
   contrail-analytics-api will check the object level read permissions using
   VNC API.

## Phase 2:
1. For flow and log queries, we will evaluate the object read permissions for
   each AND term in the where query.
2. For statistics queries, we will add annotations to sandesh file so that
   indexes/tag on statistics queries can be associated with objects/UVEs.
   These can then be added to the schema and used by contrail-analytics-api
   to determine the object level read permissions.
3. Some tables like the FieldNamesTable will still be accessible to only
   cloud-admin role and hence drop-downs on some of the UI pages will not
   work for tenants.
4. For UVEs list queries (e.g. analytics/uve/virtual-networks/),
   contrail-analytics-api will use VNC API to determine whether the user
   has read permission for each UVE object.

##3.1 Alternatives considered
None

##3.2 API schema changes
None

##3.3 User workflow impact
Tenants will not be able to view the query pages in Phase 1.

##3.4 UI changes
### Phase 1:
1. UI needs to use VNC API to get list of networks to display in the
   network monitoring page based on the tenant user.
2. Query pages need to be greyed out for tenant user.

##3.5 Notification impact
None

#4. Implementation
## Phase 1:
* Currently VNC API provides obj_perms function which takes object
   UUID and token and returns the permissions. obj_perms will be
   modified to also accept fq-object-name parameter and pass that to
   API server. API server will use the fq-name-to-uuid mapping table
   returns permissions configured.
* For every UVE, fq-name or UUID of corresponding config object will be
  derived from its contents. All the UVEs will be divided in few categories
  for this purpose:
    * UVEs having ContrailConfig structure :

      This cateogory contains most of the UVEs that this this project aims to protect :
        * virtual_network
        * service_instance
        * virtual_router
        * analytics_node
        * database_node
        * config_node
        * service_chain
        * physical_router
        * bgp_router

      For these UVEs, UUID of corresponding config object object is
      available in ContrailConfig structure which will be used to query API server
      to get corresponding permissions.

    * Infrastructure UVEs :

      This category contains the UVEs which are corresponding to internal
      objects:
        * storage-pools
        * servers
        * storage-disks
        * generators
        * storage-clusters
        * storage-osds
        * loadbalancers
        * user-defined-log-statistics

      For these UVEs, analytics-api will allow cloud-admin roles and won't
      check object level permissions

    * Other UVEs with fq-name:

      There are some other UVEs which use fq-name of corresponding config
object as UVE name. This fq-name will be used to get user permissions. Here are
some example UVEs which fall in this category:
        * bgp-peer
        * virtual_machine
        * virtual-machine-interface

    * Other UVEs without fq-name:

      All the remaining UVEs don't have fq-name. There is no way to deduce
      it form UVE contents. In these case, either the owners need to change
      the name to include fq-name in UVE or treat them as infrastructure UVEs.
      Here are some examples:
        * xmpp-peers
        * prouters

#5. Performance and scaling impact
##5.1 API and control plane
API server will have to make additional calls per invocation of obj_perms
API from VNC API for each analytics API call to verify the object level read
permissions, and also for mapping the name to UUID for each object.

##5.2 Forwarding performance
Not affected

#6. Upgrade
We will not change the ```DEFAULTS.aaa_mode``` during upgrade if it exists in
```/etc/contrail/contrail-analytics-api.conf```. However new installations
will default to ```rbac``` instead of the current ```cloud-admin```.

#7. Deprecations
None

#8. Dependencies
None

#9. Testing
##9.1 Unit tests
Opserver systemless tests will be added.
##9.2 Dev tests
##9.3 System tests

#10. Documentation Impact
```/etc/contrail/contrail-analytics-api.conf``` - section ```DEFAULTS```,
parameter ```aaa_mode``` will now support ```rbac``` as one of the values.

#11. References
RBAC - https://github.com/Juniper/contrail-controller/wiki/RBAC
