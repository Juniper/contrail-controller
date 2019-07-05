package client

import (
	"context"
	"fmt"
	"log"
	"reflect"

	"github.com/vmware/govmomi"
	"github.com/vmware/govmomi/view"
	"github.com/vmware/govmomi/vim25/methods"
	"github.com/vmware/govmomi/vim25/mo"
	"github.com/vmware/govmomi/vim25/soap"
	"github.com/vmware/govmomi/vim25/types"
)

// EsxiPort contains the vmnic details of ESXI-Host, read from vCenter
type EsxiPort struct {
	Name       string
	MacAddress string
	SwitchName string
	PortIndex  string
	ChassisID  string
	PortID     string
	DvsName    string
	UUID       string
}

// EsxiHost contains ESXI-Host and vmnic details , read from vCenter
type EsxiHost struct {
	Hostname string
	UUID     string
	Ports    map[string]EsxiPort
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
//return EsxiHost array to the caller.
func GetDataCenterHostSystems(ctx context.Context,
	datacenter string,
	sdkURL string,
	insecureFlag bool) ([]EsxiHost, error) {

	u, err := soap.ParseURL(sdkURL)
	if err != nil {
		fmt.Printf("ParseURL error\n")
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
		log.Fatal(err)
		return nil, err
	}
	fmt.Println(reflect.TypeOf(d))
	defer containerViewDestroy(ctx, d)
	var dcs []mo.Datacenter
	err = d.Retrieve(ctx, []string{"Datacenter"}, nil, &dcs)
	if err != nil {
		log.Fatal(err)
		return nil, err
	}
	var esxiHosts []EsxiHost
	for _, dc := range dcs {
		fmt.Println("DataCenter", dc.Name)
		if dc.Name == datacenter {
			fmt.Println("DataCenter1", dc.Name)
			v, err := m.CreateContainerView(ctx, dc.Reference(), []string{"HostSystem"}, true)
			if err != nil {
				fmt.Println("exiting-1")
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
			esxiHosts = getEsxiHosts(ctx, hss, c)
		}
	}
	fmt.Println("ESXI_HOSTS", esxiHosts)
	return esxiHosts, nil
}
func getEsxiHosts(ctx context.Context, hss []mo.HostSystem, c *govmomi.Client) []EsxiHost {
	var esxiHosts []EsxiHost
	for _, hs := range hss {
		var esxiHost EsxiHost
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
		esxiPorts := getEsxiPorts(hs, val)
		fmt.Println("hs.Name: => ", hs.Name)
		esxiHost = EsxiHost{hs.Name, "", esxiPorts}
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
func getEsxiPorts(hs mo.HostSystem, val *types.QueryNetworkHintResponse) map[string]EsxiPort {
	var esxiPorts map[string]EsxiPort
	esxiPorts = make(map[string]EsxiPort)
	for _, pnic := range hs.Config.Network.Pnic {
		var esxiPort EsxiPort
		esxiPort.MacAddress = pnic.Mac
		esxiPort.Name = pnic.Device
		fmt.Println("VMNIC: ", esxiPort.Name)
		esxiPort.DvsName = getDvsName(hs, pnic)
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
				var esxiPort EsxiPort
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
