package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"log"
        "os"

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

func getContrailCommandData(inputJSON *jobManagerInput) (*command.ContrailCommand,  error){
	ccHostDetails := inputJSON.Input.ContrailCommand
	ccData := command.GetDefaultValues(ccHostDetails.Host, ccHostDetails.Username, ccHostDetails.Password, inputJSON.ContrailClusterID, inputJSON.AuthToken)
	var err error
	if inputJSON.ContrailClusterID != "" && inputJSON.AuthToken != "" {
		_, err = ccData.GetTokenViaClusterToken()
		if err != nil {
                        return nil, err
		}
	} else {
		_, err = ccData.GetTokenViaUsername()
		if err != nil {
                        return nil, err
		}
	}
	return ccData, nil
}

func main() {
	var jobInput = flag.String("job-input", "", "Job input for the reading vcenter nodes")
	flag.Parse()
	inputData := *jobInput

	inputJSON := &jobManagerInput{}
	err := json.Unmarshal([]byte(inputData), inputJSON)
	if err != nil {
                fmt.Println(err)
		os.Exit(1)
	}
	ctx := context.Background()
	var vcenterInput = inputJSON.Input.Vcenter
	if vcenterInput.SdkURL == "" {
		vcenterInput.SdkURL = "/sdk"
	}
	vcenterSdkURL := fmt.Sprintf("https://%s:%s@%s%s", vcenterInput.Username, vcenterInput.Password, vcenterInput.Host, vcenterInput.SdkURL)
	hosts, err := vcenter.GetDataCenterHostSystems(ctx, vcenterInput.Datacenter, vcenterSdkURL, vcenterInput.Secure)
	if err != nil {
		log.Fatal(err)
	}

	ccData, err := getContrailCommandData(inputJSON)
	createContrailCommandNodes(ccData, hosts)
}

func createContrailCommandNodes(ccData *command.ContrailCommand, hosts []vcenter.ESXIHost)( error ){
	nodes, err := ccData.GetNodes()
	if err != nil {
            return err
	}
	for _, host := range hosts {
		fmt.Println(host.Hostname)
		hostValue, ok := nodes[host.Hostname]
		if ok {
			host.UUID = hostValue.UUID
		}
		err = ccData.CreateNode(host)
		if err != nil {
                        //continue to next server
			log.Print(err)
                        continue
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
	return nil
}
