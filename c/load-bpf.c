#include <linux/bpf.h>
#include <linux/version.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#define LOG_BUF_SIZE 65536

char bpf_log_buf[LOG_BUF_SIZE];

static inline __u64 ptr_to_u64(const void *ptr) {
  return (__u64)(unsigned long)ptr;
}

/**
 * Taken from the man page for bpf(2), though two critical lines
 * of code that are missing from that man page are:
 * (1) The bpf_attr must be zeroed-out before it is used.
 *     Failing to do so will likely result in an EINVAL when
 *     doing the BPF_PROG_LOAD.
 *
 *     memset(&attr, 0, sizeof(attr))
 *
 * (2) kern_version must be defined if the program type is
 *     BPF_PROG_TYPE_KPROBE. Note that LINUX_VERSION_CODE is defined
 *     in <linux/version.h>.
 *
 *     attr.kern_version = LINUX_VERSION_CODE;
 */
int bpf_prog_load(enum bpf_prog_type type, const struct bpf_insn *insns,
                  int insn_cnt, const char *license) {
  union bpf_attr attr;
  memset(&attr, 0, sizeof(attr));

  attr.prog_type = type;
  attr.insns = ptr_to_u64(insns);
  attr.insn_cnt = insn_cnt;
  attr.license = ptr_to_u64(license);

  attr.log_buf = ptr_to_u64(bpf_log_buf);
  attr.log_size = LOG_BUF_SIZE;
  attr.log_level = 1;

  // As noted in bpf(2), kern_version is checked when prog_type=kprobe.
  attr.kern_version = LINUX_VERSION_CODE;

  // If this returns a non-zero number, printing the contents of
  // bpf_log_buf may help. libbpf.c has a bpf_print_hints() function that
  // can help with this.
  return syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));
}

int waitForSigInt() {
  sigset_t set;
  sigemptyset(&set);
  int rc = sigaddset(&set, SIGINT);
  if (rc < 0) {
    perror("Error calling sigaddset()");
    return 1;
  }

  rc = sigprocmask(SIG_BLOCK, &set, NULL);
  if (rc < 0) {
    perror("Error calling sigprocmask()");
    return 1;
  }

  int sig;
  rc = sigwait(&set, &sig);
  if (rc < 0) {
    perror("Error calling sigwait()");
    return 1;
  } else if (sig == SIGINT) {
    fprintf(stderr, "SIGINT received!\n");
    return 0;
  } else {
    fprintf(stderr, "Unexpected signal received: %d\n", sig);
    return 0;
  }
}

int main(int argc, char **argv) {
  // This array was generated from bpf_trace_printk.py.
  struct bpf_insn prog[] = {
      ((struct bpf_insn){
          .code = 0x18,
          .dst_reg = BPF_REG_1,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = 1914727791,
      }),
      ((struct bpf_insn){
          .code = 0x00,
          .dst_reg = BPF_REG_0,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = 175403893,
      }),
      ((struct bpf_insn){
          .code = 0x7b,
          .dst_reg = BPF_REG_10,
          .src_reg = BPF_REG_1,
          .off = -24,
          .imm = 0,
      }),
      ((struct bpf_insn){
          .code = 0x18,
          .dst_reg = BPF_REG_1,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = 1819043176,
      }),
      ((struct bpf_insn){
          .code = 0x00,
          .dst_reg = BPF_REG_0,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = 1919295599,
      }),
      ((struct bpf_insn){
          .code = 0x7b,
          .dst_reg = BPF_REG_10,
          .src_reg = BPF_REG_1,
          .off = -32,
          .imm = 0,
      }),
      ((struct bpf_insn){
          .code = 0xb7,
          .dst_reg = BPF_REG_1,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = 0,
      }),
      ((struct bpf_insn){
          .code = 0x73,
          .dst_reg = BPF_REG_10,
          .src_reg = BPF_REG_1,
          .off = -16,
          .imm = 0,
      }),
      ((struct bpf_insn){
          .code = 0xbf,
          .dst_reg = BPF_REG_1,
          .src_reg = BPF_REG_10,
          .off = 0,
          .imm = 0,
      }),
      ((struct bpf_insn){
          .code = 0x07,
          .dst_reg = BPF_REG_1,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = -32,
      }),
      ((struct bpf_insn){
          .code = 0xb7,
          .dst_reg = BPF_REG_2,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = 17,
      }),
      ((struct bpf_insn){
          .code = 0xb7,
          .dst_reg = BPF_REG_3,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = 17,
      }),
      ((struct bpf_insn){
          .code = 0x85,
          .dst_reg = BPF_REG_0,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = 6,
      }),
      ((struct bpf_insn){
          .code = 0xb7,
          .dst_reg = BPF_REG_0,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = 0,
      }),
      ((struct bpf_insn){
          .code = 0x95,
          .dst_reg = BPF_REG_0,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = 0,
      }),
  };

  int insn_cnt = sizeof(prog) / sizeof(struct bpf_insn);
  int fd = bpf_prog_load(BPF_PROG_TYPE_KPROBE, prog, insn_cnt, "GPL");
  if (fd == -1) {
    perror("Error calling bpf_prog_load()");
    return 1;
  }

  fprintf(stderr, "Result of bpf_prog_load(): %d\n", fd);
  // TODO(mbolin): The program is loaded, but it seems that we need to
  // attach a kprobe? bpf_attach_kprobe() is defined in libbpf for this
  // purpose.

  // Sleep for awhile before closing so the user can tail
  fprintf(stderr, "Run "
                  "`sudo cat /sys/kernel/debug/tracing/trace_pipe`"
                  " in another terminal to verify bpf_trace_printk()"
                  " is working as expected.\n");

  int exitCode = waitForSigInt();
  close(fd);
  return exitCode;
}
