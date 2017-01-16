#1. Introduction

The contrail BGP implementation was designed from scratch to run on modern server environments.
The main goals were to be able to take advantage of multicore CPUs, large (>4G) memory footprints
and modern software development techniques.

BGP can be divided in the following components:

1. **Input processing**: decoding and validating messages received from each peer.
2. **Routing table operations**: modifying the routing table and determining the set
     of updates to generate.
3. **Update encoding**: draining the update queue and encoding messages to a set of peers.
4. **Management operations**: configuration processing and diagnostics.

This blueprint provides a detailed description on defining a new origin field by:

1. Making changes in Contrail configuration files.
2. Making changes in Contrail GUI.
3. Making changes in controller.

All of these steps are to be performed for the new functionality to work successfully.


#2. Problem statement
###Normalize route origin when learning routes from a VM/VNF.
This feature request is related to BGP as a Service (i.e. the vRouter peering with a VNF running in
a VM). It concerns the route origin field in BGP. On PNF routers it is possible to override the
route origin field of incoming routes that are learned from peers. This same capability is needed
when Contrail learns routes from VNFs. In particular, this is related to the vLB VNF because VNF
does NOT want route origin to be part of the tiebreaker protocol when choosing between routes.
It is necessary to be able to set the route origin field of routes learned from vLB VNFs to a single
value across entire network so that differences in route origin value between different vLB
instances won't contribute to route selection.

#3. Proposed solution
Contrail by default exposes certain configurable options to the admin in management console which
are eventually used by underlying service when making certain decisions or creating packets.
In order to make origin field configurable, following set of changes are needed:

+ Pass this value from UI to the configuration node and IF-MAP
+ From configuration node pass it to controller where BGP (core) implementation can access it.
+ If a user-specified value is provided, then use it otherwise follow existing logic

##3.1 Alternatives considered
Describe pros and cons of alternatives considered.
##3.2 API schema changes

**Configuration Changes:**

+ When used in the control node process, BGP derives its internal configuration from the network
+ configuration distributed by the IFMAP server. This functionality is handled by the
+ BgpConfigManager class.

+ The **first step** towards defining a new knob is to add it to the schema. OpenContrail
+ auto-generates the **REST API** that stores the configuration and makes it available through the
+ IF-MAP server. It also generates the **API client library** that is can be used to set the
+ configuration parameters. The BGP related schema is present in
+ **controller/src/schema/bgp_schema.xsd**.

**Changes in bgp_schema.xsd:**

+ Add a new XSD element called bgp-origin in the type BgpSessionAttributes. This is the data type
+ that is associated with bgp peering sessions.

