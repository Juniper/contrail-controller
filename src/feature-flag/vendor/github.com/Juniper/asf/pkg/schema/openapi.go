package schema

import (
	"github.com/go-openapi/spec"
)

//ToOpenAPI generates OpenAPI commands.
// nolint: gocyclo
func (api *API) ToOpenAPI() (*spec.Swagger, error) {
	definitions := spec.Definitions{}
	paths := &spec.Paths{
		Paths: map[string]spec.PathItem{},
	}
	openAPI := &spec.Swagger{
		SwaggerProps: spec.SwaggerProps{
			Swagger:  "2.0",
			BasePath: "/",
			Schemes:  []string{"https"},
			Consumes: []string{"application/json"},
			Produces: []string{"application/json"},
			Info: &spec.Info{
				InfoProps: spec.InfoProps{
					Version: "5.1",
					Title:   "Contrail API OpenAPI2.0 Definitions",
					License: &spec.License{
						Name: "Apache2.0",
					},
				},
			},
			Paths:       paths,
			Definitions: definitions,
		},
	}
	for _, apiSchema := range api.Schemas {
		if apiSchema.Type == AbstractType {
			continue
		}
		d, err := apiSchema.JSONSchema.ToOpenAPI()
		if err != nil {
			return nil, err
		}
		// add reference and back ref

		for _, reference := range apiSchema.References {
			referenceSchema := &spec.Schema{
				SchemaProps: spec.SchemaProps{
					Properties: map[string]spec.Schema{
						"uuid": {
							SchemaProps: spec.SchemaProps{
								Description: "UUID of the referenced resource.",
								Type:        spec.StringOrArray([]string{"string"}),
							},
						},
						"to": {
							SchemaProps: spec.SchemaProps{
								Description: "FQName of the referenced resource.",
								Type:        spec.StringOrArray([]string{"array"}),
								Items: &spec.SchemaOrArray{
									Schema: &spec.Schema{
										SchemaProps: spec.SchemaProps{
											Type: spec.StringOrArray([]string{"string"}),
										},
									},
								},
							},
						},
					},
				},
			}
			if reference.RefType != "" {
				var ref spec.Ref
				ref, err = spec.NewRef("#/definitions/" + reference.RefType)
				if err != nil {
					return nil, err
				}
				referenceSchema.Properties["attr"] = spec.Schema{
					SchemaProps: spec.SchemaProps{
						Ref: ref,
					},
				}
			}
			d.Properties[reference.LinkTo.ID+"_refs"] = spec.Schema{
				SchemaProps: spec.SchemaProps{
					Description: reference.Description,
					Type:        spec.StringOrArray([]string{"array"}),
					Items: &spec.SchemaOrArray{
						Schema: referenceSchema,
					},
				},
			}
		}

		for _, child := range apiSchema.Children {
			var ref spec.Ref
			ref, err = spec.NewRef("#/definitions/" + child.LinkTo.JSONSchema.GoName + "APIType")
			if err != nil {
				return nil, err
			}
			d.Properties[child.LinkTo.ID+"s"] = spec.Schema{
				SchemaProps: spec.SchemaProps{
					Description: child.Description,
					Type:        spec.StringOrArray([]string{"array"}),
					Items: &spec.SchemaOrArray{
						Schema: &spec.Schema{
							SchemaProps: spec.SchemaProps{
								Ref: ref,
							},
						},
					},
				},
				SwaggerSchemaProps: spec.SwaggerSchemaProps{
					Example: []string{},
				},
			}
		}
		definitions[apiSchema.JSONSchema.GoName+"APIType"] = *d

		ref, err := spec.NewRef("#/definitions/" + apiSchema.JSONSchema.GoName + "APIType")
		if err != nil {
			return nil, err
		}
		listAPIRef, err := spec.NewRef("#/definitions/" + apiSchema.JSONSchema.GoName + "APIListType")
		if err != nil {
			return nil, err
		}
		singleAPIRef, err := spec.NewRef("#/definitions/" + apiSchema.JSONSchema.GoName + "APISingleType")
		if err != nil {
			return nil, err
		}
		//TODO add path for this resource.

		pathItem := spec.PathItem{
			PathItemProps: spec.PathItemProps{
				Parameters: []spec.Parameter{
					{
						SimpleSchema: spec.SimpleSchema{Type: StringType},
						ParamProps: spec.ParamProps{
							Name:     "id",
							Required: true,
							In:       "path",
						},
					},
				},
				Get: &spec.Operation{
					OperationProps: spec.OperationProps{
						//TODO Parameters:
						Responses: &spec.Responses{
							ResponsesProps: spec.ResponsesProps{
								StatusCodeResponses: map[int]spec.Response{
									200: {
										ResponseProps: spec.ResponseProps{
											Description: "Show resource",
											Schema: &spec.Schema{
												SchemaProps: spec.SchemaProps{
													Ref: singleAPIRef,
												},
											},
										},
									},
									404: {
										ResponseProps: spec.ResponseProps{
											Description: "Resource not found",
										},
									},
									401: {
										ResponseProps: spec.ResponseProps{
											Description: "Unauthorized",
										},
									},
									500: {
										ResponseProps: spec.ResponseProps{
											Description: "Server Side Error",
										},
									},
								},
							},
						},
					},
				},
				//TODO
				Delete: &spec.Operation{
					OperationProps: spec.OperationProps{
						//TODO Parameters:
						Responses: &spec.Responses{
							ResponsesProps: spec.ResponsesProps{
								StatusCodeResponses: map[int]spec.Response{
									200: {
										ResponseProps: spec.ResponseProps{
											Description: "Delete a resource",
										},
									},
									401: {
										ResponseProps: spec.ResponseProps{
											Description: "Unauthorized",
										},
									},
									404: {
										ResponseProps: spec.ResponseProps{
											Description: "Resource not found",
										},
									},
									409: {
										ResponseProps: spec.ResponseProps{
											Description: "Data conflict",
										},
									},
									500: {
										ResponseProps: spec.ResponseProps{
											Description: "Server Side Error",
										},
									},
								},
							},
						},
					},
				},
				//TODO
				Put: &spec.Operation{
					OperationProps: spec.OperationProps{
						Parameters: []spec.Parameter{
							{
								ParamProps: spec.ParamProps{
									Name:     apiSchema.TypeName,
									Required: true,
									In:       "body",
									Schema: &spec.Schema{
										SchemaProps: spec.SchemaProps{
											Ref: singleAPIRef,
										},
									},
								},
							},
						},
						Responses: &spec.Responses{
							ResponsesProps: spec.ResponsesProps{
								StatusCodeResponses: map[int]spec.Response{
									200: {
										ResponseProps: spec.ResponseProps{
											Description: "Update a resource",
											Schema: &spec.Schema{
												SchemaProps: spec.SchemaProps{
													Ref: singleAPIRef,
												},
											},
										},
									},
									400: {
										ResponseProps: spec.ResponseProps{
											Description: "Bad request",
										},
									},
									401: {
										ResponseProps: spec.ResponseProps{
											Description: "Unauthorized",
										},
									},
									404: {
										ResponseProps: spec.ResponseProps{
											Description: "Resource not found",
										},
									},
									409: {
										ResponseProps: spec.ResponseProps{
											Description: "Data conflict",
										},
									},
									500: {
										ResponseProps: spec.ResponseProps{
											Description: "Server Side Error",
										},
									},
								},
							},
						},
					},
				},
			},
		}
		pluralPathItem := spec.PathItem{
			PathItemProps: spec.PathItemProps{
				Post: &spec.Operation{
					OperationProps: spec.OperationProps{
						Parameters: []spec.Parameter{
							{
								ParamProps: spec.ParamProps{
									Name:     apiSchema.TypeName,
									In:       "body",
									Required: true,
									Schema: &spec.Schema{
										SchemaProps: spec.SchemaProps{
											Ref: singleAPIRef,
										},
									},
								},
							},
						},
						Responses: &spec.Responses{
							ResponsesProps: spec.ResponsesProps{
								StatusCodeResponses: map[int]spec.Response{
									200: {
										ResponseProps: spec.ResponseProps{
											Description: "Create a resource",
											Schema: &spec.Schema{
												SchemaProps: spec.SchemaProps{
													Ref: singleAPIRef,
												},
											},
										},
									},
									400: {
										ResponseProps: spec.ResponseProps{
											Description: "Bad request",
										},
									},
									401: {
										ResponseProps: spec.ResponseProps{
											Description: "Unauthorized",
										},
									},
									404: {
										ResponseProps: spec.ResponseProps{
											Description: "Resource not found",
										},
									},
									409: {
										ResponseProps: spec.ResponseProps{
											Description: "Data conflict",
										},
									},
									500: {
										ResponseProps: spec.ResponseProps{
											Description: "Server Side Error",
										},
									},
								},
							},
						},
					},
				},
				Get: &spec.Operation{
					OperationProps: spec.OperationProps{
						Parameters: []spec.Parameter{
							{
								SimpleSchema: spec.SimpleSchema{Type: StringType},
								ParamProps: spec.ParamProps{
									In:          "query",
									Name:        "parent_id",
									Description: "parent_uuid",
									Required:    false,
								},
							},
							{
								SimpleSchema: spec.SimpleSchema{Type: StringType},
								ParamProps: spec.ParamProps{
									In:          "query",
									Name:        "parent_fq_name_str",
									Description: "parent’s fully-qualified name delimited by ‘:’",
									Required:    false,
								},
							},
							{
								SimpleSchema: spec.SimpleSchema{Type: StringType},
								ParamProps: spec.ParamProps{
									In:          "query",
									Name:        "pobj_uuids",
									Description: "Commna separated object uuids <example1_uuid>,<example2_uuid>",
									Required:    false,
								},
							},
							{
								SimpleSchema: spec.SimpleSchema{Type: StringType},
								ParamProps: spec.ParamProps{
									In:          "query",
									Name:        "detail",
									Description: "True if you need detailed data",
									Required:    false,
								},
							},
							{
								SimpleSchema: spec.SimpleSchema{Type: StringType},
								ParamProps: spec.ParamProps{
									In:          "query",
									Name:        "back_ref_id",
									Description: "back_ref_uuid",
									Required:    false,
								},
							},
							{
								SimpleSchema: spec.SimpleSchema{Type: StringType},
								ParamProps: spec.ParamProps{
									In:          "query",
									Name:        "page_marker",
									Description: "Pagination start marker",
									Required:    false,
								},
							},
							{
								SimpleSchema: spec.SimpleSchema{Type: StringType},
								ParamProps: spec.ParamProps{
									In:          "query",
									Name:        "page_limit",
									Description: "Pagination limit",
									Required:    false,
								},
							},
							{
								SimpleSchema: spec.SimpleSchema{Type: StringType},
								ParamProps: spec.ParamProps{
									In:          "query",
									Name:        "count",
									Description: "Return only resource counts",
									Required:    false,
								},
							},
							{
								SimpleSchema: spec.SimpleSchema{Type: StringType},
								ParamProps: spec.ParamProps{
									In:          "query",
									Name:        "fields",
									Description: "Comma separated object field list you are interested in",
									Required:    false,
								},
							},
							{
								SimpleSchema: spec.SimpleSchema{Type: StringType},
								ParamProps: spec.ParamProps{
									In:          "query",
									Name:        "shared",
									Description: "Included shared object in response.",
									Required:    false,
								},
							},
							{
								SimpleSchema: spec.SimpleSchema{Type: StringType},
								ParamProps: spec.ParamProps{
									In:          "query",
									Name:        "filters",
									Description: "Comma separated fileter list. Example check==a,check==b,name==Bob",
									Required:    false,
								},
							},
							{
								SimpleSchema: spec.SimpleSchema{Type: StringType},
								ParamProps: spec.ParamProps{
									In:          "query",
									Name:        "exclude_hrefs",
									Description: "",
									Required:    false,
								},
							},
						},
						Responses: &spec.Responses{
							ResponsesProps: spec.ResponsesProps{
								StatusCodeResponses: map[int]spec.Response{
									200: {
										ResponseProps: spec.ResponseProps{
											Description: "list a resource",
											Schema: &spec.Schema{
												SchemaProps: spec.SchemaProps{
													Ref: listAPIRef,
												},
											},
										},
									},
									400: {
										ResponseProps: spec.ResponseProps{
											Description: "Bad request",
										},
									},
									404: {
										ResponseProps: spec.ResponseProps{
											Description: "Resource not found",
										},
									},
									401: {
										ResponseProps: spec.ResponseProps{
											Description: "Unauthorized",
										},
									},
									500: {
										ResponseProps: spec.ResponseProps{
											Description: "Server Side Error",
										},
									},
								},
							},
						},
					},
				},
			},
		}

		paths.Paths["/"+apiSchema.Path+"/{id}"] = pathItem
		paths.Paths["/"+apiSchema.PluralPath] = pluralPathItem

		definitions[apiSchema.JSONSchema.GoName+"APIListType"] = spec.Schema{
			SchemaProps: spec.SchemaProps{
				Properties: map[string]spec.Schema{
					apiSchema.PluralPath: {
						SchemaProps: spec.SchemaProps{
							Type: spec.StringOrArray([]string{"array"}),
							Items: &spec.SchemaOrArray{
								Schema: &spec.Schema{
									SchemaProps: spec.SchemaProps{
										Ref: ref,
									},
								},
							},
						},
					},
				},
			},
		}
		definitions[apiSchema.JSONSchema.GoName+"APISingleType"] = spec.Schema{
			SchemaProps: spec.SchemaProps{
				Properties: map[string]spec.Schema{
					apiSchema.Path: {
						SchemaProps: spec.SchemaProps{
							Ref: ref,
						},
					},
				},
			},
		}
	}
	for _, definitionSchema := range api.Definitions {
		for path, definition := range definitionSchema.Definitions {
			d, err := definition.ToOpenAPI()
			if err != nil {
				return nil, err
			}
			definitions[path] = *d
		}
	}
	return openAPI, nil
}

