package command

import (
	"bytes"
	"crypto/tls"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"

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

type contrailCommandNodes struct {
	Nodes []struct {
		Node struct {
			UUID     string   `json:"uuid"`
			Name     string   `json:"name"`
			FqName   []string `json:"fq_name"`
			Hostname string   `json:"hostname"`
			NodeType string   `json:"node_type"`
			Ports    []struct {
				UUID       string   `json:"uuid"`
				Name       string   `json:"name"`
				ParentUUID string   `json:"parent_uuid"`
				ParentType string   `json:"parent_type"`
				FqName     []string `json:"fq_name"`
			} `json:"ports"`
		} `json:"node"`
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

// ContrailCommand Struct to hold values from job_input to about contrail-command
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

// CCPort holds name and uuid for port from contrail-command
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

// GetDefaultValues Initializes the ContrailCommand struct from provided
// values and default values.
func GetDefaultValues(CCHost, username, password, clusterID, clusterToken string) (*ContrailCommand) {
	CCData := &ContrailCommand{}
	CCData.AuthHost = CCHost
	CCData.Username = username
	CCData.Password = password
	CCData.ClusterID = clusterID
	CCData.ClusterToken = clusterToken
	CCData.AuthPath = "/keystone/v3/auth/tokens"
	CCData.UserDomain = "default"
	CCData.ProjectName = "admin"
	CCData.ProjectDomain = "default"
	return CCData
}

// GetTokenViaClusterToken gets the token from contrail-command using
// cluster-token and clusterId
func (ccData ContrailCommand) GetTokenViaClusterToken() (string, error) {
	authData := keyStoneAuthViaClusterToken{
		Auth: auth{
			Identity: identity{
				Methods: []string{"cluster-token"},
				Cluster: cluster{
					ID: ccData.ClusterID,
					Token: token{
						ID: ccData.ClusterToken,
					},
				},
			},
		},
	}

	jsonData, err := json.Marshal(authData)
	if err != nil {
		return "", err
	}
	resp, _, err := ccData.sendRequest(ccData.AuthPath, string(jsonData), "POST")
	if err != nil {
		return "", err
	}
	if resp.StatusCode != 200 && resp.StatusCode != 201 {
		return "", nil
	}
	token := resp.Header["X-Subject-Token"][0]
	ccData.AuthToken = token
	return token, nil
}

// GetTokenViaUsername get the auth token from contrail-command using
// username/password and domain
func (ccData *ContrailCommand) GetTokenViaUsername() (string, error) {
	authData := keyStoneAuthViaUsername{
		Auth: userAuth{
			Identity: userIdentity{
				Methods: []string{"password"},
				Password: password{
					User: user{
						Name:     ccData.Username,
						Password: ccData.Password,
						Domain: domain{
							ID: ccData.UserDomain,
						},
					},
				},
			},
			Scope: scope{
				Project: project{
					Name: ccData.ProjectName,
					Domain: domain{
						ID: ccData.ProjectDomain,
					},
				},
			},
		},
	}

	jsonData, err := json.Marshal(authData)
	if err != nil {

	}
	resp, _, err := ccData.sendRequest(ccData.AuthPath, string(jsonData), "POST")
	if err != nil {
		return "", err
	}
	if resp.StatusCode != 200 && resp.StatusCode != 201 {
		fmt.Println("returning due to non-200")
		//TODO: send error back
		return "", nil
	}
	token := resp.Header["X-Subject-Token"][0]
	ccData.AuthToken = token
	return token, nil
}

func responseBodyClose(resp *http.Response) {
	err := resp.Body.Close()
	if err != nil {
		log.Print(err)
	}
}
func (ccData ContrailCommand) sendRequest(resourcePath string, resourceData string, requestMethod string) (*http.Response, []byte, error) {
	url := fmt.Sprintf("https://%s%s", ccData.AuthHost, resourcePath)
	tr := &http.Transport{
		TLSClientConfig: &tls.Config{InsecureSkipVerify: true},
	}
	req, err := http.NewRequest(requestMethod, url, bytes.NewBufferString(resourceData))
	if err != nil {
		return nil, nil, err
	}
	req.Header.Set("Content-Type", "application/json")
	if ccData.AuthToken != "" {
		req.Header.Set("X-Auth-Token", ccData.AuthToken)
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

// GetNodes get all nodes,ports from contrail-command and returns CCNode map
func (ccData ContrailCommand) GetNodes() (map[string]CCNode, error) {
	_, body, err := ccData.sendRequest("/nodes?detail=true", "", "GET")
	if err != nil {
		return nil, err
	}

	ccNodes := map[string]CCNode{}

	nodeJSON := &contrailCommandNodes{}
	if err = json.Unmarshal(body, &nodeJSON); err != nil {
		return nil, err
	}
	for _, node := range nodeJSON.Nodes {
		ccPorts := make(map[string]CCPort)
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
func (ccData ContrailCommand) CreateNode(host vcenter.ESXIHost) error {
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
	_, _, err = ccData.sendRequest("/sync", string(jsonData), "POST")
	return nil
}

// CreatePort create port into contrail-command from ESXIPort and ESXiHost
func (ccData ContrailCommand) CreatePort(port vcenter.ESXIPort, host vcenter.ESXIHost) error {
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
	_, _, err = ccData.sendRequest("/sync", string(jsonData), "POST")
	if err != nil {
		return err
	}
	return nil
}
