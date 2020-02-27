package fileutil

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/pkg/errors"

	yaml "gopkg.in/yaml.v2"
)

//SaveFile saves object to file. suffix of filepath will be
// used as file type. currently, json and yaml is supported
func SaveFile(file string, data interface{}) error {
	var bytes []byte
	var err error
	if strings.HasSuffix(file, ".json") {
		bytes, err = json.MarshalIndent(data, "", "    ")
	} else if strings.HasSuffix(file, ".yaml") || strings.HasSuffix(file, ".yml") {
		bytes, err = yaml.Marshal(data)

	}
	if err != nil {
		return err
	}
	return ioutil.WriteFile(file, bytes, 0644)
}

//LoadFile loads object from file. suffix of filepath will be
// used as file type. currently, json and yaml is supported
func LoadFile(filePath string, data interface{}) error {
	bodyBuff, err := GetContent(filePath)
	if err != nil {
		return err
	}
	if strings.HasSuffix(filePath, ".json") {
		return json.Unmarshal(bodyBuff, data)
	} else if strings.HasSuffix(filePath, ".yaml") || strings.HasSuffix(filePath, ".yml") {
		return yaml.Unmarshal(bodyBuff, data)
	}
	return fmt.Errorf("format isn't supported")
}

//GetContent loads file from remote or local
func GetContent(url string) ([]byte, error) {
	if strings.HasPrefix(url, "http://") || strings.HasPrefix(url, "https://") {
		resp, err := http.Get(url)
		if err != nil {
			return nil, err
		}
		defer resp.Body.Close() // nolint: errcheck
		content, err := ioutil.ReadAll(resp.Body)
		if err != nil {
			return nil, err
		}
		return content, err
	}
	if strings.HasPrefix(url, "file://") {
		url = strings.TrimPrefix(url, "file://")
	}
	content, err := ioutil.ReadFile(url)
	return content, err
}

// TempFile creates a temporary file with the specified prefix and suffix
func TempFile(dir string, prefix string, suffix string) (*os.File, error) {
	if dir == "" {
		dir = os.TempDir()
	}

	name := filepath.Join(dir, fmt.Sprint(prefix, time.Now().UnixNano(), suffix))
	return os.OpenFile(name, os.O_RDWR|os.O_CREATE|os.O_EXCL, 0600)
}

//YAMLtoJSONCompat converts yaml format for json format.
func YAMLtoJSONCompat(yamlData interface{}) interface{} {
	yamlMap, ok := yamlData.(map[interface{}]interface{})
	if ok {
		mapData := map[string]interface{}{}
		for key, value := range yamlMap {
			mapData[key.(string)] = YAMLtoJSONCompat(value)
		}
		return mapData
	}
	yamlList, ok := yamlData.([]interface{})
	if ok {
		mapList := []interface{}{}
		for _, value := range yamlList {
			mapList = append(mapList, YAMLtoJSONCompat(value))
		}
		return mapList
	}
	return yamlData
}

//WriteToFile writes content to a file (path and content are provided as params)
func WriteToFile(path string, content []byte, perm os.FileMode) error {
	// create file if it doesn't exist
	err := os.MkdirAll(filepath.Dir(path), os.ModePerm)
	if err != nil {
		return err
	}

	// write content to file
	return ioutil.WriteFile(path, content, perm)
}

//AppendToFile append content to file
func AppendToFile(path string, content []byte, perm os.FileMode) error {
	err := os.MkdirAll(filepath.Dir(path), os.ModePerm)
	if err != nil {
		return err
	}

	f, err := os.OpenFile(path, os.O_APPEND|os.O_CREATE|os.O_WRONLY, perm)
	if err != nil {
		return err
	}
	_, err = f.Write(content)
	return err
}

//CopyFile copies file.
func CopyFile(src, dest string, overwrite bool) error {
	srcInfo, err := os.Stat(src)
	if err != nil {
		return errors.Wrap(err, "copying file failed, cannot read source file")
	}
	if !overwrite {
		if _, err = os.Stat(dest); err == nil {
			return errors.New("copying file failed, destination file already exists")
		} else if !os.IsNotExist(err) {
			return errors.Wrap(err, "copying file failed, destination file already exists")
		}
	}

	content, err := ioutil.ReadFile(src)
	if err != nil {
		return errors.Wrap(err, "copying file failed, cannot read source file")
	}
	return WriteToFile(dest, content, srcInfo.Mode())
}
