// vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//
package contrailCni

import (
    "bytes"
    "encoding/json"
    "fmt"
    "os"
    "io/ioutil"
    "net/http"
    "regexp"
    "strconv"
    "strings"

    "../common"
    log "../logging"
    "github.com/containernetworking/cni/pkg/skel"
    "github.com/containernetworking/cni/pkg/types"
    "github.com/containernetworking/cni/pkg/types/current"
)

/* Example configuration file
{
    "cniVersion": "0.3.1",
    "cniName": "contrail-k8s-cni",
    "contrail" : {
        "cluster-name"  : "k8s",
        "mode"          : "k8s/mesos",
        "meta-plugin"   : "multus",
        "vif-type"      : "veth/macvlan",
        "parent-interface" : "eth0",
        "mtu"           : 1500,
        "vrouter-ip"    : "127.0.0.1",
        "vrouter-port"  : 9092,
        "config-dir"    : "/var/lib/contrail/ports/vm",
        "poll-timeout"  : 15,
        "poll-retries"  : 5,
        "log-level"     : "4",
        "log-file"      : "/var/log/contrail/cni/opencontrail.log"
    },
    "type": "contrail-k8s-cni"
}
*/

const CONTRAIL_CNI_NAME = "contrail-k8s-cni"

const K8S_CLUSTER_NAME = "k8s"

// Container orchestrator modes
const CNI_MODE_K8S = "k8s"
const CNI_MODE_MESOS = "mesos"

const META_PLUGIN = "multus"

// Type of virtual interface to be created for container
const VIF_TYPE_VETH = "veth"
const VIF_TYPE_MACVLAN = "macvlan"
const VIF_TYPE_ETH = "eth"

// In case of macvlan, the container interfaces will run as sub-interface
// to interface on host network-namespace. Name of the interface inside
// host network-namespace is defined below
const CONTRAIL_PARENT_INTERFACE = "eth0"

const LOG_FILE = "/var/log/contrail/cni/opencontrail.log"
const LOG_LEVEL = "4"

const CNI_CONF_DIR = "/etc/cni/net.d/"
const CONTRAIL_CONF_FILE = "contrail.conf"

// Definition of Logging arguments in form of json in STDIN
type ContrailCni struct {
    cniArgs       *skel.CmdArgs
    ContainerUuid string
    ContainerName string
    ContainerVn   string
    ClusterName   string `json:"cluster-name"`
    Mode          string `json:"mode"`
    MetaPlugin    string `json:"meta-plugin"`
    VifParent     string `json:"parent-interface"`
    VifType       string `json:"vif-type"`
    Mtu           int    `json:"mtu"`
    NetworkName   string `json:"network-name"`
    MesosIP       string `json:"mesos-ip"`
    MesosPort     string `json:"mesos-port"`
    LogFile       string `json:"log-file"`
    LogLevel      string `json:"log-level"`
    VRouter       VRouter
}

type cniJson struct {
    ContrailCni ContrailCni `json:"contrail"`
    CniVersion  string      `json:"cniVersion"`
    NetworkName string      `json:"name"`
}

// CNIVersion is the version from the network configuration
var CNIVersion string
var MetaPluginCall bool

// Apply logging configuration. We use log packet for logging.
// log supports log-dir and log-level as arguments only.
func (cni *ContrailCni) loggingInit() error {
    log.Init(cni.LogFile, 10, 10)
    return nil
}

func (cni *ContrailCni) Log() {
    log.Infof("K8S Cluster Name : %s\n", cni.ClusterName)
    log.Infof("CNI Version : %s\n", CNIVersion)
    log.Infof("CNI Args : %s\n", cni.cniArgs.Args)
    log.Infof("CNI Args StdinData : %s\n", cni.cniArgs.StdinData)
    log.Infof("ContainerID : %s\n", cni.cniArgs.ContainerID)
    log.Infof("NetNS : %s\n", cni.cniArgs.Netns)
    log.Infof("Container Ifname : %s\n", cni.cniArgs.IfName)
    log.Infof("Meta Plugin Call : %t\n", MetaPluginCall)
    log.Infof("Network Name: %s\n", cni.NetworkName)
    log.Infof("MTU : %d\n", cni.Mtu)
    cni.VRouter.Log()
    log.Infof("%+v\n", cni)
}

