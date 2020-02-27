package models

import (
	"fmt"
	"net"
	"strconv"
	"strings"

	"github.com/pkg/errors"
)

const (
	bgpRouteTargetMinID = 8000000
	routeTargetPrefix   = "target"
)

// Validate validates format of the Route Target name
func (rt *RouteTarget) Validate() error {
	_, _, _, err := parseRouteTarget(rt.GetName())
	return err
}

// RouteTargetString constructs route target string for given asn and target number.
func RouteTargetString(asn int64, target int64) string {
	return fmt.Sprintf("target:%v:%v", asn, target)
}

// IsRouteTargetUserDefined checks if route target was user defined.
func IsRouteTargetUserDefined(routeTarget string, globalAsn int64) (bool, error) {
	ip, asn, target, err := parseRouteTarget(routeTarget)
	if err != nil {
		return false, err
	}

	// If ip is specified, rt is user defined for sure
	if ip != nil {
		return true, nil
	}
	if int64(asn) == globalAsn && target >= bgpRouteTargetMinID {
		return false, nil
	}

	return true, nil
}

// parseRouteTarget parses route target name and validates format of the name
func parseRouteTarget(rtName string) (ip net.IP, asn int, target int, err error) {
	routeTarget := strings.Split(rtName, ":")
	if len(routeTarget) != 3 || routeTarget[0] != routeTargetPrefix {
		return nil, 0, 0, errors.Errorf("invalid RouteTarget specified: %v \n"+
			"Route target must be of the format 'target:<asn>:<number>' or 'target:<ip>:<number>'", routeTarget)
	}

	ip = net.ParseIP(routeTarget[1])
	if ip == nil {
		asn, err = strconv.Atoi(routeTarget[1])
		if err != nil {
			return nil, 0, 0, errors.Errorf("invalid RouteTarget specified: %v \n"+
				"Invalid asn (should be ip or int) %v", routeTarget, err)
		}
	}
	target, err = strconv.Atoi(routeTarget[2])
	if err != nil {
		return nil, 0, 0, errors.Errorf("invalid RouteTarget specified: %v \n"+
			"Invalid target id (should be int) %v", routeTarget, err)
	}

	return ip, asn, target, nil
}
