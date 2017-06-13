# Introduction
This document addresses security enhancements to contrail product.
* **FW security policy feature enhancements**
* **Decouple routing from security policy**
* _Integrate with third party NG FWs_
* _Support FWAASv2 API_

This feature development addresses ‘Decoupling routing from security policy’ and ‘FW security policy feature enhancements’. It also considers future support for FWAASv2 API, while developing this feature.

# Problem statement

Network security is orthogonal to network topology. Network topology addresses some of the network security elements. OpenStack tenant networks are isolated, and can’t communicate by default. Virtual networks with in a tenant requires neutron router for connectivity, in a way traffic is isolated/segmented between networks. Tenant is isolated from other tenants.

Contrail network policy provides security between networks by allow/deny certain traffic. Contrail network policy also provides connectivity between virtual networks.

OpenStack security groups allow access between workloads/instances for specified traffic types and rest denied.

Security Policy model for a given customer needs to map to above OpenStack/Contrail network policy and security group constructs.

Customer deployments may contain multiple dimension entities (multiple deployments, multiple applications, multiple tiers). Security policy model might contain many ways of cross cutting those dimensions to control traffic among workloads.

User might want to segregate traffic on the following categories
* Site: Site could be Country or City or Rack or Region or all together Country/City/Rack or it
could be any other arbitrary ways of dividing place
* OS: User might want to communicate among same OS
* Environment: Modeling, Testing, production
* Application: HR, salesforce app, oracle ordering app
* Workload type: low sensitivity, financial, or personal identifiable information
* Application-Tier: web tier, database tier
* Many more ...

User wants to cross section between above categories, which makes it harder to express with existing network policy/security group constructs.

Addressing this requirement with Security Groups will be tough, and have create combinatorial SGs to address it. As customer comes with different ways to segregate traffic, then number of SGs will explode and also it is not possible without changing the definition of SGs and their attachment at workloads.

A customer scenario was to have multi-tier application, that supports both development and production environment workloads. Security requirement is to application shouldn’t have cross environment traffic, which is a simple ask. It became pretty hard to do with existing constructs.Customer started with a VN per tier (VNweb, VNapp, VNdb) and have SGs to support application isolation (SGhr, SGsalesforce, SGemail), now it is hard to environment isolation without exploding VNs or SGs.

Now imagine having 100 applications and 10 environments, somebody might have to create 1000 SGs to manage them and also any cross relation between environments won’t work.
It might work some specific cases, but not all.

Workday like customer wants to segment traffic based on Application and Deployment, it was hard to solve using SGs, will produce too many rules in the SGs.  As number of applications grows, it is impossible to manage rule explosion.

# Proposal for FW security policy enhancements

This feature introduces decoupling of routing from security policies, multi dimension segmentation and policy portability. This feature also enhances user visibility and analytics functions.

## Routing and policy decoupling

Network Policy objects are tightly coupled with routing. Hence, we are introducing new firewall policy objects, which decouples policy from routing.

## Multi dimension segmentation

As part of enhancing FW features, we are introducing multi dimension segmentation.
Multi dimension segmentation (Example dimensions: Application, Tier, Deployment, Site, UserGroup and etc...) is to segment traffic based on multiple dimensions of entities with security features.

## Policy portability

Customers are looking for portability of their security policies to different environments. Portability might be required ‘from development to production’, ‘from pci-complaint to production’, ‘to bare metal environment’ and ‘to container environment’.

## Visibility and Analytics
[Anish]

# Proposed solution

This proposal is to address multi dimension traffic segmentation using tags in security policy definitions. High level idea is to use tag regular expressions in the source and destination fields of policy rules. These tag regular expressions will give cross sections of tag dimensions.

Also today, policies suffer portability from one environment to other, this proposal also enhances with match condition to address this issue. Match tags will be added to policy rule to match tag values of source and destination workloads without mentioning tag values. Example is ‘allow protocol tcp source application-tier=web destination application-tier=application match application and site’. application and site values should match for this rule to take effect.

We would like to limit tags types to make it easier for customers to consume. The following tag types are defined/allowed, and additional tags will be added later as needed. Contrail supports up to 32 tag types.

Proposal also considers FWAASv2, while creating the policy objects.

## Predefined Tags

