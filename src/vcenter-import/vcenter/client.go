package vcenter

import (
	"context"
	"fmt"

	"github.com/vmware/govmomi"
	"github.com/vmware/govmomi/view"
	"github.com/vmware/govmomi/vim25/methods"
	"github.com/vmware/govmomi/vim25/mo"
	"github.com/vmware/govmomi/vim25/soap"
	"github.com/vmware/govmomi/vim25/types"

	log "github.com/sirupsen/logrus"
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
	if err := c.Logout(ctx); err != nil {
		log.Warn(err)
	}
}
func containerViewDestroy(ctx context.Context, d *view.ContainerView) {
	if err := d.Destroy(ctx); err != nil {
		log.Warn(err)
	}
}

//DataCenterHostSystems reads ESXI-Host and vmnic details from vCenter and
//return ESXIHost array to the caller.
func DataCenterHostSystems(ctx context.Context,
	datacenter string,
	sdkURL string,
	insecureFlag bool) ([]ESXIHost, error) {

	u, err := soap.ParseURL(sdkURL)
	if err != nil {
		return nil, err
	}

	log.Debug("Parsed sdkURL")
	c, err := govmomi.NewClient(ctx, u, insecureFlag)
	if err != nil {
		return nil, fmt.Errorf("new client error : %s", err)
	}

	defer vCenterLogout(ctx, c)
	log.Debug("Created new client")
	m := view.NewManager(c.Client)
	d, err := m.CreateContainerView(ctx, c.ServiceContent.RootFolder, []string{"Datacenter"}, true)
	if err != nil {
		return nil, err
	}
	defer containerViewDestroy(ctx, d)
	log.Debug("Created Datacenter View")
	var dcs []mo.Datacenter
	if err := d.Retrieve(ctx, []string{"Datacenter"}, nil, &dcs); err != nil {
		return nil, err
	}
	log.Debug("Retrieved Datacenter details")
	var esxiHosts []ESXIHost
	for _, dc := range dcs {
		log.Debug("Checking datacenter: ", dc.Name)
		if dc.Name != datacenter {
			continue
		}
		v, err := m.CreateContainerView(ctx, dc.Reference(), []string{"HostSystem"}, true)
		if err != nil {
			return nil, fmt.Errorf("failed to create containerview for hostsystem: %v", err)
		}
		log.Debug("Creating View for ", datacenter)
		defer containerViewDestroy(ctx, v)
		var hss []mo.HostSystem
		if err := v.Retrieve(ctx, []string{"HostSystem"}, nil, &hss); err != nil {
			log.Warn(err)
			return nil, err
		}
		esxiHosts = readESXIHosts(ctx, hss, c)
	}
	return esxiHosts, nil
}
func readESXIHosts(ctx context.Context, hss []mo.HostSystem, c *govmomi.Client) []ESXIHost {
	var esxiHosts []ESXIHost
	for _, hs := range hss {
		ns := hs.ConfigManager.NetworkSystem
		req := types.QueryNetworkHint{
			This: ns.Reference(),
		}
		lldpInfo, err := methods.QueryNetworkHint(ctx, c.Client, &req)
		if err != nil {
			// it may fail to query, try with next server
			log.Warn("Failed to Query :", err)
			continue
		}
		esxiPorts := esxiPorts(hs, lldpInfo)
		var esxiHost = ESXIHost{
			Hostname: hs.Name,
			UUID:     "",
			Ports:    esxiPorts,
		}
		esxiHosts = append(esxiHosts, esxiHost)
	}
	return esxiHosts
}
func dvsName(hs mo.HostSystem, pnic types.PhysicalNic) string {
	for _, proxySw := range hs.Config.Network.ProxySwitch {
		for _, key := range proxySw.Pnic {
			if key == pnic.Key {
				return proxySw.DvsName
			}
		}
	}
	return ""
}
func esxiPorts(hs mo.HostSystem, lldpInfo *types.QueryNetworkHintResponse) map[string]ESXIPort {
	esxiPorts := map[string]ESXIPort{}
	for _, pnic := range hs.Config.Network.Pnic {
		esxiPorts[pnic.Device] = ESXIPort{
			MACAddress: pnic.Mac,
			Name:       pnic.Device,
			DVSName:    dvsName(hs, pnic),
		}
	}
	for _, lldpNic := range lldpInfo.Returnval {
		if lldpNic.LldpInfo == nil {
			continue
		}
		p, ok := esxiPorts[lldpNic.Device]
		if !ok {
			continue
		}
		var switchName string
		var portID string
		for _, param := range lldpNic.LldpInfo.Parameter {
			switch param.Key {
			case "Port Description":
				portID, _ = param.Value.(string) // nolint: errcheck
			case "System Name":
				switchName, _ = param.Value.(string) // nolint: errcheck
			}
		}
		p.PortID = portID
		p.SwitchName = switchName
		p.PortIndex = lldpNic.LldpInfo.PortId
		p.ChassisID = lldpNic.LldpInfo.ChassisId
		esxiPorts[lldpNic.Device] = p
	}
	return esxiPorts
}
