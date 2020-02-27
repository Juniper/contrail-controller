package models

import (
	"fmt"

	"github.com/Juniper/asf/pkg/errutil"
)

func uuidToMac(uuid string) (mac string, err error) {
	if len(uuid) < 11 {
		return "", errutil.ErrorBadRequestf("could not generate mac address: vn uuid (%v) too short", uuid)
	}

	return fmt.Sprintf("02:%s:%s:%s:%s:%s", uuid[0:2], uuid[2:4], uuid[4:6], uuid[6:8], uuid[9:11]), nil
}
