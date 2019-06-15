package client

import (
	"bytes"
	"crypto/tls"
	"encoding/json"
	"fmt"
	vc "github.com/Juniper/contrail-controller/src/config/go/pkg/vcenter"
	"io/ioutil"
	"net/http"
)

type ContrailCommandNodeSync struct {
	Resources []NodeResources `json:"resources"`
}
type NodeData struct {
	NodeType string   `json:"node_type"`
	UUID     string   `json:"uuid"`
	Hostname string   `json:"hostname"`
	FqName   []string `json:"fq_name"`
}
type NodeResources struct {
	Kind string   `json:"kind"`
	Data NodeData `json:"data"`
}

type ContrailCommandPortSync struct {
	Resources []PortResources `json:"resources"`
}
type LocalLinkConnection struct {
	PortID     string `json:"port_id"`
	SwitchInfo string `json:"switch_info"`
	SwitchID   string `json:"switch_id"`
	PortIndex  string `json:"port_index"`
}
type BmsPortInfo struct {
	Address             string              `json:"address"`
	LocalLinkConnection LocalLinkConnection `json:"local_link_connection"`
}
type EsxiPortInfo struct {
	DvsName string `json:"dvs_name"`
}
type PortData struct {
	Name         string       `json:"name"`
	UUID         string       `json:"uuid"`
	FqName       []string     `json:"fq_name"`
	BmsPortInfo  BmsPortInfo  `json:"bms_port_info"`
	EsxiPortInfo EsxiPortInfo `json:"esxi_port_info"`
}
type PortResources struct {
	Kind string   `json:"kind"`
	Data PortData `json:"data"`
}

