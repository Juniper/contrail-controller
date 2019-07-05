package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	log "github.com/sirupsen/logrus"
	"os"

	"vcenter-import/command"
	"vcenter-import/vcenter"
)

type jobManagerCfg struct {
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

func getContrailCommandData(cfg *jobManagerCfg) (*command.ContrailCommand, error) {
	ccHostDetails := cfg.Input.ContrailCommand
	cClient := command.New(ccHostDetails.Host, ccHostDetails.Username, ccHostDetails.Password, cfg.ContrailClusterID, cfg.AuthToken)
	// token is updated a variable of cClient
	err := cClient.Token()
	if err != nil {
		return nil, err
	}
	return cClient, nil
}

func main() {
	var jobInput = flag.String("job-input", "", "Job input for the reading vcenter nodes")
	var debug = flag.Bool("debug", false, "Enable debug logs")
	flag.Parse()
	inputData := *jobInput
	debug_flag := *debug
	if debug_flag {
		log.SetLevel(log.DebugLevel)
	} else {
		log.SetLevel(log.WarnLevel)
	}

	cfg := &jobManagerCfg{}
	err := json.Unmarshal([]byte(inputData), cfg)
	if err != nil {
		log.Warn(err)
		os.Exit(1)
	}
	ctx := context.Background()
	var vcenterCfg = cfg.Input.Vcenter
	if vcenterCfg.SdkURL == "" {
		vcenterCfg.SdkURL = "/sdk"
	}
	vcenterSdkURL := fmt.Sprintf("https://%s:%s@%s%s", vcenterCfg.Username, vcenterCfg.Password, vcenterCfg.Host, vcenterCfg.SdkURL)
	hosts, err := vcenter.GetDataCenterHostSystems(ctx, vcenterCfg.Datacenter, vcenterSdkURL, !vcenterCfg.Secure)
	if err != nil {
		log.Fatal(err)
	}

	cClient, err := getContrailCommandData(cfg)
	if err != nil {
		log.Fatal(err)
	}
	createContrailCommandNodes(cClient, hosts)
}

func createContrailCommandNodes(cClient *command.ContrailCommand, hosts []vcenter.ESXIHost) error {
	nodes, err := cClient.GetNodes()
	if err != nil {
		return err
	}
	for _, host := range hosts {
		log.Debug("Creating Host: ", host.Hostname)
		hostValue, ok := nodes[host.Hostname]
		if ok {
			host.UUID = hostValue.UUID
		}
		err = cClient.CreateNode(host)
		if err != nil {
			//continue to next server
			log.Debug(err)
			continue
		}
		for _, port := range host.Ports {
			if hostValue.Ports != nil {
				if portValue, ok := hostValue.Ports[port.Name]; ok {
					port.UUID = portValue.UUID
				}
			}
			err = cClient.CreatePort(port, host)
			if err != nil {
				log.Debug(err)
			}
		}
	}
	return nil
}
