// vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//
/****************************************************************************
 * VRouter routines for CNI plugin
 ****************************************************************************/
package contrailCni

import (
	log "../logging"
	"bytes"
	"encoding/json"
	"fmt"
	"github.com/containernetworking/cni/pkg/types"
	"github.com/containernetworking/cni/pkg/types/current"
	"io/ioutil"
	"net"
	"net/http"
	"os"
	"strconv"
	"time"
)

// Default VRouter values
const VROUTER_AGENT_IP = "127.0.0.1"
const VROUTER_AGENT_PORT = 9091
const VROUTER_POLL_TIMEOUT = 3
const VROUTER_POLL_RETRIES = 20

//Directory containing configuration for the container
const VROUTER_CONFIG_DIR = "/var/lib/contrail/ports/vm"

/* struct to hold data for a connection to VRouter Agent */
type VRouter struct {
	Server        string `json:"vrouter-ip"`
	Port          int    `json:"vrouter-port"`
	Dir           string `json:"config-dir"`
	PollTimeout   int    `json:"poll-timeout"`
	PollRetries   int    `json:"poll-retries"`
	containerUuid string
	containerVn   string
	httpClient    *http.Client
}

type vrouterJson struct {
	VRouter VRouter `json:"contrail"`
}

// Make filename to store config
func (vrouter *VRouter) makeFileName() string {
	fname := vrouter.Dir + "/" + vrouter.containerUuid
	if vrouter.containerVn != "" {
		fname = fname + "/" + vrouter.containerVn
	}

	return fname
}

// Make URL for operation
func (vrouter *VRouter) makeUrl(containerUuid, containerVn,
	page string) string {
	url := "http://" + vrouter.Server + ":" + strconv.Itoa(vrouter.Port) + page
	if len(containerUuid) > 0 {
		url = url + "/" + vrouter.containerUuid
	}
	if len(containerVn) > 0 {
		url = url + "/" + vrouter.containerVn
	}
	return url
}

// Do a HTTP operation to VRouter
func (vrouter *VRouter) doOp(op, containerUuid, containerVn, page string,
	msg []byte) (*http.Response, error) {

	url := vrouter.makeUrl(containerUuid, containerVn, page)
	req, err := http.NewRequest(op, url, bytes.NewBuffer(msg))
	if err != nil {
		log.Errorf("Error creating http Request. Op %s Url %s Msg %s."+
			"Error : %+v", op, url, err)
		return nil, err
	}
	req.Header.Set("Content-Type", "application/json")

	resp, err := vrouter.httpClient.Do(req)
	if err != nil {
		log.Errorf("Failed HTTP operation :  %+v. Error : %+v", req, err)
		return nil, err
	}

	return resp, nil
}

/****************************************************************************
 * GET /vm response from VRouter
 ****************************************************************************/
type Result struct {
	VmUuid string `json:"vm-uuid"`
	Nw     string `json:"network-label"`
	Ip     string `json:"ip-address"`
	Plen   int    `json:"plen"`
	Gw     string `json:"gateway"`
	Dns    string `json:"dns-server"`
	Mac    string `json:"mac-address"`
	VlanId int    `json:"vlan-id"`
}

// Convert result from VRouter format to CNI format
func MakeCniResult(ifname string, vrouterResult *Result) *current.Result {
	result := &current.Result{}

	intf := &current.Interface{Name: ifname, Mac: vrouterResult.Mac}
	result.Interfaces = append(result.Interfaces, intf)

	mask := net.CIDRMask(vrouterResult.Plen, 32)
	ip := &current.IPConfig{
		Version: "4",
		Address: net.IPNet{IP: net.ParseIP(vrouterResult.Ip), Mask: mask},
		Gateway: net.ParseIP(vrouterResult.Gw)}
	result.IPs = append(result.IPs, ip)

	_, defaultNet, _ := net.ParseCIDR("0.0.0.0/0")
	rt := &types.Route{Dst: *defaultNet, GW: net.ParseIP(vrouterResult.Gw)}
	result.Routes = append(result.Routes, rt)

	result.DNS.Nameservers = append(result.DNS.Nameservers, vrouterResult.Dns)
	return result
}

