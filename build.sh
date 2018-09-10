#!/bin/sh
cargo rustc --release -- --emit=llvm-ir
mkdir -p target/bpf
cp target/release/deps/hello-*.ll target/bpf/hello.ll
cargo rustc --release -- --emit=llvm-bc
cp target/release/deps/hello-*.bc target/bpf/hello.bc
llc-7 target/bpf/hello.bc -march=bpf -filetype=obj -o target/bpf/hello.o
