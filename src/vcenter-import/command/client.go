package command

import (
	"bytes"
	"crypto/tls"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net/http"

	log "github.com/sirupsen/logrus"

	"vcenter-import/vcenter"
)

type contrailCommandNodeSync struct {
	Resources []*nodeResources `json:"resources"`
}
type nodeData struct {
	NodeType string   `json:"node_type"`
	UUID     string   `json:"uuid"`
	Hostname string   `json:"hostname"`
	FqName   []string `json:"fq_name"`
}
type nodeResources struct {
	Kind string    `json:"kind"`
	Data *nodeData `json:"data"`
}

type contrailCommandPortSync struct {
	Resources []portResources `json:"resources"`
}
type localLinkConnection struct {
	PortID     string `json:"port_id"`
	SwitchInfo string `json:"switch_info"`
	SwitchID   string `json:"switch_id"`
	PortIndex  string `json:"port_index"`
}
type bmsPortInfo struct {
	Address             string              `json:"address"`
	LocalLinkConnection localLinkConnection `json:"local_link_connection"`
}
type esxiPortInfo struct {
	DVSName string `json:"dvs_name"`
}
type portData struct {
	Name         string       `json:"name"`
	UUID         string       `json:"uuid"`
	FqName       []string     `json:"fq_name"`
	BmsPortInfo  bmsPortInfo  `json:"bms_port_info"`
	ESXIPortInfo esxiPortInfo `json:"esxi_port_info"`
}
type portResources struct {
	Kind string   `json:"kind"`
	Data portData `json:"data"`
}

type port struct {
	UUID       string   `json:"uuid"`
	Name       string   `json:"name"`
	ParentUUID string   `json:"parent_uuid"`
	ParentType string   `json:"parent_type"`
	FqName     []string `json:"fq_name"`
}
type node struct {
	UUID     string   `json:"uuid"`
	Name     string   `json:"name"`
	FqName   []string `json:"fq_name"`
	Hostname string   `json:"hostname"`
	NodeType string   `json:"node_type"`
	Ports    []port   `json:"ports"`
}

type contrailCommandNodes struct {
	Nodes []struct {
		Node node `json:"node"`
	} `json:"nodes"`
}

type keyStoneAuthViaUsername struct {
	Auth userAuth `json:"auth"`
}
type domain struct {
	ID string `json:"id"`
}
type user struct {
	Name     string `json:"name"`
	Password string `json:"password"`
	Domain   domain `json:"domain"`
}
type password struct {
	User user `json:"user"`
}
type userIdentity struct {
	Methods  []string `json:"methods"`
	Password password `json:"password"`
}
type project struct {
	Name   string `json:"name"`
	Domain domain `json:"domain"`
}
type scope struct {
	Project project `json:"project"`
}
type userAuth struct {
	Identity userIdentity `json:"identity"`
	Scope    scope        `json:"scope"`
}

type keyStoneAuthViaClusterToken struct {
	Auth auth `json:"auth"`
}
type token struct {
	ID string `json:"id"`
}
type cluster struct {
	ID    string `json:"id"`
	Token token  `json:"token"`
}
type identity struct {
	Methods []string `json:"methods"`
	Cluster cluster  `json:"cluster"`
}
type auth struct {
	Identity identity `json:"identity"`
}

// ContrailCommand provides configuration data for connecting to contrail-command
type ContrailCommand struct {
	AuthHost      string
	AuthPort      string
	AuthPath      string
	Username      string
	Password      string
	UserDomain    string
	ProjectDomain string
	ProjectName   string
	ClusterID     string
	ClusterToken  string
	AuthToken     string
}

// CCPort stores port-name and port-uuid from contrail-contrail
type CCPort struct {
	Name string
	UUID string
}

// CCNode hold name, ports and uuid from contrail-command
type CCNode struct {
	Name  string
	UUID  string
	Ports map[string]CCPort
}

// New Initializes the ContrailCommand struct from provided
// values and default values.
func New(ccHost, username, password, clusterID, clusterToken string) *ContrailCommand {
	return &ContrailCommand{
		AuthHost:      ccHost,
		Username:      username,
		Password:      password,
		ClusterID:     clusterID,
		ClusterToken:  clusterToken,
		AuthPath:      "/keystone/v3/auth/tokens",
		UserDomain:    "default",
		ProjectName:   "admin",
		ProjectDomain: "default",
	}
}

// Token will fetch the token either if clusterID and ClusterToken is provided or from Username
func (cc *ContrailCommand) Token() error {
	var err error
	var jsonData []byte
	if cc.ClusterID != "" && cc.ClusterToken != "" {
		authData := keyStoneAuthViaClusterToken{
			Auth: auth{
				Identity: identity{
					Methods: []string{"cluster-token"},
					Cluster: cluster{
						ID: cc.ClusterID,
						Token: token{
							ID: cc.ClusterToken,
						},
					},
				},
			},
		}

		jsonData, err = json.Marshal(authData)
	} else {
		authData := keyStoneAuthViaUsername{
			Auth: userAuth{
				Identity: userIdentity{
					Methods: []string{"password"},
					Password: password{
						User: user{
							Name:     cc.Username,
							Password: cc.Password,
							Domain: domain{
								ID: cc.UserDomain,
							},
						},
					},
				},
				Scope: scope{
					Project: project{
						Name: cc.ProjectName,
						Domain: domain{
							ID: cc.ProjectDomain,
						},
					},
				},
			},
		}
		jsonData, err = json.Marshal(authData)
	}
	if err != nil {
		return fmt.Errorf("failed to marshal ")
	}
	resp, _, err := cc.sendRequest(cc.AuthPath, string(jsonData), "POST") //nolint: bodyclose
	if err != nil {
		return err
	}
	switch resp.StatusCode {
	default:
		return fmt.Errorf("authToken is not created, %d", resp.StatusCode)
	case 200, 201:
		cc.AuthToken = resp.Header.Get("X-Subject-Token")
	}
	if cc.AuthToken == "" {
		return fmt.Errorf("failed to get token from resp: %v", resp.Header)
	}
	return nil
}

