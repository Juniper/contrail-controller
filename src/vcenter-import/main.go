package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"log"

	"vcenter-import/command"
	"vcenter-import/vcenter"
)

type jobManagerInput struct {
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
			SdkURL     string `json:"sdk_url"`
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

func getContrailCommandData(inputJSON jobManagerInput) command_client.ContrailCommand {
	ccHostDetails := inputJSON.Input.ContrailCommand
	ccData := command_client.GetDefaultValues(ccHostDetails.Host,
		ccHostDetails.Username,
		ccHostDetails.Password,
		inputJSON.ContrailClusterID,
		inputJSON.AuthToken)
	var token string
	var err error
	if inputJSON.ContrailClusterID != "" && inputJSON.AuthToken != "" {
		token, err = ccData.GetTokenViaClusterToken()
		if err != nil {
			log.Fatal(err)
		}
	} else {
		token, err = ccData.GetTokenViaUsername()
		if err != nil {
			log.Fatal(err)
		}
	}
	fmt.Println("TOKEN: ", token)
	return ccData
}

func main() {
	var jobInput = flag.String("job-input", "", "Job input for the reading vcenter nodes")
	flag.Parse()
	inputData := *jobInput

	var inputJSON jobManagerInput
	err := json.Unmarshal([]byte(inputData), &inputJSON)
	if err != nil {
		panic(err)
	}
	ctx := context.Background()
	var vcenterInput = inputJSON.Input.Vcenter
	if vcenterInput.SdkURL == "" {
		vcenterInput.SdkURL = "/sdk"
	}
	vcenterSdkURL := fmt.Sprintf("https://%s:%s@%s%s",
		vcenterInput.Username,
		vcenterInput.Password,
		vcenterInput.Host,
		vcenterInput.SdkURL)
	insecureFlag := true
	if vcenterInput.Secure {
		insecureFlag = false
	}

	hosts, err := vcenter_client.GetDataCenterHostSystems(ctx,
		vcenterInput.Datacenter,
		vcenterSdkURL,
		insecureFlag)
	if err != nil {
		log.Fatal(err)
	}

	ccData := getContrailCommandData(inputJSON)
	createContrailCommandNodes(ccData, hosts)
}

func createContrailCommandNodes(ccData command_client.ContrailCommand, hosts []vcenter_client.EsxiHost) {
	nodes, err := ccData.GetNodes()
	if err != nil {
		log.Fatal(err)
	}
	for _, host := range hosts {
		fmt.Println(host.Hostname)
		hostValue, ok := nodes[host.Hostname]
		if ok {
			host.UUID = hostValue.UUID
		}
		err = ccData.CreateNode(host)
		if err != nil {
			log.Print(err)
		}
		for _, port := range host.Ports {
			if hostValue.Ports != nil {
				if portValue, ok := hostValue.Ports[port.Name]; ok {
					port.UUID = portValue.UUID
				}
			}
			err = ccData.CreatePort(port, host)
			if err != nil {
				log.Print(err)
			}
		}
	}
}