type ContrailCommandNodes struct {
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

type KeyStoneAuthViaUsername struct {
	Auth UserAuth `json:"auth"`
}
type Domain struct {
	ID string `json:"id"`
}
type User struct {
	Name     string `json:"name"`
	Password string `json:"password"`
	Domain   Domain `json:"domain"`
}
type Password struct {
	User User `json:"user"`
}
type UserIdentity struct {
	Methods  []string `json:"methods"`
	Password Password `json:"password"`
}
type Project struct {
	Name   string `json:"name"`
	Domain Domain `json:"domain"`
}
type Scope struct {
	Project Project `json:"project"`
}
type UserAuth struct {
	Identity UserIdentity `json:"identity"`
	Scope    Scope        `json:"scope"`
}

type KeyStoneAuthViaClusterToken struct {
	Auth Auth `json:"auth"`
}
type Token struct {
	ID string `json:"id"`
}
type Cluster struct {
	ID    string `json:"id"`
	Token Token  `json:"token"`
}
type Identity struct {
	Methods []string `json:"methods"`
	Cluster Cluster  `json:"cluster"`
}
type Auth struct {
	Identity Identity `json:"identity"`
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
func GetDefaultValues(CCHost string, username string, password string, clusterID string, clusterToken string) ContrailCommand {
	CCData := ContrailCommand{}
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
	fmt.Println("CCData", ccData.ClusterID, ccData.ClusterToken)
	authData := KeyStoneAuthViaClusterToken{
		Auth: Auth{
			Identity: Identity{
				Methods: []string{"cluster-token"},
				Cluster: Cluster{
					ID: ccData.ClusterID,
					Token: Token{
						ID: ccData.ClusterToken,
					},
				},
			},
		},
	}

	jsonData, err := json.Marshal(authData)
	fmt.Println(string(jsonData))
	resp, _, err := ccData.sendRequest(ccData.AuthPath, string(jsonData), "POST")
	if err != nil {
		return "", err
	}
	fmt.Println("response Status:", resp.StatusCode)
	fmt.Println("response Headers:", resp.Header["X-Subject-Token"])
	//fmt.Println("response Body:", string(body))
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
	authData := KeyStoneAuthViaUsername{
		Auth: UserAuth{
			Identity: UserIdentity{
				Methods: []string{"password"},
				Password: Password{
					User: User{
						Name:     ccData.Username,
						Password: ccData.Password,
						Domain: Domain{
							ID: ccData.UserDomain,
						},
					},
				},
			},
			Scope: Scope{
				Project: Project{
					Name: ccData.ProjectName,
					Domain: Domain{
						ID: ccData.ProjectDomain,
					},
				},
			},
		},
	}

	jsonData, err := json.Marshal(authData)
	fmt.Println(string(jsonData))
	resp, _, err := ccData.sendRequest(ccData.AuthPath, string(jsonData), "POST")
	if err != nil {
		return "", err
	}
	fmt.Println("response0 Status:", resp.StatusCode)
	if resp.StatusCode != 200 && resp.StatusCode != 201 {
		fmt.Println("returning due to non-200")
		//TODO: send error back
		return "", nil
	}
	fmt.Println("response Headers:", resp.Header["X-Subject-Token"])
	//fmt.Println("response Body:", string(body))
	token := resp.Header["X-Subject-Token"][0]
	ccData.AuthToken = token
	return token, nil
}

// Caller should call resp.Body.Close()
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
	fmt.Println("REQ", req)
	resp, err := client.Do(req)
	fmt.Println("RESP-CODE: ", resp.StatusCode)
	defer resp.Body.Close()
	if err != nil {
		return nil, nil, err
	}
	body, _ := ioutil.ReadAll(resp.Body)
	return resp, body, nil
}

// GetNodes get all nodes,ports from contrail-command and returns CCNode map
func (ccData ContrailCommand) GetNodes() (map[string]CCNode, error) {
	resp, body, err := ccData.sendRequest("/nodes?detail=true", "", "GET")
	if err != nil {
		fmt.Println("GetNodes-Returning", err)
		return nil, err
	}

	fmt.Println("GetNodes Status:", resp.StatusCode)
	ccNodes := make(map[string]CCNode)

	var nodeJSON ContrailCommandNodes
	err = json.Unmarshal(body, &nodeJSON)
	if err != nil {
		panic(err)
	}
	for num, node := range nodeJSON.Nodes {
		fmt.Println("NODE ", node.Node.UUID, node.Node.FqName)
		fmt.Println(num)
		ccPorts := make(map[string]CCPort)
		for _, port := range node.Node.Ports {
			ccport := CCPort{port.Name, port.UUID}
			ccPorts[port.Name] = ccport
		}
		ccNode := CCNode{node.Node.Name, node.Node.UUID, ccPorts}
		ccNodes[node.Node.Name] = ccNode
	}
	fmt.Println("NODES", ccNodes)
	return ccNodes, nil
}

// CreateNode creates node into contrail-command, it takes EsxiHost as input
func (ccData ContrailCommand) CreateNode(host vc.EsxiHost) error {
	nodeResource := ContrailCommandNodeSync{
		Resources: []NodeResources{
			NodeResources{
				Kind: "node",
				Data: NodeData{
					NodeType: "esxi",
					UUID:     host.UUID,
					Hostname: host.Hostname,
					FqName:   []string{"default-global-system-config", host.Hostname},
				},
			},
		},
	}
	jsonData, err := json.Marshal(nodeResource)
	resp, _, err := ccData.sendRequest("/sync", string(jsonData), "POST")
	fmt.Println("CreateNode:", resp.StatusCode, err)
	return nil
}

// CreatePort create port into contrail-command from EsxiPort and EsxiHost
func (ccData ContrailCommand) CreatePort(port vc.EsxiPort, host vc.EsxiHost) error {
	fmt.Println(port, host.Hostname)
	portResource := ContrailCommandPortSync{
		Resources: []PortResources{
			PortResources{
				Kind: "port",
				Data: PortData{
					Name:   port.Name,
					UUID:   port.UUID,
					FqName: []string{"default-global-system-config", host.Hostname, port.Name},
					BmsPortInfo: BmsPortInfo{
						Address: port.MacAddress,
						LocalLinkConnection: LocalLinkConnection{
							PortID:     port.PortID,
							SwitchInfo: port.SwitchName,
							SwitchID:   port.ChassisID,
							PortIndex:  port.PortIndex,
						},
					},
					EsxiPortInfo: EsxiPortInfo{
						DvsName: port.DvsName,
					},
				},
			},
		},
	}
	jsonData, err := json.Marshal(portResource)
	resp, _, err := ccData.sendRequest("/sync", string(jsonData), "POST")
	fmt.Println("CreateNode:", resp.StatusCode, err)
	if err != nil {
		return err
	}
	return nil
}
