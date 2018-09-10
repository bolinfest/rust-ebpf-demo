The code for `main.go` was adapted from
https://kinvolk.io/blog/2017/09/an-update-on-gobpf---elf-loading-uprobes-more-program-types/.

The differences are:

* Replacing `"./program.o"` with `"../target/bpf/hello.o"`.
* Replacing `"kprobe/SyS_open"` with `"kprobe/SyS_clone"`.

As this code loads `../target/bpf/hello.o`, make sure you have run
`./build.sh` from the root of this project first to generate it.

Once you have `hello.o`, do the following from this directory:

```
$ go build
$ sudo go run main.go  # This runs until you ctrl-C.
```

This will install the kprobe and leave it in place until you
ctrl-C `sudo go run main.go`. While it is running, run:

```
sudo cat /sys/kernel/debug/tracing/trace_pipe
```

Assuming there is code on your system that is running clone(2)
(if you have Google Chrome open, then it's probably spawning things),
you should expect to see `hello from rust` printed for each call.
