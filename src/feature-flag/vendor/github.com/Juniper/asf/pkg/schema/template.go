package schema

import (
	"bytes"
	"context"
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"

	"github.com/Juniper/asf/pkg/fileutil"
	"github.com/flosch/pongo2"
	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
)

const (
	dictGetJSONSchemaByStringKeyFilter = "dict_get_JSONSchema_by_string_key"
)

// GenerateConfig holds configuration for template-base files generation.
type GenerateConfig struct {
	TemplateConfigs    []TemplateConfig
	DBImportPath       string
	ETCDImportPath     string
	ModelsImportPath   string
	ServicesImportPath string
	NoRegenerate       bool
}

// TemplateConfig contains a configuration for the template.
type TemplateConfig struct {
	TemplateType string `yaml:"type"`
	TemplatePath string `yaml:"template_path"`
	Module       string `yaml:"module"`
	OutputDir    string `yaml:"output_dir"`
	OutputPath   string `yaml:"-"`
}

// LoadTemplateConfigs loads template configurations from given path.
func LoadTemplateConfigs(path string) ([]TemplateConfig, error) {
	var tcs []TemplateConfig
	err := fileutil.LoadFile(path, &tcs)
	return tcs, err
}

// GenerateFiles generates files by applying API data to templates specified in template configs.
func GenerateFiles(api *API, gc *GenerateConfig) error {
	if api == nil {
		return errors.New("received API is nil")
	}

	if err := registerCustomFilters(); err != nil {
		return errors.Wrap(err, "register filters")
	}

	for _, tc := range gc.TemplateConfigs {
		if err := tc.resolveTemplatePath(); err != nil {
			return err
		}
		tc.resolveOutputPath()
		if gc.NoRegenerate && !isOutdated(api, &tc) {
			logrus.WithField(
				"template-config", fmt.Sprintf("%+v", tc),
			).Debug("Target file is up to date - skipping generation")
			continue
		}

		err := generateFile(api, gc, &tc)
		if err != nil {
			return errors.Wrap(err, "generate file")
		}
	}
	return nil
}

func (tc *TemplateConfig) resolveTemplatePath() error {
	if mp := tc.Module; mp != "" {
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		moduleDir, err := resolveModuleDir(ctx, mp)
		if err != nil {
			return err
		}
		tc.TemplatePath = filepath.Join(moduleDir, tc.TemplatePath)
	}
	return nil
}

var moduleDirCache = map[string]string{}

// resolveModuleDir gets absolute path for given module using go list.
// In case of successful retrieval the result is cached.
func resolveModuleDir(ctx context.Context, module string) (string, error) {
	if dir, ok := moduleDirCache[module]; ok {
		return dir, nil
	}
	dir, err := resolveModuleDirWithGoList(ctx, module)
	if err != nil {
		return "", err
	}
	moduleDirCache[module] = dir
	return dir, nil
}

func resolveModuleDirWithGoList(ctx context.Context, module string) (string, error) {
	cmd := exec.CommandContext(ctx, "go", "list", "-f", `{{ .Dir }}`, module)
	var out, eout bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &eout
	if err := cmd.Run(); err != nil {
		return "", errors.Wrapf(err, "calling %s returned: %s", cmd, eout.String())
	}
	path := strings.Trim(strings.TrimSpace(out.String()), "'")
	return path, nil
}

func (tc *TemplateConfig) resolveOutputPath() {
	tDir, tFile := filepath.Split(tc.TemplatePath)
	if tc.OutputDir != "" {
		tc.OutputPath = filepath.Join(tc.OutputDir, generatedFileName(tFile))
	} else {
		tc.OutputPath = filepath.Join(tDir, generatedFileName(tFile))
	}
}

func generatedFileName(templateFile string) string {
	return "gen_" + strings.TrimSuffix(templateFile, ".tmpl")
}

func isOutdated(api *API, tc *TemplateConfig) bool {
	if api.Timestamp.IsZero() {
		return true
	}
	sourceInfo, err := os.Stat(tc.TemplatePath)
	if err != nil {
		return true
	}
	targetInfo, err := os.Stat(tc.OutputPath)
	if err != nil {
		return true
	}
	return sourceInfo.ModTime().After(targetInfo.ModTime()) || api.Timestamp.After(targetInfo.ModTime())
}

