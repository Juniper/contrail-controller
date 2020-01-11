// vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//
/****************************************************************************
 * Main routines for kubernetes CNI plugin
 ****************************************************************************/
package main

import (
	"context"
	"os"

	"../contrail"
	log "../logging"
	"github.com/containernetworking/cni/pkg/skel"
	cniSpecVersion "github.com/containernetworking/cni/pkg/version"
	"github.com/docker/docker/client"
)

// Use "docker inspect" equivalent API to get UUID and Name for container
func getPodInfo(skelArgs *skel.CmdArgs) (string, string, error) {
	os.Setenv("DOCKER_API_VERSION", "1.22")
	cli, err := client.NewEnvClient()
	if err != nil {
		log.Errorf("Error creating docker client. %+v", err)
		return "", "", err
	}

	data, err := cli.ContainerInspect(context.Background(),
		skelArgs.ContainerID)
	if err != nil {
		log.Errorf("Error querying for container %s. %+v",
			skelArgs.ContainerID, err)
		return "", "", err
	}

	uuid := data.Config.Labels["io.kubernetes.pod.uid"]
	name := data.Config.Hostname
	log.Infof("getPodInfo success. container-id %s uuid %s name %s",
		skelArgs.ContainerID, uuid, name)
	return uuid, name, nil
}

// Add command
func CmdAdd(skelArgs *skel.CmdArgs) error {
	// Initialize ContrailCni module
	cni, err := contrailCni.Init(skelArgs)
	if err != nil {
		return err
	}

	log.Infof("Came in Add for container %s", skelArgs.ContainerID)
	// Get UUID and Name for container
	containerUuid, containerName, err := getPodInfo(skelArgs)
	if err != nil {
		log.Errorf("Error getting UUID/Name for Container")
		return err
	}

	// Update UUID and Name for container
	cni.Update(containerName, containerUuid, "")
	cni.Log()

	// Handle Add command
	err = cni.CmdAdd()
	if err != nil {
		log.Errorf("Failed processing Add command.")
		return err
	}

	return nil
}

// Del command
func CmdDel(skelArgs *skel.CmdArgs) error {
	// Initialize ContrailCni module
	cni, err := contrailCni.Init(skelArgs)
	if err != nil {
		return err
	}

	log.Infof("Came in Del for container %s", skelArgs.ContainerID)
	// Get UUID and Name for container
	containerUuid, containerName, err := getPodInfo(skelArgs)
	if err != nil {
		log.Errorf("Error getting UUID/Name for Container")
		return err
	}

	// Update UUID and Name for container
	cni.Update(containerName, containerUuid, "")
	cni.Log()

	// Handle Del command
	err = cni.CmdDel()
	if err != nil {
		log.Errorf("Failed processing Del command.")
		return err
	}

	return nil
}

func main() {
	// Let CNI skeletal code handle demux based on env variables
	skel.PluginMain(CmdAdd, CmdDel, cniSpecVersion.All)
}