Predefined tags are chosen based on customer requirements and also look into existing deployment issues. User can pick relevant tag values for their environment. Tag values are unique.

Predefine tag types are -
* application
* application-tier
* deployment
* site
* user
* compliance
* label (is a special tag, allows the user to label objects)

Example usage of tags, as follows
application = HRApp
application-tier = Web
site = USA

## Implicit tags (facts)
Implicit tags are facts of an existing environment. These tags could be added as part of provisioning or configured manually.
* compute node
* rack
* pod
* cluster
* dc

## Tagging objects

User can tag objects Project, VN, VM, VMI with tags and values, to map their security requirements. Tags follow hierarchy of Project, VN, VM and VMI and inherited in that order. This gives an option for user to provide default settings for any tags at any level. As mentioned earlier, policies could specify their security in term of tagged end points. Policies also can express in terms of ip prefix, network and address groups end points.

## Policy application

Policy application is via application tag. Policy application is new object. User can create list of policies per application, to be applied during the flow acceptance evaluation. Introducing global scoped policies and project scoped policies. Some policies can be defined/applied globally for all the projects, others project specific policies.

## Configuration objects

Identified the following objects as starting point for new security features.
* firewall-policy
* firewall-rule
* policy-management
* application-policy
* service-group
* address-group
* tag
* global-application-policy

[Import picture from word document for configuration objects and their layout]

### Tags

#### Configuration Tag object

Tag object contains type, value, description, tag_id.
type is one of the defined tag types, stored as string.
value is a string.
description is a string to describe the tag
configuration_id is a 32-bit value
* 5 bits for tag types
* 27 bits for tag values

As each value entered by user will create a unique id and will be set in the tag_id field.
System can have up to 64 million tag values. On an average each tag can up to 2k values, but there are no restriction per tag.

Tags/labels can be attached to any object (e.g. project, VN, VM, VMI and policy). These objects will have tag ref list to support multiple tags. RBAC will control the users to modify or remove the attached tags. Some tags will be attached by system by default or via introspection, these tags are typically facts.

Tag APIs
* REST: HTTP POST to `/set_tag_<tag_type>/<obj_uuid>`
* Python: `set_tag_<tag_type> (object_type, object_uuid, tag_value)`

The above allows us to give RBAC per tag in any object (VMI, VM, Project ….)

Configuration should support the following API also
* Tag query
* tags(policy)
* tags (application tag)
* Object query
* tags(object)
* tags (type, value)

Label

label is special tag type, used to assign labels for objects. All the above tag constructs are valid, except that tag type will be ‘label'. One difference for tags is that an object can have any number of labels. All other tag types are restrticted to one tag per object.

For lables, we will have the following APIs:
* REST: HTTP POST to `/add_tag_label/<obj_uuid>`
* REST: HTTP POST to `/delete_tag_label/<obj_uuid>`
* Python: `add_tag_label (object_type, object_uuid, tag_value)`
* Python: `delete_tag_label (object_type, object_uuid, tag_value)`

Local vs globally scoped tags

Tags can be defined globally or under a project (i.e., tag objects are children of either config-root or a project). An object can be tagged with a tag in its project or a globally scoped tag.


#### Analytics
Given tag SQL where clause and select clause, analytics should give out objects. Query may contain labels also, whereas labels will have different operators.
Examples: User might want to know ...
list of VMIs where ’site == USA and deployment == Production'
list of VMIs where ’site == USA and deployment == Production has <label name>’
Given tag SQL where clause and select clause, analytics should give out flows.

#### Control node
Control node passes the tags along with route updates to agents and other control nodes.

#### Agent
Agent gets attached tags along with configuration objects. Agent also gets route updates containing tags associated with IP route. This process is similar to getting SG ids along with the route update.

### Address-group

There are multiple ways to add IP address to address-group.
1. user can manually add IP prefixes to it via configuration.
2. user can label a work load with address-group’s specified label. In this case, all ports that are also labelled with the same label are considered to be part of that address-group.
3. Introspect workloads and based on certain criteria add ip-address to address-group. [Needs discussion]

#### Configuration
address-group object refer to label object, description and list of IP prefixes.
label - object is created using the tag APIs.

#### Agent
Agent gets address-group and label objects referenced in policy configuration. Agent uses this address group for matching policy rules.
#### Analytics
Given address group label, get all the objects associated with it.
Given address group label, get all the flows associated with it.

