1. Introduction
This blueprint describes the use-case to configure different routing protocols such as OSPF/PIM on the IRB interface on unicast IP address and eBGP on loopback interfaces within a VRF between fabric devices (border leaf/spine) and external un-managed device

2. Problem statement
In R2003 release, CEM supports users to specify the unicast IRB IP address on QFX devices where LR is extended and configuring eBGP and static routing as the two supported routing protocols from the IRB interface on the QFX to an external device. 
As lot of legacy routing devices still run OSPF, there is a need to extend the support to configure OSPF as the routing protocol with configurable OSPF parameters.
PIM support is also needed to support multicast traffic between fabric devices and external un-managed device
There is an ask to run eBGP protocol on the loopback interface within a VRF, which requires unique loopback addresses for each logical router.

3. Proposed solution
There will be support for different routing protocols from R2005 onwards:
- OSPF on the IRB interface for each routed virtual network on different QFX devices. Configurable parameters are as below:
     - Area id (0.0.0.0 or different from 0)
     - Area Type (NSSA/Stub/Regular)
     - hello interval
     - dead interval
     - BFD parameters for OSPF session
     - import/export routing policies
     - Authentication key (only MD5 is supported as of now)
     
- PIM on the IRB interface or for all interfaces for each routed virtual network. Configurable parameters are as below: 
     - RP IP address
     - BFD parameters for PIM session
     - Option to enable PIM on all interfaces
     - PIM mode (Either sparse or sparse-dense)
     Along with PIM, igmp and igmp-snooping will also be enabled

- eBGP on the loopback interface within VRF, all the configurable parameters as supported for IRB interface are supported for loopback interface as well
     - eBGP on loopback interface will be multi-hop and user can configure TTL value from UI
     - To support eBGP on loopback, there will be unique loopback address for each each QFX device for each LR, which comes from the new fabric namespace, defined for overlay loopback subnets.
     - User can either enter loopback IP address manually from the subnet defined for overlay loopback in fabric namespace. If user does not specify any IP address, CEM will auto-allocate the IP address from the subnets defined.

4. API schema changes
New protocols are added in routed-virtual-network-properties to include OSPF and PIM
<xsd:simpleType name="FabricNetworkType">
     <xsd:restriction base="xsd:string">
         <xsd:enumeration value="management"/>
         <xsd:enumeration value="loopback"/>
         <xsd:enumeration value="ip-fabric"/>
         <xsd:enumeration value="pnf-servicechain"/>
         <xsd:enumeration value="overlay-loopback"/>     --> This is the new fabric namespace to define overlay loopback                                                      subnets
     </xsd:restriction>
</xsd:simpleType>

<xsd:complexType name='RoutedProperties'>
    <xsd:all>
        <xsd:element name='physical-router-uuid' type='xsd:string' required='true'
             description='Routed properties for particular physical router for this virtual-network.'/>
        <xsd:element name='logical-router-uuid' type='xsd:string' required='true'
             description='Routed properties for particular logical router for this virtual-network.'/>
        <xsd:element name='routed-interface-ip-address' type='IpAddressType' required='true'
             description='IP address of the routed interface from the VN subnet.'/>
        <xsd:element name='loopback-ip-address' type='IpAddressType' required='optional'    
             description='IP address of the loopback interface from the overlay loopback subnet.'/>   ---> To add loopback IP address
        <xsd:element name='routing-protocol' type='RoutingProtocolType' required='true'
             description='Protocol used for exchanging route information.'/>
        <xsd:element name='bgp-params' type='BgpParameters' required='optional'
             description='BGP parameters such as ASN, peer IP address, authentication method/key.'/>
        <xsd:element name='ospf-params' type='OspfParameters' required='optional'
             description='OSPF parameters such as area ID, area type, hello-interval, dead-interval, authentication method/key.'/>      ---> To add OSPF protocol parameters
        <xsd:element name='pim-params' type='PimParameters' required='optional'
             description='PIM parameters such as RP IP address, mode.'/>    ---> To add PIM protocol parameters
        <xsd:element name='static-route-params' type='StaticRouteParameters' required='optional'
             description='Static route parameters such as interface route table uuid, next hop IP address.'/>
        <xsd:element name='bfd-params' type='BfdParameters' required='optional'
             description='BFD parameters such as time interval, detection time multiplier.'/>
        <xsd:element name='routing-policy-params' type='RoutingPolicyParameters' required='optional'
             description='List of import/export routing policy uuids.'/>
    </xsd:all>
</xsd:complexType>


