package sync_test

import (
	"context"
	"encoding/json"
	"fmt"
	"math/rand"
	"path"
	"testing"
	"time"

	//"github.com/Juniper/asf/pkg/constants"
	"github.com/Juniper/asf/pkg/db"
	"github.com/Juniper/asf/pkg/etcd"
	"github.com/Juniper/asf/pkg/models"
	"github.com/Juniper/asf/pkg/models/basemodels"
	"github.com/Juniper/asf/pkg/services"
	"github.com/Juniper/asf/pkg/sync"
	"github.com/Juniper/asf/pkg/testutil/integration"
	"github.com/coreos/etcd/clientv3"
	"github.com/gogo/protobuf/types"
	"github.com/spf13/viper"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	integrationetcd "github.com/Juniper/asf/pkg/testutil/integration/etcd"
)

func TestSyncService(t *testing.T) {
	tests := []struct {
		name     string
		ops      func(*testing.T, services.WriteService)
		dump     bool
		watchers integration.Watchers
	}{
		{
			name: "some initial resources are dumped by Sync",
			dump: true,
			watchers: integration.Watchers{
				"/test/virtual_network/5720afd0-d5a6-46ef-bd81-3be7f715cd27": []integration.Event{
					{
						Data: map[string]interface{}{
							"uuid":        "5720afd0-d5a6-46ef-bd81-3be7f715cd27",
							"parent_uuid": integration.DefaultProjectUUID,
							"parent_type": "project",
							"fq_name":     []interface{}{"default-domain", "default-project", "default-virtual-network"},
							"id_perms": map[string]interface{}{
								"enable":        true,
								"created":       "2018-05-23T17:29:57.559916",
								"user_visible":  true,
								"last_modified": "2018-05-23T17:29:57.559916",
								"permissions": map[string]interface{}{
									"owner":        "cloud-admin",
									"owner_access": 7,
									"other_access": 7,
									"group":        "cloud-admin-group",
									"group_access": 7,
								},
								"uuid": map[string]interface{}{
									"uuid_mslong": uint64(6278211192026973935),
									"uuid_lslong": uint64(13655261412632939815),
								},
							},
							"perms2": map[string]interface{}{
								"owner":        "cloud-admin",
								"owner_access": 7,
							},
							"virtual_network_network_id": 1,
							"routing_instances": []interface{}{
								map[string]interface{}{
									"uuid": "d59c5934-1dbd-4865-b8e9-ff9d7f3f16d0",
									"fq_name": []interface{}{
										"default-domain",
										"default-project",
										"default-virtual-network",
										"default-virtual-network",
									},
								},
							},
						},
					},
				},
			},
		},
		{
			name: "create and update virtual network",
			ops: func(t *testing.T, sv services.WriteService) {
				ctx := context.Background()
				_, err := sv.CreateVirtualNetwork(ctx, &services.CreateVirtualNetworkRequest{
					VirtualNetwork: &models.VirtualNetwork{
						UUID: "vn-blue",
						Name: "vn_blue",
						IDPerms: &models.IdPermsType{
							UUID: &models.UuidType{
								UUIDMslong: 1,
								UUIDLslong: 2,
							},
						},
					},
				})
				require.NoError(t, err, "create virtual network failed")

				_, err = sv.UpdateVirtualNetwork(ctx, &services.UpdateVirtualNetworkRequest{
					VirtualNetwork: &models.VirtualNetwork{
						UUID: "vn-blue",
						Name: "vn_bluuee",
						IDPerms: &models.IdPermsType{
							UUID: &models.UuidType{
								UUIDMslong: 1337,
								UUIDLslong: 2778,
							},
						},
					},
					FieldMask: types.FieldMask{Paths: []string{
						"name", "id_perms.uuid.uuid_lslong", "id_perms.uuid.uuid_mslong",
					}},
				})
				assert.NoError(t, err, "update virtual network failed")

				_, err = sv.DeleteVirtualNetwork(ctx, &services.DeleteVirtualNetworkRequest{ID: "vn-blue"})
				assert.NoError(t, err, "delete virtual network failed")
			},
			watchers: integration.Watchers{
				"/test/virtual_network/vn-blue": []integration.Event{
					{
						Data: map[string]interface{}{
							"name": "vn_blue",
							"id_perms": map[string]interface{}{
								"uuid": map[string]interface{}{
									"uuid_mslong": 1,
									"uuid_lslong": 2,
								},
							},
						},
					},
					{
						Data: map[string]interface{}{
							"name": "vn_bluuee",
							"id_perms": map[string]interface{}{
								"uuid": map[string]interface{}{
									"uuid_mslong": 1337,
									"uuid_lslong": 2778,
								},
							},
						},
					},
					{
						Data: nil,
					},
				},
			},
		},
		{
			name: "create and delete reference from virtual network to Network IPAM",
			ops: func(t *testing.T, sv services.WriteService) {
				ctx := context.Background()
				_, err := sv.CreateVirtualNetwork(ctx, &services.CreateVirtualNetworkRequest{
					VirtualNetwork: &models.VirtualNetwork{
						UUID: "vn-blue",
						Name: "vn_blue",
					},
				})
				assert.NoError(t, err, "create virtual network failed")

				_, err = sv.CreateNetworkIpam(ctx, &services.CreateNetworkIpamRequest{
					NetworkIpam: &models.NetworkIpam{
						UUID:   "ni-blue",
						Name:   "ni_blue",
						FQName: []string{"ni_blue"},
					},
				})
				assert.NoError(t, err, "create Network IPAM failed")

				_, err = sv.CreateVirtualNetworkNetworkIpamRef(ctx,
					&services.CreateVirtualNetworkNetworkIpamRefRequest{
						ID: "vn-blue",
						VirtualNetworkNetworkIpamRef: &models.VirtualNetworkNetworkIpamRef{
							UUID: "ni-blue",
							Attr: &models.VnSubnetsType{HostRoutes: &models.RouteTableType{
								Route: []*models.RouteType{{Prefix: "test_prefix", NextHop: "1.2.3.5"}},
							}},
						},
					},
				)
				assert.NoError(t, err, "create vn-ni reference failed")

				// Check if update requests preserve refs
				_, err = sv.UpdateVirtualNetwork(ctx, &services.UpdateVirtualNetworkRequest{
					VirtualNetwork: &models.VirtualNetwork{
						UUID: "vn-blue",
						Name: "vn_bluuee",
					},
					FieldMask: types.FieldMask{Paths: []string{"name"}},
				})
				assert.NoError(t, err, "update virtual network failed")

				_, err = sv.DeleteVirtualNetworkNetworkIpamRef(ctx,
					&services.DeleteVirtualNetworkNetworkIpamRefRequest{
						ID: "vn-blue",
						VirtualNetworkNetworkIpamRef: &models.VirtualNetworkNetworkIpamRef{
							UUID: "ni-blue",
						}})
				assert.NoError(t, err, "delete vn-ni reference failed")

				_, err = sv.DeleteVirtualNetwork(ctx, &services.DeleteVirtualNetworkRequest{ID: "vn-blue"})
				assert.NoError(t, err, "delete virtual network failed")

				_, err = sv.DeleteNetworkIpam(ctx, &services.DeleteNetworkIpamRequest{ID: "ni-blue"})
				assert.NoError(t, err, "delete Network IPAM failed")
			},
			watchers: integration.Watchers{
				"/test/virtual_network/vn-blue": []integration.Event{
					{
						Data: map[string]interface{}{
							"name":              "vn_blue",
							"network_ipam_refs": "$null",
						},
					},
					{
						Data: map[string]interface{}{
							"name": "vn_blue",
							"network_ipam_refs": []interface{}{map[string]interface{}{
								"uuid": "ni-blue",
								"to":   []interface{}{"ni_blue"},
								"attr": map[string]interface{}{
									"ipam_subnets": nil,
									"host_routes": map[string]interface{}{
										"route": []interface{}{
											map[string]interface{}{
												"next_hop": "1.2.3.5",
												"prefix":   "test_prefix",
											},
										},
									},
								},
							}},
						},
					},
					{
						Data: map[string]interface{}{
							"name": "vn_bluuee",
							"network_ipam_refs": []interface{}{map[string]interface{}{
								"uuid": "ni-blue",
								"attr": map[string]interface{}{
									"ipam_subnets": nil,
									"host_routes": map[string]interface{}{
										"route": []interface{}{map[string]interface{}{
											"next_hop": "1.2.3.5",
											"prefix":   "test_prefix",
										}},
									},
								},
							}},
						},
					},
					{
						Data: map[string]interface{}{
							"name":              "vn_bluuee",
							"network_ipam_refs": "$null",
						},
					},
					{
						Data: nil,
					},
				},
				"/test/network_ipam/ni-blue": []integration.Event{
					{
						Data: map[string]interface{}{
							"name":                      "ni_blue",
							"virtual_network_back_refs": "$null",
						},
					},
					{
						Data: map[string]interface{}{
							"name": "ni_blue",
							"virtual_network_back_refs": []interface{}{
								map[string]interface{}{
									"uuid": "vn-blue",
								},
							},
						},
					},
					{
						Data: map[string]interface{}{
							"name":                      "ni_blue",
							"virtual_network_back_refs": "$null",
						},
					},
					{
						Data: nil,
					},
				},
			},
		},
		{
			name: "create Network IPAM, VirtualNetwork with ref to that Network IPAM and delete them",
			ops: func(t *testing.T, sv services.WriteService) {
				ctx := context.Background()

				_, err := sv.CreateNetworkIpam(ctx, &services.CreateNetworkIpamRequest{
					NetworkIpam: &models.NetworkIpam{
						UUID: "ni-red",
						Name: "ni_red",
					},
				})
				assert.NoError(t, err, "create Network IPAM failed")

				_, err = sv.CreateVirtualNetwork(ctx, &services.CreateVirtualNetworkRequest{
					VirtualNetwork: &models.VirtualNetwork{
						UUID: "vn-red",
						Name: "vn_red",
						NetworkIpamRefs: []*models.VirtualNetworkNetworkIpamRef{
							{
								UUID: "ni-red",
								Attr: &models.VnSubnetsType{HostRoutes: &models.RouteTableType{
									Route: []*models.RouteType{{Prefix: "test_prefix", NextHop: "1.2.3.5"}},
								}},
							},
						},
					},
				})
				assert.NoError(t, err, "create virtual network failed")

				_, err = sv.DeleteVirtualNetwork(ctx, &services.DeleteVirtualNetworkRequest{ID: "vn-red"})
				assert.NoError(t, err, "delete virtual network failed")

				_, err = sv.DeleteNetworkIpam(ctx, &services.DeleteNetworkIpamRequest{ID: "ni-red"})
				assert.NoError(t, err, "delete Network IPAM failed")
			},
			watchers: integration.Watchers{
				"/test/virtual_network/vn-red": []integration.Event{
					{
						Data: map[string]interface{}{
							"name": "vn_red",
							"network_ipam_refs": []interface{}{map[string]interface{}{
								"uuid": "ni-red",
								"attr": map[string]interface{}{
									"ipam_subnets": nil,
									"host_routes": map[string]interface{}{
										"route": []interface{}{map[string]interface{}{
											"next_hop": "1.2.3.5",
											"prefix":   "test_prefix",
										}},
									},
								},
							}},
						},
					},
					{
						Data: nil,
					},
				},
				"/test/network_ipam/ni-red": []integration.Event{
					{
						Data: map[string]interface{}{
							"name":                      "ni_red",
							"virtual_network_back_refs": "$null",
						},
					},
					{
						Data: map[string]interface{}{
							"name": "ni_red",
							"virtual_network_back_refs": []interface{}{
								map[string]interface{}{
									"uuid": "vn-red",
								},
							},
						},
					},
					{
						Data: map[string]interface{}{
							"name":                      "ni_red",
							"virtual_network_back_refs": "$null",
						},
					},
					{
						Data: nil,
					},
				},
			},
		},
	}

	// TODO(Daniel): remove that in order not to depend on Viper and use constructors' parameters instead
	viper.Set(etcd.ETCDPathVK, "test")
	ec := integrationetcd.NewEtcdClient(t)
	defer ec.Close(t)

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			integration.SetDefaultSyncConfig(tt.dump)

			dbService, err := db.NewServiceFromConfig()
			require.NoError(t, err)

			rev := ec.Clear(t)

			check := integration.StartWatchers(t, tt.name, tt.watchers, clientv3.WithRev(rev+1))

			sync, err := sync.NewService()
			require.NoError(t, err)

			defer integration.RunNoError(t, sync)(t)
			<-sync.DumpDone()

			if tt.ops != nil {
				tt.ops(t, dbService)
			}

			check(t)
		})
	}
}

