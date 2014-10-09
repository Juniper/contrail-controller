/**
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

package net.juniper.contrail.vcenter;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.URL;
import java.util.TreeMap;
import java.util.SortedMap;
import java.util.UUID;

import org.apache.log4j.Logger;

import com.vmware.vim25.DVPortSetting;
import com.vmware.vim25.DVPortgroupConfigInfo;
import com.vmware.vim25.GuestInfo;
import com.vmware.vim25.GuestNicInfo;
import com.vmware.vim25.IpPool;
import com.vmware.vim25.IpPoolIpPoolConfigInfo;
import com.vmware.vim25.ManagedObjectReference;
import com.vmware.vim25.NetIpConfigInfo;
import com.vmware.vim25.NetIpConfigInfoIpAddress;
import com.vmware.vim25.NetworkSummary;
import com.vmware.vim25.VMwareDVSPortSetting;
import com.vmware.vim25.VirtualDevice;
import com.vmware.vim25.VirtualDeviceBackingInfo;
import com.vmware.vim25.VirtualEthernetCard;
import com.vmware.vim25.VirtualEthernetCardDistributedVirtualPortBackingInfo;
import com.vmware.vim25.VirtualMachineConfigInfo;
import com.vmware.vim25.VirtualMachinePowerState;
import com.vmware.vim25.VirtualMachineRuntimeInfo;
import com.vmware.vim25.VmwareDistributedVirtualSwitchPvlanSpec;
import com.vmware.vim25.VmwareDistributedVirtualSwitchVlanSpec;
import com.vmware.vim25.VMwareDVSConfigInfo;
import com.vmware.vim25.VMwareDVSPvlanMapEntry;
import com.vmware.vim25.mo.Datacenter;
import com.vmware.vim25.mo.DistributedVirtualPortgroup;
import com.vmware.vim25.mo.Folder;
import com.vmware.vim25.mo.HostSystem;
import com.vmware.vim25.mo.InventoryNavigator;
import com.vmware.vim25.mo.IpPoolManager;
import com.vmware.vim25.mo.Network;
import com.vmware.vim25.mo.ServiceInstance;
import com.vmware.vim25.mo.VirtualMachine;
import com.vmware.vim25.mo.VmwareDistributedVirtualSwitch;

public class VCenterDB {
    private static final Logger s_logger =
            Logger.getLogger(VCenterDB.class);
    private static final String contrailDvSwitchName = "dvSwitch";
    private static final String contrailDataCenterName = "Datacenter";
    private static final String contrailVRouterVmNamePrefix = "contrailVM";
    private final String vcenterUrl;
    private final String vcenterUsername;
    private final String vcenterPassword;
    
    private ServiceInstance serviceInstance;
    private Folder rootFolder;
    private InventoryNavigator inventoryNavigator;
    private IpPoolManager ipPoolManager;
    
    public VCenterDB(String vcenterUrl, String vcenterUsername,
            String vcenterPassword) {
        this.vcenterUrl = vcenterUrl;
        this.vcenterUsername = vcenterUsername;
        this.vcenterPassword = vcenterPassword;
    }
    
    public void Initialize() throws Exception {
        // Connect to VCenter
        serviceInstance = new ServiceInstance(new URL(vcenterUrl),
                vcenterUsername, vcenterPassword, true);
        rootFolder = serviceInstance.getRootFolder();
        inventoryNavigator = new InventoryNavigator(rootFolder);
        ipPoolManager = serviceInstance.getIpPoolManager();
    }
    
    private static IpPool getIpPool(
            DistributedVirtualPortgroup portGroup, IpPool[] ipPools) {
        NetworkSummary summary = portGroup.getSummary();
        Integer poolid = summary.getIpPoolId();
        if (poolid == null) {
            s_logger.info("dvPg: " + portGroup.getName() + 
                    " IpPool NOT configured");
            return null;
        }
        // Validate that the IpPool id exists
        for (IpPool pool : ipPools) {
            if (pool.id == poolid.intValue()) {
                return pool;
            }
        }
        s_logger.error("dvPg: " + portGroup.getName() + 
                " INVALID IpPoolId " + poolid);
        return null;
    }
    
    private static short getIsolatedVlanId(String dvPgName, DVPortSetting portSetting) {
        if (portSetting instanceof VMwareDVSPortSetting) {
            VMwareDVSPortSetting vPortSetting = 
                    (VMwareDVSPortSetting) portSetting;
            VmwareDistributedVirtualSwitchVlanSpec vlanSpec = 
                    vPortSetting.getVlan();
            if (vlanSpec instanceof VmwareDistributedVirtualSwitchPvlanSpec) {
                VmwareDistributedVirtualSwitchPvlanSpec pvlanSpec = 
                        (VmwareDistributedVirtualSwitchPvlanSpec) vlanSpec;
                return (short)pvlanSpec.getPvlanId();
            } else {
                s_logger.error("dvPg: " + dvPgName + 
                        " port setting: " +  vPortSetting + 
                        ": INVALID vlan spec: " + vlanSpec);
                return -1;
            }
        } else {
            s_logger.error("dvPg: " + dvPgName + 
                    " INVALID port setting: " + portSetting);
            return -1;
        }
    }
    
    private static String getVirtualMachineMacAddress(
            VirtualMachineConfigInfo vmConfigInfo,
            DistributedVirtualPortgroup portGroup) {
        VirtualDevice devices[] = vmConfigInfo.getHardware().getDevice();
        for (VirtualDevice device : devices) {
            // XXX Assuming only one interface
            if (device instanceof VirtualEthernetCard) {
                VirtualDeviceBackingInfo backingInfo = 
                        device.getBacking();
                // Is it backed by the distributed virtual port group? 
                if (backingInfo instanceof 
                    VirtualEthernetCardDistributedVirtualPortBackingInfo) {
                    VirtualEthernetCardDistributedVirtualPortBackingInfo
                    dvpBackingInfo = 
                    (VirtualEthernetCardDistributedVirtualPortBackingInfo)
                    backingInfo;
                    if (dvpBackingInfo.getPort().getPortgroupKey().
                            equals(portGroup.getKey())) {
                        String vmMac = ((VirtualEthernetCard) device).
                                getMacAddress();
                        return vmMac;
                    }
                }
            } 
        }
        s_logger.error("dvPg: " + portGroup.getName() + " vmConfig: " + 
                vmConfigInfo + " MAC Address NOT found");
        return null;
    }
    
    private String getVirtualMachineIpAddress(String dvPgName,
            String hostName, HostSystem host, String vmNamePrefix) 
                    throws Exception {
        VirtualMachine[] vms = host.getVms();
        for (VirtualMachine vm : vms) {
            String vmName = vm.getName();
            if (!vmName.toLowerCase().contains(vmNamePrefix.toLowerCase())) {
                continue;
            }
            // XXX Assumption here is that VMware Tools are installed
            // and IP address is available
            GuestInfo guestInfo = vm.getGuest();
            if (guestInfo == null) {
                s_logger.info("dvPg: " + dvPgName + " host: " + hostName +
                        " vm:" + vmName + " GuestInfo - VMware Tools " +
                        " NOT installed");
                continue;
            }
            GuestNicInfo[] nicInfos = guestInfo.getNet();
            if (nicInfos == null) {
                s_logger.info("dvPg: " + dvPgName + " host: " + hostName +
                        " vm:" + vmName + " GuestNicInfo - VMware Tools " +
                        " NOT installed");
                continue;
            }
            for (GuestNicInfo nicInfo : nicInfos) {
                // Extract the IP address associated with simple port 
                // group. Assumption here is that Contrail VRouter VM will
                // have only one standard port group
                String networkName = nicInfo.getNetwork();
                if (networkName == null) {
                    continue;
                }
                Network network = (Network) 
                        inventoryNavigator.searchManagedEntity("Network",
                                networkName);
                if (network == null) {
                    s_logger.info("dvPg: " + dvPgName + "host: " + 
                            hostName + " vm: " + vmName + " network: " + 
                            networkName + " NOT found");
                    continue;
                }
                if (network instanceof DistributedVirtualPortgroup) {
                    s_logger.info("dvPg: " + dvPgName + "host: " + 
                            hostName + "vm: " + vmName + " network: " +
                            networkName + " is distributed virtual port " +
                            "group");
                    continue;
                }
                NetIpConfigInfo ipConfigInfo = nicInfo.getIpConfig();
                if (ipConfigInfo == null) {
                    s_logger.error("dvPg: " + dvPgName + "host: " + 
                            hostName + " vm: " + vmName + " network: " + 
                            networkName + " IP config info NOT present");
                    continue;
                }
                NetIpConfigInfoIpAddress[] ipAddrConfigInfos = 
                        ipConfigInfo.getIpAddress();
                if (ipAddrConfigInfos == null || 
                        ipAddrConfigInfos.length == 0) {
                    s_logger.error("dvPg: " + dvPgName + "host: " + 
                            hostName + " vm: " + vmName + " network: " + 
                            networkName + " IP addresses NOT present");
                    continue;

                }
                for (NetIpConfigInfoIpAddress ipAddrConfigInfo : 
                    ipAddrConfigInfos) {
                    String ipAddress = ipAddrConfigInfo.getIpAddress();
                    // Choose IPv4 only
                    InetAddress ipAddr = InetAddress.getByName(ipAddress);
                    if (ipAddr instanceof Inet4Address) {
                        return ipAddress;
                    }
                }

            }
        }
        return null;
    }
    
    private static boolean doIgnoreVirtualMachine(String vmName) {
        // Ignore contrailVRouterVMs since those should not be reflected in
        // Contrail VNC
        if (vmName.toLowerCase().contains(
                contrailVRouterVmNamePrefix.toLowerCase())) {
            return true;
        }
        return false;
    }
    
    private SortedMap<String, VmwareVirtualMachineInfo> 
        populateVirtualMachineInfo(
                DistributedVirtualPortgroup portGroup) throws Exception {
        String dvPgName = portGroup.getName();
        // Get list of virtual machines connected to the port group
        VirtualMachine[] vms = portGroup.getVms();
        if (vms == null || vms.length == 0) {
            s_logger.info("dvPg: " + dvPgName + 
                    " NO virtual machines connected");
            return null;
        }
        SortedMap<String, VmwareVirtualMachineInfo> vmInfos = 
                new TreeMap<String, VmwareVirtualMachineInfo>();
        for (VirtualMachine vm : vms) {
            // Name
            String vmName = vm.getName();
            // Ignore virtual machine?
            if (doIgnoreVirtualMachine(vmName)) {
                s_logger.debug("dvPg: " + dvPgName + 
                        " Ignoring vm: " + vmName);
                continue;
            }
            // Is it powered on?
            VirtualMachineRuntimeInfo vmRuntimeInfo = vm.getRuntime();
            VirtualMachinePowerState powerState = 
                    vmRuntimeInfo.getPowerState();
            if (powerState != VirtualMachinePowerState.poweredOn) {
                s_logger.debug("dvPg: " + dvPgName + " Ignoring vm: " + 
                        vmName + " Power State: " + powerState);
                continue;
            }

            // Extract configuration info
            VirtualMachineConfigInfo vmConfigInfo = vm.getConfig();
            // Extract MAC address
            String vmMac = getVirtualMachineMacAddress(vmConfigInfo,
                    portGroup);
            if (vmMac == null) {
                s_logger.error("dvPg: " + dvPgName + " vm: " + 
                        vmName + " MAC Address NOT found");
                continue;
            }
            // Get instance UUID
            String instanceUuid = vmConfigInfo.getInstanceUuid();
            // Get host information
            ManagedObjectReference hmor = vmRuntimeInfo.getHost();
            HostSystem host = new HostSystem(
                vm.getServerConnection(), hmor);
            String hostName = host.getName();
            // Get Contrail VRouter virtual machine information from the host
            String vrouterIpAddress = getVirtualMachineIpAddress(dvPgName,
                    hostName, host, contrailVRouterVmNamePrefix);
            if (vrouterIpAddress == null) {
                s_logger.error("dvPg: " + dvPgName + " vm: " + vmName + 
                        " host: " + hostName + " Contrail VRouter VM: " + 
                        contrailVRouterVmNamePrefix + " NOT found");
            }
            VmwareVirtualMachineInfo vmInfo = new 
                    VmwareVirtualMachineInfo(vmName, hostName, 
                            vrouterIpAddress, vmMac); 
            vmInfos.put(instanceUuid, vmInfo);
        }
        if (vmInfos.size() == 0) {
            return null;
        }
        return vmInfos;
    }
    
    private static boolean doIgnoreVirtualNetwork(DVPortSetting portSetting) {
        // Ignore dvPgs that do not have PVLAN configured
        if (portSetting instanceof VMwareDVSPortSetting) {
            VMwareDVSPortSetting vPortSetting = 
                    (VMwareDVSPortSetting) portSetting;
            VmwareDistributedVirtualSwitchVlanSpec vlanSpec = 
                    vPortSetting.getVlan();
            if (vlanSpec instanceof VmwareDistributedVirtualSwitchPvlanSpec) {
                return false;
            }
        }
        return true;
    }
    
    public SortedMap<String, VmwareVirtualNetworkInfo> 
        populateVirtualNetworkInfo() throws Exception {
        // Search contrailDvSwitch
        VmwareDistributedVirtualSwitch contrailDvs = 
                (VmwareDistributedVirtualSwitch) 
                inventoryNavigator.searchManagedEntity(
                        "VmwareDistributedVirtualSwitch",
                        contrailDvSwitchName);
        if (contrailDvs == null) {
            s_logger.error("dvSwitch: " + contrailDvSwitchName + 
                    " NOT configured");
            return null;
        }
        // Extract distributed virtual port groups 
        DistributedVirtualPortgroup[] dvPgs = contrailDvs.getPortgroup();
        if (dvPgs == null || dvPgs.length == 0) {
            s_logger.error("dvSwitch: " + contrailDvSwitchName + 
                    " Distributed portgroups NOT configured");
            return null;
        }
        // Extract IP Pools
        Datacenter contrailDC = (Datacenter) inventoryNavigator.
                searchManagedEntity(
                "Datacenter",
                contrailDataCenterName);
        IpPool[] ipPools = ipPoolManager.queryIpPools(contrailDC);
        if (ipPools == null || ipPools.length == 0) {
            s_logger.error("dvSwitch: " + contrailDvSwitchName +
                    " Datacenter: " + contrailDC.getName() + " IP Pools NOT " +
                    "configured");
            return null;
        }

        // Extract private vlan entries for the virtual switch
        VMwareDVSConfigInfo dvsConfigInfo = (VMwareDVSConfigInfo) contrailDvs.getConfig();
        if (dvsConfigInfo == null) {
            s_logger.error("dvSwitch: " + contrailDvSwitchName +
                    " Datacenter: " + contrailDC.getName() + " ConfigInfo " +
                    "is empty");
            return null;
        }

        if (!(dvsConfigInfo instanceof VMwareDVSConfigInfo)) {
            s_logger.error("dvSwitch: " + contrailDvSwitchName +
                    " Datacenter: " + contrailDC.getName() + " ConfigInfo " +
                    "isn't instanceof VMwareDVSConfigInfo");
            return null;
        }

        VMwareDVSPvlanMapEntry[] pvlanMapArray = dvsConfigInfo.getPvlanConfig();
        if (pvlanMapArray == null) {
            s_logger.error("dvSwitch: " + contrailDvSwitchName +
                    " Datacenter: " + contrailDC.getName() + " Private VLAN NOT" +
                    "configured");
            return null;
        }

        // Populate VMware Virtual Network Info
        SortedMap<String, VmwareVirtualNetworkInfo> vnInfos =
                new TreeMap<String, VmwareVirtualNetworkInfo>();
        for (DistributedVirtualPortgroup dvPg : dvPgs) {
            s_logger.info("dvPg: " + dvPg.getName());
            // Extract dvPg configuration info and port setting
            DVPortgroupConfigInfo configInfo = dvPg.getConfig();
            DVPortSetting portSetting = configInfo.getDefaultPortConfig();
            // Ignore network?
            if (doIgnoreVirtualNetwork(portSetting)) {
                continue;
            }
            // Find associated IP Pool
            IpPool ipPool = getIpPool(dvPg, ipPools);
            if (ipPool == null) {
                s_logger.info("no ip pool is associated to dvPg: " + dvPg.getName());
                continue;
            }
            byte[] vnKeyBytes = dvPg.getKey().getBytes();
            String vnUuid = UUID.nameUUIDFromBytes(vnKeyBytes).toString();
            String vnName = dvPg.getName();
            s_logger.info("VN name: " + vnName);
            IpPoolIpPoolConfigInfo ipConfigInfo = ipPool.getIpv4Config();

            // Find associated isolated secondary VLAN Id
            short isolatedVlanId = getIsolatedVlanId(dvPg.getName(), portSetting);

            // Find primaryVLAN corresponsing to isolated secondary VLAN
            short primaryVlanId = 0;
            for (short i=0; i < pvlanMapArray.length; i++) {

                if ((short)pvlanMapArray[i].getSecondaryVlanId() != isolatedVlanId)
                    continue;
                if (!pvlanMapArray[i].getPvlanType().equals("isolated"))
                    continue;
                s_logger.info("    PvlanType = " + pvlanMapArray[i].getPvlanType() + " PrimaryVLAN = " + pvlanMapArray[i].getPrimaryVlanId() + " IsolatedVLAN = " + pvlanMapArray[i].getSecondaryVlanId());
                primaryVlanId = (short)pvlanMapArray[i].getPrimaryVlanId();
            }

            // Populate associated VMs
            SortedMap<String, VmwareVirtualMachineInfo> vmInfo = 
                    populateVirtualMachineInfo(dvPg);
            VmwareVirtualNetworkInfo vnInfo = new
                    VmwareVirtualNetworkInfo(vnName, isolatedVlanId, 
                            primaryVlanId, vmInfo,
                            ipConfigInfo.getSubnetAddress(),
                            ipConfigInfo.getNetmask(),
                            ipConfigInfo.getGateway());
            vnInfos.put(vnUuid, vnInfo);
        }
        return vnInfos;
    }   
}
