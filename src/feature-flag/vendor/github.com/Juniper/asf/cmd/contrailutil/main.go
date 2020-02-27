package main

import (
	"github.com/Juniper/asf/pkg/cmd/contrailutil"
	"github.com/Juniper/asf/pkg/logutil"
)

func main() {
	err := contrailutil.ContrailUtil.Execute()
	if err != nil {
		logutil.FatalWithStackTrace(err)
	}
}
