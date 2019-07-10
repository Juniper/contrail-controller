// Code generated by protoc-gen-go. DO NOT EDIT.
// source: source_cache.proto

/*
Package pb is a generated protocol buffer package.

It is generated from these files:
	source_cache.proto

It has these top-level messages:
	Constraint
	ProjectProperties
	LockedProject
*/
package pb

import proto "github.com/golang/protobuf/proto"
import fmt "fmt"
import math "math"

// Reference imports to suppress errors if they are not otherwise used.
var _ = proto.Marshal
var _ = fmt.Errorf
var _ = math.Inf

// This is a compile-time assertion to ensure that this generated file
// is compatible with the proto package it is being compiled against.
// A compilation error at this line likely means your copy of the
// proto package needs to be updated.
const _ = proto.ProtoPackageIsVersion2 // please upgrade the proto package

type Constraint_Type int32

const (
	Constraint_Revision      Constraint_Type = 0
	Constraint_Branch        Constraint_Type = 1
	Constraint_DefaultBranch Constraint_Type = 2
	Constraint_Version       Constraint_Type = 3
	Constraint_Semver        Constraint_Type = 4
)

var Constraint_Type_name = map[int32]string{
	0: "Revision",
	1: "Branch",
	2: "DefaultBranch",
	3: "Version",
	4: "Semver",
}
var Constraint_Type_value = map[string]int32{
	"Revision":      0,
	"Branch":        1,
	"DefaultBranch": 2,
	"Version":       3,
	"Semver":        4,
}

func (x Constraint_Type) String() string {
	return proto.EnumName(Constraint_Type_name, int32(x))
}
func (Constraint_Type) EnumDescriptor() ([]byte, []int) { return fileDescriptor0, []int{0, 0} }

// Constraint is a serializable representation of a gps.Constraint or gps.UnpairedVersion.
type Constraint struct {
	Type  Constraint_Type `protobuf:"varint,1,opt,name=type,enum=pb.Constraint_Type" json:"type,omitempty"`
	Value string          `protobuf:"bytes,2,opt,name=value" json:"value,omitempty"`
}

func (m *Constraint) Reset()                    { *m = Constraint{} }
func (m *Constraint) String() string            { return proto.CompactTextString(m) }
func (*Constraint) ProtoMessage()               {}
func (*Constraint) Descriptor() ([]byte, []int) { return fileDescriptor0, []int{0} }

func (m *Constraint) GetType() Constraint_Type {
	if m != nil {
		return m.Type
	}
	return Constraint_Revision
}

func (m *Constraint) GetValue() string {
	if m != nil {
		return m.Value
	}
	return ""
}

// ProjectProperties is a serializable representation of gps.ProjectRoot and gps.ProjectProperties.
type ProjectProperties struct {
	Root       string      `protobuf:"bytes,1,opt,name=root" json:"root,omitempty"`
	Source     string      `protobuf:"bytes,2,opt,name=source" json:"source,omitempty"`
	Constraint *Constraint `protobuf:"bytes,3,opt,name=constraint" json:"constraint,omitempty"`
}

func (m *ProjectProperties) Reset()                    { *m = ProjectProperties{} }
func (m *ProjectProperties) String() string            { return proto.CompactTextString(m) }
func (*ProjectProperties) ProtoMessage()               {}
func (*ProjectProperties) Descriptor() ([]byte, []int) { return fileDescriptor0, []int{1} }

func (m *ProjectProperties) GetRoot() string {
	if m != nil {
		return m.Root
	}
	return ""
}

func (m *ProjectProperties) GetSource() string {
	if m != nil {
		return m.Source
	}
	return ""
}

func (m *ProjectProperties) GetConstraint() *Constraint {
	if m != nil {
		return m.Constraint
	}
	return nil
}

// LockedProject is a serializable representation of gps.LockedProject.
type LockedProject struct {
	Root            string      `protobuf:"bytes,1,opt,name=root" json:"root,omitempty"`
	Source          string      `protobuf:"bytes,2,opt,name=source" json:"source,omitempty"`
	UnpairedVersion *Constraint `protobuf:"bytes,3,opt,name=unpairedVersion" json:"unpairedVersion,omitempty"`
	Revision        string      `protobuf:"bytes,4,opt,name=revision" json:"revision,omitempty"`
	Packages        []string    `protobuf:"bytes,5,rep,name=packages" json:"packages,omitempty"`
}

func (m *LockedProject) Reset()                    { *m = LockedProject{} }
func (m *LockedProject) String() string            { return proto.CompactTextString(m) }
func (*LockedProject) ProtoMessage()               {}
func (*LockedProject) Descriptor() ([]byte, []int) { return fileDescriptor0, []int{2} }

func (m *LockedProject) GetRoot() string {
	if m != nil {
		return m.Root
	}
	return ""
}

func (m *LockedProject) GetSource() string {
	if m != nil {
		return m.Source
	}
	return ""
}

