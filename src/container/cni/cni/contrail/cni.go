// vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//
package contrailCni

import (
	"../common"
	"encoding/json"
	"flag"
	"github.com/containernetworking/cni/pkg/skel"
	"github.com/containernetworking/cni/pkg/types"
	"github.com/containernetworking/cni/pkg/version"
	"github.com/golang/glog"
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
        "log-level"     : "2"
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
	LogLevel      string `json:"log-level"`
	ContainerUuid string
	ContainerName string
	ContainerVn   string
	VRouter       VRouter
	VEthIntf      cniVEth.VEth
}

type cniJson struct {
	ContrailCni ContrailCni `json:"contrail"`
}

// Apply logging configuration. We use glog packet for logging.
// glog supports log-dir and log-level as arguments only.
func (cni *ContrailCni) loggingInit() error {
	flag.Parse()
	flag.Lookup("log_dir").Value.Set(cni.LogDir)
	flag.Lookup("v").Value.Set(cni.LogLevel)
	return nil
}

func (cni *ContrailCni) Log() {
	glog.V(2).Infof("ContainerID : %s\n", cni.cniArgs.ContainerID)
	glog.V(2).Infof("NetNS : %s\n", cni.cniArgs.Netns)
	glog.V(2).Infof("Container Ifname : %s\n", cni.cniArgs.IfName)
	glog.V(2).Infof("Args : %s\n", cni.cniArgs.Args)
	glog.V(2).Infof("Config File : %s\n", cni.cniArgs.StdinData)
	glog.V(2).Infof("%+v\n", cni)
	cni.VRouter.Log()
}

func Init(args *skel.CmdArgs) (*ContrailCni, error) {
	vrouter, _ := VRouterInit(args.StdinData)
	cni := ContrailCni{cniArgs: args, Mode: CNI_MODE_K8S,
		VifType: VIF_TYPE_VETH, VifParent: CONTRAIL_PARENT_INTERFACE,
		LogDir: LOG_DIR, LogLevel: LOG_LEVEL, VRouter: *vrouter}
	json_args := cniJson{ContrailCni: cni}

	if err := json.Unmarshal(args.StdinData, &json_args); err != nil {
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
	// Create the interface object
	intf := cniVEth.Init(cni.cniArgs.IfName, cni.cniArgs.Netns,
		cni.ContainerUuid)

	// Create interface inside the container
	if err := intf.Create(); err != nil {
		glog.Errorf("Error creating VEth interface. %+v", err)
		return err
	}

	intf.Log()
	// Inform vrouter about interface-add. The interface inside container
	// must be created by this time
	result, err := cni.VRouter.Add(cni.ContainerName, cni.ContainerUuid,
		cni.ContainerVn, cni.cniArgs.ContainerID, cni.cniArgs.Netns,
		cni.cniArgs.IfName, intf.HostIfName)
	if err != nil {
		glog.V(2).Infof("Error in Add to VRouter. %+v", err)
		return err
	}

	// Convert from VRouter response to CNI Response
	typesResult := MakeCniResult(cni.cniArgs.IfName, result)

	// Configure the interface based on config received above
	err = intf.Configure(result.Mac, typesResult)
	if err != nil {
		glog.Errorf("Error configuring container interface. %+v", err)
		return err
	}

	versionDecoder := &version.ConfigDecoder{}
	confVersion, err := versionDecoder.Decode(cni.cniArgs.StdinData)
	if err != nil {
		return err
	}
	types.PrintResult(typesResult, confVersion)
	return nil
}

/****************************************************************************
 * Delete message handlers
 ****************************************************************************/
func (cni *ContrailCni) CmdDel() error {
	// Create the interface object
	intf := cniVEth.Init(cni.cniArgs.IfName, cni.cniArgs.Netns,
		cni.ContainerUuid)
	if err := intf.Delete(); err != nil {
		glog.Errorf("Error deleting interface. Error %v\n", err)
	} else {
		glog.V(2).Infof("Deleted interface %s inside container",
			cni.cniArgs.IfName)
	}

	// Inform vrouter about interface-delete.
	err := cni.VRouter.Del(cni.ContainerUuid, cni.ContainerVn)
	if err != nil {
		glog.Errorf("Error deleting interface from agent. Error %v\n", err)
	}

	// Build CNI response from response
	return nil
}
