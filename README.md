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

## Building and Running the Code

I built and ran this code on Ubuntu 18.04.1 with LLVM 7
(the setup steps I went through to get the LLVM libraries are included as part
of https://github.com/iovisor/bcc/issues/1964).

First, use the LLVM toolchain to compile the Rust code to LLVM bytecode,
and then to BPF bytecode using the shell script from the blog post:

```sh
$ ./build.sh
```

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
