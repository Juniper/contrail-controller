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
* Adds a `share` attribute allowing sharing of firewall rules between
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
tenant_id | CR | Project UUID
name | CRU | ''
description | CRU | ''
enabled | CRU | True
share | CRU | False
firewall_policy_id | RD | null | Never exposed on the API?
ip_version | CRU | 4
source_ip_address | CRUD | null
destination_ip_address | CRUD | null
protocol | CRUD | null
source_port | CRUD | null
destination_port | CRUD | null
position | R | null
action | CRU | deny [allow &#124; deny &#124; reject]

* Firewall policy:

Attribute Name | CRU | default
-------------- | --- | -------
id | R | Generated UUID
tenant_id | CR | Project UUID
name | CRU | ''
description | CRU | ''
share | CRU | False
firewall_rules | CRUD | empty
audited | CRU | False

* Firewall group:

Attribute Name | CRU | default
-------------- | --- | -------
id | R | Generated UUID
tenant_id | CR | Project UUID
name | CRU | ''
description | CRU | ''
admin_state_up | CRU | True
status | R | ''
share | CRU | False
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
The idea here is to map that FWaaS v2 model to the new firewall policy model
recently
[introduced in Contrail](../blob/master/specs/fw_security_enhancements.md). As
describe above, the FWaaS v2 model introduced **three** new resources and
**two** can be map directly to the Contrail firewall security mode:

* Firewall rule:

That Neutron resource will be map to the Contrail `firewall-rule` resource like
this:

Attribute Name | Contrail `firewall-rule` attribute
--------------| ------------------------------
id | `id_perms.uuid`
tenant_id | `project` parent reference
name | `display_name`
description | `id_perms.description`
enabled | `id_perms.enable`
share | Contrail RBAC mechanism
firewall_policy_id | `firewall-policy` back-reference
ip_version | IP version determined by `source_ip_address` and `destination_ip_address`
source_ip_address | `endpoint-1.subnet`
destination_ip_address | `endpoint-2.subnet`
protocol | `service.protocol[-id]`
source_port | `service.src-ports`
destination_port | `service.dst-ports`
position | Sequence of reference between `firewall-policy` and `firewall-rule`
action | `action-list.simple-action`*

\* *Contrail is missing the action `reject`. That needs to be implemented*

* Firewall policy:

That Neutron resource will be map to the Contrail `firewall-policy` resource
like this:

Attribute Name | Contrail `firewall-policy` attribute
-------------- | ------------------------------------
id | `id_perms.uuid`
tenant_id | `project` parent reference
name | `display_name`
description | `id_perms.description`
share | Contrail RBAC mechanism
firewall_rules | `firewall-rule` references with a sequence number for ordering
audited | `audited`*

\* Need to be added to the Contrail model

But two differences persist between the two models:
- How firewall polices are applied on resources:
  - **Contrail** uses the `tag` resources regular expressions in the source and
    destination fields of policy rules. These tag regular expressions will give
    cross sections of tag dimensions. Tag resource can be applied to all
    contrail resources (`virtual-machine-interface`, `virtual-network`, ...)
  - **Neutron** FWaaS v2 only permits to define one ingress and one egress
    firewall policies per firewall group.
- On which direction firewall policy are applied:
  - **Contrail** defines if firewall rules are applied on endpoint 1 or
    2 or both with the  firewall rule `direction` attribut
  - **Neutron** definies if a firewall policy is applied on ingress or egress
    port traffic when a policy is applied to a firewall group.

To leverage that two differences, we proposed to not rely on the Contrail `tag`
resources. We introduce a new Contrail resource named `firewall-group` which
maps to the Neutron FWaaS v2 resource `firewall-group`. That resource have two
reference types, one to `firewall-policy` with a property to define the
direction where policies are applied and a second one to
`virtual-machine-interface` to specify which ports the firewall be applied.

* Firewall group:

New resource introduced for that blueprint and mapped like this:

Attribute Name | Contrail `firewall-group` attribute
-------------- | -----------------------------------
id | `id_perms.uuid`
tenant_id | `project` parent reference
name | `display_name`
description | `id_perms.description`
admin_state_up | `id_perms.enable`
status | Set by the FWaaS v2 Contrail driver
share | Contrail RBAC mechanism
ports | `virtual-machine-interface` references
ingress_firewall_policy_id | `firewall-policy` reference with direction property set to `TrafficDirectionType.ingress`
egress_firewall_policy_id | `firewall-policy` reference with direction property set to `TrafficDirectionType.egress`
TrafficDirectionType
Some use cases are not address yet. The Neutron FWaaS v2 model permits to set
`firewall-group` on Neutron router ports but in Contrail that ports does not
realy exist. `virtual-machine-interface` are created for that but are purely
virtual. We still need to find a way to filter north/south traffic (floating IP
and SNAT) and inter-`virtual-network` traffic.

## 3.3 User workflow impact
User could use the OpenStack Neutron FWaaS v2 API extension with Contrail as SDN
controller.

## 3.4 UI changes
No change for the Contrail WebUI. OpenStack dashboard proposes new views to
administrate and use the OpenStack Neutron FWaaS v2 API extension.

## 3.5 Notification impact
Add new UVEs entries.
When a `firewall_policy` is audited, we need to log it with user details and
timestamp.

# 4. Implementation
## 4.1 Work items
### Contrail data model
Add the `firewall-group` resources to the Contrail data model.

### VNC API server
Add sanity check on firewall resources modifications.

### Vrouter agent
Enforce filtering on interfaces.
Add support to send ICMP reject message when the firewall rule action is
`reject`

# 5. Performance and scaling impact
## 5.1 API and control plane
More resources to validate, store and translate.

## 5.2 Forwarding performance
N/A

# 6. Upgrade
N/A

# 7. Deprecations
N/A

# 8. Dependencies
N/A

# 9. Testing
## 9.1 Unit tests
* Add unit tests to the added code on the config side.

## 9.2 Dev tests

## 9.3 System tests
* End to end tests
* Regression tests
* Check performance impact, that vrouter througput is maintained with this
  firewall rules

# 10. Documentation Impact
N/A

# 11. References
* Blueprint: [https://blueprints.launchpad.net/opencontrail/+spec/neutron-fwaasv2](https://blueprints.launchpad.net/opencontrail/+spec/neutron-fwaasv2)
* Neutron FWaaS v2 extension spec: [https://specs.openstack.org/openstack/neutron-specs/specs/newton/fwaas-api-2.0.html](https://specs.openstack.org/openstack/neutron-specs/specs/newton/fwaas-api-2.0.html)
* Neutron FWaaS documentation: [http://docs.openstack.org/developer/neutron-fwaas](http://docs.openstack.org/developer/neutron-fwaas)
* Neutron API: [https://developer.openstack.org/api-ref/networking/v2/#fwaas-v2-0-current-fwaas-firewall-groups-firewall-policies-firewall-rules](https://developer.openstack.org/api-ref/networking/v2/#fwaas-v2-0-current-fwaas-firewall-groups-firewall-policies-firewall-rules)
