
#1. Introduction
==================
This blueprint describes high level changes required to enhance Contrail vRouter 'vifdump' tool (like tcpdump) for analyzing packets on individual member links of a bond interface configured on a DPDK based vRouter.


#2. Problem Statement
==================
The Kernel level tools, like tcpdump, do not work when vRouter is deployed in DPDK mode, since the Linux network stack is bypassed. There exist already a tcpdump like command (i.e. vifdump), which works on the bond interface but it can't be used to monitor its slave/member interfaces; so there is no way to debug each slave individually.


#3. Proposed solution
==================
List of changes in vRouter application to enhance 'vifdump' tool for monitoring slave interaces are described below.

##3.1 'vif --list' command output
--------
a. To display slave interfaces attached to a bond interface:
--------
With slave interfaces also visible in command output, user will be able to specify a slave interface as interface to be monitored in 'vifdump' command.

Slave interfaces shall be shown as follows:

    vif0/1/slave3     Bond.slave: vif0/1/slave3 PCI: 0000:00:00.0
                      Type:Physical HWaddr:00:11:ac:1d:c6:1c IPaddr:0

    vif0/1/slave4     Bond.slave: vif0/1/slave4 PCI: 0000:00:00.1
                      Type:Physical HWaddr:00:11:ac:1d:c6:1d IPaddr:0

b. To display the monitoring interface opened for monitoring slave interface:
--------
Monitoring interfaces for slaves shall be shown as follows:

    vif0/4348/slave3  Monitoring: mon1/slave3 for vif0/1/slave3
                      Type:Monitoring HWaddr:00:11:ac:1d:c6:1c IPaddr:0

    vif0/4347/slave4  Monitoring: mon1/slave4 for vif0/1/slave4
                      Type:Monitoring HWaddr:00:11:ac:1d:c6:1d IPaddr:0


##3.2 Changes in "vifdump" to add/delete the monitoring interfaces for the slaves
--------
Most of the current logic in this script shall work as it is even when monitored interface is a slave interface of a bond interface. However due to the fact that there is no vif-id and corresponding vif context in vrouter for the slave interfaces therefore the naming convention for slave interfaces had to be improvised accordingly.

a. Add monitoring interface for a slave interface:
--------
Vifdump shall invoke following vif command to add the monitoring interface for a slave (for ex. to add monitoring for a slave vif0/1/slave3):

    => vifdump -i vif0/1/slave3 [tcpdump_arguments]

for above command, vifdump invokes following vif command:

    => vif --add mon1/slave3 --type monitoring --vif 1 --id 4348

b. Delete monitoring interface created for a slave interface:
--------
Vifdump shall invoke following vif command to delete the monitoring interface for a slave (for ex. to delete monitoring for a slave vif0/1/slave3):

    => vifdump stop vif0/4348/slave3

for above command, vifdump invokes following vif command:

    => vif --delete 4348


##3.3  Monitoring configuration
--------
In current handling of 'vifdump' command, Vif interface manager sends 'vr_interface_req' sandesh message to vRouter to add the interface for monitoring, and vRouter DPDK driver then creates a mirror interface (i.e. KNI/tuntap) towards the kernel.

vRouter DPDK driver logic shall be modified to check if the monitored interface is a slave-interface. If not, then existing handling will be used, i.e. enable the monitoring for the interface by marking it in vr_dpdk.monitorings[] array and set its VIF_FLAG_MONITORED flag to true. If yes, a new logic shall be added, which is as follows.

a. Add an entry in a new global array 'slave_monitorings[]' which will maintain the vif-id of the monitoring interface indexed on the ethernet port-id of the slave port. This is done this way because there is no vif-context in vrouter for the slave interface.

b. Call 'rte_eth_add_tx_callback' and 'rte_eth_add_rx_callback' APIs of DPDK to register RX and TX call-back functions for the specified slave interface. This call-back function shall be called by DPDK library whenever a packet is transmitted or received on the slave interface, and the callback function will perform mirroring of the packet data onto the KNI/Tuntap interface towards the kernel.


##3.4  Transmitted Packet Monitoring
--------
a. As per existing handling vRouter application calls the rte_port_ethdev_writer_tx API of DPDK to transmit the packet over a bond interface. This API in turn calls rte_eth_tx_burst API for the bond interface.

b. DPDK PMD bond driver then decides which slave interface to use for transmitting the package based on the configured bonding mode. Finally for the selected slave interface, rte_eth_tx_burst API is called.

c. When rte_eth_tx_burst API is called for a slave interface which was being monitored then it would have a 'pre_tx_burst_cbs' callback registered as explained above. The pre_tx_burst_cbs function will mirror the packet towards the KNI/Tuntap mirror interface in same way as currently done in 'dpdk_if_tx' function.


##3.5   Received Packet Monitoring
--------
a. As per existing handling, DPDK PMD bond driver calls 'rte_eth_rx_burst' API for each of the slave interfaces of a bond. The slave which was being monitored would have a 'post_rx_burst_cbs' callback registered as explained above.

b. The 'post_rx_burst_cbs' function will mirror the packet towards the KNI/Tuntap mirror interface in same way as currently done in 'dpdk_if_rx' function.



##3.6 Alternatives Design
--------
As Rx port (slave port) information is not modified by DPDK bonding driver while handling over collected packet to application, so it is possible to mirror received packet on the basis of slave port on which it has been received without having to register a 'post_rx_burst_cbs' for slave interface.