// nolint: gocyclo
func generateFile(api *API, gc *GenerateConfig, tc *TemplateConfig) error {
	logrus.WithFields(logrus.Fields{
		"template-type": tc.TemplateType,
		"template-path": tc.TemplatePath,
		"output-path":   tc.OutputPath,
	}).Debug("Generating file")

	tpl, err := loadTemplate(tc.TemplatePath)
	if err != nil {
		return err
	}

	if err = ensureDirectoryExists(tc.OutputPath); err != nil {
		return errors.Wrapf(err, "ensure the directory exists for output path: %q", tc.OutputPath)
	}

	if tc.TemplateType == "all" {
		data, err := tpl.Execute(pongo2.Context{
			"schemas":            api.Schemas,
			"types":              api.Types,
			"dbImportPath":       gc.DBImportPath,
			"etcdImportPath":     gc.ETCDImportPath,
			"modelsImportPath":   gc.ModelsImportPath,
			"servicesImportPath": gc.ServicesImportPath,
		})
		if err != nil {
			return errors.Wrapf(err, "execute template %q", tc.TemplatePath)
		}

		if err = writeFile(tc.OutputPath, data, tc.TemplatePath); err != nil {
			return errors.Wrapf(err, "generate the file from template %q", tc.TemplatePath)
		}
	} else if tc.TemplateType == "alltype" {
		var schemas []*Schema
		for typeName, typeJSONSchema := range api.Types {
			typeJSONSchema.GoName = typeName
			schemas = append(schemas, &Schema{
				JSONSchema:     typeJSONSchema,
				Children:       map[string]*BackReference{},
				BackReferences: map[string]*BackReference{},
			})
		}
		for _, schema := range api.Schemas {
			if schema.Type == AbstractType || schema.ID == "" {
				continue
			}
			schemas = append(schemas, schema)
		}
		data, err := tpl.Execute(pongo2.Context{
			"schemas":            schemas,
			"modelsImportPath":   gc.ModelsImportPath,
			"servicesImportPath": gc.ServicesImportPath,
		})
		if err != nil {
			return errors.Wrapf(err, "execute template %q", tc.TemplatePath)
		}

		if err = writeFile(tc.OutputPath, data, tc.TemplatePath); err != nil {
			return errors.Wrapf(err, "generate the file from template %q", tc.TemplatePath)
		}
	}
	return nil
}

func loadTemplate(templatePath string) (*pongo2.Template, error) {
	o, err := fileutil.GetContent(templatePath)
	if err != nil {
		return nil, errors.Wrapf(err, "get content of template %q", templatePath)
	}
	return pongo2.FromString(string(o))
}

func ensureDirectoryExists(path string) error {
	return os.MkdirAll(filepath.Dir(path), os.ModePerm)
}

func registerCustomFilters() error {
	/* When called like this: {{ dict_value|dict_get_JSONSchema_by_string_key:key_var }}
	then: dict_value is here as `in' variable and key_var is here as `param'
	This is needed to obtain value from map with a key in variable (not as a hardcoded string)
	*/
	if !pongo2.FilterExists(dictGetJSONSchemaByStringKeyFilter) {
		if err := pongo2.RegisterFilter(
			dictGetJSONSchemaByStringKeyFilter,
			func(in *pongo2.Value, param *pongo2.Value) (*pongo2.Value, *pongo2.Error) {
				m, _ := in.Interface().(map[string]*JSONSchema) //nolint: errcheck
				return pongo2.AsValue(m[param.String()]), nil
			},
		); err != nil {
			return errors.Wrapf(err, "register filter %q", dictGetJSONSchemaByStringKeyFilter)
		}
	}
	return nil
}

func writeFile(outputPath, data, templatePath string) error {
	if err := ioutil.WriteFile(
		outputPath,
		[]byte(generationComment(outputPath, templatePath)+data),
		0644,
	); err != nil {
		return errors.Wrapf(err, "write the file to path %q", outputPath)
	}
	return nil
}

func generationComment(path, templatePath string) string {
	prefix := "# "
	if strings.HasSuffix(path, ".go") || strings.HasSuffix(path, ".proto") {
		prefix = "// "
	} else if strings.HasSuffix(path, ".sql") {
		prefix = "-- "
	}
	return prefix + fmt.Sprintf(
		"Code generated by contrailschema tool from template %s; DO NOT EDIT.\n\n", templatePath,
	)
}