### Service-group

#### Configuration
service-group contains list of port list and protocol.
Whereas, open stack service-group has list of service objects and service object contains the following attributes

id, name, service group id, protocol, source_port, destination_port, icmp_code, icmp_type, timeout, tenant id

#### Agent
Agent gets service-group object as it is referred in a policy/rule. Agent uses this service group during policy evaluation.

### Application-policy-set

application-policy-set configuration object can refer to a tag of type application, network-policy objects and firewall-policy objects. This object could be project or globally scoped.
When an application tag is attached to an application-policy-set object, the policies referred by that object are automatically applied to the ports that have the same application tag. firewall-policies referred by the application-policy-set objects are ordered using sequence numbers. If the same application tag is attached to multiple application-policy-sets, all those sets will apply, but order among those sets is undefined.

One application-policy-set (called default-policy-application-set) is special in that policies referred by it are applied to all interfaces by default *after* applying policies referred to other application-policy-sets.

Upon seeing application tag for any object, the associated policies will be send to agent. Agent will use this information to find out the list of policies to be applied and their sequence during flow evaluation. User could attach application tag to allowed objects (Project, VN, VM or VMI).

### Policy-management

Policy-management is a global container object for all policy related configuration. Policy-management object contains -
network-policies (NPs), firewall-policies (FWPs), application-policy-sets , global-policy objects, global-policy-apply objects
NPs - List of contrail networking policy objects
FWPs - List of new firewall policy objects
Application-policies - List of Application-policy objects
Global-policies - List of new firewall policy objects, that are defined for global access
Global-policy-apply - List of global policies in a sequence, and these policies applied during flow evaluation.

Network Policies (NP) references in the policy-management is long term plan, these changes are not required in the first release. NP policies will be available, as they are today.

### Firewall-policy
We discussed to use existing network policy, instead of creating new firewall-policy object. But, network policy supports both connectivity and policy.
We decided to keep a separate new object for firewall policy to do all firewall related features, and network policy could slowly move towards to connectivity and includes service chain connectivity.
firewall-policy object will keep adding firewall related features like DDOS, filtering features and etc.…
Firewall-policy is a new policy object, which contains list of firewall-rule-objects and audited flag. Firewall-policy could be project or global scoped depends on the usage.

audited
boolean flag to indicate that, owner of the policy indicated that policy is audited. Default is False, and will have to explicitly set to True after review.

Generate a log event for audited with timestamp and user details.


### Firewall-rule
Firewall-rule is a new rule object, which contains the following fields and syntax is to give information about their layout inside the rule.

* `<sequence number>`
* [< id >]
* [name < name >]
* [description < description >]
* public
* {permit | deny}
* [ protocol {< protocol-name > | any } destination-port { < port range > | any } [ source-port { < port range > | any} ] ] | service-group < name >
* endpoint-1 { [ip < prefix > ] | [virtual-network < vnname >] | [address-group < group name >] | [tags T1 == V1 && T2 == V2 … && Tn == Vn && label == label name...] | any}
* { -> | <- | <-> }
* endpoint-2 { [ip < prefix > ] | [virtual-network < vnname >] | [address-group < group name >] | [tags T1 == V1 && T2 == V2 … && Tn == Vn && label == label name...] | any }
* [ match_tags {T1 …. Tn} | none} ]
* [ timer < start-time > < limit >]
* [ log| mirror | alert | activate <rule id> | drop | reject | sdrop ]
* { enable | disable}
* filter

#### sequence number
We put a sequence number on the link from firewall-policy to firewall-policy-rule objects. It is a string object, instead of integer. It decides the order in which the rules are applied.

#### id
uuid

#### name
Unique name selected by user

#### display-name
Readable name, it is not unique

#### description
as it says

#### actions
permit/deny [3]

#### direction
->, <-, <->
It specifies connection direction. All the rules are connection oriented and this option gives the direction of the connection.

#### complex actions
log/mirror/alert/active <rule id>/drop/reject/sdrop [3]

#### endpoint tags option
Tags at endpoints support an expression of tags. We support only ‘==‘ and ‘&&’ operators. User can specify labels also as part the expression.
Configuration object contains list of tag names (or global:tag-name in case of global tags) for endpoints. We need to see, how to support different operators in RE (future requirements)?

