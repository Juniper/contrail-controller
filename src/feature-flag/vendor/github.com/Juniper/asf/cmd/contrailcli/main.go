package main

import (
	"github.com/Juniper/asf/pkg/cmd/contrailcli"
	"github.com/Juniper/asf/pkg/logutil"
)

func main() {
	err := contrailcli.ContrailCLI.Execute()
	if err != nil {
		logutil.FatalWithStackTrace(err)
	}
}
