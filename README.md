# rust-ebpf-demo

This provides a working example of building and running an eBPF program in Rust.
This code was adapted from
http://unhandledexpression.com/general/rust/2018/02/02/poc-compiling-to-ebpf-from-rust.html.

I created this repo because:

* The original blog post does not link to a GitHub repo with the actual code.
* The code samples and command-line output in the blog post have been improperly HTML-escaped
  such that they are incorrect. For example, in the Rust code, where there should
  be `->`, there is `-&gt;`. What's worse is that snippets of code that exist
  between opening and closing angle brackets (`<` and `>`) have also been stripped,
  which means critical code for traits is missing.

## Building the Code

I built and ran this code on Ubuntu 18.04.1 with LLVM 7
(the setup steps I went through to get the LLVM libraries are included as part
of https://github.com/iovisor/bcc/issues/1964).

First, use the LLVM toolchain to compile the Rust code to LLVM bytecode,
and then to BPF bytecode using the shell script from the blog post:

```sh
$ ./build.sh
```

Note that the contents of `hello.ll` in the blog post are not quite correct
due to the formatting issues mentioned above. Here is what I ended up with:

```
; ModuleID = 'hello.2012078a-cgu.0'
source_filename = "hello.2012078a-cgu.0"
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@_license = local_unnamed_addr constant <{ [4 x i8] }> <{ [4 x i8] c"GPL\00" }>, section "license", align 1
@_version = local_unnamed_addr constant <{ [4 x i8] }> <{ [4 x i8] c"\FE\FF\FF\FF" }>, section "version", align 4

; Function Attrs: uwtable
define i32 @kprobe__sys_clone(i8* nocapture readnone %_ctx) unnamed_addr #0 section "kprobe/SyS_clone" {
start:
  %msg = alloca [17 x i8], align 16
  %0 = getelementptr inbounds [17 x i8], [17 x i8]* %msg, i64 0, i64 0
  call void @llvm.lifetime.start.p0i8(i64 17, i8* nonnull %0)
  %1 = bitcast [17 x i8]* %msg to <16 x i8>*
  store <16 x i8> <i8 104, i8 101, i8 108, i8 108, i8 111, i8 32, i8 102, i8 114, i8 111, i8 109, i8 32, i8 114, i8 117, i8 115, i8 116, i8 10>, <16 x i8>* %1, align 16
  %2 = getelementptr inbounds [17 x i8], [17 x i8]* %msg, i64 0, i64 16
  store i8 0, i8* %2, align 16
  %3 = call i32 inttoptr (i64 6 to i32 (i8*, i32)*)(i8* nonnull %0, i32 17)
  call void @llvm.lifetime.end.p0i8(i64 17, i8* nonnull %0)
  ret i32 0
}

; Function Attrs: argmemonly nounwind
declare void @llvm.lifetime.start.p0i8(i64, i8* nocapture) #1

; Function Attrs: argmemonly nounwind
declare void @llvm.lifetime.end.p0i8(i64, i8* nocapture) #1

attributes #0 = { uwtable "probe-stack"="__rust_probestack" }
attributes #1 = { argmemonly nounwind }
```

Also, because I used LLVM 7, my disassembly of `hello.o` is also slightly different:

```
$ llvm-objdump-7 -S target/bpf/hello.o  # Note the use of llvm-objdump-7 instead of llvm-objdump-4.0

target/bpf/hello.o:	file format ELF64-BPF

Disassembly of section kprobe/SyS_clone:
kprobe__sys_clone:
       0:	18 01 00 00 6f 6d 20 72 00 00 00 00 75 73 74 0a 	r1 = 753353985940811119 ll
       2:	7b 1a e8 ff 00 00 00 00 	*(u64 *)(r10 - 24) = r1
       3:	18 01 00 00 68 65 6c 6c 00 00 00 00 6f 20 66 72 	r1 = 8243311830880773480 ll
       5:	7b 1a e0 ff 00 00 00 00 	*(u64 *)(r10 - 32) = r1
       6:	b7 01 00 00 00 00 00 00 	r1 = 0
       7:	73 1a f0 ff 00 00 00 00 	*(u8 *)(r10 - 16) = r1
       8:	bf a1 00 00 00 00 00 00 	r1 = r10
       9:	07 01 00 00 e0 ff ff ff 	r1 += -32
      10:	b7 02 00 00 11 00 00 00 	r2 = 17
      11:	85 00 00 00 06 00 00 00 	call 6
      12:	b7 00 00 00 00 00 00 00 	r0 = 0
      13:	95 00 00 00 00 00 00 00 	exit
```

## Running the Code

I was hoping to extract the BPF bytecode from `hello.o` and use it to create
a standalone C program that loads the BPF program. My attempt lives in
[`load-bpf.c`](./c/load-bpf.c). Unfortunately, reading the bytecode out of
`hello.o` to turn it into a `bpf_insn[]` was non-trivial, so I abandoned that
approach and took the suggestion of the blog post to leverage [some Go code
referenced in a separate blog post](https://kinvolk.io/blog/2017/09/an-update-on-gobpf---elf-loading-uprobes-more-program-types/).
Indeed, after reading through [gobpf/elf/elf.go](https://github.com/iovisor/gobpf/blob/master/elf/elf.go), it appears a proper
ELF library is necessary to read the bytes out of `hello.o` and turn them into
BPF bytecode (though the Go code could potentially be used to dump the BPF bytecode
to a binary file that is read by my `load-bpf.c` file).

For convenience, the Go code also lives in this repo and should be easy to run
from the `go/` folder:

```sh
$ go build
$ sudo go run main.go  # This runs until you ctrl-C.
```

As noted in the [README](./go/README.md), until you ctrl-C `main.go`,
the kprobe will be applied. Verify that the `trace_printk()` call from your
BPF program is working correctly by running:

```sh
sudo cat /sys/kernel/debug/tracing/trace_pipe
```

Enjoy!

## PostScript ##

One surprising theing that I learned while writing `load-bpf.c` is that
despite the presence of `#include <linux/bpf.h>` and the implications of
`man 2 bpf`, you cannot invoke the `bpf()` syscall like this:

```c
bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
```

Instead, you have to use `syscall(2)`:

```c
#include <sys/syscall.h>
#include <unistd.h>

// ...

syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));
```

Hopefully this saves someone some time.
