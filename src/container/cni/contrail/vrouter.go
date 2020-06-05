// vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//
/****************************************************************************
 * VRouter routines for CNI plugin
 ****************************************************************************/
package contrailCni

import (
    "bytes"
    "encoding/json"
    "fmt"
    "io/ioutil"
    "net"
    "net/http"
    "os"
    "strconv"
    "strings"
    "time"

    log "../logging"
    "github.com/containernetworking/cni/pkg/types"
    "github.com/containernetworking/cni/pkg/types/current"
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
    containerName string
    containerId   string
    containerUuid string
    containerVn   string
    VmiUuid       string
    httpClient    *http.Client
}

type vrouterJson struct {
    VRouter VRouter `json:"contrail"`
}

// Make filename to store config
func (vrouter *VRouter) makeFileName(VmiUUID string) string {
    fname := vrouter.Dir + "/" + vrouter.containerName
    if VmiUUID != "" {
        fname = fname + "/" + VmiUUID
    }
    return fname
}

// Make URL for operation
func (vrouter *VRouter) makeUrl(page, containerNameUuid,
    vmiUuid string) string {
    url := "http://" + vrouter.Server + ":" + strconv.Itoa(vrouter.Port) + page
    if len(containerNameUuid) > 0 {
        url = url + "/" + containerNameUuid
    }
    if len(vmiUuid) > 0 {
        url = url + "/" + vmiUuid
    }
    return url
}

