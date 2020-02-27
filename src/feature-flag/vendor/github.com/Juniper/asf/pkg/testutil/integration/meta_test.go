package integration_test

import (
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"testing"

	"github.com/Juniper/asf/pkg/testutil/integration"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func TestCasesDoNotContainDuplicateUUIDsAcrossTests(t *testing.T) {
	uuidsToScenarios := duplicatedUUIDsAcrossTestScenarios(t)

	for uuid, scenarios := range uuidsToScenarios {
		if len(scenarios) > 1 && !isUUIDFromInitData(uuid) {
			assert.Fail(t, fmt.Sprintf("UUID %q duplicated in multiple test scenarios: %v", uuid, scenarios))
		}
	}
}

func isUUIDFromInitData(uuid string) bool {
	_, ok := map[string]struct{}{
		integration.DefaultGlobalSystemConfigUUID: {},
		integration.DefaultDomainUUID:             {},
		integration.DefaultProjectUUID:            {},
	}[uuid]
	return ok
}

func duplicatedUUIDsAcrossTestScenarios(t *testing.T) map[string][]string {
	uuidsToScenarios := duplicatedUUIDs(t)
	for uuid, scenarioNames := range uuidsToScenarios {
		uuidsToScenarios[uuid] = deduplicateSlice(scenarioNames)
	}

	return uuidsToScenarios
}

func duplicatedUUIDs(t *testing.T) map[string][]string {
	uuidsToScenarios := map[string][]string{}
	for _, ts := range testScenarios(t) {
		for _, task := range ts.Workflow {
			if task.Request.Method == http.MethodDelete || task.Request.Method == http.MethodGet {
				continue
			}

			resources, ok := task.Request.Data.(map[string]interface{})
			if !ok {
				continue
			}

			for _, resource := range resources {
				resourceMap, ok := resource.(map[string]interface{})
				if !ok {
					continue
				}

				var uuid interface{}
				if uuid, ok = resourceMap["uuid"]; !ok {
					continue
				}

				uuidS, ok := uuid.(string)
				require.True(t, ok, "failed to cast uuid of task %q in scenario %q", task.Name, ts.Name)

				uuidsToScenarios[uuidS] = append(uuidsToScenarios[uuidS], ts.Name)
			}
		}
	}
	return uuidsToScenarios
}

func testScenarios(t *testing.T) []*integration.TestScenario {
	var ts []*integration.TestScenario
	for _, f := range testFilePaths(t) {
		test, lErr := integration.LoadTest(f, nil)
		require.NoError(t, lErr)

		ts = append(ts, test)
	}
	return ts
}

func testFilePaths(t *testing.T) []string {
	var tfPaths []string
	err := filepath.Walk("../..", func(path string, info os.FileInfo, err error) error {
		if isTestFile(info) {
			tfPaths = append(tfPaths, path)
		}
		return nil
	})
	if err != nil {
		require.FailNow(t, err.Error())
		return nil
	}

	return tfPaths
}

func isTestFile(f os.FileInfo) bool {
	return !f.IsDir() &&
		strings.HasPrefix(f.Name(), "test_") &&
		(strings.HasSuffix(f.Name(), ".yml") || strings.HasSuffix(f.Name(), ".yaml")) &&
		f.Name() != "test_with_refs.yaml" &&
		f.Name() != "test_config.yml"
}

// deduplicateSlice remove duplication from given string slice.
// See: https://github.com/golang/go/wiki/SliceTricks#in-place-deduplicate-comparable
func deduplicateSlice(s []string) []string {
	sort.Strings(s)
	j := 0
	for i := 1; i < len(s); i++ {
		if s[j] == s[i] {
			continue
		}
		j++
		// preserve the original data
		// in[i], in[j] = in[j], in[i]
		// only set what is required
		s[j] = s[i]
	}
	return s[:j+1]
}
