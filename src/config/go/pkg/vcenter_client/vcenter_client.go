package readvcenter

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

type EsxiPort struct {
	Name       string
	MacAddress string
	SwitchName string
	PortIndex  string
	ChassisId  string
	PortId     string
	DvsName    string
	Uuid       string
}

type EsxiHost struct {
	Hostname string
	Uuid     string
	Ports    map[string]EsxiPort
}

func GetDataCenterHostSystems(ctx context.Context,
	datacenter string,
	sdk_url string,
	insecureFlag bool) ([]EsxiHost, error) {

	u, err := soap.ParseURL(sdk_url)
	if err != nil {
		fmt.Printf("ParseURL error\n")
		return nil, err
	}

	c, err := govmomi.NewClient(ctx, u, insecureFlag)
	if err != nil {
		fmt.Printf("New Client error : %s\n", err)
		return nil, err
	}

	defer c.Logout(ctx)

	m := view.NewManager(c.Client)
	d, err := m.CreateContainerView(ctx, c.ServiceContent.RootFolder, []string{"Datacenter"}, true)
	if err != nil {
		log.Fatal(err)
		return nil, err
	}
	defer d.Destroy(ctx)
	var dcs []mo.Datacenter
	err = d.Retrieve(ctx, []string{"Datacenter"}, nil, &dcs)
	if err != nil {
		log.Fatal(err)
		return nil, err
	}
	var esxi_hosts []EsxiHost
	for _, dc := range dcs {
		if dc.Name == datacenter {
			v, err := m.CreateContainerView(ctx, dc.Reference(), []string{"HostSystem"}, true)
			if err != nil {
				log.Fatal(err)
				return nil, err
			}

			defer v.Destroy(ctx)

			var hss []mo.HostSystem
			err = v.Retrieve(ctx, []string{"HostSystem"}, nil, &hss)
			if err != nil {
				log.Fatal(err)
				return nil, err
			}

			for _, hs := range hss {
				var esxi_host EsxiHost
				ns := hs.ConfigManager.NetworkSystem
				req := types.QueryNetworkHint{
					This: ns.Reference(),
				}
				val, err := methods.QueryNetworkHint(ctx, c.Client, &req)
				if err != nil {
					return nil, err
				}
				var esxi_ports map[string]EsxiPort
				esxi_ports = make(map[string]EsxiPort)
				for _, pnic := range hs.Config.Network.Pnic {
					var esxi_port EsxiPort
					esxi_port.MacAddress = pnic.Mac
					esxi_port.Name = pnic.Device
					for _, proxySw := range hs.Config.Network.ProxySwitch {
						for _, key := range proxySw.Pnic {
							if key == pnic.Key {
								esxi_port.DvsName = proxySw.DvsName
							}
						}
					}
					esxi_ports[pnic.Device] = esxi_port
				}
				for _, lldpNic := range val.Returnval {
					if lldpNic.LldpInfo != nil {
						switchName := ""
						portId := ""
						for _, param := range lldpNic.LldpInfo.Parameter {
							if param.Key == "Port Description" {
								portId = param.Value.(string)
							} else if param.Key == "System Name" {
								switchName = param.Value.(string)
							}
						}
						if _, ok := esxi_ports[lldpNic.Device]; ok {
							var esxi_port EsxiPort
							esxi_port = esxi_ports[lldpNic.Device]
							esxi_port.PortId = portId
							esxi_port.SwitchName = switchName
							esxi_port.PortIndex = lldpNic.LldpInfo.PortId
							esxi_port.ChassisId = lldpNic.LldpInfo.ChassisId
							esxi_ports[lldpNic.Device] = esxi_port
						}
					}
				}
				esxi_host = EsxiHost{hs.Name, "", esxi_ports}
				esxi_hosts = append(esxi_hosts, esxi_host)
			}
		}
	}
	return esxi_hosts, nil
}