func (m *LockedProject) GetUnpairedVersion() *Constraint {
	if m != nil {
		return m.UnpairedVersion
	}
	return nil
}

func (m *LockedProject) GetRevision() string {
	if m != nil {
		return m.Revision
	}
	return ""
}

func (m *LockedProject) GetPackages() []string {
	if m != nil {
		return m.Packages
	}
	return nil
}

func init() {
	proto.RegisterType((*Constraint)(nil), "pb.Constraint")
	proto.RegisterType((*ProjectProperties)(nil), "pb.ProjectProperties")
	proto.RegisterType((*LockedProject)(nil), "pb.LockedProject")
	proto.RegisterEnum("pb.Constraint_Type", Constraint_Type_name, Constraint_Type_value)
}

func init() { proto.RegisterFile("source_cache.proto", fileDescriptor0) }

var fileDescriptor0 = []byte{
	// 294 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x94, 0x91, 0x4f, 0x4f, 0xc2, 0x40,
	0x14, 0xc4, 0x5d, 0x28, 0x08, 0x0f, 0x41, 0x78, 0x1a, 0xd3, 0x78, 0x6a, 0x7a, 0x91, 0x53, 0x0f,
	0x78, 0xf1, 0xac, 0x1e, 0x39, 0x90, 0x6a, 0xbc, 0x9a, 0xed, 0xf2, 0x94, 0x0a, 0x76, 0x37, 0xaf,
	0xdb, 0x26, 0x7c, 0x14, 0x3f, 0x84, 0xdf, 0xd1, 0x74, 0x59, 0xf1, 0x4f, 0xe2, 0xc1, 0x5b, 0xa7,
	0xf3, 0xcb, 0xce, 0xcc, 0x2e, 0x60, 0xa9, 0x2b, 0x56, 0xf4, 0xa8, 0xa4, 0x5a, 0x51, 0x62, 0x58,
	0x5b, 0x8d, 0x2d, 0x93, 0xc5, 0x6f, 0x02, 0xe0, 0x46, 0x17, 0xa5, 0x65, 0x99, 0x17, 0x16, 0x2f,
	0x20, 0xb0, 0x5b, 0x43, 0xa1, 0x88, 0xc4, 0x74, 0x34, 0x3b, 0x49, 0x4c, 0x96, 0x7c, 0xb9, 0xc9,
	0xfd, 0xd6, 0x50, 0xea, 0x00, 0x3c, 0x85, 0x4e, 0x2d, 0x37, 0x15, 0x85, 0xad, 0x48, 0x4c, 0xfb,
	0xe9, 0x4e, 0xc4, 0x73, 0x08, 0x1a, 0x06, 0x8f, 0xa0, 0x97, 0x52, 0x9d, 0x97, 0xb9, 0x2e, 0xc6,
	0x07, 0x08, 0xd0, 0xbd, 0x66, 0x59, 0xa8, 0xd5, 0x58, 0xe0, 0x04, 0x86, 0xb7, 0xf4, 0x24, 0xab,
	0x8d, 0xf5, 0xbf, 0x5a, 0x38, 0x80, 0xc3, 0x07, 0x62, 0xc7, 0xb6, 0x1b, 0xf6, 0x8e, 0x5e, 0x6b,
	0xe2, 0x71, 0x10, 0x6b, 0x98, 0x2c, 0x58, 0xbf, 0x90, 0xb2, 0x0b, 0xd6, 0x86, 0xd8, 0xe6, 0x54,
	0x22, 0x42, 0xc0, 0x5a, 0x5b, 0xd7, 0xb0, 0x9f, 0xba, 0x6f, 0x3c, 0x83, 0xee, 0x6e, 0x9e, 0x6f,
	0xe3, 0x15, 0x26, 0x00, 0x6a, 0xdf, 0x3e, 0x6c, 0x47, 0x62, 0x3a, 0x98, 0x8d, 0x7e, 0x6e, 0x4a,
	0xbf, 0x11, 0xf1, 0xbb, 0x80, 0xe1, 0x5c, 0xab, 0x35, 0x2d, 0x7d, 0xee, 0xbf, 0xd2, 0xae, 0xe0,
	0xb8, 0x2a, 0x8c, 0xcc, 0x99, 0x96, 0x7e, 0xcf, 0x1f, 0x91, 0xbf, 0x31, 0x3c, 0x87, 0x1e, 0xfb,
	0xeb, 0x0a, 0x03, 0x77, 0xe6, 0x5e, 0x37, 0x9e, 0x91, 0x6a, 0x2d, 0x9f, 0xa9, 0x0c, 0x3b, 0x51,
	0xbb, 0xf1, 0x3e, 0x75, 0xd6, 0x75, 0xef, 0x78, 0xf9, 0x11, 0x00, 0x00, 0xff, 0xff, 0xbd, 0x52,
	0x77, 0xb3, 0xdd, 0x01, 0x00, 0x00,
}