//ToOpenAPI translate json schema to OpenAPI format.
func (s *JSONSchema) ToOpenAPI() (*spec.Schema, error) {
	if s == nil {
		return nil, nil
	}
	refType := s.getRefType()
	if refType != "" {
		ref, err := spec.NewRef("#/definitions/" + refType)
		if err != nil {
			return nil, err
		}
		return &spec.Schema{
			SchemaProps: spec.SchemaProps{
				Ref:         ref,
				Description: s.Description,
			},
		}, nil
	}
	//items
	items, err := s.Items.ToOpenAPI()
	if err != nil {
		return nil, err
	}
	//properties
	properties := map[string]spec.Schema{}
	for key, property := range s.Properties {
		var p *spec.Schema
		p, err = property.ToOpenAPI()
		if err != nil {
			return nil, err
		}
		properties[key] = *p
	}
	result := &spec.Schema{
		SchemaProps: spec.SchemaProps{
			Description: s.Description,
			Type:        spec.StringOrArray([]string{typeToOpenAPI(s.Type)}),
			Title:       s.Title,
			//TODO(nati) support this.
			//Format: s.Format,
			//Maximum: s.Maximum,
			//Minimum: s.Minimum,
			//Pattern: s.Pattern,
			//Enum: s.Enum,
			Default:    s.Default,
			Required:   s.Required,
			Properties: properties,
		},
	}

	if items != nil {
		result.Items = &spec.SchemaOrArray{
			Schema: items,
		}
	}

	return result, nil
}

func typeToOpenAPI(t string) string {
	switch t {
	case UintType:
		return IntegerType
	default:
		return t
	}
}
