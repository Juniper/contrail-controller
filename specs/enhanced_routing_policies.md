
# 1. Introduction
Enhance routing policy options to match on interface routes, interface static
routes and service interface routes.

# 2. Problem statement
The explosion of leaked routes in the routing table of the SDN Gateway
drives the requirement to enhance routing policies to provide further
granularity to prevent service interface and static routes from proliferating.

# 2.1 Use cases
## 2.1.1 Use-case 1
   * Distinguish service interface routes from other VM routes to
     prevent routes from being leaked and exploding route tables of
     SDN Gateway.

## 2.1.2 Use-case 2
   * Setting LocalPref on service interface static routes when exporting
     to distinguish routes and take further action.
   * Setting different LocalPref for all other reoriginated routes to
     distinguish routes and take further action.

## 2.1.3 Use-case3
   * Contrail sets Local Pref based on Community on imported routes, instead
     of DC GW, and allowing direct access to VPN Internet-Shared from Contrail


# 3. Proposed solution

Routing policies will be further enhanced to add
 * Additional Term Match condition to distinguish interface routes,
   service interface routes and static routes under protocol options.
 * New Action Attribrute ASPATH append with configurable AS-List.
 * All existing action attributes Add/Set/Remove Community, SetLocal-Pref
   and Set Med supported with new protocol Match condition along with
   new ASPATH list append action.

## 3.1 Alternatives considered
None, this is a feature enchancement providing more granular policies.

## 3.2 API schema changes

```
--- a/src/schema/routing_policy.xsd
+++ b/src/schema/routing_policy.xsd
@@ -22,6 +22,15 @@
     </xsd:restriction>
 </xsd:simpleType>

+<xsd:complexType name='AsListType'>
+    <xsd:element name='asn-list' type='xsd:integer' maxOccurs='unbounded'/>
+</xsd:complexType>
+
+<xsd:complexType name="ActionAsPathType">
+    <xsd:element name='expand'   type='AsListType'/>
+</xsd:complexType>
+
 <xsd:complexType name='CommunityListType'>
     <xsd:element name='community' type='CommunityAttribute' maxOccurs='unbounded'/>
 </xsd:complexType>
@@ -33,6 +42,7 @@
 </xsd:complexType>

 <xsd:complexType name="ActionUpdateType">
+    <xsd:element name="as-path"     type="ActionAsPathType"/>
     <xsd:element name="community"   type="ActionCommunityType"/>
     <xsd:element name="local-pref"  type="xsd:integer"/>
     <xsd:element name="med"         type="xsd:integer"/>
@@ -55,9 +65,11 @@
     <xsd:restriction base="xsd:string">
         <xsd:enumeration value="bgp"/>
         <xsd:enumeration value="xmpp"/>
         <xsd:enumeration value="static"/>
         <xsd:enumeration value="service-chain"/>
         <xsd:enumeration value="aggregate"/>
+        <xsd:enumeration value="interface"/>
+        <xsd:enumeration value="interface-static"/>
+        <xsd:enumeration value="service-interface"/>
     </xsd:restriction>
 </xsd:simpleType>

```

## 3.3 User workflow impact

Users will be able to configure new Term Match and
and Action attributes.

## 3.4 UI changes

 * UI to add additional protocol Term Match under
   Configure > Networking > Routing > Routing Policies
   while adding a new Route Policy for interface-route,
   interface-static and service interface outes.
 * UI to add additional ASPATH list attribute to
   Route action Policy.

## 3.5 Notification impact
None.

# 4. Implementation

### 4.1.1 Control-Node
Control-Node will need to parse the configured routing
policies and program the community, local-pref and med
attributes on the routes being delivered to Compute Node.

Control-Node will need to decide to export the
routes to the L3-VPN table based on the action
configured.


### 4.1.2 Compute Node

All routes advertised from Compute Node are bundled
under protocol XMPP, need to further distinguish these
routes as interface, service interface and static routes
before being advertised to control-node.

# 5. Performance and scaling impact
None.

## 5.2 Forwarding performance
There should be no forwarding performance impact.

# 6. Upgrade
None.

# 7. Deprecations
None.

# 8. Dependencies
None.

# 9. Testing
## 9.1 Unit tests
## 9.2 Dev tests
## 9.3 System tests

# 10. Documentation Impact

# 11. References
https://blueprints.launchpad.net/opencontrail/+spec/enhanced-routing-policies
