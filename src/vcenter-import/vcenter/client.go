package vcenter

import (
	"context"
	"fmt"
	"log"

	"github.com/vmware/govmomi"
	"github.com/vmware/govmomi/view"
	"github.com/vmware/govmomi/vim25/methods"
	"github.com/vmware/govmomi/vim25/mo"
	"github.com/vmware/govmomi/vim25/soap"
	"github.com/vmware/govmomi/vim25/types"
)

// ESXIPort contains the vmnic details of ESXI-Host, read from vCenter
type ESXIPort struct {
	Name       string
	MACAddress string
	SwitchName string
	PortIndex  string
	ChassisID  string
	PortID     string
	DVSName    string
	UUID       string
}

// ESXIHost contains ESXI-Host and vmnic details , read from vCenter
type ESXIHost struct {
	Hostname string
	UUID     string
	Ports    map[string]ESXIPort
}

func vCenterLogout(ctx context.Context, c *govmomi.Client) {
	err := c.Logout(ctx)
	if err != nil {
		log.Print(err)
	}
}
func containerViewDestroy(ctx context.Context, d *view.ContainerView) {
	err := d.Destroy(ctx)
	if err != nil {
		log.Print(err)
	}
}

//GetDataCenterHostSystems reads ESXI-Host and vmnic details from vCenter and
//return ESXIHost array to the caller.
func GetDataCenterHostSystems(ctx context.Context,
	datacenter string,
	sdkURL string,
	insecureFlag bool) ([]ESXIHost, error) {

	u, err := soap.ParseURL(sdkURL)
	if err != nil {
		log.Print("ParseURL error")
		return nil, err
	}

	c, err := govmomi.NewClient(ctx, u, insecureFlag)
	if err != nil {
		fmt.Printf("New Client error : %s\n", err)
		return nil, err
	}

	defer vCenterLogout(ctx, c)

	m := view.NewManager(c.Client)
	d, err := m.CreateContainerView(ctx, c.ServiceContent.RootFolder, []string{"Datacenter"}, true)
	if err != nil {
		return nil, err
	}
	defer containerViewDestroy(ctx, d)
	var dcs []mo.Datacenter
	err = d.Retrieve(ctx, []string{"Datacenter"}, nil, &dcs)
	if err != nil {
		return nil, err
	}
	var esxiHosts []ESXIHost
	for _, dc := range dcs {
		if dc.Name == datacenter {
			v, err := m.CreateContainerView(ctx, dc.Reference(), []string{"HostSystem"}, true)
			if err != nil {
				log.Fatal(err)
				return nil, err
			}

			defer containerViewDestroy(ctx, d)

			var hss []mo.HostSystem
			err = v.Retrieve(ctx, []string{"HostSystem"}, nil, &hss)
			if err != nil {
				log.Fatal(err)
				return nil, err
			}
			esxiHosts = getESXIHosts(ctx, hss, c)
		}
	}
	return esxiHosts, nil
}
func getESXIHosts(ctx context.Context, hss []mo.HostSystem, c *govmomi.Client) []ESXIHost {
	var esxiHosts []ESXIHost
	for _, hs := range hss {
		var esxiHost ESXIHost
		ns := hs.ConfigManager.NetworkSystem
		req := types.QueryNetworkHint{
			This: ns.Reference(),
		}
		val, err := methods.QueryNetworkHint(ctx, c.Client, &req)
		if err != nil {
			// it may fail to query, try with next
			// server
			continue
		}
		esxiPorts := getESXIPorts(hs, val)
		esxiHost = ESXIHost{hs.Name, "", esxiPorts}
		esxiHosts = append(esxiHosts, esxiHost)
	}
	return esxiHosts
}
func getDvsName(hs mo.HostSystem, pnic types.PhysicalNic) string {
	for _, proxySw := range hs.Config.Network.ProxySwitch {
		for _, key := range proxySw.Pnic {
			if key == pnic.Key {
				return proxySw.DvsName
			}
		}
	}
	return ""
}
func getESXIPorts(hs mo.HostSystem, val *types.QueryNetworkHintResponse) map[string]ESXIPort {
	var esxiPorts map[string]ESXIPort
	esxiPorts = make(map[string]ESXIPort)
	for _, pnic := range hs.Config.Network.Pnic {
		var esxiPort ESXIPort
		esxiPort.MACAddress = pnic.Mac
		esxiPort.Name = pnic.Device
		esxiPort.DVSName = getDvsName(hs, pnic)
		esxiPorts[pnic.Device] = esxiPort
	}
	for _, lldpNic := range val.Returnval {
		if lldpNic.LldpInfo != nil {
			var switchName string
			var portID string
			for _, param := range lldpNic.LldpInfo.Parameter {
				if param.Key == "Port Description" {
					portID, _ = param.Value.(string) // nolint: errcheck
				} else if param.Key == "System Name" {
					switchName, _ = param.Value.(string) // nolint: errcheck
				}
			}
			if _, ok := esxiPorts[lldpNic.Device]; ok {
				var esxiPort ESXIPort
				esxiPort = esxiPorts[lldpNic.Device]
				esxiPort.PortID = portID
				esxiPort.SwitchName = switchName
				esxiPort.PortIndex = lldpNic.LldpInfo.PortId
				esxiPort.ChassisID = lldpNic.LldpInfo.ChassisId
				esxiPorts[lldpNic.Device] = esxiPort
			}
		}
	}
	return esxiPorts
}