func responseBodyClose(resp *http.Response) {
	err := resp.Body.Close()
	if err != nil {
		log.Warn(err)
	}
}
func (cc *ContrailCommand) sendRequest(resourcePath string, resourceData string, requestMethod string) (*http.Response, []byte, error) {
	url := fmt.Sprintf("https://%s%s", cc.AuthHost, resourcePath)
	tr := &http.Transport{
		TLSClientConfig: &tls.Config{InsecureSkipVerify: true},
	}
	req, err := http.NewRequest(requestMethod, url, bytes.NewBufferString(resourceData))
	if err != nil {
		return nil, nil, err
	}
	req.Header.Set("Content-Type", "application/json")
	if cc.AuthToken != "" {
		req.Header.Set("X-Auth-Token", cc.AuthToken)
	}
	client := &http.Client{Transport: tr}
	resp, err := client.Do(req)
	if err != nil {
		return nil, nil, err
	}
	defer responseBodyClose(resp)
	body, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		return nil, nil, err
	}
	return resp, body, nil
}

// Nodes get all nodes,ports from contrail-command and returns CCNode map
func (cc *ContrailCommand) Nodes() (map[string]CCNode, error) {
	resp, body, err := cc.sendRequest("/nodes?detail=true", "", "GET") //nolint: bodyclose
	if err != nil {
		return nil, err
	}

	log.Debug("Status from Server: ", resp.StatusCode)
	switch resp.StatusCode {
	default:
		return nil, fmt.Errorf("resource creation failed, %d", resp.StatusCode)
	case 200, 201:
	}
	nodeJSON := &contrailCommandNodes{}
	if err = json.Unmarshal(body, &nodeJSON); err != nil {
		return nil, err
	}
	ccNodes := map[string]CCNode{}
	for _, node := range nodeJSON.Nodes {
		ccPorts := map[string]CCPort{}
		for _, port := range node.Node.Ports {
			ccport := CCPort{port.Name, port.UUID}
			ccPorts[port.Name] = ccport
		}
		ccNode := CCNode{node.Node.Name, node.Node.UUID, ccPorts}
		ccNodes[node.Node.Name] = ccNode
	}
	return ccNodes, nil
}

// CreateNode creates node into contrail-command, it takes ESXiHost as input
func (cc *ContrailCommand) CreateNode(host vcenter.ESXIHost) error {
	log.Debug("Create Node:", cc.AuthToken)
	nodeResource := contrailCommandNodeSync{
		Resources: []*nodeResources{
			{
				Kind: "node",
				Data: &nodeData{
					NodeType: "esxi",
					UUID:     host.UUID,
					Hostname: host.Hostname,
					FqName:   []string{"default-global-system-config", host.Hostname},
				},
			},
		},
	}
	jsonData, err := json.Marshal(nodeResource)
	if err != nil {
		return err
	}
	log.Debug("Sending Request")
	resp, _, err := cc.sendRequest("/sync", string(jsonData), "POST") //nolint: bodyclose
	if err != nil {
		return err
	}
	log.Debug("Got status : ", resp.StatusCode)
	switch resp.StatusCode {
	default:
		return fmt.Errorf("resource creation failed, %d", resp.StatusCode)
	case 200, 201:
	}
	return nil
}

// CreatePort create port into contrail-command from ESXIPort and ESXiHost
func (cc *ContrailCommand) CreatePort(port vcenter.ESXIPort, host vcenter.ESXIHost) error {
	portResource := contrailCommandPortSync{
		Resources: []portResources{
			{
				Kind: "port",
				Data: portData{
					Name:   port.Name,
					UUID:   port.UUID,
					FqName: []string{"default-global-system-config", host.Hostname, port.Name},
					BmsPortInfo: bmsPortInfo{
						Address: port.MACAddress,
						LocalLinkConnection: localLinkConnection{
							PortID:     port.PortID,
							SwitchInfo: port.SwitchName,
							SwitchID:   port.ChassisID,
							PortIndex:  port.PortIndex,
						},
					},
					ESXIPortInfo: esxiPortInfo{
						DVSName: port.DVSName,
					},
				},
			},
		},
	}
	jsonData, err := json.Marshal(portResource)
	if err != nil {
		return err
	}
	resp, _, err := cc.sendRequest("/sync", string(jsonData), "POST") //nolint: bodyclose
	if err != nil {
		return err
	}
	log.Debug("Got status : ", resp.StatusCode)
	switch resp.StatusCode {
	default:
		return fmt.Errorf("resource creation failed, %d", resp.StatusCode)
	case 200, 201:
	}
	return nil
}