<xsd:complexType name="BgpParameters">
    <xsd:all>
        <xsd:element name='peer-autonomous-system' type='xsd:integer' required='true'
         description='Peer autonomous system number for this eBGP session.'/>
    <xsd:element name='peer-ip-address' type='smi:IpAddress' required='true'
         description='Peer ip address used for this eBGP session.'/>
    <xsd:element name='hold-time' type='BgpHoldTime' default='0' required='optional'
         description='BGP hold time in seconds [0-65535], Max time to detect liveliness to peer. Value 0 will result in default value of 90 seconds'/>
    <xsd:element name='auth-data' type='AuthenticationData' required='optional'
         description='Authentication related configuration like type, keys etc.'/>
    <xsd:element name='local-autonomous-system' type='xsd:integer' required='optional'
         description='BgpRouter specific Autonomous System number if different from global AS number.'/>
    <xsd:element name='multihop-ttl' type='xsd:integer' required='optional'
         description='time-to-live (TTL) value in the BGP packets to control how far they propagate.'/>    ---> To define multihop BGP TTL value
    </xsd:all>
</xsd:complexType>

<xsd:complexType name="PimParameters">     ---> PIM parameters
    <xsd:all>
        <xsd:element name='rp-ip-address' type='smi:IpAddress' required='true'
             description='Static rendezvous point IP address.'/>
        <xsd:element name='mode' type='PimMode' default='sparse' required='optional'
             description='Pim mode.'/>
        <xsd:elememt name='enable-all-interfaces' type='xsd:boolean' required='optional'
             description='Boolean to enable PIM on all interfaces.'/>
    </xsd:all>
</xsd:complexType>

<xsd:simpleType name="RoutingProtocolType">   ---> New routing protocols are added in enum
    <xsd:restriction base='xsd:string'>
        <xsd:enumeration value='static-routes'/>
        <xsd:enumeration value='bgp'/>
        <xsd:enumeration value='ospf'/>
        <xsd:enumeration value='pim'/>
    </xsd:restriction>
</xsd:simpleType>

<xsd:simpleType name="PimMode">
    <xsd:restriction base='xsd:string'>
        <xsd:enumeration value='sparse'/>
        <xsd:enumeration value='sparse-dense'/>
        <xsd:enumeration value='dense'/>
    </xsd:restriction>
</xsd:simpleType>

<xsd:complexType name="OspfParameters">   ---> OSPF parameters
    <xsd:all>
        <xsd:element name='auth-data' type='AuthenticationData' required='optional'
             description='Authentication related configuration like type, keys etc.'/>
        <xsd:element name='hello-interval' type='xsd:integer' default='10' required='optional'
             description='Specifies the length of time, in seconds, before the routing device sends a hello packet out of an interface.'/>
        <xsd:element name='dead-interval' type='xsd:integer' default='40' required='optional'
             description='Specifies how long OSPF waits before declaring that a neighboring routing device is unavailable'/>
        <xsd:element name='area-id' type='smi:IpAddress' required='true'
             description='OSPF area ID'/>
        <xsd:element name='area-type' type='OspfAreaType' required='true'
             description='OSPF area type'/>
        <xsd:elememt name='advertise-loopback' type='xsd:boolean' required='optional'
             description='Boolean to enable advertising loopback address.'/>
        <xsd:elememt name='orignate-summary-lsa' type='xsd:boolean' required='optional'
             description='Boolean to enable originating summary LSA.'/>
        <xsd:elememt name='send-lsa-in-nssa' type='xsd:boolean' required='optional'
             description='Boolean to enable sending re-distributed LSA in NSSA area.'/>
    </xsd:all>
</xsd:complexType>

<xsd:simpleType name="OspfAreaType">
    <xsd:restriction base='xsd:string'>
        <xsd:enumeration value='nssa'/>
        <xsd:enumeration value='stub'/>
        <xsd:enumeration value='backbone'/>
    </xsd:restriction>
</xsd:simpleType>

5. Alternatives considered
None

6. UI changes / User workflow impact
- Defining new fabric namespace 'Overlay Loopback subnets'. These subnets are used to allocate the loopback interface IP address within a VRF (for each LR per QFX device)
    UI will expose an option to add a new fabric namespace for both ZTP and brownfield wizard. If the fabric is already on-boarded, user can still add a new namespace to define these subnets, without running the fabric on-boarding again
    User can only add a new subnet to existing namespace but cannot delete/edit the already existing subnet

- For each routed virtual network, user can add routing protocols (OSPF and PIM)
- User can run eBGP for loopback interface. User needs to select the virtual-network created for overlay loopback subnets and either pick an IP address from that VN or let CEM auto-allocate the IP address from that subnet.

7. Notification impact
N/A

8. Provisioning changes
None

9. Performance and scaling impact
N/A

10. Upgrade
N/A

11. Deprecations
N/A

12. Dependencies
N/A

13. Security Considerations
N/A

14. Testing
Unit tests
Dev tests
System tests

16. Documentation Impact

17. References
JIRA story : https://contrail-jws.atlassian.net/browse/CEM-11916
UX design: https://contrail-jws.atlassian.net/browse/CEM-13293