#### match_tags
List of tag types or none.
User can specify either match with list of tags or none. Match with list of tags mean, source and destination tag values should match for the rule to take effect.

#### enabled
A boolean flag to indicate the rule is enabled or disabled. Facilitates selectively turn off the rules, without remove the rule from the policy. Default is True.

#### filter
Will be discussed later

#### Rest of the fields
Rest of the fields are same as existing contrail network policy (NP)

#### "Compilation" of rules

Whenever API server receives request to create/update a firewall policy rule object, it analyzes the object data to make sure that all virtual-networks, address-group, tag objects exist. If any of them do not exist, the request will be rejected. In addition, it will actually create a reference to those objects mentioned in the two endpoints. This achieves two purposes. First, we don't allow users to name non-existent objects in the rule and second, the user is not allowed to delete those objects without first removing them from all rules that are referring to them.

## User workflow

### Policy attachment/apply

Policy attachment happens via application tag. These application-policies are defined/scoped at global or project. As the application tag matches both global and project scoped list will be picked during the flow evaluation. Each application-policy has the sequence of policies to be applied in the order.

### Policy evaluation

global_apply list contains list of ordered FWP
global: application_policy list contains two list of ordered FWP and NP
project: application_policy list contains two list of ordered FWP and NP

Create a list of FWP [G1]
This list formed picking FWP from global_apply, global:application_policy and project:application_policy list. At the end of these lists, there will be a ‘default deny all’

Create a list of NP [G2]
This list formed picking NP from global_apply, global:application_policy and project:application_policy list. At the end of these lists, there will be a ‘default deny all’. May be we don’t need to change the existing behavior.

Create a list of SG [G3]
List of SGs attached to the workload, at the end there is a ‘default deny all’

Policies evaluation goes through multiple gates. Each policy type is a gate, those are security groups, network policies, project application firewall policies, global firewall policies and in future FWAASv2 policies.

All the gates have to pass to allow the traffic flow. Otherwise, at the end, there is default deny to stop the flow. Policy rules might have terminate rule or non-terminate rule, evaluation proceeds until it hits terminate rule or end default deny rule.
Ingress traffic gates
Ingress traffic gates are as follows... Network policies, Firewall policies and SGs
Egress traffic gates
evaluates reserve of ingress

### Operation mode

There are two mode options, ‘production’ and ‘test’. Production mode is default and keeps default deny at the end of policy list. Whereas ‘test’ mode, will add default ‘allow' rule at the end, instead of ‘deny'. The idea is to conduct test phases, to figure out all the interactions and convert them into policy/rules for user. Customer could go on test loop to see any more flows not hitting the regular rules and keep converting them to rules. These rules might use ‘disabled’ flag and add it to the policy to present to the user, admin could check these rules to enable and audit.
Where should we add this ‘operation mode’ field?

### Policy portability

This proposal makes it portable as much as possible via match conditions. Match condition is a way to match both source and destination tag values, without explicitly giving out the environment values (tag values). User can match on multiple tags. It is allowed to set default match tags on project, so that all the policies’ rules get this match default. If a match is specified at rule, it overrides the default match. It is allowed to set match with ‘none’ to avoid this feature.

### Analytics and UI

High level goals for WebUI and Analytics are -
* Help configure/design security posture
* Monitor
* Security

#### Aid configure/design security policies/posture
The idea is to run the system in test mode, where connectivity between networks are present and security policies allow all traffic. Analytics engine and security admin with multiple iterations to come up with security policies based on the traffic ran in test mode. Analytics engine aids in identifying pattern in the flow traffic and presents user with possible tags to fit the traffic. This is an iterative process until figure out desired security policies.

#### Monitor
WebUI/Analytics to present user, system topology view and be able to drill down traffic based on network or security group or etc..
Monitoring should also consider tags to show flow

## High level implementation details

### Agent

#### Configuration

Configuration objects global-apply, global application-policy, project application-policy, corresponding firewall policies and any dependent objects will be downloaded to agent, as tags are attached to VMIs.

Tags can be attached to VMI via direct manual attachment or inheritance from VM/VN/Project.

Firewall policies will be send as is to agent.

