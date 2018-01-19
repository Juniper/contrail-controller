
# 1. Introduction
contrail-vrouter-dpdk binary implements the data-plane functionality when
contrail vrouter is run in dpdk mode which runs on compute node in a contrail
cluster.

# 2. Problem statement
Currently contrail-vrouter-dpdk runs on compute host and needs to run inisde a
docker container for ease of deployment.

# 3. Proposed solution

On compute host:
1.edit /etc/sysctl.conf and add below paramteres
a.vm.nr_hugepages = 48341
b.vm.max_map_count = 96682
c.kernel.core_pattern = /var/crashes/core.%e.%p.%h.%t
2.mkdir -p /hugepages
3.echo “hugetlbfs    /hugepages    hugetlbfs defaults      0       0 “ >> /etc/fstab
4.sudo mount -t hugetlbfs hugetlbfs /hugepages

On container:
1. Install pciutils
2. Install below packages in the container
a> contrail-vrouter-dpdk-init
b> contrail-vrouter-dpdk
c> contrail-vrouter-utils
d> python-opencontrail-vrouter-netns
e> python-contrail-vrouter-api
3./opt/contrail/bin/dpdk_nic_bind.py -b ixgbe 0000:02:00.0
4./opt/contrail/bin/dpdk_nic_bind.py -s
5.taskset 0xf  /usr/bin/contrail-vrouter-dpdk --no-daemon

## 3.1 Alternatives considered
None

## 3.2 API schema changes
Not Applicable

## 3.3 User workflow impact
contrail-vrouter-dpdk will run in container. 

## 3.4 UI changes
None

## 3.5 Notification impact
None

# 4. Implementation
## 4.1 Work items
1. Add docker file for centos and ubuntu to install required packages for
dpdk docker image, set path for coredump also start contrail-vrouter-dpdk process
with config knobs

2. Add entrypoint.sh script to build contrail-vrouter-dpdk.ini with config knobs
and use it as a configuration file for contrail-vrouter-dpdk binary,
Find the fabric interfaces and bind it to DPDK using dpdk_nic_bind.py,
Run contrail-compute-setup script to do host configuration.

3. Earlier agent config file was on host and was used by vrouter-functions.sh
for physical interface configuration now build it in entrypoint.sh . 

# 5. Performance and scaling impact
None

## 5.1 API and control plane
None

## 5.2 Forwarding performance
None

# 6. Upgrade
None

# 7. Deprecations
None

# 8. Dependencies
None

# 9. Testing
## 9.1 Unit tests
1. Check that relevant configuration options are parsed

## 9.2 Dev tests
1. Check provisioning updates the configuration files
2. Check fabric interfaces binding to dpdk
3. Check hugepage configuration 
4. Check for running status of contrail-vrouter-dpdk binary

## 9.3 System tests
1. Check compute node works well with dpdk running in container.

# 10. Documentation Impact
Dpdk configuration options need to be passed in common.env .

# 11. References
