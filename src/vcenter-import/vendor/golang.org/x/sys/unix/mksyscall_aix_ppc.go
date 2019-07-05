// Copyright 2019 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// +build ignore

/*
This program reads a file containing function prototypes
(like syscall_aix.go) and generates system call bodies.
The prototypes are marked by lines beginning with "//sys"
and read like func declarations if //sys is replaced by func, but:
	* The parameter lists must give a name for each argument.
	  This includes return parameters.
	* The parameter lists must give a type for each argument:
	  the (x, y, z int) shorthand is not allowed.
	* If the return parameter is an error number, it must be named err.
	* If go func name needs to be different than its libc name,
	* or the function is not in libc, name could be specified
	* at the end, after "=" sign, like
	  //sys getsockopt(s int, level int, name int, val uintptr, vallen *_Socklen) (err error) = libsocket.getsockopt
*/
package main

import (
	"bufio"
	"flag"
	"fmt"
	"os"
	"regexp"
	"strings"
)

var (
	b32  = flag.Bool("b32", false, "32bit big-endian")
	l32  = flag.Bool("l32", false, "32bit little-endian")
	aix  = flag.Bool("aix", false, "aix")
	tags = flag.String("tags", "", "build tags")
)

// cmdLine returns this programs's commandline arguments
func cmdLine() string {
	return "go run mksyscall_aix_ppc.go " + strings.Join(os.Args[1:], " ")
}

// buildTags returns build tags
func buildTags() string {
	return *tags
}

// Param is function parameter
type Param struct {
	Name string
	Type string
}

// usage prints the program usage
func usage() {
	fmt.Fprintf(os.Stderr, "usage: go run mksyscall_aix_ppc.go [-b32 | -l32] [-tags x,y] [file ...]\n")
	os.Exit(1)
}

// parseParamList parses parameter list and returns a slice of parameters
func parseParamList(list string) []string {
	list = strings.TrimSpace(list)
	if list == "" {
		return []string{}
	}
	return regexp.MustCompile(`\s*,\s*`).Split(list, -1)
}

// parseParam splits a parameter into name and type
func parseParam(p string) Param {
	ps := regexp.MustCompile(`^(\S*) (\S*)$`).FindStringSubmatch(p)
	if ps == nil {
		fmt.Fprintf(os.Stderr, "malformed parameter: %s\n", p)
		os.Exit(1)
	}
	return Param{ps[1], ps[2]}
}