+ Execute the command **scons controller/src/api-lib**. This command builds the Python client api
+ library that we will use later on to set the new configuration parameter. You can poke around at
+ the generated code: **grep bgp-origin build/debug/api-lib/vnc_api/gen/**

+ Add bpp_origin in bgp_sess_attrs in **controller/src/config/utils/provision_bgp.py**.

##3.3 User workflow impact

Contrail GUI allows the user define a new route origin with multiple options. User can click
advanced options in Create to view the BGP Origin field. It has four options: IGP, EGP, INCOMPLETE
or NONE to be selected by the user.

##3.4 UI changes

Details in contrail-web-controller repo README.md

##3.5 Notification impact

There were no changes made in logs, UVEs or alarms.



#4. Implementation
##4.1  Work items

It has 4 modules. The first module involves the changes in configuration files mentioned in
section3.2 above. The second module involves the changes in UI files mentioned in
contrail-web-controller repo README.md. The backend changes are mentioned below.

###4.1.1 Controller
Following changes are implemented in Controller to define a new origin field.
####4.1.1.1 BGP Config:
+ **bgp_config.h:** In **bgp_config.h**, new attribute **bgp_origin** is added in
+ **BgpNeighborConfig** class. For manipulation of this attribute, we have added a **getter/setter**
+ in the same class.

+ **bgp_config.cc:** In **bgp_config.cc**, we call the setter method for **bgp_origin** defined in
+ header file. The coding convention was followed and **bgp_origin** was added in **CopyValues**
+ method and the same was done for **CompareTo** method.

####4.1.1.2 BGP Peer:

+ In **bgp_peer.h** the new attribute **bgp_origin** is added in **BgpPeer** class.

+ In the file **bgp_peer.cc**, the **RibExportPolicy** in the **BuildRibExportPolicy** methodreturns
+ an additional argument which is **bgp_origin**.

####4.1.1.3 In BGPRibOut:

+ In **bgpRibout.h**, bgp origin function is defined which returns a constant value.

+ In **bgp_ribout.cc**, changes are made in the if statements of RibOut constructor.

####4.1.1.4 BgpRibOutPolicy:

+ In **bgp_rib_policy.h**, a new integer bgp origin is defined.

+ **bgp_rib_policy.cc**: In the structure **RibExportPolicy**, we add the attribute **bgp_origin**
+ so that origin attribute is advertised to all BGP Peers. In the structure **RibExportPolicy**,
+ the attribute **bgp_origin** is set in the constructor method. As there are total 4 constructors
+ for the structure **RibExportPolicy**, **bgp_origin** is set for the rest of 3 constructors.

####4.1.1.5 BgpShowConfig:

+ **bgp_show_config.cc**: In the **FillBgpNeighborConfigInfo** method, we set the **bgp_origin** for
+ **ShowBgpNeighborConfig** with the value of **BgpNeighborConfig bgp_origin**.

+ **bgp_peer.sandesh**: Declare bgp_origin in sandesh structure

+ In **bgp_config_ifmap.cc**, **bgp_origin** attribute is set.

###4.1.2 Core files for BgpAttrOrigin:

####4.1.2.1 BgpAttrOrigin:

+ **bgp_attr_base.h:** The BgpAttribute class defines an enumeration of Code which contain the BGP
+ Attributes.  Origin being a part of BGP is assigned value 1.

+ In **bgp_attr.h**, the structure BgpAttr inherits BgpAttribute. This structure contains methods
+ and attributes for manipulating Origin.

+ The class BgpAttr contains a getter/setter for Origin.

####4.1.2.2 Bgp_attr.cc:

+ The declared methods in bgp_attr.h are implemented. A total of 3 different code flows are
+ initiated within Contrail to set RouteOrigin attribute.
+ **(1)** BGP Message Builder **(2)** Routing Instance **(3)** BGP XMPP RTarget Manager

####4.1.2.3 BgpXmppRtargetManager:
+ **bgp_xmpp_rtarget_manager.cc**: In the GetRouteTargetRouteAttr method, the origin is set with a
+ initiated a value of IGP (defined in enum OriginType in struct BgpAttrOrigin).

####4.1.2.4 RoutingInstance:
+ **routing_instance.cc**: In RoutingInstance class, the method AddRTargetRoute sets the origin with
+ a value of IGP.

####4.1.2.5 BgpMessageBuilder:
+ **bgp_message_builder.h**: In class BgpMessage, new private constants are defined.

+ **bgp_message_builder.cc**: In BgpMessageBuilder class, the **StartReach** method has
+ **RibOutAttr** type reference in the parameters. A BgpAttr type pointer is referenced to
+ RibOutAttr attribute.

###4.1.3 Checking condition for overriding Bgp Origin value

On creating BGPaas, we check if session.bgp_origin is not equal to 3. Then we override the current
BGP origin. Otherwise, go with the default settings.

####4.1.3.1 bgp_message_builder.cc:

Check if value of bgp_origin is set by the user from 0 to 2. Override in this case. Otherwise go
with the default behavior.

####4.1.3.2 bgp_xmpp_rtarget_manager.cc:

In this cc file, the override logic for bgp_origin is implemented.

#5. Performance and scaling impact
##5.1 API and control plane

There are no changes in scalability of API and Control Plane.
##5.2 Forwarding performance
We do not expect any change to the forwarding performance.

#6. Upgrade
The BGP origin field is a new field and hence does not have any upgrade impact.

#7. Deprecations
There are no deprecations when this change is made.

#8. Dependencies
There are no dependencies for this feature.

#9. Testing
##9.1 Unit test

IFMAP unit test: Check whether value passed from front end has been received on IFMAP server.

BGPaaS: Check that the value of BGP origin received can be overridden.


##9.2 Dev test

Flow Test Steps:

+ Check if this value is received by IFMAP server at backend.

+ Check that when BGPaaS is created, default (original) value is overridden by user-defined value.

These tests were completed successfully.

#10. Documentation Impact
BGP origin field details have to be added in user documentation.

#11. References
[bgp_design](http://juniper.github.io/contrail-vnc/bgp_design.html)

[adding-bgp-knob-to-opencontrail](http://www.opencontrail.org/adding-bgp-knob-to-opencontrail/)