func (cni *ContrailCni) readContrailConf() ([]byte, error) {
    var dataBytes []byte
    files, err := ioutil.ReadDir(CNI_CONF_DIR)
    if err != nil {
        log.Errorf("Failed to open %s. Error %+v\n", CNI_CONF_DIR, err)
        return nil, err
    }

    for _, f := range files {
        fname := CNI_CONF_DIR + f.Name();
        if strings.HasSuffix(fname, CONTRAIL_CONF_FILE) {
            dataBytes, err = ioutil.ReadFile(fname)
            if err != nil {
                log.Errorf("Failed to read %s. Error %+v\n", fname, err)
                return nil, err
            }
            return dataBytes, nil
        }
    }

    log.Infof("File *%s is not found in %s\n",
        CONTRAIL_CONF_FILE, CNI_CONF_DIR)
    dataBytes, _ = json.Marshal(cniJson{})
    return dataBytes, nil
}

func (cni *ContrailCni) getPodInfo(args string) {
    re := regexp.MustCompile(
        "(K8S_POD_NAMESPACE|K8S_POD_NAME)=([a-zA-z0-9-\\.]+)")
    result := re.FindAllStringSubmatchIndex(args, -1)
    kv := make(map[string]string)
    /*
     * match[0] --> first char of the regex pattern
     * match[1] --> last char of the regex pattern
     * match[2] --> first char of the first substring in the regex pattern
     * match[3] --> first char of the second substring in the regex pattern
     * match[4] --> first char of the third substring in the regex pattern
     * match[5] --> last char of the regex pattern
     */
    for _, match := range result {
        key := args[match[2]:match[3]]
        value := args[match[4]:match[5]]
        kv[key] = value
    }
    containerName := cni.ClusterName + "__" +
        kv["K8S_POD_NAMESPACE"] + "__" + kv["K8S_POD_NAME"]
    cni.UpdateContainerInfo(containerName, "", "")
    return
}

func (cni *ContrailCni) isMetaPlugin() bool {
    ppid := os.Getppid()
    commPath := fmt.Sprintf("/proc/%d/comm", ppid)
    dataBytes, err := ioutil.ReadFile(commPath)
    if err != nil {
        log.Errorf("Error in Getting Process Details for pid %d\n. Error %+v",
            ppid, err)
        return false
    }
    processName := string(dataBytes)
    processName = processName[:len(processName)-1]
    log.Infof("Parent Process Name %s\n", processName)
    if processName != cni.MetaPlugin {
        return false
    }
    return true
}

func (cni *ContrailCni) getMetaPluginDir() (string) {
    metaPluginDir := cni.VRouter.Dir + "/../" + cni.MetaPlugin
    return metaPluginDir
}

func (cni *ContrailCni) getMetaPluginContainerFile(
    metaPluginDir string) (string) {
    containerFile := metaPluginDir + "/" + cni.ContainerName
    return containerFile
}

func (cni *ContrailCni) readVrouterPollResults() (*[]Result, error) {
    metaPluginDir  := cni.getMetaPluginDir()
    if _, err := os.Stat(metaPluginDir); os.IsNotExist(err) {
        if err := os.Mkdir(metaPluginDir, 0644); err != nil {
            log.Errorf("Error Creating MetaPlugin directory %s. Error : %s",
                metaPluginDir, err)
            return nil, err
        }
    }
    containerFile := cni.getMetaPluginContainerFile(metaPluginDir)
    if _, err := os.Stat(containerFile); os.IsNotExist(err) {
        return nil, nil
    }
    bytes, err := ioutil.ReadFile(containerFile)
    if err != nil {
        log.Errorf("Error Reading Vrouter Poll Results from file %s. " +
            "Error : %s", containerFile, err)
        return nil, err
    }
    results := make([]Result, 0)
    err = json.Unmarshal(bytes, &results)
    if err != nil {
        log.Errorf("Error Decoding Vrouter Poll Results. Error : %+v", err)
        return nil, err
    }
    return &results, nil
}

