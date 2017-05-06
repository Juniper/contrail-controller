Compute provisioning package.
=============================

## Overview
The package contains the code for provisioning contrail compute services. `contrail-compute-setup` is the entrypoint script used to provision contrail compute.

This python package is packaged as part of `contrail-vrouter-agent` package, So in a compute where `contrail-vrouter-agent` package is installed one can issue the following command to get help about the `contrail-compute-setup` and supported arguments.

```sh
$ contrail-compute-setup --help
```

## Supported modes
Following modes of contrail vrouter provisioning are supported.

* Kernel mode
* DPDK mode
* SRIOV mode

###  kernel mode
Commandline with mandatory argument to provision vrouter in kernel mode follows,

```sh
$ contrail-compute-setup --self_ip 5.5.5.1\
                         --hypervisor libvirt\
                         --cfgm_ip 2.2.2.1\
                         --collectors 3.3.3.1 3.3.3.2 3.3.3.3\
                         --control-nodes 4.4.4.1 4.4.4.2 4.4.4.3\
                         --keystone_ip 1.1.1.1\
                         --keystone_auth_protocol http\
                         --keystone_auth_port 35357\
                         --keystone_admin_user admin\
                         --keystone_admin_password contrail123\
                         --keystone_admin_tenant_name admin
```

The  script does the following,

* Disable selinux
* Disable iptables
* Configure coredump
* Add's /dev/net/tun in cgroup device acl
* Configure `contrail-vrouter-agent.conf`
* Configure vhost0 interface with the configs of the present in physical interface.
* Configure `contrail-vrouter-nodemgr.conf`
* Configure `contrail-lbaas-auth.conf`
* Enable agent services
* Create virtual-router object for the compute in api-server (Optional based on the `--register` flag)

### DPDK mode
Commandline with mandatory argument to provision vrouter in DPDK mode follows,

```sh
$ contrail-compute-setup --self_ip 5.5.5.1\
                         --hypervisor libvirt\
                         --cfgm_ip 2.2.2.1\
                         --collectors 3.3.3.1 3.3.3.2 3.3.3.3\
                         --control-nodes 4.4.4.1 4.4.4.2 4.4.4.3\
                         --keystone_ip 1.1.1.1\
                         --keystone_auth_protocol http\
                         --keystone_auth_port 35357\
                         --keystone_admin_user admin\
                         --keystone_admin_password contrail123\
                         --keystone_admin_tenant_name admin\
                         --dpdk coremask=0x3,huge_pages=50,uio_driver=uio_pci_generic
```

* The DPDK argument takes the following
    * `coremask` - Forwarding cores for vrouter
    * `huge_pages` - The percentage of memory that needs to be reserved for hugepages
    * `uio_driver` - This is optional and can take values of `uio_pci_generic`/`igb_uio`/`vfio-pci`. Default is `igb_uio`
* Internally the script does the following
    * Configures hugepages by adding an entry `vm.nr_hugepages` in /etc/sysctl.conf
    * Mounts the hugetlbfs in /hugepages
    * Configures the maximum number of memory map pages for qemu using `vm.max_map_count`

### SRIOV mode
Commandline with mandatory argument to provision vrouter in SRIOV mode follows,

```sh
$ contrail-compute-setup --self_ip 5.5.5.1\
                         --hypervisor libvirt\
                         --cfgm_ip 2.2.2.1\
                         --collectors 3.3.3.1 3.3.3.2 3.3.3.3\
                         --control-nodes 4.4.4.1 4.4.4.2 4.4.4.3\
                         --keystone_ip 1.1.1.1\
                         --keystone_auth_protocol http\
                         --keystone_auth_port 35357\
                         --keystone_admin_user admin\
                         --keystone_admin_password contrail123\
                         --keystone_admin_tenant_name admin\
                         --sriov "p6p1:7,p6p2:5"
```

* The SRIOV argument takes the following
    * Interface where the VF's needs to be created
    * Number of VF's needed for that interface
* SRIOV argument can work with DPDK argument also

### Filing Bugs
Use http://bugs.launchpad.net/juniperopenstack
It will be useful to include the log file in the bug, log files will be created at `/var/log/contrail/contrail_vrouter_provisioning.log` in the compute node.

### Queries
Mail to
dev@lists.opencontrail.org,
users@lists.opencontrail.org

### IRC
opencontrail on freenode.net
