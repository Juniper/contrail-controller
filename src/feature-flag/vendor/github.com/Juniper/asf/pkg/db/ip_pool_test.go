package db

import (
	"context"
	"net"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/Juniper/asf/pkg/db/basedb"
)

func TestStringIPv6(t *testing.T) {
	tests := []struct {
		ip     net.IP
		output string
	}{
		{
			ip:     net.ParseIP("10.0.0.1"),
			output: "::ffff:a00:1",
		},
		{
			ip:     net.ParseIP("0.0.1.1"),
			output: "::ffff:0:101",
		},
		{
			ip:     net.ParseIP("2001:db8:ac10:fe01::"),
			output: "2001:db8:ac10:fe01::",
		},
		{
			ip:     net.IP{},
			output: "",
		},
	}

	for _, tt := range tests {
		assert.Equal(t, basedb.StringIPv6(tt.ip), tt.output)
	}
}

func TestCreateIpPool(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	testSets := []struct {
		name      string
		firstByte byte
	}{
		{
			name:      "IPv4",
			firstByte: 0,
		},
		{
			name:      "IPv6",
			firstByte: 1,
		},
	}
	tests := []struct {
		name            string
		poolKey         string
		ipPoolsToCreate []ipPool
		expectedPools   int
	}{
		{
			name: "Create one IP Pool for each key",
			ipPoolsToCreate: []ipPool{
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("10.0.0.1"),
					end:   net.ParseIP("10.0.0.10"),
				},
				{
					key:   "subnet-uuid-2",
					start: net.ParseIP("10.0.0.1"),
					end:   net.ParseIP("10.0.0.10"),
				},
			},
			poolKey:       "subnet-uuid-1",
			expectedPools: 1,
		},
		{
			name: "Create 2 overlapping IP Pools",
			ipPoolsToCreate: []ipPool{
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("10.0.0.1"),
					end:   net.ParseIP("10.0.0.10"),
				},
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("10.0.0.5"),
					end:   net.ParseIP("10.0.0.15"),
				},
				{
					key:   "subnet-uuid-2",
					start: net.ParseIP("10.0.0.1"),
					end:   net.ParseIP("10.0.0.10"),
				},
			},
			poolKey:       "subnet-uuid-1",
			expectedPools: 1,
		},

		{
			name: "Create 2 separate IP Pools",
			ipPoolsToCreate: []ipPool{
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("10.0.0.1"),
					end:   net.ParseIP("10.0.0.10"),
				},
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("11.0.0.1"),
					end:   net.ParseIP("11.0.0.10"),
				},
				{
					key:   "subnet-uuid-2",
					start: net.ParseIP("10.0.0.1"),
					end:   net.ParseIP("10.0.0.10"),
				},
			},
			poolKey:       "subnet-uuid-1",
			expectedPools: 2,
		},

		{
			name: "Create IP Pool that encloses the other",
			ipPoolsToCreate: []ipPool{
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("10.0.0.1"),
					end:   net.ParseIP("10.0.0.10"),
				},
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("9.0.0.1"),
					end:   net.ParseIP("11.0.0.10"),
				},
				{
					key:   "subnet-uuid-2",
					start: net.ParseIP("10.0.0.1"),
					end:   net.ParseIP("10.0.0.10"),
				},
			},
			poolKey:       "subnet-uuid-1",
			expectedPools: 1,
		},
	}
	for _, ts := range testSets {
		t.Run(ts.name, func(t *testing.T) {
			for _, tt := range tests {
				t.Run(tt.name, func(t *testing.T) {
					err := db.DoInTransaction(
						ctx,
						func(ctx context.Context) error {
							for _, pool := range tt.ipPoolsToCreate {
								tmpPool := customPool(pool, ts.firstByte)
								err := db.createIPPool(ctx, &tmpPool)
								assert.NoError(t, err, "create pool failed")
							}

							pools, err := db.getIPPools(ctx, &ipPool{key: tt.poolKey})
							assert.NoError(t, err)
							assert.Equal(t, tt.expectedPools, len(pools))

							err = clearIPAddressPool(ctx)
							assert.NoError(t, err)
							return nil
						})
					assert.NoError(t, err, "DoInTransaction Failed")
				})
			}
		})
	}
}

