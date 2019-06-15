// Tomltestgen is a program that retrieves a given version of
// https://github.com/BurntSushi/toml-test and generates go code for go-toml's unit tests
// based on the test files.
//
// Usage: go run github.com/pelletier/go-toml/cmd/tomltestgen > toml_testgen_test.go
package main

import (
	"archive/zip"
	"bytes"
	"flag"
	"fmt"
	"go/format"
	"io"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"regexp"
	"strconv"
	"strings"
	"text/template"
	"time"
)

type invalid struct {
	Name  string
	Input string
}

type valid struct {
	Name    string
	Input   string
	JsonRef string
}

type testsCollection struct {
	Ref       string
	Timestamp string
	Invalid   []invalid
	Valid     []valid
	Count     int
}

const srcTemplate = "// Generated by tomltestgen for toml-test ref {{.Ref}} on {{.Timestamp}}\n" +
	"package toml\n" +
	" import (\n" +
	"	\"testing\"\n" +
	")\n" +

	"{{range .Invalid}}\n" +
	"func TestInvalid{{.Name}}(t *testing.T) {\n" +
	"	input := {{.Input|gostr}}\n" +
	"	testgenInvalid(t, input)\n" +
	"}\n" +
	"{{end}}\n" +
	"\n" +
	"{{range .Valid}}\n" +
	"func TestValid{{.Name}}(t *testing.T) {\n" +
	"   input := {{.Input|gostr}}\n" +
	"   jsonRef := {{.JsonRef|gostr}}\n" +
	"   testgenValid(t, input, jsonRef)\n" +
	"}\n" +
	"{{end}}\n"

func downloadTmpFile(url string) string {
	log.Println("starting to download file from", url)
	resp, err := http.Get(url)
	if err != nil {
		panic(err)
	}
	defer resp.Body.Close()

	tmpfile, err := ioutil.TempFile("", "toml-test-*.zip")
	if err != nil {
		panic(err)
	}
	defer tmpfile.Close()

	copiedLen, err := io.Copy(tmpfile, resp.Body)
	if err != nil {
		panic(err)
	}
	if resp.ContentLength > 0 && copiedLen != resp.ContentLength {
		panic(fmt.Errorf("copied %d bytes, request body had %d", copiedLen, resp.ContentLength))
	}
	return tmpfile.Name()
}

func kebabToCamel(kebab string) string {
	camel := ""
	nextUpper := true
	for _, c := range kebab {
		if nextUpper {
			camel += strings.ToUpper(string(c))
			nextUpper = false
		} else if c == '-' {
			nextUpper = true
		} else {
			camel += string(c)
		}
	}
	return camel
}

func readFileFromZip(f *zip.File) string {
	reader, err := f.Open()
	if err != nil {
		panic(err)
	}
	defer reader.Close()
	bytes, err := ioutil.ReadAll(reader)
	if err != nil {
		panic(err)
	}
	return string(bytes)
}

func templateGoStr(input string) string {
	if len(input) > 0 && input[len(input)-1] == '\n' {
		input = input[0 : len(input)-1]
	}
	if strings.Contains(input, "`") {
		lines := strings.Split(input, "\n")
		for idx, line := range lines {
			lines[idx] = strconv.Quote(line + "\n")
		}
		return strings.Join(lines, " + \n")
	}
	return "`" + input + "`"
}

var (
	ref = flag.String("r", "master", "git reference")
)

func usage() {
	_, _ = fmt.Fprintf(os.Stderr, "usage: tomltestgen [flags]\n")
	flag.PrintDefaults()
}

func main() {
	flag.Usage = usage
	flag.Parse()

	url := "https://codeload.github.com/BurntSushi/toml-test/zip/" + *ref
	resultFile := downloadTmpFile(url)
	defer os.Remove(resultFile)
	log.Println("file written to", resultFile)

	zipReader, err := zip.OpenReader(resultFile)
	if err != nil {
		panic(err)
	}
	defer zipReader.Close()

	collection := testsCollection{
		Ref:       *ref,
		Timestamp: time.Now().Format(time.RFC3339),
	}

	zipFilesMap := map[string]*zip.File{}

	for _, f := range zipReader.File {
		zipFilesMap[f.Name] = f
	}

	testFileRegexp := regexp.MustCompile(`([^/]+/tests/(valid|invalid)/(.+))\.(toml)`)
	for _, f := range zipReader.File {
		groups := testFileRegexp.FindStringSubmatch(f.Name)
		if len(groups) > 0 {
			name := kebabToCamel(groups[3])
			testType := groups[2]

			log.Printf("> [%s] %s\n", testType, name)

			tomlContent := readFileFromZip(f)

			switch testType {
			case "invalid":
				collection.Invalid = append(collection.Invalid, invalid{
					Name:  name,
					Input: tomlContent,
				})
				collection.Count++
			case "valid":
				baseFilePath := groups[1]
				jsonFilePath := baseFilePath + ".json"
				jsonContent := readFileFromZip(zipFilesMap[jsonFilePath])

				collection.Valid = append(collection.Valid, valid{
					Name:    name,
					Input:   tomlContent,
					JsonRef: jsonContent,
				})
				collection.Count++
			default:
				panic(fmt.Sprintf("unknown test type: %s", testType))
			}
		}
	}

	log.Printf("Collected %d tests from toml-test\n", collection.Count)

	funcMap := template.FuncMap{
		"gostr": templateGoStr,
	}
	t := template.Must(template.New("src").Funcs(funcMap).Parse(srcTemplate))
	buf := new(bytes.Buffer)
	err = t.Execute(buf, collection)
	if err != nil {
		panic(err)
	}
	outputBytes, err := format.Source(buf.Bytes())
	if err != nil {
		panic(err)
	}
	fmt.Println(string(outputBytes))
}
