#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/bpf.h>
#include <linux/perf_event.h>
#include <linux/version.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

// This seems like it should be in <linux/sched.h>,
// but I don't have it there on Ubuntu 18.04.
#ifndef TASK_COMM_LEN
// Task command name length:
#define TASK_COMM_LEN 16
#endif

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

/**
 * Port of bpf_attach_tracing_event() from libbpf.c.
 */
int attachTracingEvent(int progFd, const char *event_path, int *pfd) {
  int efd;
  ssize_t bytes;
  char buf[PATH_MAX];
  struct perf_event_attr attr = {};
  // Caller did not provided a valid Perf Event FD. Create one with the debugfs
  // event path provided.
  snprintf(buf, sizeof(buf), "%s/id", event_path);
  efd = open(buf, O_RDONLY, 0);
  if (efd < 0) {
    fprintf(stderr, "open(%s): %s\n", buf, strerror(errno));
    return -1;
  }

  bytes = read(efd, buf, sizeof(buf));
  if (bytes <= 0 || bytes >= sizeof(buf)) {
    fprintf(stderr, "read(%s): %s\n", buf, strerror(errno));
    close(efd);
    return -1;
  }
  close(efd);
  buf[bytes] = '\0';
  attr.config = strtol(buf, NULL, 0);
  attr.type = PERF_TYPE_TRACEPOINT;
  attr.sample_period = 1;
  attr.wakeup_events = 1;
  *pfd = syscall(__NR_perf_event_open, &attr, -1 /* pid */, 0 /* cpu */,
                 -1 /* group_fd */, PERF_FLAG_FD_CLOEXEC);
  if (*pfd < 0) {
    fprintf(stderr, "perf_event_open(%s/id): %s\n", event_path,
            strerror(errno));
    return -1;
  }

  if (ioctl(*pfd, PERF_EVENT_IOC_SET_BPF, progFd) < 0) {
    perror("ioctl(PERF_EVENT_IOC_SET_BPF)");
    return -1;
  }
  if (ioctl(*pfd, PERF_EVENT_IOC_ENABLE, 0) < 0) {
    perror("ioctl(PERF_EVENT_IOC_ENABLE)");
    return -1;
  }

  return 0;
}

/**
 * Simplified version of bpf_attach_kprobe() from libbpf.c.
 */
int attachKprobe(int progFd) {
  static char *event_type = "kprobe";

  // Note that bpf_try_perf_event_open_with_probe() fails on my system
  // because I don't have either of
  // /sys/bus/event_source/devices/kprobe/type or
  // /sys/bus/event_source/devices/kprobe/format/retprobe, so this is
  // a port of the fallback code path within bpf_attach_kprobe().
  int kfd =
      open("/sys/kernel/debug/tracing/kprobe_events", O_WRONLY | O_APPEND, 0);
  if (kfd < 0) {
    perror("Error opening /sys/kernel/debug/tracing/kprobe_events");
    return -1;
  }

  char buf[256];
  char event_alias[128];
  const char *ev_name = "p_do_sys_open";
  // I don't think fn_name matters: I think it's just used to help namespace
  // the probe ID?
  const char *fn_name = "do_sys_open";

  // I believe that parameterizing the event alias by PID was done because of:
  // https://github.com/iovisor/bcc/issues/872.
  snprintf(event_alias, sizeof(event_alias), "%s_bcc_%d", ev_name, getpid());

  // These are defined in libbpf.h, not bpf.h.
  int BPF_PROBE_ENTRY = 0;
  int BPF_PROBE_RETURN = 1;

  // I'm assuming the function offset is 0. I'm not sure where to get the
  // function offset because I do not build my program the way libbpf does.
  int attach_type = BPF_PROBE_ENTRY;
  snprintf(buf, sizeof(buf), "%c:%ss/%s %s",
           attach_type == BPF_PROBE_ENTRY ? 'p' : 'r', event_type, event_alias,
           fn_name);

  // We appear to be writing some wacky like:
  // "p:kprobes/p_do_sys_open_bcc_<pid> do_sys_open" to the special kernel file.
  if (write(kfd, buf, strlen(buf)) < 0) {
    if (errno == ENOENT) {
      // write(2) doesn't mention ENOENT, so perhaps this is something special
      // with respect to this kernel file descriptor?
      fprintf(stderr, "cannot attach kprobe, probe entry may not exist\n");
    } else {
      fprintf(stderr, "cannot attach kprobe, %s\n", strerror(errno));
    }
    close(kfd);
    return -1;
  }
  close(kfd);

  // Set buf to:
  // "/sys/kernel/debug/tracing/events/kprobes/p_do_sys_open_bcc_<pid>".
  snprintf(buf, sizeof(buf), "/sys/kernel/debug/tracing/events/%ss/%s",
           event_type, event_alias);

  int pfd = -1;
  // This should read the event ID from the path in buf, create the
  // Perf Event event using that ID, and updated value of pfd.
  if (attachTracingEvent(progFd, buf, &pfd) < 0) {
    return -1;
  }

  return pfd;
}