func TestAllocateIp(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	testSets := []struct {
		name      string
		firstByte byte
	}{
		{
			name:      "IPv4",
			firstByte: 0,
		},
		{
			name:      "IPv6",
			firstByte: 1,
		},
	}
	tests := []struct {
		name    string
		poolKey string
		ipPools []ipPool

		expectedIPs   []net.IP
		expectedOneOf []net.IP
		fails         bool
	}{
		{
			name: "Simple example, one pool with correct key",
			ipPools: []ipPool{
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("10.0.0.1"),
					end:   net.ParseIP("10.0.0.10"),
				},
				{
					key:   "subnet-uuid-2",
					start: net.ParseIP("9.0.0.1"),
					end:   net.ParseIP("9.0.0.10"),
				},
			},
			poolKey:     "subnet-uuid-1",
			expectedIPs: []net.IP{net.ParseIP("10.0.0.1")},
			fails:       false,
		},
		{
			name: "Several pools",
			ipPools: []ipPool{
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("10.0.0.1"),
					end:   net.ParseIP("10.0.0.10"),
				},
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("20.0.0.1"),
					end:   net.ParseIP("20.0.0.10"),
				},
			},
			poolKey:       "subnet-uuid-1",
			expectedOneOf: []net.IP{net.ParseIP("10.0.0.1"), net.ParseIP("20.0.0.1")},
			fails:         false,
		},
		{
			name: "Full subnet allocation",
			ipPools: []ipPool{
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("10.0.0.0"),
					end:   net.ParseIP("10.0.0.2"),
				},
			},
			poolKey: "subnet-uuid-1",
			expectedIPs: []net.IP{
				net.ParseIP("10.0.0.0"),
				net.ParseIP("10.0.0.1"),
				net.ParseIP("10.0.0.2"),
			},
			fails: false,
		},
		{
			name: "Exhaust subnet",
			ipPools: []ipPool{
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("10.0.0.0"),
					end:   net.ParseIP("10.0.0.2"),
				},
			},
			poolKey: "subnet-uuid-1",
			expectedIPs: []net.IP{
				net.ParseIP("10.0.0.0"),
				net.ParseIP("10.0.0.1"),
				net.ParseIP("10.0.0.2"),
				net.ParseIP(""),
			},
			fails: true,
		},
		{
			name:        "Empty pool",
			poolKey:     "subnet-uuid-1",
			expectedIPs: []net.IP{net.ParseIP("")},
			fails:       true,
		},
	}
	for _, ts := range testSets {
		t.Run(ts.name, func(t *testing.T) {
			for _, tt := range tests {
				t.Run(tt.name, func(t *testing.T) {
					err := db.DoInTransaction(
						ctx,
						func(ctx context.Context) error {
							for _, pool := range tt.ipPools {
								tmpPool := customPool(pool, ts.firstByte)
								err := db.createIPPool(ctx, &tmpPool)
								assert.NoError(t, err, "create pool failed")
							}
							var err error
							for _, expectedIP := range tt.expectedIPs {
								var ipReceived net.IP
								ipReceived, err = db.allocateIP(ctx, tt.poolKey)
								assert.Equal(t, customIP(expectedIP, ts.firstByte), ipReceived.To16())
								if err != nil {
									break
								}
							}
							if tt.expectedOneOf != nil {
								var customExpectedIPs []net.IP
								for _, expectedIP := range tt.expectedOneOf {
									customExpectedIPs = append(customExpectedIPs, customIP(expectedIP, ts.firstByte))
								}
								var ipReceived net.IP
								ipReceived, err = db.allocateIP(ctx, tt.poolKey)
								assert.Contains(t, customExpectedIPs, ipReceived.To16())
							}
							if tt.fails {
								assert.Error(t, err)
							} else {
								assert.NoError(t, err)
							}

							err = clearIPAddressPool(ctx)
							assert.NoError(t, err)
							return nil
						})
					assert.NoError(t, err, "DoInTransaction Failed")
				})
			}
		})
	}
}

