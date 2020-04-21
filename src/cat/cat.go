// Package cat provides integration testing framework for control-nodes, agents
// and CRPDs. Each control-node and agent runs as a separate unix process.
// Each CRPD runs inside a docker container. Framework hangles the confguration
// and life cycle management of all these objects.
package cat

import (
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"time"

	"cat/agent"
	"cat/config"
	"cat/controlnode"
	"cat/crpd"
	"cat/sut"

	log "github.com/sirupsen/logrus"
)

// CAT is the main place-holder object of all other objects managed.
type CAT struct {
	SUT          sut.Component
	ControlNodes []*controlnode.ControlNode
	Agents       []*agent.Agent
	CRPDs        []*crpd.CRPD
	FqNameTable  config.FQNameTableType
	UuidTable    config.UUIDTableType
	ConfigMap    config.ConfigMap
}

// Timestamp format for logfiles.
const timestamp = "20060102_150405"

// Utiulity command to load CRPD docker image dynamically.
const crpdImageGetCommand = "sshpass -p c0ntrail123 ssh 10.84.5.39 cat /cs-shared/crpd/crpd.tgz | sudo --non-interactive docker load"

// New creates and initializes a CAT instance.
func New() (*CAT, error) {
	c := &CAT{
		ControlNodes: []*controlnode.ControlNode{},
		Agents:       []*agent.Agent{},
		CRPDs:        []*crpd.CRPD{},
		FqNameTable:  config.FQNameTableType{},
		UuidTable:    config.UUIDTableType{},
		ConfigMap:    config.ConfigMap{},
	}
	now := time.Now()

	cwd, err := os.Getwd()
	if err != nil {
		return nil, fmt.Errorf("Cannot find present working directory: %v", err)
	}
	c.SUT.Manager.RootDir = filepath.Join(cwd+"../../../../build/debug/cat", now.Format(timestamp))
	if err := os.MkdirAll(c.SUT.Manager.RootDir, 0700); err != nil {
		return nil, fmt.Errorf("failed to create rootdir %q :%v", c.SUT.Manager.RootDir, err)
	}
	c.SUT.Manager.ReportDir = filepath.Join(c.SUT.Manager.RootDir, "reports")
	err = os.MkdirAll(c.SUT.Manager.ReportDir, 0700)
	if err != nil {
		return nil, fmt.Errorf("failed to make report directory: %v", err)
	}
	c.setHostIP()

	// Check whether CRPD can be used in this test environment.
	if crpd.CanUseCRPD() {
		cmd := exec.Command("sudo", "--non-interactive", "/usr/bin/docker", "image", "inspect", "crpd")
		if _, err := cmd.Output(); err != nil {
			// Setup crpd docker image.
			cmd := exec.Command("/bin/bash", "-c", crpdImageGetCommand)
			if _, err := cmd.Output(); err != nil {
				return nil, fmt.Errorf("Cannot load crpd docker image")
			}
		}
	}
	log.Infof("Test data in %q", c.SUT.Manager.RootDir)
	return c, err
}

// Teardown stops all components and closes down the CAT instance.
func (c *CAT) Teardown() error {
	for _, cn := range c.ControlNodes {
		if err := cn.Teardown(); err != nil {
			return err
		}
	}
	for _, a := range c.Agents {
		if err := a.Teardown(); err != nil {
			return err
		}
	}

	for _, cr := range c.CRPDs {
		if err := cr.Teardown(); err != nil {
			return err
		}
	}
	return nil
}

// setHostIP finds self host ip address.
func (c *CAT) setHostIP() error {
	cmd := exec.Command("hostname", "-i")
	out, err := cmd.CombinedOutput()
	if err != nil {
		log.Fatal("Cannot find host ip address")
		return err
	}

	ips := strings.Split(string(out), " ")
	for _, ip := range ips {
		if !strings.HasPrefix(ip, "127.") {
			c.SUT.Manager.IP = strings.Trim(ip, "\n")
			return nil
		}
	}
	return errors.New("Cannot retrieve host ip address")
}

// AddAgent creates a contrail-vrouter-agent object and starts the mock agent
// process in background.
func (c *CAT) AddAgent(test string, name string, control_nodes []*controlnode.ControlNode) (*agent.Agent, error) {
	endpoints := []sut.Endpoint{}
	for _, control_node := range control_nodes {
		endpoints = append(endpoints, sut.Endpoint{
			IP:   control_node.IPAddress,
			Port: control_node.Config.XMPPPort,
		})
	}
	agent, err := agent.New(c.SUT.Manager, name, test, endpoints)
	if err != nil {
		return nil, fmt.Errorf("failed create agent: %v", err)
	}
	c.Agents = append(c.Agents, agent)
	return agent, nil
}

// AddControlNode creates a contrail-control object and starts the mock
// control-node process in the background.
func (c *CAT) AddControlNode(test, name, ip_address, conf_file string, bgp_port int) (*controlnode.ControlNode, error) {
	cn, err := controlnode.New(c.SUT.Manager, name, ip_address, conf_file, test, bgp_port)
	if err != nil {
		return nil, fmt.Errorf("failed to create control-node: %v", err)
	}
	cn.Verbose = c.SUT.Manager.Verbose
	c.ControlNodes = append(c.ControlNodes, cn)
	log.Infof("Started %s at http://%s:%d\n", cn.Name, c.SUT.Manager.IP, cn.Config.HTTPPort)
	return cn, nil
}

