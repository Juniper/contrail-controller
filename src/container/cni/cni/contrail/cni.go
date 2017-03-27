// vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//
package contrailCni

import (
	"../common"
	log "../logging"
	"encoding/json"
	"github.com/containernetworking/cni/pkg/skel"
	"github.com/containernetworking/cni/pkg/types"
	"github.com/containernetworking/cni/pkg/version"
)

const CniVersion = "0.2.0"

/* Example configuration file
{
    "cniVersion": "0.2.0",
    "contrail" : {
        "vrouter-ip"    : "127.0.0.1",
        "vrouter-port"  : 9092,
        "config-dir"    : "/var/lib/contrail/ports/vm",
        "poll-timeout"  : 15,
        "poll-retries"  : 5,
        "log-dir"       : "/var/log/contrail/cni",
        "log-level"     : "2",
        "mode"          : "k8s/mesos",
        "vif-type"      : "veth/macvlan",
        "parent-interface" : "eth0"
    },

    "name": "contrail",
    "type": "contrail"
}
*/

const LOG_DIR = "/var/log/contrail/cni"
const LOG_LEVEL = "2"

// Container orchestrator modes
const CNI_MODE_K8S = "k8s"
const CNI_MODE_MESOS = "mesos"

// Type of virtual interface to be created for container
const VIF_TYPE_VETH = "veth"
const VIF_TYPE_MACVLAN = "macvlan"

// In case of macvlan, the container interfaces will run as sub-interface
// to interface on host network-namespace. Name of the interface inside
// host network-namespace is defined below
const CONTRAIL_PARENT_INTERFACE = "eth0"

// Definition of Logging arguments in form of json in STDIN
type ContrailCni struct {
	cniArgs       *skel.CmdArgs
	Mode          string `json:"mode"`
	VifType       string `json:"vif-type"`
	VifParent     string `json:"parent-interface"`
	LogDir        string `json:"log-dir"`
	LogFile       string `json:"log-file"`
	LogLevel      string `json:"log-level"`
	ContainerUuid string
	ContainerName string
	ContainerVn   string
	VRouter       VRouter
}

type cniJson struct {
	ContrailCni ContrailCni `json:"contrail"`
}

// Apply logging configuration. We use log packet for logging.
// log supports log-dir and log-level as arguments only.
func (cni *ContrailCni) loggingInit() error {
	log.Init(cni.LogFile, 10, 5)
	//flag.Parse()
	//flag.Lookup("log_dir").Value.Set(cni.LogDir)
	//flag.Lookup("v").Value.Set(cni.LogLevel)
	return nil
}

func (cni *ContrailCni) Log() {
	log.Infof("ContainerID : %s\n", cni.cniArgs.ContainerID)
	log.Infof("NetNS : %s\n", cni.cniArgs.Netns)
	log.Infof("Container Ifname : %s\n", cni.cniArgs.IfName)
	log.Infof("Args : %s\n", cni.cniArgs.Args)
	log.Infof("Config File : %s\n", cni.cniArgs.StdinData)
	log.Infof("%+v\n", cni)
	cni.VRouter.Log()
}

func Init(args *skel.CmdArgs) (*ContrailCni, error) {
	vrouter, _ := VRouterInit(args.StdinData)
	cni := ContrailCni{cniArgs: args, Mode: CNI_MODE_K8S,
		VifType: VIF_TYPE_VETH, VifParent: CONTRAIL_PARENT_INTERFACE,
		LogDir: LOG_DIR, LogLevel: LOG_LEVEL, VRouter: *vrouter}
	json_args := cniJson{ContrailCni: cni}

	if err := json.Unmarshal(args.StdinData, &json_args); err != nil {
		log.Errorf("Error decoding stdin\n %s \n. Error %+v",
			string(args.StdinData), err)
		return nil, err
	}

	json_args.ContrailCni.loggingInit()
	return &json_args.ContrailCni, nil
}

func (cni *ContrailCni) Update(containerName, containerUuid,
	containerVn string) {
	cni.ContainerUuid = containerUuid
	cni.ContainerName = containerName
	cni.ContainerVn = containerVn
}

func (cni *ContrailCni) makeInterface(vlanId int) cniIntf.CniIntfMethods {
	if cni.VifType == VIF_TYPE_MACVLAN {
		return cniIntf.CniIntfMethods(cniIntf.InitMacVlan(cni.VifParent,
			cni.cniArgs.IfName, cni.ContainerUuid, cni.cniArgs.Netns, vlanId))
	}

	return cniIntf.CniIntfMethods(cniIntf.InitVEth(cni.cniArgs.IfName,
		cni.ContainerUuid, cni.cniArgs.Netns))
}

/****************************************************************************
 * Add message handlers
 ****************************************************************************/
/*
 ADD handler for a container
 - Pre-fetch interface configuration from VRouter.
   - Gets MAC address for the interface
   - In case of sub-interface, gets VLAN-Tag for the interface
 - Create interface based on the "mode"
 - Invoke Add handler from VRouter module
 - Update interface with configuration got from VRouter
   - Configures IP address
   - Configures routes
   - Bring-up the interface
 - Return result in form of types.Result
*/
func (cni *ContrailCni) CmdAdd() error {
	// Pre-fetch initial configuration for the interface from vrouter
	// This will give MAC address for the interface and in case of
	// VMI sub-interface, we will also get the vlan-tag
	result, err := cni.VRouter.Poll(cni.ContainerUuid, cni.ContainerVn)
	if err != nil {
		log.Errorf("Error polling for configuration of %s and %s",
			cni.ContainerUuid, cni.ContainerVn)
		return err
	}

	intf := cni.makeInterface(result.VlanId)
	intf.Log()

	err = intf.Create()
	if err != nil {
		log.Errorf("Error creating interface object")
		return err
	}

	// Inform vrouter about interface-add. The interface inside container
	// must be created by this time
	result, err = cni.VRouter.Add(cni.ContainerName, cni.ContainerUuid,
		cni.ContainerVn, cni.cniArgs.ContainerID, cni.cniArgs.Netns,
		cni.cniArgs.IfName, intf.GetHostIfName())
	if err != nil {
		log.Infof("Error in Add to VRouter")
		return err
	}

	// Convert from VRouter response to CNI Response
	typesResult := MakeCniResult(cni.cniArgs.IfName, result)

	// Configure the interface based on config received above
	err = intf.Configure(result.Mac, typesResult)
	if err != nil {
		log.Errorf("Error configuring container interface")
		return err
	}

	versionDecoder := &version.ConfigDecoder{}
	confVersion, err := versionDecoder.Decode(cni.cniArgs.StdinData)
	if err != nil {
		log.Errorf("Error decoding VRouter response")
		return err
	}
	types.PrintResult(typesResult, confVersion)
	return nil
}

/****************************************************************************
 * Delete message handlers
 ****************************************************************************/
func (cni *ContrailCni) CmdDel() error {
	intf := cni.makeInterface(0)
	intf.Log()

	err := intf.Delete()
	if err != nil {
		log.Errorf("Error deleting interface")
	} else {
		log.Infof("Deleted interface %s inside container",
			cni.cniArgs.IfName)
	}

	// Inform vrouter about interface-delete.
	err = cni.VRouter.Del(cni.ContainerUuid, cni.ContainerVn)
	if err != nil {
		log.Errorf("Error deleting interface from agent")
	}

	// Build CNI response from response
	return nil
}