func TestDeleteIpPools(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	testSets := []struct {
		name      string
		firstByte byte
	}{
		{
			name:      "IPv4",
			firstByte: 0,
		},
		{
			name:      "IPv6",
			firstByte: 1,
		},
	}
	tests := []struct {
		name    string
		poolKey string
		ipPools []ipPool

		deletePool          ipPool
		expectedCount       int
		expectedDeleteCount int64
	}{
		{
			name: "Remove all pools",
			ipPools: []ipPool{
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("10.0.0.1"),
					end:   net.ParseIP("10.0.0.10"),
				},
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("11.0.0.1"),
					end:   net.ParseIP("11.0.0.10"),
				},
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("12.0.0.1"),
					end:   net.ParseIP("12.0.0.10"),
				},
				{
					key:   "subnet-uuid-2",
					start: net.ParseIP("10.0.0.1"),
					end:   net.ParseIP("10.0.0.10"),
				},
			},
			deletePool: ipPool{
				key:   "subnet-uuid-1",
				start: net.ParseIP("12.0.0.1"),
			},
			poolKey:             "subnet-uuid-1",
			expectedCount:       0,
			expectedDeleteCount: 3,
		},
		{
			name: "No overlapping pools",
			ipPools: []ipPool{
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("10.0.0.1"),
					end:   net.ParseIP("10.0.0.10"),
				},
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("11.0.0.1"),
					end:   net.ParseIP("11.0.0.10"),
				},
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("12.0.0.1"),
					end:   net.ParseIP("12.0.0.10"),
				},
				{
					key:   "subnet-uuid-2",
					start: net.ParseIP("10.0.0.1"),
					end:   net.ParseIP("10.0.0.10"),
				},
			},
			deletePool: ipPool{
				key:   "subnet-uuid-1",
				start: net.ParseIP("13.0.0.1"),
				end:   net.ParseIP("13.0.0.10"),
			},
			poolKey:       "subnet-uuid-1",
			expectedCount: 3,
		},
		{
			name: "Two overlapping pools",
			ipPools: []ipPool{
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("10.0.0.1"),
					end:   net.ParseIP("10.0.0.10"),
				},
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("11.0.0.1"),
					end:   net.ParseIP("11.0.0.9"),
				},
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("12.0.0.1"),
					end:   net.ParseIP("12.0.0.10"),
				},
			},
			deletePool: ipPool{
				key:   "subnet-uuid-1",
				start: net.ParseIP("10.0.0.8"),
				end:   net.ParseIP("11.0.0.2"),
			},
			poolKey:             "subnet-uuid-1",
			expectedCount:       1,
			expectedDeleteCount: 2,
		},
	}

	for _, ts := range testSets {
		t.Run(ts.name, func(t *testing.T) {
			for _, tt := range tests {
				t.Run(tt.name, func(t *testing.T) {
					err := db.DoInTransaction(
						ctx,
						func(ctx context.Context) error {
							for _, pool := range tt.ipPools {
								tmpPool := customPool(pool, ts.firstByte)
								err := db.createIPPool(ctx, &tmpPool)
								assert.NoError(t, err, "create pool failed")
							}

							_, err := db.getIPPools(ctx, &ipPool{key: tt.poolKey})
							assert.NoError(t, err)

							delPool := customPool(tt.deletePool, ts.firstByte)
							deletedCount, err := db.deleteIPPools(ctx, &delPool)
							assert.NoError(t, err)
							assert.Equal(t, tt.expectedDeleteCount, deletedCount)

							pools, err := db.getIPPools(ctx, &ipPool{key: tt.poolKey})
							assert.NoError(t, err)
							assert.Equal(t, tt.expectedCount, len(pools))

							err = clearIPAddressPool(ctx)
							assert.NoError(t, err)
							return nil
						})
					assert.NoError(t, err, "DoInTransaction Failed")
				})
			}
		})
	}
}