In current handling, vRouter application on receiving the packet from DPDK PMD bond driver (i.e. vr_dpdk_lcore_vroute and dpdk_if_rx functions) does the mirroring for the ports which are listed in vr_dpdk.monitorings[] array and have their VIF_FLAG_MONITORED flag set to true.

In the same functions, vRouter could check if the interface for which packet is received from DPDK PMD bond driver, is configured for monitoring or not. If yes, then it could do the mirroring of the received packet towards the KNI/Tuntap interface.

However as there is no vif-context in vrouter for slave interface therefore there was unnecessary complexity in maintaining the monitoring status for the slave interface. Some new mechanism would have been required which would have more code level impacts

In comparison, as per chosen approach of registering the RX callback function, automatically if a slave port is being monitored and there is some data received on that slave port then call back will get invoked wherein the packet can be mirrored directly without having to make any checks etc.

Pros :  This would have avoided slight overhead to have a call-back function getting called for each receive operation.

Cons :  Not aligned to the approach followed in transmit path. Moreover this has more impact to code


##3.7 API schema changes
--------
None


##3.8 User workflow impact
--------
a. Impact to 'vif' command syntax:
--------
    --list (Display all of the interfaces of which the vrouter is aware.)
         The output shall also display all the slave/member interfaces for a bond interface.

    --Get (Displays a specific interface)
         The get option shall also accept slave/member interfaces for a bond interface.

Slave interfaces shall be shown as follows:

    vif0/1/slave3     Bond.slave: vif0/1/slave3 PCI: 0000:00:00.0
                      Type:Physical HWaddr:00:11:ac:1d:c6:1c IPaddr:0

    vif0/1/slave4     Bond.slave: vif0/1/slave4 PCI: 0000:00:00.1
                      Type:Physical HWaddr:00:11:ac:1d:c6:1d IPaddr:0

Monitoring interfaces for slaves shall be shown as follows:

    vif0/4348/slave3  Monitoring: mon1/slave3 for vif0/1/slave3
                      Type:Monitoring HWaddr:00:11:ac:1d:c6:1c IPaddr:0

    vif0/4347/slave4  Monitoring: mon1/slave4 for vif0/1/slave4
                      Type:Monitoring HWaddr:00:11:ac:1d:c6:1d IPaddr:0


b. Impact to 'vifdump' command syntax:
--------
There is no change to the 'vifdump' command syntax as such. However, now the user can also specify slave interface as vif to start or stop the monitoring on it.

    vRouter/DPDK vif tcmpdump script
    Usage:
        vifdump [-i] <vif> [tcpdump arguments]
            - to run tcpdump on a specified vif

        vifdump stop <monitoring_if>
            - to force stop and clean up the monitoring interface

    Example:
        vifdump -i vif0/1 -nvv

To Add monitoring interface for a slave interface: Vifdump shall invoke following vif command to add the monitoring interface for a slave (for ex. to add monitoring for a slave vif0/1/slave3):

    => vifdump -i vif0/1/slave3 [tcpdump_arguments]

To delete monitoring interface created for a slave interface: Vifdump shall invoke following vif command to delete the monitoring interface for a slave (for ex. to delete monitoring for a slave vif0/1/slave3):

    => vifdump stop vif0/4348/slave3


##3.9 UI Changes
--------
None


##3.10 Operations and Notification
--------
Refer to section #3.8


#4. Implementation
==================
##4.1 Assignee(s)
--------
##4.2 Work Items
--------
a. Modification in vif interface manager to for displaying slave interfaces as well as any monitoring interfaces created for them  in 'vif -- list' and 'vif --get' commands

b. Changes in 'contrail-vrouter/utils/vifdump' shell script to account for the naming convention for slave interface. Slave interfaces shall be named as 'vif0/<bond_vif_id>/slave<slave_eth_port_id>'

c. Modifications in vRouter code for monitoring configuration; use 'rte_eth_add_tx_callback' and 'rte_eth_add_rx_callback' functions to register a 'pre_tx_burst_cbs' and 'post_rx_burst_cbs' for the monitored slave interface whenever vif interface manager asks to add a monitoring interface for a slave interface of a bond.

d. Modifications in vRouter code to perform the mirroring of Tx packet in the above call-back function towards the KNI/Tuntap interface

e. Modifications in vRouter code to perform the mirroring of Rx packet received from DPDK PMD bond driver towards the KNI/Tuntap interface

Refer to section 3 for more details.


#5. Performance and scaling impact
==================
vRouter performance with mirroring configured on slave interface shall be same as vRouter performance when mirroring is configured on normal VIF interface.


##5.1 API and control plane
--------
None


##5.2 Forwarding Performance
--------
None


#6. Upgrade
==================
None


#7. Deprecations
==================
None


#8. Dependencies
==================
None


#9. Testing
==================
##9.1 Unit tests
--------
##9.2 Dev tests
--------
##9.3 System tests
--------
Proposed test-setup
--------
1. To be tested with KVM hypervisor on compute node
2. To be tested with 10GBE NIC with PMD/Vhost interface on DPDK
3. To be tested for different LAG distribution modes


#10. Documentation Impact
==================
Modifications will be needed to Contrail-feature-guide document to update the syntax and semantics for 'vif' and 'vifdump' commands


#11. References
==================
1. DPDK Link Bonding Poll Mode Driver Library
http://dpdk.org/doc/guides/prog_guide/link_bonding_poll_mode_drv_lib.html

2. DPDK Kernel NIC Interface
http://dpdk.org/doc/guides-16.04/prog_guide/kernel_nic_interface.html

3. Contrail Feature Guide