func (cni *ContrailCni) writeVrouterPollResults(results *[]Result) error {
    metaPluginDir := cni.getMetaPluginDir()
    containerFile := cni.getMetaPluginContainerFile(metaPluginDir)
    bytes, err := json.Marshal(*results)
    if err != nil {
        log.Errorf("Error Encoding Vrouter Poll Results. Error : %+v", err)
        return err
    }
    err = ioutil.WriteFile(containerFile, bytes, 0644)
    if err != nil {
        log.Errorf("Error Writing Vrouter Poll Results in file %s. " +
            "Error : %s", containerFile, err)
        return err
    }
    return nil
}

func (cni *ContrailCni) deleteVrouterPollResults() {
    metaPluginDir := cni.getMetaPluginDir()
    containerFile := cni.getMetaPluginContainerFile(metaPluginDir)
    _, err := os.Stat(containerFile)
    if err != nil {
        log.Infof("File %s not found. Error : %s", containerFile, err)
        return
    }
    err = os.Remove(containerFile)
    if err != nil {
        log.Infof("Failed deleting file %s. Error : %s", containerFile, err)
        return
    }
    log.Infof("File %s is deleted", containerFile)
    return
}

func Init(args *skel.CmdArgs) (*ContrailCni, error) {
    /*
     * Contrail Config is constructed in the following orders
     * 1. build with the default Values
     * 2. override with the contrail cni conf Values
     * 3. override with the cni args
     */
     contrailCni := ContrailCni{ClusterName: K8S_CLUSTER_NAME,
        Mode: CNI_MODE_K8S, VifType: VIF_TYPE_VETH,
        VifParent: CONTRAIL_PARENT_INTERFACE, Mtu: cniIntf.CNI_MTU,
        MetaPlugin: META_PLUGIN, LogLevel: LOG_LEVEL, LogFile: LOG_FILE}
    json_args := cniJson{ContrailCni: contrailCni}
    json_args.ContrailCni.loggingInit()
    var dataBytes []byte
    var err error
    dataBytes, err = contrailCni.readContrailConf()
    if err != nil {
        return nil, err
    }
    if err = json.Unmarshal(dataBytes, &json_args); err != nil {
        log.Errorf("Error decoding dataBytes %s. Error %+v",
        string(dataBytes), err)
        return nil, err
    }
    if err = json.Unmarshal(args.StdinData, &json_args); err != nil {
        log.Errorf("Error decoding stdin %s. Error %+v",
            string(args.StdinData), err)
        return nil, err
    }
    var vrouter *VRouter
    vrouter, err = VRouterInit(args.StdinData, dataBytes)
    if err != nil {
        return nil, err
    }

    // Update contrailCni
    json_args.ContrailCni.cniArgs = args
    json_args.ContrailCni.VRouter = *vrouter
    json_args.ContrailCni.NetworkName = json_args.NetworkName
    json_args.ContrailCni.getPodInfo(args.Args)

    // If CNI version is blank, set to "0.2.0"
    CNIVersion = json_args.CniVersion
    if CNIVersion == "" {
        CNIVersion = "0.2.0"
    }

    MetaPluginCall = json_args.ContrailCni.isMetaPlugin()

    json_args.ContrailCni.Log()

    return &json_args.ContrailCni, nil
}

func (cni *ContrailCni) UpdateContainerInfo(containerName, containerUuid,
    containerVn string) {
    cni.ContainerName = containerName
    cni.ContainerUuid = containerUuid
    cni.ContainerVn = containerVn
}

func (cni *ContrailCni) UpdateContainerUuid(containerUuid string) {
    cni.ContainerUuid = containerUuid
}

/*
 *  makeInterface- Method to intialize interface object of type VETh or MACVLAN
 */
