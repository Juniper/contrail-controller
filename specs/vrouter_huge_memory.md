
# 1. Introduction

Vrouter Kernel Module allocates huge pieces of memory for Flow tables
and Bridge Tables. Being Hash Tables the memory for these need to
contiguous to hash into the buckets. Vrouter currently uses Btables to
divide the allocation of memory into multiple chunks of 4MB. Inspite of
this, the allocation can still fail in a running system putting the
constraint of allocating the memory immditely after the bootup. This
decreases the possibility of memory allocation failure but losing the
flexibility of inserting the Vrouter module when ever desired. 

# 2. Problem statement

As mentioned in the "Introduction" Vrouter Kernel Module needs to be
inserted immediately after bootup to decrease the memory allocation
failure. This is a big constraint and does not allow incremental
updates, Bug Fixes to be tested on fly. Compute node needs to be
rebooted every time there is a change in the Vrouter. The requirement is
to remove this constraint of inserting the module soon after the bootup.

# 3. Proposed solution

As it is a hard requirement to have contiguous memory to maintain the
Hash tables, the proposed solution is to make the memory available to
Vrouter module. This can be achieved using the HugePages of Linux. Idea
is to allocate huge pages of size 1G in the linux. Once the Linux boots
up with the required persistent huge pages, Vrouter kernel module makes
use of this huge pages for allocating memory for Flow ad Bridge Tables. 
The allocation is similar to Dpdk forwarding except that the Dpdk is in
user space and here the HugePage allocation and usage is in the kernel
space. There is going to be some impact in the way Agent mmaps this
memory for Flow and Bridge table aging. This needs to be further studied
and impcated changes need to be identified to have minimal changes in
Agent space.

## 3.1 Alternatives considered

Currently no alternatives are considered for the proposed solution

## 3.2 API schema changes

As this is Vrouter chage, there is no impact on API schema

## 3.3 User workflow impact

Users must be able to insert the Vrouter module as and when desired as
against the boot time. Other than this impact, there is no affect on the
user experience.

## 3.4 UI changes

There is no change in the UI 

## 3.5 Notification impact

There will be logs in the kernel space and UVE from Agent if Vrouter
fails to makes use of Huge pages or if huge pages are not setup at the
time of booting. 

# 4. Implementation
## 4.1 Work items

Changes are going to be in Vrouter provisioning scripts, Vrouter kernel
module and possibly in Vrouter Agent module. The impact of mmap() of
Flow and Bridge  tables to Agent space for aging etc, need to be
studied once the huge tables are used. 

# 5. Performance and scaling impact
## 5.1 API and control plane

There is no impact on the API and control plane

## 5.2 Forwarding performance

There is not going to be any impcast on the forwarding of the packets
per se. 

# 6. Upgrade

Upgrade of Vrouter module from non Huge pages to Huge pages need to be
handled, so that user does not see any change in the flow and bridge
table handling.  

#### Schema migration/transition

# 7. Deprecations
#### If this feature deprecates any older feature or API then list it here.

# 8. Dependencies
#### Describe dependent features or components.

# 9. Testing
## 9.1 Unit tests
## 9.2 Dev tests
## 9.3 System tests

# 10. Documentation Impact

# 11. References