func TestSyncSynchronizesExistingPostgresDataToEtcd(t *testing.T) {
	s := integration.NewRunningAPIServer(t, &integration.APIServerConfig{
		RepoRootPath: "../../..",
	})
	defer s.CloseT(t)
	hc := integration.NewTestingHTTPClient(t, s.URL(), integration.AdminUserID)
	ec := integrationetcd.NewEtcdClient(t)
	defer ec.Close(t)

	testID := generateTestID(t)
	projectUUID := testID + "-project"
	networkIPAMUUID := testID + "-network-ipam"
	vnRedUUID := testID + "-red-vn"
	vnGreenUUID := testID + "-green-vn"
	vnBlueUUID := testID + "-blue-vn"
	vnUUIDs := []string{vnRedUUID, vnGreenUUID, vnBlueUUID}

	checkNoSuchVirtualNetworksInAPIServer(t, hc, vnUUIDs)
	checkNoSuchVirtualNetworksInEtcd(t, ec, vnUUIDs)

	vnRedWatch, redCtx, cancelRedCtx := ec.WatchResource(integrationetcd.VirtualNetworkSchemaID, vnRedUUID)
	defer cancelRedCtx()
	vnGreenWatch, greenCtx, cancelGreenCtx := ec.WatchResource(integrationetcd.VirtualNetworkSchemaID, vnGreenUUID)
	defer cancelGreenCtx()
	vnBlueWatch, blueCtx, cancelBlueCtx := ec.WatchResource(integrationetcd.VirtualNetworkSchemaID, vnBlueUUID)
	defer cancelBlueCtx()

	integration.CreateProject(t, hc, project(projectUUID))
	defer integration.DeleteProject(t, hc, projectUUID)

	integration.CreateNetworkIpam(t, hc, networkIPAM(networkIPAMUUID, projectUUID))
	defer integration.DeleteNetworkIpam(t, hc, networkIPAMUUID)

	integration.CreateVirtualNetwork(t, hc, virtualNetworkRed(vnRedUUID, projectUUID, networkIPAMUUID))
	integration.CreateVirtualNetwork(t, hc, virtualNetworkGreen(vnGreenUUID, projectUUID, networkIPAMUUID))
	integration.CreateVirtualNetwork(t, hc, virtualNetworkBlue(vnBlueUUID, projectUUID, networkIPAMUUID))
	defer deleteVirtualNetworksFromAPIServer(t, hc, vnUUIDs)
	defer ec.DeleteKey(t, integrationetcd.JSONEtcdKey(integrationetcd.VirtualNetworkSchemaID, ""),
		clientv3.WithPrefix()) // delete all VNs

	vnRed := integration.GetVirtualNetwork(t, hc, vnRedUUID)
	vnGreen := integration.GetVirtualNetwork(t, hc, vnGreenUUID)
	vnBlue := integration.GetVirtualNetwork(t, hc, vnBlueUUID)

	integration.SetDefaultSyncConfig(true)
	sync, err := sync.NewService()
	require.NoError(t, err)

	defer integration.RunNoError(t, sync)(t)

	<-sync.DumpDone()

	redEvent := integrationetcd.RetrieveCreateEvent(redCtx, t, vnRedWatch)
	greenEvent := integrationetcd.RetrieveCreateEvent(greenCtx, t, vnGreenWatch)
	blueEvent := integrationetcd.RetrieveCreateEvent(blueCtx, t, vnBlueWatch)

	checkSyncedVirtualNetwork(t, redEvent, vnRed)
	checkSyncedVirtualNetwork(t, greenEvent, vnGreen)
	checkSyncedVirtualNetwork(t, blueEvent, vnBlue)
}