func main() {
	flag.Usage = usage
	flag.Parse()
	if len(flag.Args()) <= 0 {
		fmt.Fprintf(os.Stderr, "no files to parse provided\n")
		usage()
	}

	endianness := ""
	if *b32 {
		endianness = "big-endian"
	} else if *l32 {
		endianness = "little-endian"
	}

	pack := ""
	text := ""
	cExtern := "/*\n#include <stdint.h>\n#include <stddef.h>\n"
	for _, path := range flag.Args() {
		file, err := os.Open(path)
		if err != nil {
			fmt.Fprintf(os.Stderr, err.Error())
			os.Exit(1)
		}
		s := bufio.NewScanner(file)
		for s.Scan() {
			t := s.Text()
			t = strings.TrimSpace(t)
			t = regexp.MustCompile(`\s+`).ReplaceAllString(t, ` `)
			if p := regexp.MustCompile(`^package (\S+)$`).FindStringSubmatch(t); p != nil && pack == "" {
				pack = p[1]
			}
			nonblock := regexp.MustCompile(`^\/\/sysnb `).FindStringSubmatch(t)
			if regexp.MustCompile(`^\/\/sys `).FindStringSubmatch(t) == nil && nonblock == nil {
				continue
			}

			// Line must be of the form
			//	func Open(path string, mode int, perm int) (fd int, err error)
			// Split into name, in params, out params.
			f := regexp.MustCompile(`^\/\/sys(nb)? (\w+)\(([^()]*)\)\s*(?:\(([^()]+)\))?\s*(?:=\s*(?:(\w*)\.)?(\w*))?$`).FindStringSubmatch(t)
			if f == nil {
				fmt.Fprintf(os.Stderr, "%s:%s\nmalformed //sys declaration\n", path, t)
				os.Exit(1)
			}
			funct, inps, outps, modname, sysname := f[2], f[3], f[4], f[5], f[6]

			// Split argument lists on comma.
			in := parseParamList(inps)
			out := parseParamList(outps)

			inps = strings.Join(in, ", ")
			outps = strings.Join(out, ", ")

			// Try in vain to keep people from editing this file.
			// The theory is that they jump into the middle of the file
			// without reading the header.
			text += "// THIS FILE IS GENERATED BY THE COMMAND AT THE TOP; DO NOT EDIT\n\n"

			// Check if value return, err return available
			errvar := ""
			retvar := ""
			rettype := ""
			for _, param := range out {
				p := parseParam(param)
				if p.Type == "error" {
					errvar = p.Name
				} else {
					retvar = p.Name
					rettype = p.Type
				}
			}

			// System call name.
			if sysname == "" {
				sysname = funct
			}
			sysname = regexp.MustCompile(`([a-z])([A-Z])`).ReplaceAllString(sysname, `${1}_$2`)
			sysname = strings.ToLower(sysname) // All libc functions are lowercase.

			cRettype := ""
			if rettype == "unsafe.Pointer" {
				cRettype = "uintptr_t"
			} else if rettype == "uintptr" {
				cRettype = "uintptr_t"
			} else if regexp.MustCompile(`^_`).FindStringSubmatch(rettype) != nil {
				cRettype = "uintptr_t"
			} else if rettype == "int" {
				cRettype = "int"
			} else if rettype == "int32" {
				cRettype = "int"
			} else if rettype == "int64" {
				cRettype = "long long"
			} else if rettype == "uint32" {
				cRettype = "unsigned int"
			} else if rettype == "uint64" {
				cRettype = "unsigned long long"
			} else {
				cRettype = "int"
			}
			if sysname == "exit" {
				cRettype = "void"
			}

			// Change p.Types to c
			var cIn []string
			for _, param := range in {
				p := parseParam(param)
				if regexp.MustCompile(`^\*`).FindStringSubmatch(p.Type) != nil {
					cIn = append(cIn, "uintptr_t")
				} else if p.Type == "string" {
					cIn = append(cIn, "uintptr_t")
				} else if regexp.MustCompile(`^\[\](.*)`).FindStringSubmatch(p.Type) != nil {
					cIn = append(cIn, "uintptr_t", "size_t")
				} else if p.Type == "unsafe.Pointer" {
					cIn = append(cIn, "uintptr_t")
				} else if p.Type == "uintptr" {
					cIn = append(cIn, "uintptr_t")
				} else if regexp.MustCompile(`^_`).FindStringSubmatch(p.Type) != nil {
					cIn = append(cIn, "uintptr_t")
				} else if p.Type == "int" {
					cIn = append(cIn, "int")
				} else if p.Type == "int32" {
					cIn = append(cIn, "int")
				} else if p.Type == "int64" {
					cIn = append(cIn, "long long")
				} else if p.Type == "uint32" {
					cIn = append(cIn, "unsigned int")
				} else if p.Type == "uint64" {
					cIn = append(cIn, "unsigned long long")
				} else {
					cIn = append(cIn, "int")
				}
			}

			if funct != "fcntl" && funct != "FcntlInt" && funct != "readlen" && funct != "writelen" {
				if sysname == "select" {
					// select is a keyword of Go. Its name is
					// changed to c_select.
					cExtern += "#define c_select select\n"
				}
				// Imports of system calls from libc
				cExtern += fmt.Sprintf("%s %s", cRettype, sysname)
				cIn := strings.Join(cIn, ", ")
				cExtern += fmt.Sprintf("(%s);\n", cIn)
			}

			// So file name.
			if *aix {
				if modname == "" {
					modname = "libc.a/shr_64.o"
				} else {
					fmt.Fprintf(os.Stderr, "%s: only syscall using libc are available\n", funct)
					os.Exit(1)
				}
			}

			strconvfunc := "C.CString"

			// Go function header.
			if outps != "" {
				outps = fmt.Sprintf(" (%s)", outps)
			}
			if text != "" {
				text += "\n"
			}

			text += fmt.Sprintf("func %s(%s)%s {\n", funct, strings.Join(in, ", "), outps)

			// Prepare arguments to Syscall.
			var args []string
			n := 0
			argN := 0
			for _, param := range in {
				p := parseParam(param)
				if regexp.MustCompile(`^\*`).FindStringSubmatch(p.Type) != nil {
					args = append(args, "C.uintptr_t(uintptr(unsafe.Pointer("+p.Name+")))")
				} else if p.Type == "string" && errvar != "" {
					text += fmt.Sprintf("\t_p%d := uintptr(unsafe.Pointer(%s(%s)))\n", n, strconvfunc, p.Name)
					args = append(args, fmt.Sprintf("C.uintptr_t(_p%d)", n))
					n++
				} else if p.Type == "string" {
					fmt.Fprintf(os.Stderr, path+":"+funct+" uses string arguments, but has no error return\n")
					text += fmt.Sprintf("\t_p%d := uintptr(unsafe.Pointer(%s(%s)))\n", n, strconvfunc, p.Name)
					args = append(args, fmt.Sprintf("C.uintptr_t(_p%d)", n))
					n++
				} else if m := regexp.MustCompile(`^\[\](.*)`).FindStringSubmatch(p.Type); m != nil {
					// Convert slice into pointer, length.
					// Have to be careful not to take address of &a[0] if len == 0:
					// pass nil in that case.
					text += fmt.Sprintf("\tvar _p%d *%s\n", n, m[1])
					text += fmt.Sprintf("\tif len(%s) > 0 {\n\t\t_p%d = &%s[0]\n\t}\n", p.Name, n, p.Name)
					args = append(args, fmt.Sprintf("C.uintptr_t(uintptr(unsafe.Pointer(_p%d)))", n))
					n++
					text += fmt.Sprintf("\tvar _p%d int\n", n)
					text += fmt.Sprintf("\t_p%d = len(%s)\n", n, p.Name)
					args = append(args, fmt.Sprintf("C.size_t(_p%d)", n))
					n++
				} else if p.Type == "int64" && endianness != "" {
					if endianness == "big-endian" {
						args = append(args, fmt.Sprintf("uintptr(%s>>32)", p.Name), fmt.Sprintf("uintptr(%s)", p.Name))
					} else {
						args = append(args, fmt.Sprintf("uintptr(%s)", p.Name), fmt.Sprintf("uintptr(%s>>32)", p.Name))
					}
					n++
				} else if p.Type == "bool" {
					text += fmt.Sprintf("\tvar _p%d uint32\n", n)
					text += fmt.Sprintf("\tif %s {\n\t\t_p%d = 1\n\t} else {\n\t\t_p%d = 0\n\t}\n", p.Name, n, n)
					args = append(args, fmt.Sprintf("_p%d", n))
				} else if regexp.MustCompile(`^_`).FindStringSubmatch(p.Type) != nil {
					args = append(args, fmt.Sprintf("C.uintptr_t(uintptr(%s))", p.Name))
				} else if p.Type == "unsafe.Pointer" {
					args = append(args, fmt.Sprintf("C.uintptr_t(uintptr(%s))", p.Name))
				} else if p.Type == "int" {
					if (argN == 2) && ((funct == "readlen") || (funct == "writelen")) {
						args = append(args, fmt.Sprintf("C.size_t(%s)", p.Name))
					} else if argN == 0 && funct == "fcntl" {
						args = append(args, fmt.Sprintf("C.uintptr_t(%s)", p.Name))
					} else if (argN == 2) && ((funct == "fcntl") || (funct == "FcntlInt")) {
						args = append(args, fmt.Sprintf("C.uintptr_t(%s)", p.Name))
					} else {
						args = append(args, fmt.Sprintf("C.int(%s)", p.Name))
					}
				} else if p.Type == "int32" {
					args = append(args, fmt.Sprintf("C.int(%s)", p.Name))
				} else if p.Type == "int64" {
					args = append(args, fmt.Sprintf("C.longlong(%s)", p.Name))
				} else if p.Type == "uint32" {
					args = append(args, fmt.Sprintf("C.uint(%s)", p.Name))
				} else if p.Type == "uint64" {
					args = append(args, fmt.Sprintf("C.ulonglong(%s)", p.Name))
				} else if p.Type == "uintptr" {
					args = append(args, fmt.Sprintf("C.uintptr_t(%s)", p.Name))
				} else {
					args = append(args, fmt.Sprintf("C.int(%s)", p.Name))
				}
				argN++
			}

			// Actual call.
			arglist := strings.Join(args, ", ")
			call := ""
			if sysname == "exit" {
				if errvar != "" {
					call += "er :="
				} else {
					call += ""
				}
			} else if errvar != "" {
				call += "r0,er :="
			} else if retvar != "" {
				call += "r0,_ :="
			} else {
				call += ""
			}
			if sysname == "select" {
				// select is a keyword of Go. Its name is
				// changed to c_select.
				call += fmt.Sprintf("C.c_%s(%s)", sysname, arglist)
			} else {
				call += fmt.Sprintf("C.%s(%s)", sysname, arglist)
			}

			// Assign return values.
			body := ""
			for i := 0; i < len(out); i++ {
				p := parseParam(out[i])
				reg := ""
				if p.Name == "err" {
					reg = "e1"
				} else {
					reg = "r0"
				}
				if reg != "e1" {
					body += fmt.Sprintf("\t%s = %s(%s)\n", p.Name, p.Type, reg)
				}
			}

			// verify return
			if sysname != "exit" && errvar != "" {
				if regexp.MustCompile(`^uintptr`).FindStringSubmatch(cRettype) != nil {
					body += "\tif (uintptr(r0) ==^uintptr(0) && er != nil) {\n"
					body += fmt.Sprintf("\t\t%s = er\n", errvar)
					body += "\t}\n"
				} else {
					body += "\tif (r0 ==-1 && er != nil) {\n"
					body += fmt.Sprintf("\t\t%s = er\n", errvar)
					body += "\t}\n"
				}
			} else if errvar != "" {
				body += "\tif (er != nil) {\n"
				body += fmt.Sprintf("\t\t%s = er\n", errvar)
				body += "\t}\n"
			}

			text += fmt.Sprintf("\t%s\n", call)
			text += body

			text += "\treturn\n"
			text += "}\n"
		}
		if err := s.Err(); err != nil {
			fmt.Fprintf(os.Stderr, err.Error())
			os.Exit(1)
		}
		file.Close()
	}
	imp := ""
	if pack != "unix" {
		imp = "import \"golang.org/x/sys/unix\"\n"

	}
	fmt.Printf(srcTemplate, cmdLine(), buildTags(), pack, cExtern, imp, text)
}

const srcTemplate = `// %s
// Code generated by the command above; see README.md. DO NOT EDIT.

// +build %s

package %s


%s
*/
import "C"
import (
	"unsafe"
)


%s

%s
`
