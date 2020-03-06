// Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
package contrailCni

import (
    "encoding/json"
    "errors"
    "fmt"
    "io/ioutil"
    "os"

    log "../logging"
)

// FileNotExist error message
const FileNotExist = "File/Dir does not exist"

/*
 * checkFileOrDirExists: Method to check if file or directory
 * exists in the location.
 * Inputs:
 *  String fname - full path to File/Dir
 * Outputs:
 *  bool - true/false
 */
func checkFileOrDirExists(fname string) bool {
    if _, err := os.Stat(fname); err != nil {
        log.Infof("File/Dir - %s does not exist. Error - %+v", fname, err)
        return false
    }

    log.Infof("File/Dir - %s exists", fname)
    return true
}

/*
 * getFilesinDir: Method to retrieve all files from given location
 *
 * Inputs:
 *  String dirName - full path to directory
 * Outputs:
 *  List FileInfo  - list of files in directory
 */
func getFilesinDir(dirName string) ([]os.FileInfo, error) {
    if checkFileOrDirExists(dirName) {
        files, err := ioutil.ReadDir(dirName)
        if err != nil {
            log.Errorf("Error in reading directory - %s. Error - %+v", dirName, err)
            return nil, err
        }
        return files, nil
    }
    err := errors.New(FileNotExist)
    return nil, err
}

/*
 * readContrailAddMsg: Unmarshal file to ContrailAddMsg object
 *
 * Inputs:
 *  String fname - full path to File
 * Outputs:
 *  contrailAddMsg - contrailAddMsg struct
 */
func readContrailAddMsg(fname string) (contrailAddMsg, error) {
    var msg contrailAddMsg
    if checkFileOrDirExists(fname) {
        file, err := ioutil.ReadFile(fname)
        if err != nil {
            log.Errorf("Error reading file %s. Error : %s", fname, err)
            return msg, fmt.Errorf("Error reading file %s. Error : %+v", fname, err)
        }

        err = json.Unmarshal(file, &msg)
        if err != nil {
            log.Errorf("Error decoding file. Error : %+v", err)
            return msg, err
        }

        return msg, nil
    }
    err := errors.New(FileNotExist)
    return msg, err
}
