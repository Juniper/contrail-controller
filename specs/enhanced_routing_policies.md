
# 1. Introduction
Enhance routing policy options to match service interface and static routes.


# 2. Problem statement
The explosion of exported routes in the routing table of the MX
Gateway drives the routing policies to provide furthur granuality
to filter service interface and static routes.

# 2.1 Use cases
## 2.1.1 Use-case 1
   * Setting LocalPref on static routes when exporting to distinguish routes.
   * Setting different LocalPref for all other reoriginated routes to
     distinguish routes.

## 2.1.2 Use-case2
   Contrail sets Local Pref based on Community on imported routes, instead
   of DC GW, and allowing direct access to VPN Internet-Shared from Contrail


# 3. Proposed solution

Routing policies will be furthur enhanced to add
 * Additional Match condition for interface and static routes under protocol.
 * New Action Attibrute ASPATH append with configurable AS-List.
 * All existing action attributes Add/Set/Remove Community, SetLocal-Pref
   and Set Med supported with new protocol Match condition along with
   new ASPATH list append action.

## 3.1 Alternatives considered
None, this is a feature enchancement providing more granualar policies.

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
+        <xsd:enumeration value="interface-route"/>
     </xsd:restriction>
 </xsd:simpleType>

```

## 3.3 User workflow impact

Users will be able to configure new Term Match and
and action attributes.

## 3.4 UI changes

 * UI to add additional protocol Term Match under
   Configure > Networking > Routing > Routing Policies
   while adding a new Route Policy for interface-route
   and static routes.
 * UI to add additional ASPATH list attribute to
   Route action Policy.

## 3.5 Notification impact
None.

# 4. Implementation

### 4.1.1 Control-Node
Control-Node will need to parse the configured routing
policy and program the community, local-pref and med
attributes on the routes being delivered to Compute Node.

Control-Node will need to decide to export the
routes to the L3-VPN table based on the action
configured.


### 4.1.2 Compute Node


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
https://app.asana.com/0/239951276108262/244701955544790

