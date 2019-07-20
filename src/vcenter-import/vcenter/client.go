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

// Client is a vcenter client.
type Client struct {
	c *govmomi.Client
	m *view.Manager
}

// New returns a new vcenter client.
func New(ctx context.Context, url string, insecure bool) (*Client, error) {
	u, err := soap.ParseURL(url)
	if err != nil {
		return nil, fmt.Errorf("failed to create URL: %v", err)
	}
	c, err := govmomi.NewClient(ctx, u, insecure)
	if err != nil {
		return nil, err
	}
	m := view.NewManager(c.Client)
	return &Client{
		c: c,
		m: m,
	}, nil
}

// Close closes the client.
func (c *Client) Close() {
	if err := c.c.Logout(context.Background()); err != nil {
		log.Warn(err)
	}
}

// DataCenterHostSystems reads ESXI-Host and vmnic details from vCenter and
// returns ESXIHost array to the caller.
func (c *Client) DataCenterHostSystems(ctx context.Context, datacenter string) ([]ESXIHost, error) {
	d, err := c.m.CreateContainerView(ctx, c.c.ServiceContent.RootFolder, []string{"Datacenter"}, true)
	if err != nil {
		return nil, err
	}
	defer containerViewDestroy(ctx, d)
	log.Info("Created Datacenter View")
	var dcs []mo.Datacenter
	if err := d.Retrieve(ctx, []string{"Datacenter"}, nil, &dcs); err != nil {
		return nil, err
	}
	log.Infof("Retrieved Datacenter details: %v", dcs)
	var esxiHosts []ESXIHost
	for _, dc := range dcs {
		if dc.Name != datacenter {
			continue
		}

		log.Info("Checking datacenter: ", dc.Name)
		v, err := c.m.CreateContainerView(ctx, dc.Reference(), []string{"HostSystem"}, true)
		if err != nil {
			return nil, fmt.Errorf("failed to create containerview for hostsystem: %v", err)
		}
		log.Info("Create View for ", datacenter)
		defer containerViewDestroy(ctx, v)

		var hss []mo.HostSystem
		if err := v.Retrieve(ctx, []string{"HostSystem"}, nil, &hss); err != nil {
			return nil, fmt.Errorf("failed to retrieve hostsystems: %v", err)
		}
		hosts := c.readESXIHosts(ctx, hss)
		log.Info("hosts ", hosts)
		esxiHosts = append(esxiHosts, hosts...)
	}
	return esxiHosts, nil
}

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

func containerViewDestroy(ctx context.Context, d *view.ContainerView) {
	if err := d.Destroy(ctx); err != nil {
		log.Warn(err)
	}
}

func (c *Client) readESXIHosts(ctx context.Context, hss []mo.HostSystem) []ESXIHost {
	var esxiHosts []ESXIHost
	for _, hs := range hss {
		ns := hs.ConfigManager.NetworkSystem
		req := types.QueryNetworkHint{
			This: ns.Reference(),
		}
		lldpInfo, err := methods.QueryNetworkHint(ctx, c.c.Client, &req)
		if err != nil {
			// it may fail to query, try with next server
			log.Warn("Failed to get lldp info:", err)
			continue
		}
		esxiPorts := esxiPorts(hs, lldpInfo)
		esxiHost := ESXIHost{
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
