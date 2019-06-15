package main

import (
	"context"
	"fmt"
	"github.com/Juniper/contrail_command_client"
	"github.com/Juniper/readvcenter"
	"log"
)

func main() {
	fmt.Println("Starting....")
	ctx := context.Background()
	insecureFlag := true
	url := "https://Administrator@vsphere.local:Contrail123!@10.87.69.75/sdk"
	datacenter := "d1"
	username := "admin"
	password := "contrail123"
	cchost := "10.87.69.71:9091"
	ccData := contrail_command_client.GetDefaultValues(cchost, username, password)
	token, err := ccData.GetTokenViaUsername()
	if err != nil {
		log.Fatal(err)
	}
	fmt.Println(token)

	hosts, err := readvcenter.GetDataCenterHostSystems(ctx, datacenter, url, insecureFlag)
	if err != nil {
		log.Fatal(err)
	}
	fmt.Println(ccData.AuthToken)
	nodes, err := ccData.GetNodes()
	for _, host := range hosts {
		fmt.Println(host.Hostname)
		hostValue, ok := nodes[host.Hostname]
		if ok {
			host.Uuid = hostValue.Uuid
		}
		ccData.CreateNode(host)
		for _, port := range host.Ports {
			//fmt.Println("   ", port.Name, port.MacAddress, port.SwitchName, port.PortId, port.DvsName)
			if hostValue.Ports != nil {
				if portValue, ok := hostValue.Ports[port.Name]; ok {
					//fmt.Println("PORT-UUID:", portValue.Uuid)
					port.Uuid = portValue.Uuid
				}
			}

			ccData.CreatePort(port, host)
		}
	}

	//cluster_token, err := ccData.GetTokenViaClusterToken()
	//if err != nil {
	//log.Fatal(err)
	//}
	//fmt.Println(cluster_token)
	//_, err = ccData.GetPorts()
	//fmt.Println("NODES", nodes)
	//fmt.Println("PORTS", ports)

}