// generateTestID creates pseudo-random string and is used to create resources with
// unique UUIDs and FQNames.
func generateTestID(t *testing.T) string {
	rand.Seed(time.Now().UnixNano())
	return fmt.Sprintf("%v-%v", t.Name(), rand.Uint64())
}

func checkNoSuchVirtualNetworksInAPIServer(t *testing.T, hc *integration.HTTPAPIClient, uuids []string) {
	for _, uuid := range uuids {
		hc.CheckResourceDoesNotExist(t, path.Join(integration.VirtualNetworkSingularPath, uuid))
	}
}

func checkNoSuchVirtualNetworksInEtcd(t *testing.T, ec *integrationetcd.EtcdClient, uuids []string) {
	for _, uuid := range uuids {
		ec.CheckKeyDoesNotExist(t, integrationetcd.JSONEtcdKey(integrationetcd.VirtualNetworkSchemaID, uuid))
	}
}

func project(uuid string) *models.Project {
	return &models.Project{
		UUID:       uuid,
		ParentType: integration.DomainType,
		ParentUUID: integration.DefaultDomainUUID,
		FQName:     []string{integration.DefaultDomainID, integration.AdminProjectID, uuid + "-fq-name"},
		Quota:      &models.QuotaType{},
	}
}

func networkIPAM(uuid string, parentUUID string) *models.NetworkIpam {
	return &models.NetworkIpam{
		UUID:       uuid,
		ParentType: integration.ProjectType,
		ParentUUID: parentUUID,
		FQName:     []string{integration.DefaultDomainID, integration.AdminProjectID, uuid + "-fq-name"},
	}
}