func TestSetIp(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	testSets := []struct {
		name      string
		firstByte byte
	}{
		{
			name:      "IPv4",
			firstByte: 0,
		},
		{
			name:      "IPv6",
			firstByte: 1,
		},
	}
	tests := []struct {
		name         string
		poolKey      string
		startPool    ipPool
		setIPRequest net.IP

		expectedPools []ipPool
		fails         bool
	}{
		{
			name:    "SetIp fail",
			poolKey: "subnet-uuid-1",
			startPool: ipPool{
				key:   "subnet-uuid-1",
				start: net.ParseIP("10.0.0.1"),
				end:   net.ParseIP("10.0.0.10"),
			},
			setIPRequest: net.ParseIP("10.0.0.12"),
			expectedPools: []ipPool{
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("10.0.0.1"),
					end:   net.ParseIP("10.0.0.10"),
				},
			},
			fails: true,
		},
		{
			name:    "SetIp at pool start",
			poolKey: "subnet-uuid-1",
			startPool: ipPool{
				key:   "subnet-uuid-1",
				start: net.ParseIP("10.0.0.1"),
				end:   net.ParseIP("10.0.0.10"),
			},
			setIPRequest: net.ParseIP("10.0.0.1"),
			expectedPools: []ipPool{
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("10.0.0.2"),
					end:   net.ParseIP("10.0.0.10"),
				},
			},
			fails: false,
		},
		{
			name:    "SetIp at pool end",
			poolKey: "subnet-uuid-1",
			startPool: ipPool{
				key:   "subnet-uuid-1",
				start: net.ParseIP("10.0.0.1"),
				end:   net.ParseIP("10.0.0.10"),
			},
			setIPRequest: net.ParseIP("10.0.0.9"),
			expectedPools: []ipPool{
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("10.0.0.1"),
					end:   net.ParseIP("10.0.0.9"),
				},
			},
			fails: false,
		},
		{
			name:    "SetIp in the middle of the pool",
			poolKey: "subnet-uuid-1",
			startPool: ipPool{
				key:   "subnet-uuid-1",
				start: net.ParseIP("10.0.0.1"),
				end:   net.ParseIP("10.0.0.10"),
			},
			setIPRequest: net.ParseIP("10.0.0.5"),
			expectedPools: []ipPool{
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("10.0.0.1"),
					end:   net.ParseIP("10.0.0.5"),
				},
				{
					key:   "subnet-uuid-1",
					start: net.ParseIP("10.0.0.6"),
					end:   net.ParseIP("10.0.0.10"),
				},
			},
			fails: false,
		},
	}
	for _, ts := range testSets {
		t.Run(ts.name, func(t *testing.T) {
			for _, tt := range tests {
				t.Run(tt.name, func(t *testing.T) {
					err := db.DoInTransaction(
						ctx,
						func(ctx context.Context) error {
							tmp := customPool(tt.startPool, ts.firstByte)
							err := db.createIPPool(ctx, &tmp)
							assert.NoError(t, err, "create pool failed")

							err = db.setIP(ctx, tt.poolKey, customIP(tt.setIPRequest, ts.firstByte))

							if tt.fails {
								assert.Error(t, err)
							} else {
								assert.NoError(t, err)
							}

							resultPools, err := db.getIPPools(ctx, &ipPool{key: tt.poolKey})
							assert.NoError(t, err)
							require.Equal(t, len(tt.expectedPools), len(resultPools))
							for i, resultPool := range resultPools {
								assert.Equal(t, customPool(tt.expectedPools[i], ts.firstByte), *resultPool)
							}

							err = clearIPAddressPool(ctx)
							assert.NoError(t, err)
							return err
						})
					assert.NoError(t, err, "DoInTransaction Failed")
				})

			}
		})
	}
}

func clearIPAddressPool(ctx context.Context) error {
	_, err := basedb.GetTransaction(ctx).ExecContext(ctx, "delete from ipaddress_pool")
	return err
}

func customIP(ip net.IP, val byte) net.IP {
	if len(ip) == 0 {
		return ip
	}
	res := make(net.IP, len(ip))
	copy(res, ip)
	res[0] = val
	return res
}

func customPool(pool ipPool, val byte) ipPool {
	res := pool
	res.start = customIP(res.start, val)
	res.end = customIP(res.end, val)
	return res
}