func (cni *ContrailCni) makeInterface(
    vlanId int, containerIntfName string) cniIntf.CniIntfMethods {
    if cni.VifType == VIF_TYPE_MACVLAN {
        return cniIntf.CniIntfMethods(cniIntf.InitMacVlan(cni.VifParent,
            containerIntfName, cni.cniArgs.ContainerID, cni.ContainerUuid,
            cni.cniArgs.Netns, cni.Mtu, vlanId))
    }

    return cniIntf.CniIntfMethods(cniIntf.InitVEth(
        containerIntfName, cni.cniArgs.ContainerID,
        cni.ContainerUuid, cni.cniArgs.Netns, cni.Mtu))
}

/*
 * buildContainerIntfName - Method to construct interface name for container.
 * - When Contrail CNI Plugin is used as a meta plugin, interface names are
 *   are generated by appending the provided 'index' to "eth".
 * - If the case when CNI plugin is invoked by a delegating plugin,
 *   the provided IfName arg is returned.
 */
func (cni *ContrailCni) buildContainerIntfName(
    intfName string, index int) string {
    if intfName != "" {
        return intfName
    }
    if MetaPluginCall && cni.cniArgs.IfName != "" {
        intfName = cni.cniArgs.IfName
    } else {
        intfName = VIF_TYPE_ETH + strconv.Itoa(index)
    }
    log.Infof("Built container interface name - %s", intfName)
    return intfName
}

/****************************************************************************
 * Add message handlers
 ****************************************************************************/

/* createInterfaceAndUpdateVrouter -
 *   Method to create interface in a container and notify VRouter about it
 */
func (cni *ContrailCni) createInterfaceAndUpdateVrouter(
    containerIntfName string, result Result) error {
    intf := cni.makeInterface(result.VlanId, containerIntfName)
    intf.Log()

    err := intf.Create()
    if err != nil {
        log.Errorf("Error creating interface object. Error %+v", err)
        return err
    }

    // Inform vrouter about interface-add
    // The interface inside container must be created by this time.
    updateAgent := true
    if cni.VifType == VIF_TYPE_MACVLAN {
        updateAgent = false
    }

    err = cni.VRouter.Add(cni.ContainerName, cni.ContainerUuid,
        result.VnId, cni.cniArgs.ContainerID, cni.cniArgs.Netns,
        containerIntfName, intf.GetHostIfName(), result.VmiUuid, updateAgent)
    if err != nil {
        log.Errorf("Error in Add to VRouter. Error %+v", err)
        return err
    }

    return nil
}

/* configureContainerInterface -
 *   Method to configure the interface in a container
 */
func (cni *ContrailCni) configureContainerInterface(
    containerIntfName string, vRouterResult Result) (*current.Result, error) {
    containerInterface := cni.makeInterface(
        vRouterResult.VlanId, containerIntfName)
    typesResult := MakeCniResult(containerIntfName, &vRouterResult)

    // Configure the interface based on config received above
    err := containerInterface.Configure(vRouterResult.Mac, typesResult)
    if err != nil {
        log.Errorf("Error configuring container interface. Error %+v", err)
        return nil, err
    }

    return typesResult, nil
}

/*
 *  CmdAdd - Method to
 *  - ADD handler for a container
 *  - Pre-fetch interface configuration from VRouter.
 *  - Gets MAC address for the interface
 *  - In case of sub-interface, gets VLAN-Tag for the interface
 *  - Create interface based on the "mode"
 *  - Invoke Add handler from VRouter module
 *  - Update interface with configuration got from VRouter
 *  - Configures IP address
 *  - Configures routes
 *  - Bring-up the interface
 *  - Return result in form of types.Result
 */