Firewall policies rules’ end points (source and destination) allows tag regular expressions. These expressions are limited to ‘==’ and ‘&&’ operators for first revision of implementation. Configuration will be converted to list configuration tag ids (32bit) in the rule and send it to agent.

#### Building of tags

All tags information in agent would be maintained at interface level. Interface would parse through all the links of interest and build the active tag list. Below are links of interest to VMI

* Virtual-Machine-Interface -> Tag
* Virtual-Machine-Interface -> Virtual-Machine -> Tag
* Virtual-Machine-Interface -> Virtual-Network -> Tag
* Virtual-Machine-Interface -> Project -> Tag

IFMAP dependency rules would be specified such that any of the above link gets added or deleted it would result in VMI revaluation. Tag value can change hence agent should also revaluate tags associated to VMI when any property in Tag object changes. Every time tag list associated with VMI changes all the routes (IP, EVPN, FIP, AAP and Static route) should be exported with new updated tag list. New extended community would be used to export tag value to control-node.

#### Policy Set

Firewall Policy contains a list of firewall rule to be applied sequentially, Firewall policy maps to existing ACL DB entry and rule maps to existing ACL Entry in agent operational DB. Sequential list of policy becomes Policy set and agent operation DB would create a new DB entry which has list of ACL DB entry to be applied. Policy set has an application tag associated with it which in turn could link to VMI, VM, VN or Project via tag link. Policy set are applied at interface by following the same order of preference with VMI first followed by VM, VN and Project. On an interface there can be only one application policy-set active at a given point.

Current ACL Entry has ability to match SG ID which could be reused to match tag values also. Match condition would be a new object under ACL Entry which would be used to match if tag of same type match between source endpoint and destination endpoint for example if policy rule allow communication between tier in same deployment agent has to get tag value if deployment and match that its same across source and destination.

#### Flow evaluation

There were multiple options to optimize RE comparison, while evaluating policy rules during the flow creation. It is ideal to optimize RE match during the evaluation. For the first cut of implementation FW rules would be evaluated sequentially.

Get list of tags associated with ip end point and during the flow evaluation, match the policy rule’s tag list. Other option is to create RE list as policies received. These RE list evaluated during local or remote route updates and tag matched REs in the route/ip operational DB. Flow evaluation doesn’t have to compare tag list in rule, instead compare RE in one operation.

Flow would be revaluated when any of the below thing changes
1. Policy Set associated with VMI changes
1. Policy set rule list change
1. FW rules changes (Flow module already does this)
1. Policy changes (Flow module already handles this)
1. Tag value changes.
1. Tag gets added or deleted to VMI.

#### Updates

Configuration updates on policies, application tags and route update should evaluate necessary flows, in addition to existing evaluations.

##### Flow reevaluation
Reevaluation of flows in addition to the existing reevaluation conditions are –
Changes in FWPx –
FWPx  flow’s application tag’s project policy_application FWP list or global policy_application FWP list or global_apply FWP list

### Control node

* policy and tags information along with flow.
* Session aggregate feature needs to be enhanced to support tags.
* Need a way to get metrics based on the tag or list of tags (tag expression)


### Analytics
* Provide elastic search features for flows, logs and other features
* Show mapping of flow to ACL/ACE policy
* Finger printing of flows (Analytics Engine Vs vRouter)
* Show attack surface and help in reducing attack surface via visualization
* Tag (dimension) based usage, sessions, metrics
* Example: Show application usage based on the flows
* 	   Multi dimension usage, sessions, metrics

#### Logs
Flow records with logging
Log historical usage to see what rules are actually used and to aid in troubleshooting
Log only configured rules/policies, (no logging for implicit flows, only during the troubleshooting, ex: Deny inter group traffic only allow intra group traffic)

#### Reports
Periodic reports or alerts to send out defined end points (email/channel)

#### Compliance
Pre-defined rules based on virtual security best practices
Build whitelists (wanted configuration) or blacklists or unwanted configuration


Refs
> 1. http://specs.openstack.org/openstack/neutron-specs/specs/kilo/service-group.html
> 2. https://specs.openstack.org/openstack/neutron-specs/specs/newton/fwaas-api-2.0.html
> 3. http://manual-snort-org.s3-website-us-east-1.amazonaws.com/node29.html#SECTION00421000000000000000
