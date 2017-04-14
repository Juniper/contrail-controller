Compute provisioning package.
=============================

## Overview
The package contains the code for provisioning contrail compute services. `contrail-compute-setup` is the entrypoint script used to provision contrail compute.

This python package is packaged as part of contrail-vrouter-agent package, So in a compute where contrail-vrouter-agent package is installed one can issue the following command to get help about the contrail-compute-setup and supported arguments.

```sh
$ contrail-compute-setup --help
```

## Supported modes
Following modes of contrail vrouter provisioning are supported.

* Kernel mode
* dpdk mode

###  kernel mode
Following is the commandline with mandatory argument to provision vrouter in kernel mode.

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

### dpdk mode
Following is the commandline with mandatory argument to provision vrouter in dpdk mode.

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

### Filing Bugs
Use http://bugs.launchpad.net/juniperopenstack
It will be useful to include the log file in the bug, log files will be created at `/var/log/contrail/contrail_vrouter_provisioning.log` in the compute node.

### Queries
Mail to
dev@lists.opencontrail.org,
users@lists.opencontrail.org

### IRC
opencontrail on freenode.net