func (cni *ContrailCni) CmdAdd() error {

    // ContainerIntfNames - map of vmi uuid to interface names
    // Key : vmi uuid
    // Value : interface name
    containerIntfNames := make(map[string]string)
    // vrouterResultMap - map of vmi uuid to Vrouter Result
    // Key : vmi uuid
    // Value : VRouter result
    vrouterResultMap := make(map[string]Result)

    var finalTypesResult *current.Result

    if cni.Mode == CNI_MODE_MESOS {
        op := "POST"
        url := "http://" + cni.MesosIP +
                ":" + cni.MesosPort + "/" + "add_cni_info"
        values := map[string]string {
            "cid": cni.cniArgs.ContainerID,
            "cmd": "ADD",
            "args": string(cni.cniArgs.StdinData) }
        jsonValue, _ := json.Marshal(values)
        log.Infof("IN CNI MESOS mode - Updating Mesos Manager: URL -  %s\n", url)
        req, err := http.NewRequest(op, url, bytes.NewBuffer(jsonValue))
        if err != nil {
            log.Errorf("Error creating http Request. Op %s Url %s Msg %s."+
                       "Error : %+v", op, url, err)
            return err
        }

        req.Header.Set("Content-Type", "application/json")
        httpClient := new(http.Client)
        resp, err := httpClient.Do(req)
        if err != nil {
            log.Errorf("Failed HTTP operation :  %+v. Error : %+v", req, err)
            return err
        }
        defer resp.Body.Close()

        if resp.StatusCode != http.StatusOK {
            msg := fmt.Sprintf("Failed HTTP POST operation. Return code %d",
                          int(resp.StatusCode))
            log.Errorf(msg)
            return fmt.Errorf(msg)
        }
        log.Infof("IN CNI MESOS mode - Post to mesos manager success!\n")
    }

    var results *[]Result
    var err error
    vrouterPoll := true
    if MetaPluginCall {
        results, err = cni.readVrouterPollResults()
        if err != nil {
            return err
        }
        if results != nil {
            vrouterPoll = false
        }
    }

    if vrouterPoll {
        // Pre-fetch initial configuration for the interfaces from vrouter
        // This will give MAC address for the interface and in case of
        // VMI sub-interface, we will also get the vlan-Tag
        results, err = cni.VRouter.PollVmCfg(cni.ContainerName)
        if err != nil {
            log.Errorf("Error polling for configuration of %s",
                cni.ContainerUuid)
            return err
        }
        if MetaPluginCall {
            err = cni.writeVrouterPollResults(results)
            if err != nil {
                return err
            }
        }
    }

    // For each interface in the result create an interface in the container,
    // persist the configuration and notify the Vrouter agent
    vmiUuid := ""
    for _, result := range *results {
        // Index annotation is expected in the following format -
        //         <index>/<total num of interfaces>
        indices := strings.Split(result.Annotations.Index, "/")
        index, err := strconv.Atoi(indices[0])
        if err != nil {
            log.Errorf("Could not retrieve index from result - %+v", result)
            return err
        }

        // Update the pod/vm uuid
        cni.UpdateContainerUuid(result.VmUuid)

        if MetaPluginCall {
            // When invoked by a delegating plugin, work on the vrouter result
            // that matches the given network name. In the scenario when
            // multiple results match the nw name, check if a config file exists
            // with the VMI UUID from the result. If the file exists look for
            // other result.
            networkName := result.Annotations.Network
            validateNetwork := false
            if ((len(cni.NetworkName) > 0) &&
                (cni.NetworkName != CONTRAIL_CNI_NAME)) {
                if (networkName == cni.NetworkName) {
                    validateNetwork = true
                }
            } else if (networkName == "default") {
                    validateNetwork = true
            }
            if validateNetwork {
                fname := cni.VRouter.makeFileName(result.VmiUuid)
                if checkFileOrDirExists(fname) {
                    continue
                }
            } else {
                continue
            }
        }

        containerIntfName := cni.buildContainerIntfName(
            result.Annotations.Interface, index)

        log.Infof("Creating interface - %s for result - %v",
            containerIntfName, result)
        cni.createInterfaceAndUpdateVrouter(containerIntfName, result)
        containerIntfNames[result.VmiUuid] = containerIntfName

        if MetaPluginCall {
            // When invoked by a delegating plugin, Work only on the given
            // interface name and ignore the rest of the interfaces
            vmiUuid = result.VmiUuid
            break
        }
    }

    vRouterResults, poll_err := cni.VRouter.PollVm(cni.ContainerUuid, vmiUuid)
    if poll_err != nil {
        log.Errorf("Error in polling VRouter ")
        return poll_err
    }

    for _, vRouterResult := range *vRouterResults {
        vrouterResultMap[vRouterResult.VmiUuid] = vRouterResult
    }

    log.Infof("About to configure %d interfaces for container",
        len(containerIntfNames))
    for vmiUuid := range containerIntfNames {
        containerIntfName := containerIntfNames[vmiUuid]
        vRouterResult, ok := vrouterResultMap[vmiUuid]
        if ok == false {
            msg := fmt.Sprintf("VMI UUID %s does not exist " +
                "in the Vrouter Result", vmiUuid)
            log.Errorf(msg)
            return fmt.Errorf(msg)
        }

        log.Infof("Working on VrouterResult - %+v  and Interface name - %s",
            vRouterResult, containerIntfName)
        typesResult, err := cni.configureContainerInterface(
            containerIntfName, vRouterResult)
        if err != nil {
            return err
        }

        if MetaPluginCall {
            finalTypesResult = typesResult
        } else {
            if containerIntfName == "eth0" {
                finalTypesResult = typesResult
            }
        }
    }

    types.PrintResult(finalTypesResult, CNIVersion)
    log.Infof("CmdAdd is done")
    return nil
}

