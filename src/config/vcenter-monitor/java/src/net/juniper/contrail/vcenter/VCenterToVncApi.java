package net.juniper.contrail.vcenter;

import static com.vmware.vim.cf.NullObject.NULL;

import java.net.URL;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;

import com.vmware.vim.cf.CacheInstance;
import com.vmware.vim25.ArrayOfManagedObjectReference;
import com.vmware.vim25.DVPortgroupConfigInfo;
import com.vmware.vim25.IpPool;
import com.vmware.vim25.ManagedObjectReference;
import com.vmware.vim25.NetworkSummary;
import com.vmware.vim25.VMwareDVSPortSetting;
import com.vmware.vim25.VirtualMachinePowerState;
import com.vmware.vim25.VirtualMachineSummary;
import com.vmware.vim25.VmwareDistributedVirtualSwitchPvlanSpec;
import com.vmware.vim25.VmwareDistributedVirtualSwitchVlanSpec;
import com.vmware.vim25.mo.Datacenter;
import com.vmware.vim25.mo.DistributedVirtualPortgroup;
import com.vmware.vim25.mo.DistributedVirtualSwitch;
import com.vmware.vim25.mo.Folder;
import com.vmware.vim25.mo.InventoryNavigator;
import com.vmware.vim25.mo.IpPoolManager;
import com.vmware.vim25.mo.ManagedEntity;
import com.vmware.vim25.mo.Network;
import com.vmware.vim25.mo.ServiceInstance;
import com.vmware.vim25.mo.VirtualMachine;
import com.vmware.vim25.mo.VmwareDistributedVirtualSwitch;

class UserVNInfo {
	public int vlanId;
	public String[] vms;
	public IpPool ipp;
}

public class VCenterToVncApi
{
  public static void main(String[] args) throws Exception
  {
	final String kDvSwitch = "dvSwitch";
	final String kDataCenter = "Datacenter";
	
	ServiceInstance si = new ServiceInstance(new URL("https://10.84.24.111/sdk"), "admin", "Contrail123!", true);
    Folder rootFolder = si.getRootFolder();
    ManagedEntity[] vms = new InventoryNavigator(rootFolder).searchManagedEntities("VirtualMachine");
    
    IpPoolManager ipm = si.getIpPoolManager();
    Datacenter dc = (Datacenter) new InventoryNavigator(rootFolder).searchManagedEntity("Datacenter", kDataCenter);
    IpPool[] ipa = ipm.queryIpPools(dc);
    
    for(int ii=0; ii<vms.length; ii++) {
    	VirtualMachine vx = (VirtualMachine) vms[ii];
    	Network[] nett = vx.getNetworks();
    	if ((nett.length == 1) && (nett[0] instanceof DistributedVirtualPortgroup)) {
    	    System.out.println("vm: " + vx.getName() + " nets: " + nett.length);
    	}
    }
    
    Map<String, UserVNInfo> items = new HashMap<String,UserVNInfo>();
    
    VmwareDistributedVirtualSwitch dvs = (VmwareDistributedVirtualSwitch) new InventoryNavigator(rootFolder).searchManagedEntity("VmwareDistributedVirtualSwitch", kDvSwitch);
    DistributedVirtualPortgroup[] dvpgs = dvs.getPortgroup();
    for(int jj=0; jj<dvpgs.length; jj++) {
    	DVPortgroupConfigInfo dci = dvpgs[jj].getConfig();
    	NetworkSummary ns = dvpgs[jj].getSummary();
    	Integer poolid = ns.getIpPoolId();
    	VMwareDVSPortSetting vdvs = (VMwareDVSPortSetting)dci.getDefaultPortConfig();
    	VmwareDistributedVirtualSwitchVlanSpec svs = vdvs.getVlan();
    	if ((svs instanceof VmwareDistributedVirtualSwitchPvlanSpec) && (poolid != null)){
    		UserVNInfo uvi = new UserVNInfo();
    		uvi.ipp = null;
        	for (int pp=0;pp<ipa.length; pp++) {
        		if ( ipa[pp].id == poolid.intValue()) {
        			uvi.ipp = ipa[pp];
        		}
        	}    		
        	if (uvi.ipp == null) continue;
        	
    		VmwareDistributedVirtualSwitchPvlanSpec psvs = (VmwareDistributedVirtualSwitchPvlanSpec) svs;
            System.out.println("dvpg: " + dvpgs[jj].getName() + " vlan: " + psvs.getPvlanId() + " subnet: " + uvi.ipp.getIpv4Config().getSubnetAddress());
            uvi.vlanId = psvs.getPvlanId();
            
            VirtualMachine[] nvms = dvpgs[jj].getVms();
            ArrayList<String> avms = new ArrayList();
            for (int kk=0; kk<nvms.length; kk++) {
            	if (nvms[kk].getRuntime().getPowerState() == VirtualMachinePowerState.poweredOn) {
            		System.out.println("  vm: " + nvms[kk].getName() + " state: " + nvms[kk].getRuntime().getPowerState());
            		avms.add(nvms[kk].getName());
            	}
            }
            
            String[] ss = new String[avms.size()];
            uvi.vms = (String[]) avms.toArray(ss);
            
            items.put(dvpgs[jj].getName(), uvi);
    	}
    }
    
  }
}