func virtualNetworkRed(uuid, parentUUID, networkIPAMUUID string) *models.VirtualNetwork {
	return &models.VirtualNetwork{
		UUID:       uuid,
		ParentType: integration.ProjectType,
		ParentUUID: parentUUID,
		FQName:     []string{integration.DefaultDomainID, integration.AdminProjectID, uuid + "-fq-name"},
		Perms2:     &models.PermType2{Owner: integration.AdminUserID},
		RouteTargetList: &models.RouteTargetList{
			RouteTarget: []string{"100:200"},
		},
		DisplayName:        "red",
		MacLearningEnabled: true,
		NetworkIpamRefs: []*models.VirtualNetworkNetworkIpamRef{{
			UUID: networkIPAMUUID,
			To:   []string{integration.DefaultDomainID, integration.AdminProjectID, networkIPAMUUID + "-fq-name"},
		}},
	}
}

func virtualNetworkGreen(uuid, parentUUID, networkIPAMUUID string) *models.VirtualNetwork {
	return &models.VirtualNetwork{
		UUID:                uuid,
		ParentType:          integration.ProjectType,
		ParentUUID:          parentUUID,
		FQName:              []string{integration.DefaultDomainID, integration.AdminProjectID, uuid + "-fq-name"},
		Perms2:              &models.PermType2{Owner: integration.AdminUserID},
		DisplayName:         "green",
		PortSecurityEnabled: true,
		NetworkIpamRefs: []*models.VirtualNetworkNetworkIpamRef{{
			UUID: networkIPAMUUID,
			To:   []string{integration.DefaultDomainID, integration.AdminProjectID, networkIPAMUUID + "-fq-name"},
		}},
	}
}

