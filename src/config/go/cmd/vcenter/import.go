package main

import (
	"context"
	"encoding/json"
	"fmt"
	"github.com/Juniper/contrail_command_client"
	"github.com/Juniper/readvcenter"
	"log"
	"os"
)

type JobManagerInput struct {
	VncAPIInitParams struct {
		APIServerPort   string `json:"api_server_port"`
		AdminTenantName string `json:"admin_tenant_name"`
		AdminPassword   string `json:"admin_password"`
		AdminUser       string `json:"admin_user"`
		APIServerUseSsl string `json:"api_server_use_ssl"`
	} `json:"vnc_api_init_params"`
	JobTemplateID     string   `json:"job_template_id"`
	FabricFqName      string   `json:"fabric_fq_name"`
	JobTemplateFqname []string `json:"job_template_fqname"`
	Input             struct {
		Vcenter struct {
			Username   string `json:"username"`
			Datacenter string `json:"datacenter"`
			Host       string `json:"host"`
			Password   string `json:"password"`
			SdkUrl     string `json:"sdk_url"`
			Secure     bool   `json:"secure"`
		} `json:"vcenter"`
		ContrailCommand struct {
			Username string `json:"username"`
			Host     string `json:"host"`
			Password string `json:"password"`
		} `json:"contrail_command"`
		FabricFqName []string `json:"fabric_fq_name"`
	} `json:"input"`
	AuthToken         string   `json:"auth_token"`
	APIServerHost     []string `json:"api_server_host"`
	ContrailClusterID string   `json:"contrail_cluster_id"`
	JobExecutionID    string   `json:"job_execution_id"`
}

func main() {
	arguments := os.Args[2:]
	inputData := arguments[0]
	log.Println()
	log.Println("ARGS : ", inputData)
	var inputJSON JobManagerInput
	err := json.Unmarshal([]byte(inputData), &inputJSON)
	if err != nil {
		panic(err)
	}
	fmt.Println("Starting....")
	fmt.Println("Input :::: ", inputJSON.Input)
	ctx := context.Background()
	var vcenterInput = inputJSON.Input.Vcenter
	if vcenterInput.SdkUrl == "" {
		vcenterInput.SdkUrl = "/sdk"
	}
	vcenterSdkUrl := fmt.Sprintf("https://%s:%s@%s%s",
		vcenterInput.Username,
		vcenterInput.Password,
		vcenterInput.Host,
		vcenterInput.SdkUrl)
	insecureFlag := true
	if vcenterInput.Secure {
		insecureFlag = false
	}
	fmt.Println(vcenterSdkUrl)

	ccHostDetails := inputJSON.Input.ContrailCommand
	ccData := contrail_command_client.GetDefaultValues(ccHostDetails.Host,
		ccHostDetails.Username,
		ccHostDetails.Password)
	token, err := ccData.GetTokenViaUsername()
	if err != nil {
		log.Fatal(err)
	}
	fmt.Println(token)

	hosts, err := readvcenter.GetDataCenterHostSystems(ctx,
		vcenterInput.Datacenter,
		vcenterSdkUrl,
		insecureFlag)
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
