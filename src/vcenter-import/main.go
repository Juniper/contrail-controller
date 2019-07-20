package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"os"

	log "github.com/sirupsen/logrus"

	"vcenter-import/command"
	"vcenter-import/vcenter"
)

var (
	jobInput = flag.String("job-input", "", "Job input for the reading vcenter nodes") //nolint: gochecknoglobals
	debug    = flag.Bool("debug", false, "Enable debug logs")                          //nolint: gochecknoglobals
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
		VCenter struct {
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

func contrailCommandClient(cfg *jobManagerCfg) (*command.ContrailCommand, error) {
	ccHostDetails := cfg.Input.ContrailCommand
	cClient := command.New(ccHostDetails.Host, ccHostDetails.Username, ccHostDetails.Password, cfg.ContrailClusterID, cfg.AuthToken)
	// token is updated a variable of cClient
	if err := cClient.Token(); err != nil {
		return nil, err
	}
	return cClient, nil
}

func main() {
	flag.Parse()
	inputData := *jobInput
	debugFlag := *debug
	log.SetLevel(log.InfoLevel)
	if debugFlag {
		log.SetLevel(log.DebugLevel)
	}

	cfg := &jobManagerCfg{}
	err := json.Unmarshal([]byte(inputData), cfg)
	if err != nil {
		log.Warn(err)
		os.Exit(1)
	}
	ctx := context.Background()
	var vCenterCfg = cfg.Input.VCenter
	if vCenterCfg.SdkURL == "" {
		vCenterCfg.SdkURL = "/sdk"
	}
	vCenterSdkURL := fmt.Sprintf("https://%s:%s@%s%s", vCenterCfg.Username, vCenterCfg.Password, vCenterCfg.Host, vCenterCfg.SdkURL)
	c, err := vcenter.New(ctx, vCenterSdkURL, !vCenterCfg.Secure)
	if err != nil {
		log.Fatalf("Failed to create vcenter client: %v", err)
	}
	hosts, err := c.DataCenterHostSystems(ctx, vCenterCfg.Datacenter)
	if err != nil {
		log.Fatalf("Failed to get hosts: %v", err)
	}

	cClient, err := contrailCommandClient(cfg)
	if err != nil {
		log.Fatal(err)
	}
	err = createContrailCommandNodes(cClient, hosts)
	if err != nil {
		log.Warn("error creating nodes", err)
	}
}

func createContrailCommandNodes(cClient *command.ContrailCommand, hosts []vcenter.ESXIHost) error {
	nodes, err := cClient.Nodes()
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