/****************************************************************************
 * Delete message handlers
 ****************************************************************************/
func (cni *ContrailCni) CmdDel() error {
    if cni.Mode == CNI_MODE_MESOS {
        op := "POST"
        url := "http://" + cni.MesosIP +
                ":" + cni.MesosPort + "/" + "del_cni_info"

        values := map[string]string {
            "cid": cni.cniArgs.ContainerID,
            "cmd": "DEL",
            "args": string(cni.cniArgs.StdinData) }
        jsonValue, _ := json.Marshal(values)
        log.Infof("IN CNI MESOS mode - Updating Mesos Manager: URL -  %s\n", url)
        req, err := http.NewRequest(op, url, bytes.NewBuffer(jsonValue))
        if err != nil {
            log.Errorf("Error creating http Request. Op %s Url %s Msg %s."+
                "Error : %+v", op, url, err)
            return err
        }
        req.Header.Set("Content-Type", "application/json")
        httpClient := new(http.Client)
        resp, err := httpClient.Do(req)
        if err != nil {
            log.Errorf("Failed HTTP operation :  %+v. Error : %+v", req, err)
            return err
        }
        defer resp.Body.Close()

        if resp.StatusCode != http.StatusOK {
            msg := fmt.Sprintf("Failed HTTP POST operation. Return code %d",
                int(resp.StatusCode))
            log.Errorf(msg)
            return fmt.Errorf(msg)
        }

        log.Infof("IN CNI MESOS mode - Post to mesos manager success!\n")
    }

    vmUuid, containerIntfNames, vmiUuids, err :=
        cni.VRouter.CanDelete(cni.ContainerName, cni.cniArgs.ContainerID,
            cni.ContainerUuid, cni.ContainerVn)
    if err != nil {
        log.Errorf("Failed in CanDelete. Error %s", err)
        return nil
    }

    // Update the pod/vm uuid
    cni.UpdateContainerUuid(vmUuid)

    if len(containerIntfNames) > 0 {
        for _, containerIntfName := range containerIntfNames {
            intf := cni.makeInterface(0, containerIntfName)
            intf.Log()

            // Build CNI response from response
            err = intf.Delete()
            if err != nil {
                log.Errorf("Error deleting interface")
            } else {
                log.Infof("Deleted interface %s inside container",
                    containerIntfName)
            }
        }

        // Inform vrouter about interface-delete.
        updateAgent := true
        if cni.VifType == VIF_TYPE_MACVLAN {
            updateAgent = false
        }

        err = cni.VRouter.Del(cni.cniArgs.ContainerID, cni.ContainerUuid,
            cni.ContainerVn, updateAgent, vmiUuids)
        if err != nil {
            log.Errorf("Error deleting interface from agent")
        }

        if MetaPluginCall {
            cni.deleteVrouterPollResults()
        }
    }

    // Nothing to Do
    log.Infof("CmdDel is done")
    return nil
}
