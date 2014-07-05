/**
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

package net.juniper.contrail.vcenter;

import java.io.IOException;
import java.util.Map;
import java.util.Map.Entry;
import java.util.SortedMap;
import java.util.Iterator;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

import org.apache.log4j.BasicConfigurator;
import org.apache.log4j.Logger;

import net.juniper.contrail.api.types.VirtualMachine;
import net.juniper.contrail.api.types.VirtualMachineInterface;

class VmwareVirtualMachineInfo {
    private String hostName;
    private String vrouterIpAddress;
    private String macAddress;
    private String name;
    
    public VmwareVirtualMachineInfo(String name, String hostName, 
            String vrouterIpAddress, String macAddress) {
        this.name = name;
        this.hostName = hostName;
        this.vrouterIpAddress = vrouterIpAddress;
        this.macAddress = macAddress;
    }

    public String getHostName() {
        return hostName;
    }

    public void setHostName(String hostName) {
        this.hostName = hostName;
    }

    public String getVrouterIpAddress() {
        return vrouterIpAddress;
    }

    public void setVrouterIpAddress(String vrouterIpAddress) {
        this.vrouterIpAddress = vrouterIpAddress;
    }

    public String getMacAddress() {
        return macAddress;
    }

    public void setMacAddress(String macAddress) {
        this.macAddress = macAddress;
    }

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }
}

class VmwareVirtualNetworkInfo {
    private String name;
    private int vlanId;
    private SortedMap<String, VmwareVirtualMachineInfo> vmInfo;
    private String subnetAddress;
    private String subnetMask;
    private String gatewayAddress;
    
    public VmwareVirtualNetworkInfo(String name, int vlanId,
            SortedMap<String, VmwareVirtualMachineInfo> vmInfo,
            String subnetAddress, String subnetMask, String gatewayAddress) {
        this.name = name;
        this.vlanId = vlanId;
        this.vmInfo = vmInfo;
        this.subnetAddress = subnetAddress;
        this.subnetMask = subnetMask;
        this.gatewayAddress = gatewayAddress;
    }
    
    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public int getVlanId() {
        return vlanId;
    }

    public void setVlanId(int vlanId) {
        this.vlanId = vlanId;
    }

    public SortedMap<String, VmwareVirtualMachineInfo> getVmInfo() {
        return vmInfo;
    }

    public void setVmInfo(SortedMap<String, VmwareVirtualMachineInfo> vmInfo) {
        this.vmInfo = vmInfo;
    }

    public String getSubnetAddress() {
        return subnetAddress;
    }

    public void setSubnetAddress(String subnetAddress) {
        this.subnetAddress = subnetAddress;
    }

    public String getSubnetMask() {
        return subnetMask;
    }

    public void setSubnetMask(String subnetMask) {
        this.subnetMask = subnetMask;
    }

    public String getGatewayAddress() {
        return gatewayAddress;
    }

    public void setGatewayAddress(String gatewayAddress) {
        this.gatewayAddress = gatewayAddress;
    }
}

class VncVirtualMachineInfo {
    private VirtualMachine vmInfo;
    private VirtualMachineInterface vmInterfaceInfo;
    
    public VncVirtualMachineInfo(VirtualMachine vmInfo,
            VirtualMachineInterface vmInterfaceInfo) {
        this.vmInfo = vmInfo;
        this.vmInterfaceInfo = vmInterfaceInfo;
    }

    public VirtualMachine getVmInfo() {
        return vmInfo;
    }

    public void setVmInfo(VirtualMachine vmInfo) {
        this.vmInfo = vmInfo;
    }

    public VirtualMachineInterface getVmInterfaceInfo() {
        return vmInterfaceInfo;
    }

    public void setVmInterfaceInfo(VirtualMachineInterface vmInterfaceInfo) {
        this.vmInterfaceInfo = vmInterfaceInfo;
    }
}

class VncVirtualNetworkInfo {
    private String name;
    private SortedMap<String, VncVirtualMachineInfo> vmInfo;
    
    public VncVirtualNetworkInfo(String name,
            SortedMap<String, VncVirtualMachineInfo> vmInfo) {
        this.name = name;
        this.vmInfo = vmInfo;
    }

    public String getName() {
        return name;
    }
    
    public void setName(String name) {
        this.name = name;
    }
    
    public SortedMap<String, VncVirtualMachineInfo> getVmInfo() {
        return vmInfo;
    }

    public void setVmInfo(SortedMap<String, VncVirtualMachineInfo> vmInfo) {
        this.vmInfo = vmInfo;
    }
}

class VCenterMonitorTask implements Runnable {
    private static Logger s_logger = Logger.getLogger(VCenterMonitorTask.class);
    private VCenterDB vcenterDB;
    private VncDB vncDB;
    
    public VCenterMonitorTask(String vcenterURL, String vcenterUsername,
            String vcenterPassword, String apiServerIpAddress, 
            int apiServerPort) throws Exception {
        vcenterDB = new VCenterDB(vcenterURL, vcenterUsername,
                vcenterPassword);
        vncDB = new VncDB(apiServerIpAddress, apiServerPort);
        // Initialize the databases
        vcenterDB.Initialize();
        vncDB.Initialize();
    }
    
    private void syncVirtualMachines(String vnUuid, 
            VmwareVirtualNetworkInfo vmwareNetworkInfo,
            VncVirtualNetworkInfo vncNetworkInfo) throws IOException {
        String vncVnName = vncNetworkInfo.getName();
        String vmwareVnName = vmwareNetworkInfo.getName();
        s_logger.info("Syncing virtual machines in network: " + vnUuid + 
                " across Vnc(" + vncVnName + ") and VCenter(" +
                vmwareVnName + ") DBs");
        SortedMap<String, VmwareVirtualMachineInfo> vmwareVmInfos =
                vmwareNetworkInfo.getVmInfo();
        SortedMap<String, VncVirtualMachineInfo> vncVmInfos =
                vncNetworkInfo.getVmInfo();
        Iterator<Entry<String, VmwareVirtualMachineInfo>> vmwareIter = 
                vmwareVmInfos.entrySet().iterator();
        Iterator<Entry<String, VncVirtualMachineInfo>> vncIter =
                vncVmInfos.entrySet().iterator();
        
        Map.Entry<String, VmwareVirtualMachineInfo> vmwareItem =
                (Entry<String, VmwareVirtualMachineInfo>)
                (vmwareIter.hasNext() ? vmwareIter.next() : null);
        Map.Entry<String, VncVirtualMachineInfo> vncItem =
                (Entry<String, VncVirtualMachineInfo>)
                (vncIter.hasNext() ? vncIter.next() : null);
        
        while (vmwareItem != null && vncItem != null) {
            // Do Vmware and Vnc virtual machines match?
            String vmwareVmUuid = vmwareItem.getKey();
            String vncVmUuid = vncItem.getKey();
            s_logger.info("Comparing Vnc virtual machine: " + vncVmUuid +
                    " and VCenter virtual machine: " + vmwareVmUuid);
            if (!vmwareVmUuid.equals(vncVmUuid)) {
                // Delete Vnc virtual machine
                vncDB.DeleteVirtualMachine(vncItem.getValue());
                vncItem = (Entry<String, VncVirtualMachineInfo>) 
                        (vncIter.hasNext() ? vncIter.next() : null);
            } else {
                // Match found, advance Vmware and Vnc iters
                vncItem = vncIter.hasNext() ? vncIter.next() : null;
                vmwareItem = vmwareIter.hasNext() ? vmwareIter.next() : null;
            }
        }       
        while (vmwareItem != null) {
            // Create
            String vmwareVmUuid = vmwareItem.getKey();
            VmwareVirtualMachineInfo vmwareVmInfo = vmwareItem.getValue();
            vncDB.CreateVirtualMachine(vnUuid, vmwareVmUuid,
                    vmwareVmInfo.getMacAddress(),
                    vmwareVmInfo.getName(),
                    vmwareVmInfo.getVrouterIpAddress(),
                    vmwareVmInfo.getHostName());
            vmwareItem = vmwareIter.hasNext() ? vmwareIter.next() : null;
        }
        while (vncItem != null) {
            // Delete
            vncDB.DeleteVirtualMachine(vncItem.getValue());
            vncItem = vncIter.hasNext() ? vncIter.next() : null;
        }        
    }
    
    private void syncVirtualNetworks() throws Exception {
        s_logger.info("Syncing Vnc and VCenter DBs");
        SortedMap<String, VmwareVirtualNetworkInfo> vmwareVirtualNetworkInfos =
                vcenterDB.populateVirtualNetworkInfo();
        SortedMap<String, VncVirtualNetworkInfo> vncVirtualNetworkInfos =
                vncDB.populateVirtualNetworkInfo();
        Iterator<Entry<String, VmwareVirtualNetworkInfo>> vmwareIter = 
                vmwareVirtualNetworkInfos.entrySet().iterator();
        Iterator<Entry<String, VncVirtualNetworkInfo>> vncIter = 
                vncVirtualNetworkInfos.entrySet().iterator();
        
        Map.Entry<String, VmwareVirtualNetworkInfo> vmwareItem = 
                (Entry<String, VmwareVirtualNetworkInfo>) 
                (vmwareIter.hasNext() ? vmwareIter.next() : null);
        Map.Entry<String, VncVirtualNetworkInfo> vncItem = 
                (Entry<String, VncVirtualNetworkInfo>) 
                (vncIter.hasNext() ? vncIter.next() : null);

        while (vmwareItem != null && vncItem != null) {
            // Do Vmware and Vnc networks match?
            String vmwareVnUuid = vmwareItem.getKey();
            String vncVnUuid = vncItem.getKey();
            if (!vmwareVnUuid.equals(vncVnUuid)) {
                // Delete Vnc network
                vncDB.DeleteVirtualNetwork(vncVnUuid);
                vncItem = vncIter.hasNext() ? vncIter.next() : null;
            } else {
                // Sync
                syncVirtualMachines(vncVnUuid, vmwareItem.getValue(),
                        vncItem.getValue());
                // Advance
                vncItem = vncIter.hasNext() ? vncIter.next() : null;
                vmwareItem = vmwareIter.hasNext() ? vmwareIter.next() : null;
            }
        }
        while (vmwareItem != null) {
            // Create
            String vmwareVnUuid = vmwareItem.getKey();
            VmwareVirtualNetworkInfo vnInfo = vmwareItem.getValue();
            SortedMap<String, VmwareVirtualMachineInfo> vmInfos = vnInfo.getVmInfo();
            String subnetAddr = vnInfo.getSubnetAddress();
            String subnetMask = vnInfo.getSubnetMask();
            String gatewayAddr = vnInfo.getGatewayAddress();
            String vmwareVnName = vnInfo.getName();
            vncDB.CreateVirtualNetwork(vmwareVnUuid, vmwareVnName, subnetAddr,
                    subnetMask, gatewayAddr, vmInfos);
            vmwareItem = vmwareIter.hasNext() ? vmwareIter.next() : null;
        }
        while (vncItem != null) {
            // Delete
            vncDB.DeleteVirtualNetwork(vncItem.getKey());
            vncItem = vncIter.hasNext() ? vncIter.next() : null;
        }
    }
    
    @Override
    public void run() {
        try {
            syncVirtualNetworks();
        } catch (Exception e) {
            // TODO Auto-generated catch block
            e.printStackTrace();
        }
    }
}

class ExecutorServiceShutdownThread extends Thread {
    private static final long timeoutValue = 60;
    private static final TimeUnit timeoutUnit = TimeUnit.SECONDS;
    private static Logger s_logger = Logger.getLogger(ExecutorServiceShutdownThread.class);
    private ExecutorService es;
        
    public ExecutorServiceShutdownThread(ExecutorService es) {
        this.es = es;
    }
    
    @Override
    public void run() {
        es.shutdown();
        try {
            if (!es.awaitTermination(timeoutValue, timeoutUnit)) {
                es.shutdownNow();
                if (!es.awaitTermination(timeoutValue, timeoutUnit)) {
                    s_logger.error("ExecutorSevice: " + es + 
                            " did NOT terminate");
                }
            }
        } catch (InterruptedException e) {
            s_logger.info("ExecutorServiceShutdownThread: " + 
                Thread.currentThread() + " ExecutorService: " + e + 
                " interrupted : " + e);
        }
        
    }
}

public class VCenterMonitor {
    private static ScheduledExecutorService scheduledTaskExecutor = 
            Executors.newScheduledThreadPool(1);
    
    public static void main(String[] args) throws Exception {
        BasicConfigurator.configure();
        final String vcenterURL = "https://10.84.24.111/sdk";
        final String vcenterUsername = "admin";
        final String vcenterPassword = "Contrail123!";
        final String apiServerAddress = "10.84.13.23";
        final int apiServerPort = 8082;
        // Launch the periodic VCenterMonitorTask
        VCenterMonitorTask monitorTask = new VCenterMonitorTask(vcenterURL, 
                vcenterUsername, vcenterPassword, apiServerAddress,
                apiServerPort);
        scheduledTaskExecutor.scheduleWithFixedDelay(monitorTask, 0, 30,
                TimeUnit.SECONDS);
        Runtime.getRuntime().addShutdownHook(
                new ExecutorServiceShutdownThread(scheduledTaskExecutor));
    }
}