package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	cc "github.com/Juniper/contrail-controller/src/config/go/pkg/command"
	vc "github.com/Juniper/contrail-controller/src/config/go/pkg/vcenter"
	"log"
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
	var job_input = flag.String("job-input", "", "Job input for the reading vcenter nodes")
	flag.Parse()
	inputData := *job_input
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
	ccData := cc.GetDefaultValues(ccHostDetails.Host,
		ccHostDetails.Username,
		ccHostDetails.Password, inputJSON.ContrailClusterID, inputJSON.AuthToken)
	var token string
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

	hosts, err := vc.GetDataCenterHostSystems(ctx,
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
			if hostValue.Ports != nil {
				if portValue, ok := hostValue.Ports[port.Name]; ok {
					port.Uuid = portValue.Uuid
				}
			}

			ccData.CreatePort(port, host)
		}
	}

}
