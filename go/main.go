// main.go

package main

import (
	"fmt"
	"os"
	"os/signal"

	"github.com/iovisor/gobpf/elf"
)

func main() {
	module := elf.NewModule("../target/bpf/hello.o")
	if err := module.Load(nil); err != nil {
		fmt.Fprintf(os.Stderr, "Failed to load program: %v\n", err)
		os.Exit(1)
	}
	defer func() {
		if err := module.Close(); err != nil {
			fmt.Fprintf(os.Stderr, "Failed to close program: %v", err)
		}
	}()

	if err := module.EnableKprobe("kprobe/SyS_clone", 0); err != nil {
		fmt.Fprintf(os.Stderr, "Failed to enable kprobe: %v\n", err)
		os.Exit(1)
	}

	sig := make(chan os.Signal, 1)
	signal.Notify(sig, os.Interrupt, os.Kill)

	<-sig
}
