// vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//
/****************************************************************************
 * Main routines for kubernetes CNI plugin
 ****************************************************************************/
package main

import (
	"../contrail"
	"context"
	"github.com/containernetworking/cni/pkg/skel"
	"github.com/containernetworking/cni/pkg/version"
	"github.com/docker/docker/client"
	"github.com/golang/glog"
	"os"
)

// Use "docker inspect" equivalent API to get UUID and Name for container
func getPodUuid(skelArgs *skel.CmdArgs) (string, string, error) {
	os.Setenv("DOCKER_API_VERSION", "1.24")
	cli, err := client.NewEnvClient()
	if err != nil {
		glog.Errorf("Error creating docker client. %+v", err)
		return "", "", err
	}

	data, err := cli.ContainerInspect(context.Background(),
		skelArgs.ContainerID)
	if err != nil {
		glog.Errorf("Error querying for container %s. %+v",
			skelArgs.ContainerID, err)
		return "", "", err
	}

	return data.Config.Labels["io.kubernetes.pod.uid"], data.Config.Hostname, nil
}

// Add command
func CmdAdd(skelArgs *skel.CmdArgs) error {
	// Initialize ContrailCni module
	cni, err := contrailCni.Init(skelArgs)
	defer glog.Flush()
	if err != nil {
		glog.Errorf("Error initializing ContrailCni module. %+v", err)
		return err
	}

	// Get UUID and Name for container
	containerUuid, containerName, err := getPodUuid(skelArgs)
	if err != nil {
		glog.Errorf("Error finding UUID/Name for Container. %+v", err)
		return err
	}

	// Update UUID and Name for container
	cni.Update(containerName, containerUuid, "")
	cni.Log()

	// Handle Add command
	err = cni.CmdAdd()
	if err != nil {
		glog.Errorf("Error in processing Add command. %+v", err)
		return err
	}

	return nil
}

// Del command
func CmdDel(skelArgs *skel.CmdArgs) error {
	// Initialize ContrailCni module
	cni, err := contrailCni.Init(skelArgs)
	defer glog.Flush()
	if err != nil {
		return err
	}

	// Get UUID and Name for container
	containerUuid, containerName, err := getPodUuid(skelArgs)
	if err != nil {
		return err
	}

	// Update UUID and Name for container
	cni.Update(containerName, containerUuid, "")
	cni.Log()

	// Handle Del command
	err = cni.CmdDel()
	if err != nil {
		glog.Errorf("Error in processing Del command. %+v", err)
		return err
	}

	return nil
}

func main() {
	// Let CNI skeletal code handle demux based on env variables
	skel.PluginMain(CmdAdd, CmdDel,
		version.PluginSupports(contrailCni.CniVersion))
}
