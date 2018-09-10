#include <linux/bpf.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <unistd.h>

#define LOG_BUF_SIZE 65536

char bpf_log_buf[LOG_BUF_SIZE];

static inline __u64 ptr_to_u64(const void *ptr) {
  return (__u64)(unsigned long)ptr;
}

int bpf_prog_load(enum bpf_prog_type type, const struct bpf_insn *insns,
                  int insn_cnt, const char *license) {
  union bpf_attr attr = {
      .prog_type = type,
      .insns = ptr_to_u64(insns),
      .insn_cnt = insn_cnt,
      .license = ptr_to_u64(license),
      .log_buf = ptr_to_u64(bpf_log_buf),
      .log_size = LOG_BUF_SIZE,
      .log_level = 1,
  };

  // I would like to know why there is no function named bpf()
  // exported from linux/bpf.h since bpf(2) suggests I should be able
  // to call this directly:
  // return bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
  return syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));
}

int main(int argc, char **argv) {
  printf("argc: %d\n", argc);
  if (argc < 2) {
    fprintf(stderr, "Error: no file specified.\n");
    return 1;
  }

  // TODO: How to read the BPF bytecode out of the file specified by argv[2]?
  // Looking through the code at
  // https://github.com/iovisor/gobpf/blob/master/elf/elf.go, this appears to
  // be a bit of work, so for now, use the demo in go/main.go instead.
  struct bpf_insn prog[] = {};

  int fd = bpf_prog_load(BPF_PROG_TYPE_KPROBE, prog, sizeof(prog), "GPL");
  fprintf(stderr, "Result of bpf_prog_load(): %d\n", fd);

  // TODO: sleep for awhile before closing so the user can tail
  // /sys/kernel/debug/tracing/trace_pipe for events?
  close(fd);

  return 0;
}
