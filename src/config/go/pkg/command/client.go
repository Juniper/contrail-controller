package contrail_command_client

import (
	"bytes"
	"crypto/tls"
	"encoding/json"
	"fmt"
	vc "github.com/Juniper/contrail-controller/src/config/go/pkg/vcenter"
	"io/ioutil"
	"net/http"
)

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

type ContrailCommand struct {
	AuthHost      string
	AuthPort      string
	AuthPath      string
	Username      string
	Password      string
	UserDomain    string
	ProjectDomain string
	ProjectName   string
	ClusterId     string
	ClusterToken  string
	AuthToken     string
}

type CCPort struct {
	Name string
	Uuid string
}

type CCNode struct {
	Name  string
	Uuid  string
	Ports map[string]CCPort
}

func GetDefaultValues(CCHost string, username string, password string, clusterID string, clusterToken string) ContrailCommand {
	CCData := ContrailCommand{}
	CCData.AuthHost = CCHost
	CCData.Username = username
	CCData.Password = password
	CCData.ClusterId = clusterID
	CCData.ClusterToken = clusterToken
	CCData.AuthPath = "/keystone/v3/auth/tokens"
	CCData.UserDomain = "default"
	CCData.ProjectName = "admin"
	CCData.ProjectDomain = "default"
	return CCData
}

func (ccData ContrailCommand) GetTokenViaClusterToken() (string, error) {
	fmt.Println("CCData", ccData.ClusterId, ccData.ClusterToken)
	authData := KeyStoneAuthViaClusterToken{
		Auth: Auth{
			Identity: Identity{
				Methods: []string{"cluster-token"},
				Cluster: Cluster{
					ID: ccData.ClusterId,
					Token: Token{
						ID: ccData.ClusterToken,
					},
				},
			},
		},
	}

	jsonData, err := json.Marshal(authData)
	fmt.Println(string(jsonData))
	resp, _, err := ccData.SendRequest(ccData.AuthPath, string(jsonData), "POST")
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
	resp, _, err := ccData.SendRequest(ccData.AuthPath, string(jsonData), "POST")
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
func (cc ContrailCommand) SendRequest(resourcePath string, resourceData string, requestMethod string) (*http.Response, []byte, error) {
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
	fmt.Println("REQ", req)
	resp, err := client.Do(req)
	defer resp.Body.Close()
	if err != nil {
		return nil, nil, err
	}
	body, _ := ioutil.ReadAll(resp.Body)
	return resp, body, nil
}

func (cc ContrailCommand) GetNodes() (map[string]CCNode, error) {
	resp, body, err := cc.SendRequest("/nodes?detail=true", "", "GET")
	if err != nil {
		fmt.Println("GetNodes-Returning", err)
		return nil, err
	}

	fmt.Println("GetNodes Status:", resp.StatusCode)
	//fmt.Println("GetNodes Body:", string(body))
	var data map[string]interface{}
	ccNodes := make(map[string]CCNode)

	err = json.Unmarshal(body, &data)
	if err != nil {
		panic(err)
	}
	all_nodes := data["nodes"].([]interface{})
	for _, node_map := range all_nodes {
		nodeDict := node_map.(map[string]interface{})
		node := nodeDict["node"].(map[string]interface{})
		fmt.Println(node["fq_name"], node["uuid"])
		ccPorts := make(map[string]CCPort)
		if node["ports"] != nil {
			fmt.Println("PORT!!", node["name"])
			portsDict := node["ports"].([]interface{})
			for _, portMap := range portsDict {
				portDict := portMap.(map[string]interface{})
				fmt.Println("PORT-NAME", portDict["name"], portDict["fq_name"], portDict["uuid"])
				ccport := CCPort{portDict["name"].(string), portDict["uuid"].(string)}
				ccPorts[portDict["name"].(string)] = ccport
			}
		}
		ccNode := CCNode{node["name"].(string), node["uuid"].(string), ccPorts}
		ccNodes[node["name"].(string)] = ccNode
	}
	fmt.Println("NODES", ccNodes)
	return ccNodes, nil
}

func (cc ContrailCommand) CreateNode(host vc.EsxiHost) error {
	//fmt.Println(host)
	jsonStr := fmt.Sprintf(`{"resources": [{"kind": "node","data":{ 
                                 "node_type": "esxi",
				 "uuid": "%s",
                                 "hostname": "%s",
                                 "fq_name": ["default-global-system-config", "%s"]
			       }}]}`, host.Uuid, host.Hostname, host.Hostname)
	resp, _, err := cc.SendRequest("/sync", jsonStr, "POST")
	fmt.Println("CreateNode:", resp.StatusCode, err)
	return nil
}

func (cc ContrailCommand) CreatePort(port vc.EsxiPort, host vc.EsxiHost) error {
	fmt.Println(port, host.Hostname)
	jsonStr := fmt.Sprintf(`{"resources": [{"kind": "port","data":{ 
                                 "name": "%s",
				 "uuid": "%s",
                                 "fq_name": ["default-global-system-config", "%s", "%s"],
				 "bms_port_info": {
				   "address": "%s",
				   "local_link_connection": {
				     "port_id": "%s",
				     "switch_info": "%s",
				     "switch_id": "%s",
				     "port_index": "%s"
				 }},
				 "esxi_port_info": {
				   "dvs_name": "%s"
				}}}]}`, port.Name, port.Uuid, host.Hostname, port.Name, port.MacAddress,
		port.PortId, port.SwitchName, port.ChassisId, port.PortIndex, port.DvsName)
	_, _, err := cc.SendRequest("/sync", jsonStr, "POST")
	if err != nil {
		return err
	}
	//fmt.Println("CreatePort:", resp.StatusCode, err)
	return nil
}