struct val_t {
  __u64 id;
  __u64 ts;
  char comm[TASK_COMM_LEN];
  const char *fname;
};

int main(int argc, char **argv) {
  int exitCode = 1;
  bpf_log_buf[0] = '\0';
  int hashMapFd = -1, eventsMapFd = -1, progFd = -1, kprobeFd = -1;
  union bpf_attr attr;
  memset(&attr, 0, sizeof(attr));

  // BPF_HASH
  attr.map_type = BPF_MAP_TYPE_HASH;
  attr.key_size = sizeof(__u64);
  attr.value_size = sizeof(struct val_t);
  attr.max_entries = 10240; // Default value for BPF_HASH().
  hashMapFd = syscall(__NR_bpf, BPF_MAP_CREATE, &attr, sizeof(attr));
  if (hashMapFd < 0) {
    perror("Failed to create HASH_MAP");
    goto error;
  }

  // Note that we could update the BPF_LD_MAP_FD() instruction
  // with the observed value if it is not 3, but then we have to
  // modify the code generated from the Python script.
  if (hashMapFd != 3) {
    fprintf(stderr,
            "Invariant violation: fd for HASH_MAP must be 3, but was %d.\n",
            hashMapFd);
    goto error;
  }

  // BPF_PERF_OUTPUT
  attr.map_type = BPF_MAP_TYPE_PERF_EVENT_ARRAY;
  attr.key_size = sizeof(int);
  attr.value_size = sizeof(__u32);
  attr.max_entries = 1; // libbpf prefers num_cpus for this value.
  eventsMapFd = syscall(__NR_bpf, BPF_MAP_CREATE, &attr, sizeof(attr));
  if (eventsMapFd < 0) {
    perror("Failed to create BPF_PERF_OUTPUT");
    goto error;
  }

  // Note that we could update the BPF_LD_MAP_FD() instruction
  // with the observed value if it is not 4, but then we have to
  // modify the code generated from the Python script.
  if (eventsMapFd != 4) {
    fprintf(
        stderr,
        "Invariant violation: fd for BPF_PERF_OUTPUT must be 4, but was %d.\n",
        eventsMapFd);
    goto error;
  }

  // This code was generated from c/bpf_opensnoop.c
  // for the trace_entry() function.
  struct bpf_insn prog[] = {
      ((struct bpf_insn){
          .code = 0x79,
          .dst_reg = BPF_REG_7,
          .src_reg = BPF_REG_1,
          .off = 104,
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
          .code = 0x7b,
          .dst_reg = BPF_REG_10,
          .src_reg = BPF_REG_1,
          .off = -8,
          .imm = 0,
      }),
      ((struct bpf_insn){
          .code = 0x7b,
          .dst_reg = BPF_REG_10,
          .src_reg = BPF_REG_1,
          .off = -16,
          .imm = 0,
      }),
      ((struct bpf_insn){
          .code = 0x7b,
          .dst_reg = BPF_REG_10,
          .src_reg = BPF_REG_1,
          .off = -24,
          .imm = 0,
      }),
      ((struct bpf_insn){
          .code = 0x7b,
          .dst_reg = BPF_REG_10,
          .src_reg = BPF_REG_1,
          .off = -32,
          .imm = 0,
      }),
      ((struct bpf_insn){
          .code = 0x7b,
          .dst_reg = BPF_REG_10,
          .src_reg = BPF_REG_1,
          .off = -40,
          .imm = 0,
      }),
      ((struct bpf_insn){
          .code = 0x85,
          .dst_reg = BPF_REG_0,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = 14,
      }),
      ((struct bpf_insn){
          .code = 0xbf,
          .dst_reg = BPF_REG_6,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = 0,
      }),
      ((struct bpf_insn){
          .code = 0x7b,
          .dst_reg = BPF_REG_10,
          .src_reg = BPF_REG_6,
          .off = -48,
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
          .code = 0x7,
          .dst_reg = BPF_REG_1,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = -24,
      }),
      ((struct bpf_insn){
          .code = 0xb7,
          .dst_reg = BPF_REG_2,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = 16,
      }),
      ((struct bpf_insn){
          .code = 0x85,
          .dst_reg = BPF_REG_0,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = 16,
      }),
      ((struct bpf_insn){
          .code = 0x67,
          .dst_reg = BPF_REG_0,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = 32,
      }),
      ((struct bpf_insn){
          .code = 0x77,
          .dst_reg = BPF_REG_0,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = 32,
      }),
      ((struct bpf_insn){
          .code = 0x55,
          .dst_reg = BPF_REG_0,
          .src_reg = BPF_REG_0,
          .off = 12,
          .imm = 0,
      }),
      ((struct bpf_insn){
          .code = 0x7b,
          .dst_reg = BPF_REG_10,
          .src_reg = BPF_REG_6,
          .off = -40,
          .imm = 0,
      }),
      ((struct bpf_insn){
          .code = 0x85,
          .dst_reg = BPF_REG_0,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = 5,
      }),
      ((struct bpf_insn){
          .code = 0x7b,
          .dst_reg = BPF_REG_10,
          .src_reg = BPF_REG_0,
          .off = -32,
          .imm = 0,
      }),
      ((struct bpf_insn){
          .code = 0x7b,
          .dst_reg = BPF_REG_10,
          .src_reg = BPF_REG_7,
          .off = -8,
          .imm = 0,
      }),
      ((struct bpf_insn){
          .code = 0x18,
          .dst_reg = BPF_REG_1,
          .src_reg = BPF_REG_1,
          .off = 0,
          .imm = 3,
      }),
      ((struct bpf_insn){
          .code = 0x0,
          .dst_reg = BPF_REG_0,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = 0,
      }),
      ((struct bpf_insn){
          .code = 0xbf,
          .dst_reg = BPF_REG_2,
          .src_reg = BPF_REG_10,
          .off = 0,
          .imm = 0,
      }),
      ((struct bpf_insn){
          .code = 0x7,
          .dst_reg = BPF_REG_2,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = -48,
      }),
      ((struct bpf_insn){
          .code = 0xbf,
          .dst_reg = BPF_REG_3,
          .src_reg = BPF_REG_10,
          .off = 0,
          .imm = 0,
      }),
      ((struct bpf_insn){
          .code = 0x7,
          .dst_reg = BPF_REG_3,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = -40,
      }),
      ((struct bpf_insn){
          .code = 0xb7,
          .dst_reg = BPF_REG_4,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = 0,
      }),
      ((struct bpf_insn){
          .code = 0x85,
          .dst_reg = BPF_REG_0,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = 2,
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
  progFd = bpf_prog_load(BPF_PROG_TYPE_KPROBE, prog, insn_cnt, "GPL");
  if (progFd == -1) {
    perror("Error calling bpf_prog_load()");
    goto error;
  }

  kprobeFd = attachKprobe(progFd);
  if (kprobeFd < 0) {
    perror("Error calling attachKprobe()");
    goto error;
  }

  exitCode = 0;
  goto cleanup;

error:
  // If there is anything in the bpf_log_buf, print it
  // as it may be helpful in debugging.
  if (bpf_log_buf[0] != '\0') {
    fprintf(stderr, "%s", bpf_log_buf);
  }

cleanup:
  if (kprobeFd != -1) {
    close(kprobeFd);
  }
  if (progFd != -1) {
    close(progFd);
  }
  if (eventsMapFd != -1) {
    close(eventsMapFd);
  }
  if (hashMapFd != -1) {
    close(hashMapFd);
  }
  return exitCode;
}
