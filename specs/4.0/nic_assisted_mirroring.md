
# 1. Introduction
Contrail provides ability to mirror specific traffic to a traffic analyzer or
to a physical probe by configuring rules to identify traffic to be mirrored 
and by specifying an analyzer or physical probe as the mirror destination. 
In addition, mirroring can be configured on VM interfaces to mirror all the
traffic to and from the interface.

# 2. Problem statement
When mirroring is enabled, the vrouter throughput reduces because of the
additional packet handling overheads. Cloning the packet to be mirrored,
encapsulating it in the required header and forwarding it to the mirror
destination causes this overhead and it increases in proportion to the
amount of traffic that needs to be mirrored.

# 3. Proposed solution
A solution to avoid impact on throughput due to mirroring is to use NIC’s
mirroring capabilities.

In this approach, the vrouter doesn’t mirror the packets. When NIC assisted
mirroring is enabled, ingress packets coming from the VM that have to be
mirrored are sent to the NIC with a configured vlan tag. The NIC is programmed
to do VLAN port mirroring, that is any packet with the configured VLAN is
mirrored additionally by the NIC. Note that this change in vrouter is only for
traffic coming from the VMs. Traffic coming from the fabric is directly
mirrored from the NIC itself and there is no additional mirroring need in
vrouter.

The programming of the NIC itself for appropriate mirroring is outside the
scope of the current activity. Niantic 82599 10G NIC is assumed, which supports
VLAN port mirroring options.

### Caveats

1. VM Traffic that is sent to another VM running on the same compute
node will not be mirrored when NIC assisted mirroring is chosen.

2. Traffic coming in from the fabric interface will not be mirrored.

3. When a VLAN interface is used as the fabric interface, traffic will be
tagged first with the NIC assisted mirroring VLAN followed by the VLAN tag
on the fabric interface (NIC assisted mirroring VLAN will be the inner tag and
fabric interface VLAN will be the outer tag).

## 3.1 Alternatives considered
None

## 3.2 API schema changes
The mirroring action will have an additional option to configure NIC assisted
mirroring and the vlan tag to be used when this is enabled.

## 3.3 User workflow impact
The NIC needs to be programmed for VLAN port mirroring. While configuring
mirroring in Contrail, user can indicate NIC assisted mirroring with the VLAN
while specifying mirror action.

## 3.4 UI changes
Contrail UI supports mirroring configuration in the Ports page and in the
Policies page. Here, the additional flag for NIC assisted mirroring and vlan
tag would have to be taken from user.

## 3.5 Notification impact
None

# 4. Implementation
## 4.1 Work items
* Change schema with the new options.
* Change vrouter-agent to take the new configuration options and program the
  vrouter.
* Change vrouter to vlan tag the packet and send it to the NIC and not do
  mirroring when this option is chosen.

# 5. Performance and scaling impact
## 5.1 API and control plane
None

## 5.2 Forwarding performance
With this option, vrouter througput should not be impacted whether or not
mirroring is configured.

# 6. Upgrade
No impact. Existing mirror configurations will have NIC assisted mirroring
option disabled by default.

# 7. Deprecations
None

# 8. Dependencies
None

# 9. Testing
## 9.1 Unit tests
Vrouter-agent unit tests to have options where NIC assisted mirroring is
configured and unconfigured, to check the operational data reflects the
configuration changes and to check that kernel sync is updated appropriately.

## 9.2 Dev tests
* Enable port mirroring with NIC assisted mirroring and check that NIC receives
  vlan tagged packet and it mirrors the packet based on the configuration.
* Disabled port mirroring and check that normal traffic forwarding happens.
* Similar tests with policy based mirroring.
* Check that mirroring without NIC assisted option works fine.

## 9.3 System tests
* End to end tests for port based mirroring, policy based mirroring.
* Mirroring regression tests.
* UI tests to check all mirroring options including the new ones.
* Check performance impact, that vrouter througput is maintained with this
  mirroring.

# 10. Documentation Impact
Mirroring documentation to explain the new mode and options.

# 11. References
https://github.com/Juniper/contrail-controller/wiki/Mirroring
http://www.intel.in/content/dam/www/public/us/en/documents/datasheets/82599-10-gbe-controller-datasheet.pdf
