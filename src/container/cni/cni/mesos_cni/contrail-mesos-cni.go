// vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//
/****************************************************************************
 * Main routines for Mesos CNI plugin
 ****************************************************************************/
package main

import (
	"../contrail"
	log "../logging"
	"github.com/containernetworking/cni/pkg/skel"
	"github.com/containernetworking/cni/pkg/version"
)

// Get UUID and Name for container. UUID is same as container-id
func getPodInfo(skelArgs *skel.CmdArgs) (string, string, error) {
	return skelArgs.ContainerID, skelArgs.ContainerID, nil
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
		log.Errorf("Failed processing Add command.")
		return err
	}

	return nil
}

func main() {
	// Let CNI skeletal code handle demux based on env variables
	skel.PluginMain(CmdAdd, CmdDel,
		version.PluginSupports(contrailCni.CniVersion))
}