// Do a HTTP operation to VRouter
func (vrouter *VRouter) doOp(op, page, containerNameUuid, vmiUuid string,
    msg []byte) (*http.Response, error) {

    url := vrouter.makeUrl(page, containerNameUuid, vmiUuid)
    log.Infof("VRouter request. Operation : %s Url :  %s", op, url)
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

// Annotations are used to pass information from kube-manager to Plugin
type Annotations struct {
    Cluster   string `json:"cluster"`
    Kind      string `json:"kind"`
    Name      string `json:"name"`
    Namespace string `json:"namespace"`
    Network   string `json:"network"`
    Owner     string `json:"owner"`
    Project   string `json:"project"`
    Index     string `json:"index"`
    Interface string `json:"interface"`
}

type Result struct {
    VmUuid      string   `json:"vm-uuid"`
    Nw          string   `json:"network-label"`
    Ip          string   `json:"ip-address"`
    Plen        int      `json:"plen"`
    Gw          string   `json:"gateway"`
    Dns         string   `json:"dns-server"`
    Mac         string   `json:"mac-address"`
    VlanId      int      `json:"vlan-id"`
    VnId        string   `json:"vn-id"`
    VnName      string   `json:"vn-name"`
    VmiUuid     string   `json:"id"`
    Args        []string `json:"annotations"`
    Annotations Annotations
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

    // Set default route only for the primary interface in case of multiple
    // interfaces per pod. Currently using eth0 by default
    // TODO: Allow user to select primary interface
    if ifname == "eth0" {
        _, defaultNet, _ := net.ParseCIDR("0.0.0.0/0")
        rt := &types.Route{Dst: *defaultNet, GW: net.ParseIP(vrouterResult.Gw)}
        result.Routes = append(result.Routes, rt)
    }

    result.DNS.Nameservers = append(result.DNS.Nameservers, vrouterResult.Dns)
    return result
}

// Get operation from VRouter
func (vrouter *VRouter) Get(url, containerNameUuid,
    vmiUuid string) (*[]Result, error) {
    var req []byte
    resp, err := vrouter.doOp("GET", url, containerNameUuid, vmiUuid, req)
    if err != nil {
        log.Errorf("Failed HTTP GET operation")
        return nil, err
    }
    defer resp.Body.Close()

    if resp.StatusCode != http.StatusOK {
        msg := fmt.Sprintf("Failed HTTP Get operation. Return code %d",
                int(resp.StatusCode))
        log.Errorf(msg)
        return nil, fmt.Errorf(msg)
    }

    var body []byte
    body, err = ioutil.ReadAll(resp.Body)
    if err != nil {
        log.Errorf("Error in reading HTTP GET response. Error : %+v", err)
        return nil, err
    }
    log.Infof("VRouter response %s", string(body))

    results := make([]Result, 0)
    err = json.Unmarshal(body, &results)
    if err != nil {
        log.Errorf("Error decoding HTTP Get response. Error : %+v", err)
        return nil, err
    }

    // Parse args from each result and update the
    // Annotations member in the Result Object
    for idx, result := range results {
        str := "{"
        for id, annotation := range result.Args {
            annotation = strings.TrimRight(annotation, "}")
            annotation = strings.TrimLeft(annotation, "{")
            annSplit := strings.Split(annotation, ":")
            str = str + "\"" + annSplit[0] + "\":\"" + annSplit[1] + "\""
            if id != len(result.Args)-1 {
                str = str + ","
            }
        }
        str = str + "}"

        err = json.Unmarshal([]byte(str), &results[idx].Annotations)
        if err != nil {
            log.Errorf("Error decoding Annotations. Error : %+v", err)
            return nil, err
        }
    }

    return &results, nil
}

// Poll response from VRouter
func (vrouter *VRouter) PollVm(containerUuid,
    vmiUuid string) (*[]Result, error) {
    var msg string
    for i := 0; i < vrouter.PollRetries; i++ {
        results, err := vrouter.Get("/vm", containerUuid, vmiUuid)
        if err == nil {
            log.Infof("Get from vrouter passed. Result %+v", results)
            return results, nil
        }

        msg = err.Error()
        log.Infof("Iteration %d : Get vrouter failed", i)
        time.Sleep(time.Duration(vrouter.PollTimeout) * time.Second)
    }

    return nil, fmt.Errorf("Failed in PollVm. Error : %s", msg)
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
    VnId            string `json:"vn-uuid"`
    VmiUuid         string `json:"vmi-uuid"`
}

// Make JSON for Add Message
func makeMsg(containerName, containerUuid, containerId, containerNamespace,
    containerIfName, hostIfName, vmiUuid, vnId string) []byte {
    t := time.Now()
    addMsg := contrailAddMsg{Time: t.String(), Vm: containerId,
        VmUuid: containerUuid, VmName: containerName, HostIfName: hostIfName,
        ContainerIfName: containerIfName, Namespace: containerNamespace,
        VmiUuid: vmiUuid, VnId: vnId}

    msg, err := json.MarshalIndent(addMsg, "", "\t")
    if err != nil {
        return nil
    }

    return msg
}

/* addVmFile: Store the config to file for persistency
 * For each pod we create a separate directory with name as containerUUID.
 * and for each interface a separate config file is created with name as
 * VMI UUID.
 */
func (vrouter *VRouter) addVmFile(addMsg []byte, vmiUuid string) error {
    // Check if path to directory exists exists, else create directory
    path := vrouter.Dir + "/" + vrouter.containerName
    if _, err := os.Stat(path); os.IsNotExist(err) {
        if err := os.Mkdir(path, 0644); err != nil {
            log.Errorf("Error creating VM directory %s. Error : %s", path, err)
            return err
        }
    }

    // Write file with VMI UUID as the file name
    fname := vrouter.makeFileName(vmiUuid)
    err := ioutil.WriteFile(fname, addMsg, 0644)
    if err != nil {
        log.Errorf("Error writing VM config file %s. Error : %s", fname, err)
        return err
    }

    return nil
}

func (vrouter *VRouter) addVmToAgent(addMsg []byte) error {
    resp, err := vrouter.doOp("POST", "/vm", "",  "", addMsg)
    if err != nil {
        log.Errorf("Failed in HTTP POST operation")
        return err
    }
    defer resp.Body.Close()

    if (resp.StatusCode != http.StatusOK) &&
        (resp.StatusCode != http.StatusCreated) {
        msg := fmt.Sprintf("Failed HTTP Post operation. Return code %d",
            int(resp.StatusCode))
        log.Errorf(msg)
        return fmt.Errorf(msg)
    }

    return nil
}

/* Process add of a VM. Writes config file and send message to agent */
func (vrouter *VRouter) Add(containerName, containerUuid, containerVn,
    containerId, containerNamespace, containerIfName,
    hostIfName, vmiUuid string, updateAgent bool) error {
    vrouter.containerName = containerName
    vrouter.containerUuid = containerUuid
    vrouter.containerId = containerId
    vrouter.containerVn = containerVn
    vrouter.VmiUuid = vmiUuid

    // Make Add Message structure
    addMsg := makeMsg(containerName, containerUuid, containerId,
        containerNamespace, containerIfName, hostIfName, vmiUuid, containerVn)
    log.Infof("VRouter add message is %s", addMsg)

    // Store config to file for persistency
    if err := vrouter.addVmFile(addMsg, vmiUuid); err != nil {
        // Fail adding VM if directory not present
        log.Errorf("Error storing config file")
        return err
    }

    // Make the agent call for non-nested mode
    if updateAgent == true {
        err := vrouter.addVmToAgent(addMsg)
        if err != nil {
            log.Errorf("Error in Add to VRouter")
            return err
        }
    }

    return nil
}

/****************************************************************************
 * DEL message handling
 ****************************************************************************/
// Del VM config file
func (vrouter *VRouter) delVmFile(containerIfName string) (error, error) {
    fname := vrouter.makeFileName(containerIfName)
    _, err := os.Stat(fname)
    // File not present... nothing to do
    if err != nil {
        log.Infof("File %s not found. Error : %s", fname, err)
        return nil, nil
    }

    err = os.Remove(fname)
    if err != nil {
        log.Infof("Failed deleting file %s. Error : %s", fname, err)
        return nil, nil
    }

    log.Infof("file %s deleted", fname)
    return nil, nil
}

func (vrouter *VRouter) delVmToAgent() error {
    delMsg := makeMsg("", vrouter.containerUuid, vrouter.containerId,
        "", "", "", "", "")
    resp, err := vrouter.doOp("DELETE", "/vm", vrouter.containerUuid, "", delMsg)
    if err != nil {
        log.Errorf("Failed HTTP DELETE operation")
        return err
    }
    defer resp.Body.Close()

    if resp.StatusCode != http.StatusOK {
        msg := fmt.Sprintf("Failed HTTP Delete operation. Return code %d",
            resp.StatusCode)
        log.Errorf(msg)
        return fmt.Errorf(msg)
    }

    log.Infof("Delete response from agent %d", resp.StatusCode)
    return nil
}

/* Process delete VM. The method ignores intermediate errors and does best
 * effort cleanup
 */
func (vrouter *VRouter) Del(containerId, containerUuid,
    containerVn string, updateAgent bool, vmiUuids []string) error {
    log.Infof("Deleting container with id : %s uuid : %s Vn : %s",
        containerId, containerUuid, containerVn)
    vrouter.containerUuid = containerUuid
    vrouter.containerId = containerId
    vrouter.containerVn = containerVn
    var ret error
    for _, vmiUuid := range vmiUuids {
        // Remove the configuraion file stored for persistency
        _, _ = vrouter.delVmFile(vmiUuid)
    }

    // Delete the directory for container
    _, _ = vrouter.delVmFile("")

    // Make the del message call to agent for non-nested mode
    if updateAgent == true {
        if err := vrouter.delVmToAgent(); err != nil {
            log.Errorf("Error in Delete to VRouter")
            ret = err
        }
    }

    log.Infof("Delete return code %+v", ret)
    return ret
}

/*
 * If a container fails (due to CNI failure or otherwise), kubelet will
 * delete the failed container and create a new one. In some cases kubelet
 * is calling CNI multiple times for failed container. We create interface
 * with names based on POD-UUID. So, both new and old container will map to
 * same tap interface name.
 *
 * If delete of the container comes after new container is spawned, the delete
 * must be ignored. When new container is created, the config file stored by
 * vrouter is updated with new container-id. Compare the container-id in
 * in this instance with one present in the config file. Ignore the request if
 * they do not match
 */
func (vrouter *VRouter) CanDelete(containerName, containerId, containerUuid,
    containerVn string) (string, []string, []string, error) {
    vrouter.containerName = containerName
    vrouter.containerUuid = containerUuid
    vrouter.containerId = containerId
    vrouter.containerVn = containerVn

    var containerIntfNames []string
    var vmiUuids []string
    var vmUuid string
    dirPath := vrouter.makeFileName("")
    files, err := getFilesinDir(dirPath)
    if err != nil {
        if err.Error() == FileNotExist {
            // Nothing to do
            return vmUuid, containerIntfNames, vmiUuids, nil
        }
    }

    for _, f := range files {
        fname := vrouter.makeFileName(f.Name())
        obj, err := readContrailAddMsg(fname)
        if err != nil {
            if err.Error() == FileNotExist {
                // Nothing to do
                continue
            }
            return vmUuid, containerIntfNames, vmiUuids, err
        }
        if obj.Vm != vrouter.containerId {
            log.Infof("Mismatch in container-id between request and config file."+
                "Expected %s got %s", vrouter.containerId, obj.Vm)
            err := fmt.Errorf("Mismatch in container-id between request and "+
                " config file. Expected %s got %s", vrouter.containerId, obj.Vm)
            return vmUuid, containerIntfNames, vmiUuids, err
        }
        containerIntfNames = append(containerIntfNames, obj.ContainerIfName)
        vmiUuids = append(vmiUuids, f.Name())
        vmUuid = obj.VmUuid
    }

    log.Infof("Can delete the following interfaces - %s", containerIntfNames)
    return vmUuid, containerIntfNames, vmiUuids, nil
}

/****************************************************************************
 * POLL handling
 ****************************************************************************/
func (vrouter *VRouter) PollVmCfg(containerName string) (*[]Result,
    error) {
    var msg string
    vrouter.containerName = containerName
    // Loop till we receive all the interfaces attached to the pod
    // in the Vrouter Response.
    for i := 0; i < vrouter.PollRetries; i++ {
        results, err := vrouter.Get("/vm-cfg", containerName, "")
        if err != nil {
            msg = err.Error()
            log.Infof("Iteration %d : Get vrouter failed", i)
            time.Sleep(time.Duration(vrouter.PollTimeout) * time.Second)
            continue
        }

        log.Infof("Get from vrouter passed. Result %+v", results)
        result := (*results)[0]
        indices := strings.Split(result.Annotations.Index, "/")
        total_interfaces, err := strconv.Atoi(indices[1])
        if err != nil {
            msg = err.Error()
            log.Infof(msg)
            time.Sleep(time.Duration(vrouter.PollTimeout) * time.Second)
            continue
        }

        total_results := len(*results)
        if (total_interfaces == total_results) {
            return results, nil
        }
        msg = fmt.Sprintf("Iteration %d : Get VRouter Incomplete - " +
            "Interfaces Expected: %d, Actual: %d",
            i, total_interfaces, total_results)
        log.Infof(msg)
    }

    return nil, fmt.Errorf("Failed in PollVmCfg. Error : %s", msg)
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

func VRouterInit(stdinData []byte, dataBytes []byte) (*VRouter, error) {
    httpClient := new(http.Client)
    vrouter := VRouter{Server: VROUTER_AGENT_IP, Port: VROUTER_AGENT_PORT,
        Dir: VROUTER_CONFIG_DIR, PollTimeout: VROUTER_POLL_TIMEOUT,
        PollRetries: VROUTER_POLL_RETRIES, containerId: "", containerUuid: "",
        containerVn: "", VmiUuid: "", httpClient: httpClient}
    args := vrouterJson{VRouter: vrouter}

    if err := json.Unmarshal(dataBytes, &args); err != nil {
        msg := fmt.Sprintf("Invalid JSon string. Error : %v \nString %s\n",
            err, dataBytes)
        log.Errorf(msg)
        return nil, fmt.Errorf(msg)
    }
    if err := json.Unmarshal(stdinData, &args); err != nil {
        msg := fmt.Sprintf("Invalid JSon string. Error : %v \nString %s\n",
            err, stdinData)
        log.Errorf(msg)
        return nil, fmt.Errorf(msg)
    }

    vrouter = args.VRouter
    return &vrouter, nil
}
