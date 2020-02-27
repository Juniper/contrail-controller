package models

// SetPrefixes set prefixes.
func (i *InterfaceRouteTable) SetPrefixes(prefixes []string) {
	routes := []*RouteType{}
	for _, prefix := range prefixes {
		routes = append(routes, &RouteType{
			Prefix: prefix,
		})
	}
	i.InterfaceRouteTableRoutes.Route = routes
}