// Get operation from VRouter
func (vrouter *VRouter) Get(url string) (*Result, error) {
	var req []byte
	resp, err := vrouter.doOp("GET", vrouter.containerUuid,
		vrouter.containerVn, url, req)
	if err != nil {
		log.Errorf("Failed HTTP GET operation")
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		msg := fmt.Sprintf("Failed HTTP Get operation. Return code %s",
			string(resp.StatusCode))
		log.Errorf(msg)
		return nil, fmt.Errorf(msg)
	}

	var result Result
	var body []byte

	body, err = ioutil.ReadAll(resp.Body)
	if err != nil {
		log.Errorf("Error in reading HTTP GET response. Error : %+v", err)
		return nil, err
	}

	log.Infof("VRouter response %s", string(body))
	err = json.Unmarshal(body, &result)
	if err != nil {
		log.Errorf("Error decoding HTTP Get response. Error : %+v", err)
		return nil, err
	}

	return &result, nil
}

// Poll response from VRouter
func (vrouter *VRouter) PollUrl(url string) (*Result, error) {
	var msg string
	for i := 0; i < vrouter.PollRetries; i++ {
		result, err := vrouter.Get(url)
		if err == nil {
			log.Infof("Get from vrouter passed. Result %+v", result)
			return result, nil
		}

		msg = err.Error()
		log.Infof("Iteration %d : Get vrouter failed", i)
		time.Sleep(time.Duration(vrouter.PollTimeout) * time.Second)
	}

	return nil, fmt.Errorf("Failed in PollVM. Error : %s", msg)
}

/****************************************************************************
 * ADD message handling
 ****************************************************************************/
// Add request to VRouter
type contrailAddMsg struct {
	Time            string `json:"time"`
	Vm              string `json:"vm-id"`
	VmUuid          string `json:"vm-uuid"`
	VmName          string `json:"vm-name"`
	HostIfName      string `json:"host-ifname"`
	ContainerIfName string `json:"vm-ifname"`
	Namespace       string `json:"vm-namespace"`
}

// Make JSON for Add Message
func makeAddMsg(containerName, containerUuid, containerId, containerNamespace,
	containerIfName, hostIfName string) []byte {
	t := time.Now()
	addMsg := contrailAddMsg{Time: t.String(), Vm: containerId,
		VmUuid: containerUuid, VmName: containerName, HostIfName: hostIfName,
		ContainerIfName: containerIfName, Namespace: containerNamespace}

	msg, err := json.MarshalIndent(addMsg, "", "\t")
	if err != nil {
		return nil
	}

	return msg
}

// Store the config to file for persistency
func (vrouter *VRouter) addVmToFile(addMsg []byte) error {
	_, err := os.Stat(vrouter.Dir)
	if err != nil {
		log.Errorf("Error accessing VM config directory %s. Error : %s",
			vrouter.Dir, err)
		return err
	}

	// Write file based on VM name
	fname := vrouter.makeFileName()
	err = ioutil.WriteFile(fname, addMsg, 0644)
	if err != nil {
		log.Errorf("Error writing VM config file %s. Error : %s", fname, err)
		return err
	}

	return nil
}

func (vrouter *VRouter) addVmToAgent(addMsg []byte) error {
	resp, err := vrouter.doOp("POST", "", "", "/vm", addMsg)
	if err != nil {
		log.Errorf("Failed in HTTP POST operation")
		return err
	}
	defer resp.Body.Close()

	if (resp.StatusCode != http.StatusOK) &&
		(resp.StatusCode != http.StatusCreated) {
		msg := fmt.Sprintf("Failed HTTP Post operation. Return code %s",
			string(resp.StatusCode))
		log.Errorf(msg)
		return fmt.Errorf(msg)
	}

	return nil
}