// AddCRPD creates a CRPD object and starts the CRPD docker container process
// in the background.
func (c *CAT) AddCRPD(test, name string) (*crpd.CRPD, error) {
	cr, err := crpd.New(c.SUT.Manager, name, test)
	if err != nil {
		return nil, fmt.Errorf("failed to create crpd: %v", err)
	}
	c.CRPDs = append(c.CRPDs, cr)
	return cr, nil
}

func GetFreePort() (int, error) {
	addr, err := net.ResolveTCPAddr("tcp", "localhost:0")
	if err != nil {
		return 0, err
	}

	l, err := net.ListenTCP("tcp", addr)
	if err != nil {
		return 0, err
	}
	defer l.Close()
	return l.Addr().(*net.TCPAddr).Port, nil
}

func ReplacePortNumbers(input string, port int) (out string) {
	p := fmt.Sprintf("\"port\":%d", port)

	re1 := regexp.MustCompile("\"port\":null")
	val1 := re1.ReplaceAllString(input, p)
	re2 := regexp.MustCompile("\"port\":[[:digit:]]{3,5}")
	val2 := re2.ReplaceAllString(val1, p)

	re3 := regexp.MustCompile("\"port\": null")
	val3 := re3.ReplaceAllString(val2, p)
	re4 := regexp.MustCompile("\"port\": [[:digit:]]{3,5}")
	val4 := re4.ReplaceAllString(val3, p)

	return val4
}

func ReplaceAddress(input string, address string) (out string) {

	a := fmt.Sprintf("\"address\":\"%s\"", address)
	id := fmt.Sprintf("\"identifier\":\"%s\"", address)

	re3 := regexp.MustCompile(`(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)(\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)){3}`)
	val3 := re3.ReplaceAllString(input, address)

	re1 := regexp.MustCompile("\"address\":null")
	val1 := re1.ReplaceAllString(val3, a)
	re2 := regexp.MustCompile("\"address\": null")
	val2 := re2.ReplaceAllString(val1, a)

	re4 := regexp.MustCompile("\"identifier\":null")
	val4 := re4.ReplaceAllString(val2, id)
	re5 := regexp.MustCompile("\"identifier\": null")
	val5 := re5.ReplaceAllString(val4, id)

	return val5
}

func GetNumOfControlNodes() (ConNodesDS map[string]interface{}, err error) {
	var confile string
	confile = controlnode.GetConfFile()
	jsonFile, err := os.Open(confile)
	if err != nil {
		return nil, fmt.Errorf("failed to open ConfFile: %v", err)
	}

	defer jsonFile.Close()
	byteValue, _ := ioutil.ReadAll(jsonFile)
	//var result map[string]interface{}
	var result interface{}
	json.Unmarshal([]byte(byteValue), &result)

	ConNodesDS = make(map[string]interface{})

	//res := result["cassandra"].(map[string]interface{})
	res := result.([]interface{})
	res1 := res[0].(map[string]interface{})
	res2 := res1["OBJ_FQ_NAME_TABLE"].(map[string]interface{})
	//res1 := res["config_db_uuid"].(map[string]interface{})
	//res2 := res1["obj_fq_name_table"].(map[string]interface{})
	res3 := res2["bgp_router"].(map[string]interface{})
	//res4 := res1["obj_uuid_table"].(map[string]interface{})
	res4 := res1["db"].(map[string]interface{})
	i := 1
	ipoctet := 127
	for key := range res3 {
		re := regexp.MustCompile(":")
		val := re.Split(key, -1)
		res5 := res4[val[5]].(map[string]interface{})
		//res6 := res5["prop:bgp_router_parameters"].([]interface{})
		res6 := res5["prop:bgp_router_parameters"]
		//res7 := fmt.Sprintf("%v", res6[0])
		res7 := fmt.Sprintf("%v", res6)
		port, _ := GetFreePort()
		address := fmt.Sprintf("%d.0.0.%d", ipoctet, i)

		//Replace Port Numbers
		val2 := ReplacePortNumbers(res7, port)

		//Replace address and identifier
		val3 := ReplaceAddress(val2, address)

		res6 = val3
		//res6[0] = val2
		res5["prop:bgp_router_parameters"] = res6
		res4[val[5]] = res5
		m := make(map[string]interface{})
		m["port"] = port
		m["address"] = address
		ConNodesDS[val[4]] = m
		i++
	}
	res1["db"] = res4
	//res1["obj_uuid_table"] = res4
	res[0] = res1
	//res["config_db_uuid"] = res1
	//result["cassandra"] = res
	result = res

	write, _ := json.Marshal(result)
	err = ioutil.WriteFile(confile, write, os.ModePerm)
	jsonFile.Sync()
	return ConNodesDS, nil
}
