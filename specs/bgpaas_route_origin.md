#1. Introduction
Contrail BGP route exchange carries Route Origin attribute, which is also used
as a tie breaker while choosing a prefered route path. However in certain cases
Administrator/user may not want Route Origin to contribute to route selection.

#2. Problem statement
While learning routes from BGP Peer Contrail can receive routes with different
Route Origin attributes for same route prefix, which may be expected to form
ECMP, however because of different values of Route Origin it ends up choosing
one path over other.

####Use case
In particular this requirement is in context of supporting a virtual Load
Balancer VNF. Where Route Origin should be normalised for routes received from
VNF, to avoid its contribution to route selection

#3. Proposed solution
As PNF routers already allow overriding Route Origin attribute, same can be
extended to contrail BGP implementation as well to normalize Route Origin
attribute for received BGP routes.
Similar to PNF routers, contrail BGP should be able to override Route Origin
attribute while importing a route from a BGP peer

##3.1 Alternatives considered
NA

##3.2 API schema changes
Route Origin override will be added as an element under BgpSessionAttributes

##3.3 User workflow impact
User can use VNC APIs to configure override Route Origin as part of BGP session
attributes while creating or updating bgp-router object
Alternatively contrail UI can be used to configure override, while currently
as per use case this will be added only to BGPaas wizard
Where it will allow choice between predefined values of IGP, EGP or INCOMPLETE

##3.4 UI changes
In BGPaas wizard following changes are required
* Under Advanced Options, add a new drop down for **BGP Origin**
* The BGP Origin field has 3 values **IGP**, **EGP** and **INCOMPLETE**
* The origin-override field in bgpaas-session-attributes property should be set to false by default.
* Under Advanced Options, add a new checkbox for **Enable Origin Override**. This should cause 
  origin-override field in bgpaas-session-attributes property to be set to True.
* To override BGP Origin, select a value from **BGP Origin** drop down field and check the **Origin 
  override** checkbox


##3.5 Notification impact
There were no changes made in logs, UVEs or alarms.


#4. Implementation
##4.1 Work items
####Config Schema
As per section 3.2

####Control-node
Control node will read this Route Origin override information associated with a
BGP Peer and apply override to routes received from peer

####Contrail-UI
As per section 3.4

#5. Performance and scaling impact
##5.1 API and control plane
No impact

##5.2 Forwarding performance
No impact

#6. Upgrade
No impact

#7. Deprecations
There are no deprecations when this change is made.

#8. Dependencies
There are no dependencies for this feature.

#9. Testing
##9.1 Unit tests
Unit Test Cases will be added to validate added code

##9.2 Dev tests
Validate Route Origin override for the routes imported from BGP peer and BGPaas

##9.3 System tests
Validate the solution function against the Virtual Load Balancer VNF use case

#10. Documentation Impact
The details of Route Origin Override have to be added in the user documentation.

#11. References
[bgp_design](http://juniper.github.io/contrail-vnc/bgp_design.html)

[adding-bgp-knob-to-opencontrail](http://www.opencontrail.org/adding-bgp-knob-to-opencontrail/)

[contrail-controller (source-code)](https://github.com/Juniper/contrail-controller/tree/master/src/vnsw/agent)