func virtualNetworkBlue(uuid, parentUUID, networkIPAMUUID string) *models.VirtualNetwork {
	return &models.VirtualNetwork{
		UUID:        uuid,
		ParentType:  integration.ProjectType,
		ParentUUID:  parentUUID,
		FQName:      []string{integration.DefaultDomainID, integration.AdminProjectID, uuid + "-fq-name"},
		Perms2:      &models.PermType2{Owner: integration.AdminUserID},
		DisplayName: "blue",
		FabricSnat:  true,
		NetworkIpamRefs: []*models.VirtualNetworkNetworkIpamRef{{
			UUID: networkIPAMUUID,
			To:   []string{integration.DefaultDomainID, integration.AdminProjectID, networkIPAMUUID + "-fq-name"},
		}},
	}
}

func deleteVirtualNetworksFromAPIServer(t *testing.T, hc *integration.HTTPAPIClient, uuids []string) {
	for _, uuid := range uuids {
		integration.DeleteVirtualNetwork(t, hc, uuid)
	}
}

func checkSyncedVirtualNetwork(t *testing.T, event *clientv3.Event, expectedVN *models.VirtualNetwork) {
	syncedVN := decodeVirtualNetworkJSON(t, event.Kv.Value)
	removeHrefsFromResource(expectedVN)
	assert.Equal(t, expectedVN, syncedVN, "synced VN does not match created VN")
}

func decodeVirtualNetworkJSON(t *testing.T, vnBytes []byte) *models.VirtualNetwork {
	var vn models.VirtualNetwork
	assert.NoError(t, json.Unmarshal(vnBytes, &vn))
	return &vn
}

func removeHrefsFromResource(object basemodels.Object) {
	object.SetHref("")
	for _, ref := range object.GetReferences() {
		ref.SetHref("")
	}
	for _, backRef := range object.GetBackReferences() {
		backRef.SetHref("")
	}
	for _, child := range object.GetChildren() {
		child.SetHref("")
	}
}
