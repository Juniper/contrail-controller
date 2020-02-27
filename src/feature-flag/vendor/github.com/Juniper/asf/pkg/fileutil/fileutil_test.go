package fileutil

import (
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestCopyFile(t *testing.T) {
	const (
		srcTestData = "SRC TEST DATA"
		dstTestData = "DST TEST DATA"
	)

	tests := []struct {
		name          string
		srcFilepath   string
		dstFilepath   string
		overwrite     bool
		srcPerm       os.FileMode
		dstPerm       os.FileMode
		dstFileExists bool
		fails         bool
	}{
		{
			name:        "Simple copy test",
			srcFilepath: "fileutiltest/copy_test_file",
			dstFilepath: "fileutiltest/copy_test_output_file",
			srcPerm:     0700,
		},
		{
			name:          "Disallow overwriting file",
			srcFilepath:   "fileutiltest/copy_test_file",
			dstFilepath:   "fileutiltest/copy_test_output_file",
			srcPerm:       0777,
			dstPerm:       0644,
			dstFileExists: true,
			fails:         true,
		},
		{
			name:          "Allow overwriting file",
			srcFilepath:   "fileutiltest/copy_test_file",
			dstFilepath:   "fileutiltest/copy_test_output_file",
			srcPerm:       0777,
			dstPerm:       0644,
			dstFileExists: true,
			overwrite:     true,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			defer removeFilesIfExist(t, []string{tt.srcFilepath, tt.dstFilepath})
			assert.NoError(t, createParentDirectory(tt.srcFilepath))
			assert.NoError(t, createParentDirectory(tt.dstFilepath))
			assert.NoError(t, createFile(tt.srcFilepath, srcTestData, tt.srcPerm))

			var expectedPermissions os.FileMode
			if tt.dstFileExists {
				assert.NoError(t, createFile(tt.dstFilepath, dstTestData, tt.dstPerm))
				expectedPermissions = tt.dstPerm
			} else {
				expectedPermissions = resolveActualSrcPermissions(t, tt.srcFilepath)
			}

			err := CopyFile(tt.srcFilepath, tt.dstFilepath, tt.overwrite)
			if tt.fails {
				assert.Error(t, err)
				return
			}
			assert.NoError(t, err)
			filePermissionEqualTo(t, tt.dstFilepath, expectedPermissions)
			fileContains(t, tt.dstFilepath, srcTestData)
		})
	}
}

func createParentDirectory(file string) error {
	return os.MkdirAll(filepath.Dir(file), 0777)
}

func createFile(path, data string, perm os.FileMode) error {
	return ioutil.WriteFile(path, []byte(data), perm)
}

func filePermissionEqualTo(t *testing.T, filepath string, expectedPermissions os.FileMode) {
	stat, err := os.Stat(filepath)
	assert.NoError(t, err, "could not resolve file permissions")
	assert.Equal(t, expectedPermissions, stat.Mode().Perm())
}

func fileContains(t *testing.T, filepath, expectedData string) {
	actualData, err := ioutil.ReadFile(filepath)
	assert.NoError(t, err)
	assert.Equal(t, expectedData, string(actualData))
}

func removeFilesIfExist(t *testing.T, files []string) {
	for _, file := range files {
		if _, err := os.Stat(file); err != nil && os.IsNotExist(err) {
			continue
		}
		assert.NoError(t, os.Remove(file))
	}
}

func resolveActualSrcPermissions(t *testing.T, srcFile string) os.FileMode {
	info, err := os.Stat(srcFile)
	assert.NoError(t, err, "could not resolve permissions of created source file")
	return info.Mode().Perm()
}