/* Process add of a VM. Writes config file and send message to agent */
func (vrouter *VRouter) Add(containerName, containerUuid, containerVn,
	containerId, containerNamespace, containerIfName,
	hostIfName string) (*Result, error) {
	vrouter.containerUuid = containerUuid
	vrouter.containerVn = containerVn
	// Make Add Message structure
	addMsg := makeAddMsg(containerName, containerUuid, containerId,
		containerNamespace, containerIfName, hostIfName)
	log.Infof("VRouter add message is %s", addMsg)

	// Store config to file for persistency
	if err := vrouter.addVmToFile(addMsg); err != nil {
		// Fail adding VM if directory not present
		log.Errorf("Error storing config file")
		return nil, err
	}

	// Make the agent call
	err := vrouter.addVmToAgent(addMsg)
	if err != nil {
		log.Errorf("Error in Add to VRouter")
		return nil, err
	}

	result, poll_err := vrouter.PollUrl("/vm")
	if poll_err != nil {
		log.Errorf("Error in polling VRouter")
		return nil, poll_err
	}

	return result, nil
}

/****************************************************************************
 * DEL message handling
 ****************************************************************************/
// Del VM config file
func (vrouter *VRouter) delVmToFile() error {
	fname := vrouter.makeFileName()
	_, err := os.Stat(fname)
	// File not present... noting to do
	if err != nil {
		log.Infof("File %s not found. Error : %s", fname, err)
		return nil
	}

	// Delete file
	err = os.Remove(fname)
	if err != nil {
		log.Infof("Failed deleting file %s. Error : %s", fname, err)
		return nil
	}

	return nil
}

func (vrouter *VRouter) delVmToAgent() error {
	var req []byte
	resp, err := vrouter.doOp("DELETE", vrouter.containerUuid,
		vrouter.containerVn, "/vm", req)
	if err != nil {
		log.Errorf("Failed HTTP DELETE operation")
		return err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		msg := fmt.Sprintf("Failed HTTP Delete operation. Return code %s",
			string(resp.StatusCode))
		log.Errorf(msg)
		return fmt.Errorf(msg)
	}
	return nil
}

/* Process delete VM. The method ignores intermediate errors and does best
 * effort cleanup
 */
func (vrouter *VRouter) Del(containerUuid, containerVn string) error {
	vrouter.containerUuid = containerUuid
	vrouter.containerVn = containerVn
	var ret error
	// Remove the configuraion file stored for persistency
	if err := vrouter.delVmToFile(); err != nil {
		log.Infof("Error in deleting config file")
		ret = err
	}

	// Make the del message calll to agent
	if err := vrouter.delVmToAgent(); err != nil {
		log.Errorf("Error in Delete to VRouter")
		ret = err
	}

	return ret
}

/****************************************************************************
 * POLL handling
 ****************************************************************************/
func (vrouter *VRouter) Poll(containerUuid, containerVn string) (*Result,
	error) {
	vrouter.containerUuid = containerUuid
	vrouter.containerVn = containerVn

	result, poll_err := vrouter.PollUrl("/vm-cfg")
	if poll_err != nil {
		log.Errorf("Error in poll-url for config")
		return nil, poll_err
	}

	return result, nil
}

/****************************************************************************
 * VRouter handlers
 ****************************************************************************/
func (vrouter *VRouter) Close() error {
	return nil
}

func (vrouter *VRouter) Log() {
	log.Infof("%+v\n", *vrouter)
}

func VRouterInit(stdinData []byte) (*VRouter, error) {
	httpClient := new(http.Client)
	vrouter := VRouter{Server: VROUTER_AGENT_IP, Port: VROUTER_AGENT_PORT,
		Dir: VROUTER_CONFIG_DIR, PollTimeout: VROUTER_POLL_TIMEOUT,
		PollRetries: VROUTER_POLL_RETRIES, containerUuid: "",
		containerVn: "", httpClient: httpClient}
	args := vrouterJson{VRouter: vrouter}

	if err := json.Unmarshal(stdinData, &args); err != nil {
		msg := fmt.Sprintf("Invalid JSon string. Error : %v \nString %s\n",
			err, stdinData)
		log.Errorf(msg)
		return nil, fmt.Errorf(msg)
	}

	vrouter = args.VRouter
	return &vrouter, nil
}
