
# 1. Introduction

Vrouter Kernel Module allocates huge pieces of memory for Flow Tables
and Bridge Tables. Being Hash Tables, the memory for these need to be
contiguous to hash into the buckets. Vrouter currently uses Btables to
divide the allocation of memory into multiple chunks of 4MB. In spite of
this, the allocation can still fail in a running system creating the
constraint of allocating the memory immediately after the bootup. This
decreases the possibility of memory allocation failure but loses the
flexibility of inserting the Vrouter module whenever desired.

# 2. Problem statement

As mentioned in the "Introduction" Vrouter Kernel Module needs to be
inserted immediately after bootup to decrease the memory allocation
failure. This is a big constraint and does not allow incremental
updates or Bug Fixes to be tested on fly. Compute node needs to be
rebooted every time there is a change in the Vrouter. The requirement is
to remove this constraint of inserting the module soon after the bootup.

# 3. Proposed solution

As it is a hard requirement to have contiguous memory to maintain the
Hash Tables, the proposed solution is to make the memory available to
Vrouter module. This can be achieved using the Huge Pages of Linux. The
idea is to allocate Huge Pages of size 1G in Linux. Once Linux boots
up with the required persistent Huge Pages, Vrouter kernel module makes
use of these Huge Pages for allocating memory for Flow and Bridge Tables.
The allocation is similar to DPDK forwarding except that the DPDK is in
user space and here the Huge Page allocation and usage is in the kernel
space. There is going to be some impact in the way Agent mmaps this
memory for Flow and Bridge Table aging. This needs to be further studied
and impacted changes need to be identified to have minimal changes in
Agent space.

## 3.1 Alternatives considered

Currently no alternatives have been considered for the proposed solution.

## 3.2 API schema changes

As this is Vrouter change, there is no impact on API schema.

## 3.3 User workflow impact

Users must be able to insert the Vrouter module as and when desired as
opposed to just at boot time. Other than this impact, there is no effect on
the user experience.

## 3.4 UI changes

There is no change in the UI.

## 3.5 Notification impact

There will be logs in the kernel space and UVE from Agent if Vrouter
fails to makes use of Huge Pages or if Huge Pages are not setup at the
time of booting.

# 4. Implementation
## 4.1 Work items

Changes are going to be in Vrouter provisioning scripts, Vrouter kernel
module and possibly in Vrouter Agent module. The impact of mmap() of
Flow and Bridge  tables to Agent space for aging etc, need to be
studied once the Huge Pages are used.

# 5. Performance and scaling impact
## 5.1 API and control plane

There is no impact on the API and control plane.

## 5.2 Forwarding performance

There is not going to be any impact on the forwarding of the packets.

# 6. Upgrade

Upgrade of Vrouter module from non Huge Pages to Huge Pages needs to be
handled so that the user does not see any change in the Flow and Bridge
Table handling.

#### Schema migration/transition

# 7. Deprecations
This change does not deprecate any features.

# 8. Dependencies
This change has no dependencies.

# 9. Testing
## 9.1 Unit tests
## 9.2 Dev tests
## 9.3 System tests

# 10. Documentation Impact
Installation documentation must be updated to indicate the need for Huge
Page support to be configured on the host OS.

# 11. References
