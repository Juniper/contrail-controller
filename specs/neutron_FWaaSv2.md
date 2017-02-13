# 1. Introduction
Add the support to the OpenStack Neutron Firewall API extension named
**Neutron FWaaS** to Contrail. That specification proposes to implement version
2 of that API as version 1 was [deprecated since *Liberty* OpenStack
release](http://docs.openstack.org/releasenotes/neutron/liberty.html#id8).

This new API introduces a few enhancements to Firewall as a Service (FWaaS) API
including making it more granular by giving the users the ability to apply the
firewall rules at the port level rather than at the router level. Support is
extended to various types of Neutron ports, including VM ports and SFC ports as
well as router ports. It also aims to provide better grouping mechanisms
(firewall groups, address groups and service groups) and discuss the use of a
common classifier in achieving it.

# 2. Problem statement
The security group API, which was built for public cloud infrastructure becomes
insufficient for the security and network environments inside the enterprise.
Firewall as a Service API is the API where use cases more advanced than the
basic “let any traffic from X IP into Y port into my group of VMs” should be
supported.

# 3. Proposed solution
It is proposed to harmonize the FWaaS and Security Group models by converging
the implementation of FWaaS and Security Groups but keeping a separate API for
each of them while relying on a common backend. This spec proposes an enhanced
FWaaS API that incorporates Security Groups functionality such that the FWaaS
API becomes a superset of what is exposed by the Security Group API.

* Granularity to Neutron ports.
* Applies to various types of Neutron ports (for the moment: router, VM, SFC).
* Allows for different firewall policies with different firewall rules to be
  applied to different directions (ingress vs egress).
* Introduces the Firewall Group for binding firewall policies and Neutron
  ports.

Enhancements from security groups API:
* Add action `deny` and `reject` attribute to rules.
* Filtering source and destination address prefix and port rather than just the
  remote.
* Adds a `description` attribute to firewall rules.
* Adds an `admin status` attribute to firewall rules.
* Adds a `public` attribute allowing sharing of firewall rules between
  different projects.
* Firewall groups reference firewall rules through a firewall policy. In
  particular, this allows reuse of sharable firewall policies that are
  referenced by multiple firewall groups.

Some proposed functionalities not yet implemented on the reference driver:
* Service group and address group to allow operational separation of
  responsibilities. Expert defines that group (IP prefix or flows/services
  spec) then user use them to define rules.
* Firewall group could be used as source or destination for a rule

## 3.1 Alternatives considered
N/A

## 3.2 API schema changes
### Neutron
Three new resources added in Neutron:

* Firewall rule:

Attribute Name | CRU | default
-------------- | --- | -------
id | R | Generated UUID
tenant_id | CU | Project UUID
name | CRU | ''
description | CRU | ''
firewall_policy_id | RD | null | Never exposed on the API?
public | CRU | False
protocol | CRUD | null
ip_version | CRU | 4
source_ip_address | CRUD | null
destination_ip_address | CRUD | null
source_port | CRUD | null
destination_port | CRUD | null
position | R | null
action | CRU | deny
enabled | CRU | True

* Firewall policy:

Attribute Name | CRU | default
-------------- | --- | -------
id | R | Generated UUID
tenant_id | CU | Project UUID
name | CRU | ''
description | CRU | ''
public | CRU | False
firewall_rules | CRUD | empty
audited | CRU | False

* Firewall group:

Attribute Name | CRU | default
-------------- | --- | -------
id | R | Generated UUID
tenant_id | CU | Project UUID
name | CRU | ''
description | CRU | ''
admin_state_up | CRU | True
status | R | ''
public | CRU | False
ports | CRU | empty
ingress_firewall_policy_id | CRU | null
egress_firewall_policy_id | CRU | null

Similar to Security Groups, for each project, one Firewall Group named
`default` will be created automatically. This default Firewall Group will be
associate with all new VM ports within that project, unless it is explicitly
disassociated from the new VM port. This provides a way for a tenant network
admin to define a tenant wide firewall policy that applies to all VM ports,
except when explicitly provisioned otherwise.

### Contrail VNC model mapping
Two solutions can be considered:

#### Re-use VNC API resources

VNC data model already have `access-control-list` which can be mapped to the
Neutron `firewall policy` and since
[that patch](https://review.opencontrail.org/#/c/27149/), the direction to
apply rules can be set on the `access-control-list` resource. Then the
`access-control-list` resource attribute `acl-rule` can be mapped to the
Neutron `firewall rule` resource. But it's not a resource with it's own
ID and default attribut and cannot be re-used on different
`access-control-list` resource. Finally, a new resource need to be created to
map with the Neutron `firewall group` resource.

#### Recreating the model on the VNC API by adding 3 new resources

* Firewall rule:

Resource attributes  | Operations | Type
-------------------- | ---------- | ----
Project Owner | CR | Parent reference
Public | CRU | Boolean set to `False` by default
Rule | CRU | Property `AclRuleType`
Firewall policies | CRU | `firewall-policy` back references

* Firewall policy:

Resource attributes  | Operations | Type
-------------------- | ---------- | ----
Project Owner | CR | Parent reference
Public | CRU | Boolean set to `False` by default
Audited | CRU | Boolean set to `False` by default
Firewall groups | CRU | `firewall-group` back references
Firewall rules | CRU | `firewall-rule` references

* Firewall group:

Resource attributes  | Operations | Type
-------------------- | ---------- | ----
Project Owner | CR | Parent reference
Public | CRU | Boolean set to `False` by default
Audited | CRU | Boolean set to `False` by default
Ingress firewall policies | CRU | `firewall-policy` references
Egress firewall policies | CRU | `firewall-policy` references
Virtual machine interfaces | CRU | `virtual-machine-interface` back references

Then the schema transformer will translate that resources into
`access-control-list` applied on `virtual-machine-interface`.

## 3.3 User workflow impact
N/A

## 3.4 UI changes
N/A

## 3.5 Notification impact
Add new UVEs entries.


# 4. Implementation
## 4.1 Work items
TBD (Depends on the chosen solution)

### Contrail data model

### VNC API server

### Schema transformer

# 5. Performance and scaling impact
## 5.1 API and control plane
More resources

## 5.2 Forwarding performance
More ACL on interfaces

# 6. Upgrade
N/A

# 7. Deprecations
N/A

# 8. Dependencies
N/A

# 9. Testing
## 9.1 Unit tests
Add unit tests to the added code on the config side.

## 9.2 Dev tests
## 9.3 System tests

# 10. Documentation Impact

# 11. References
* Blueprint: [https://blueprints.launchpad.net/opencontrail/+spec/neutron-fwaasv2](https://blueprints.launchpad.net/opencontrail/+spec/neutron-fwaasv2https://blueprints.launchpad.net/opencontrail/+spec/neutron-fwaasv2)
* Neutron FWaaS v2 extension spec: [https://specs.openstack.org/openstack/neutron-specs/specs/newton/fwaas-api-2.0.html](https://specs.openstack.org/openstack/neutron-specs/specs/newton/fwaas-api-2.0.html)
* Neutron FWaaS documentation: [http://docs.openstack.org/developer/neutron-fwaas](http://docs.openstack.org/developer/neutron-fwaas)
* Neutron API: [https://developer.openstack.org/api-ref/networking/v2/#list-firewall-groups](https://developer.openstack.org/api-ref/networking/v2/#list-firewall-groups)