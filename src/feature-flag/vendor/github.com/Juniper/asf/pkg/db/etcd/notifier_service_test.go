package etcd_test

import (
	"context"
	"testing"

	"github.com/gogo/protobuf/types"
	"github.com/spf13/viper"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
	"github.com/Juniper/asf/pkg/db/etcd"
	"github.com/Juniper/asf/pkg/models"
	"github.com/Juniper/asf/pkg/services"

	integrationetcd "github.com/Juniper/asf/pkg/testutil/integration/etcd"

        //TODO(mlastawiecki): uncomment once the test is fixed
        //                    commented due to go mod errors
        //"github.com/Juniper/asf/pkg/constants"
        //"github.com/Juniper/asf/pkg/testutil/integration"
)

func TestEtcdNotifierService(t *testing.T) {
	tests := []struct {
		name     string
		ops      func(*testing.T, context.Context, services.WriteService)
		watchers integration.Watchers
	}{
		{
			name: "create and update virtual network",
			ops: func(t *testing.T, ctx context.Context, sv services.WriteService) {
				_, err := sv.CreateVirtualNetwork(ctx, &services.CreateVirtualNetworkRequest{
					VirtualNetwork: &models.VirtualNetwork{
						UUID: "vn-blue",
						Name: "vn_blue",
					},
				})
				assert.NoError(t, err, "create virtual network failed")

				_, err = sv.UpdateVirtualNetwork(ctx, &services.UpdateVirtualNetworkRequest{
					VirtualNetwork: &models.VirtualNetwork{
						UUID: "vn-blue",
						Name: "vn_bluuee",
					},
					FieldMask: types.FieldMask{Paths: []string{"name"}},
				})
				assert.NoError(t, err, "update virtual network failed")
			},
			watchers: integration.Watchers{
				"/test/virtual_network/vn-blue": []integration.Event{
					{
						Data: map[string]interface{}{
							"name": "vn_blue",
						},
					},
					{
						Data: map[string]interface{}{
							"name": "vn_bluuee",
						},
					},
				},
			},
		},
		{
			name: "create and delete reference from virtual network to logical router",
			ops: func(t *testing.T, ctx context.Context, sv services.WriteService) {
				_, err := sv.CreateVirtualNetwork(ctx, &services.CreateVirtualNetworkRequest{
					VirtualNetwork: &models.VirtualNetwork{
						UUID: "vn-blue",
						Name: "vn_blue",
					},
				})
				assert.NoError(t, err, "create virtual network failed")

				_, err = sv.CreateLogicalRouter(ctx, &services.CreateLogicalRouterRequest{
					LogicalRouter: &models.LogicalRouter{
						UUID: "lr-blue",
						Name: "lr_blue",
					},
				})
				assert.NoError(t, err, "create logical router failed")

				_, err = sv.CreateVirtualNetworkLogicalRouterRef(ctx,
					&services.CreateVirtualNetworkLogicalRouterRefRequest{
						ID: "vn-blue",
						VirtualNetworkLogicalRouterRef: &models.VirtualNetworkLogicalRouterRef{
							UUID: "lr-blue",
						}})
				assert.NoError(t, err, "create vn-lr reference failed")

				_, err = sv.DeleteVirtualNetworkLogicalRouterRef(ctx,
					&services.DeleteVirtualNetworkLogicalRouterRefRequest{
						ID: "vn-blue",
						VirtualNetworkLogicalRouterRef: &models.VirtualNetworkLogicalRouterRef{
							UUID: "lr-blue",
						}})
				assert.NoError(t, err, "delete vn-lr reference failed")
			},
			watchers: integration.Watchers{
				"/test/virtual_network/vn-blue": []integration.Event{
					{
						Data: map[string]interface{}{
							"name": "vn_blue",
						},
					},
					{
						Data: map[string]interface{}{
							"name": "vn_blue",
							"logical_router_refs": []interface{}{
								map[string]interface{}{
									"uuid": "lr-blue",
								},
							},
						},
					},
					{
						Data: map[string]interface{}{
							"name":                "vn_blue",
							"logical_router_refs": "$null",
						},
					},
				},
				"/test/logical_router/lr-blue": []integration.Event{
					{
						Data: map[string]interface{}{
							"name": "lr_blue",
						},
					},
					{
						Data: map[string]interface{}{
							"name": "lr_blue",
							"virtual_network_back_refs": []interface{}{
								map[string]interface{}{
									"uuid": "vn-blue",
								},
							},
						},
					},
					{
						Data: map[string]interface{}{
							"name":                      "lr_blue",
							"virtual_network_back_refs": "$null",
						},
					},
				},
			},
		},
	}

	etcdPath := "test"
	// TODO(Daniel): remove that in order not to depend on Viper and use constructors' parameters instead
	viper.Set(constants.ETCDEndpointsVK, integrationetcd.Endpoint)
	viper.Set(constants.ETCDPathVK, etcdPath)

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			ec := integrationetcd.NewEtcdClient(t)
			defer ec.Close(t)

			ec.Clear(t)

			check := integration.StartWatchers(t, tt.name, tt.watchers)
			sv, err := etcd.NewNotifierService(etcdPath, models.JSONCodec)
			require.NoError(t, err)

			tt.ops(t, context.Background(), sv)

			check(t)
		})
	}
}
