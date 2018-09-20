#include "generated_trace_entry.h"
#include "generated_trace_return.h"
#include "libbpf_wrapper.h"
#include <bcc/perf_reader.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/version.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// This seems like it should be in <linux/sched.h>,
// but I don't have it there on Ubuntu 18.04.
#ifndef TASK_COMM_LEN
// Task command name length:
#define TASK_COMM_LEN 16
#endif

// These constants and structs must match those
// used in the source in opensnoop.py.

#define LOG_BUF_SIZE 65536
#define NAME_MAX 255

char bpf_log_buf[LOG_BUF_SIZE];

struct val_t {
  __u64 id;
  __u64 ts;
  char comm[TASK_COMM_LEN];
  const char *fname;
};

struct data_t {
  __u64 id;
  __u64 ts;
  int ret;
  char comm[TASK_COMM_LEN];
  char fname[NAME_MAX];
};

/**
 * If a positive integer is parsed successfully, returns the value.
 * If not, returns -1 and errno is set.
 */
int parseNonNegativeInteger(const char *str) {
  errno = 0;
  int value = strtol(str, /* endptr */ NULL, /* base */ 10);
  if (errno != 0) {
    return -1;
  } else if (value < 0) {
    errno = EINVAL;
    return -1;
  } else {
    return value;
  }
}

/**
 * A considerably more laborious implementation of get_online_cpus()
 * compared to the Python code in the bcc repo:
 * https://github.com/iovisor/bcc/blob/master/src/python/bcc/utils.py#L21-L36.
 */
int getOnlineCpus(int **cpus, size_t *numCpu) {
  int fd = open("/sys/devices/system/cpu/online", O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return -1;
  }

  const int bufSize = 256;
  char buf[bufSize];
  int numRead = read(fd, buf, bufSize);
  if (numRead == bufSize || numRead == 0) {
    // We are not prepared for the output to be this big (or empty)!
    errno = EINVAL;
    return -1;
  }
  if (close(fd) < 0) {
    return -1;
  }

  // Ensure the contents of buf are NUL-terminated so that strtol() does not
  // read unintended values.
  buf[numRead] = '\0';

  size_t capacity = 16;
  *cpus = malloc(capacity * sizeof(int));
  if (*cpus == NULL) {
    return -1;
  }

  int lastEndIndex = -1;
  int lastHyphenIndex = -1;
  size_t numElements = 0;
  for (size_t i = 0; i <= numRead; i++) {
    if (i == numRead || buf[i] == ',') {
      errno = 0;
      int rangeStart =
          strtol(buf + lastEndIndex + 1, /* endptr */ NULL, /* base */ 10);
      if (errno != 0) {
        return -1;
      }

      int rangeEnd;
      if (lastHyphenIndex != -1) {
        errno = 0;
        rangeEnd =
            strtol(buf + lastHyphenIndex + 1, /* endptr */ NULL, /* base */ 10);
        if (errno != 0) {
          return -1;
        }
      } else {
        rangeEnd = rangeStart;
      }

      int numCpusToAdd = rangeEnd - rangeStart + 1;
      int extraSpace = capacity - numElements - numCpusToAdd;
      if (extraSpace < 0) {
        size_t newSize = capacity - extraSpace;
        *cpus = realloc(cpus, newSize * sizeof(int));
        if (*cpus == NULL) {
          return -1;
        }
        capacity = newSize;
      }

      for (int j = 0; j < numCpusToAdd; j++) {
        *(*cpus + numElements++) = rangeStart + j;
      }

      lastEndIndex = i;
      lastHyphenIndex = -1;
    } else if (buf[i] == '-') {
      lastHyphenIndex = i;
    }
  }

  *numCpu = numElements;
  return 0;
}

int opt_timestamp = 0;
int opt_failed = 0;
int opt_pid = -1;
int opt_tid = -1;
int opt_duration = -1;
char *opt_name = NULL;

void usage(FILE *fd) {
  // TODO: Write usage string.
  fprintf(fd, "Usage...\n");
}

void parseArgs(int argc, char **argv) {
  int c;
  while (1) {
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},

        {"timestamp", required_argument, 0, 'T'},
        {"failed", no_argument, 0, 'x'},
        {"pid", required_argument, 0, 'p'},
        {"tid", required_argument, 0, 't'},
        {"duration", required_argument, 0, 'd'},
        {"name", required_argument, 0, 'n'},
        {0, 0, 0, 0}};
    int option_index = 0;
    c = getopt_long(argc, argv, "hT:xp:t:d:n:", long_options, &option_index);
    if (c == -1) {
      break;
    }

    switch (c) {
    case 0:
      // I can't tell if this is necessary from the getopt_long man page.
      break;

    case 'T':
      opt_timestamp = parseNonNegativeInteger(optarg);
      if (opt_timestamp == -1) {
        fprintf(stderr, "Invalid value for -T: '%s'\n", optarg);
        exit(1);
      }
      break;

    case 'x':
      opt_failed = 1;
      break;

    case 'p':
      opt_pid = parseNonNegativeInteger(optarg);
      if (opt_pid == -1) {
        fprintf(stderr, "Invalid value for -p: '%s'\n", optarg);
        exit(1);
      }

    case 't':
      opt_tid = parseNonNegativeInteger(optarg);
      if (opt_tid == -1) {
        fprintf(stderr, "Invalid value for -t: '%s'\n", optarg);
        exit(1);
      }

    case 'd':
      opt_duration = parseNonNegativeInteger(optarg);
      if (opt_duration == -1) {
        fprintf(stderr, "Invalid value for -d: '%s'\n", optarg);
        exit(1);
      }

    case 'h':
      usage(stdout);
      exit(0);
      break;

    default:
      usage(stderr);
      exit(1);
      break;
    }
  }
}

void printHeader() {
  printf("%-6s %-16s %4s %3s %s\n", "PID", "COMM", "FD", "ERR", "PATH");
}

void perf_reader_raw_callback(void *cb_cookie, void *raw, int raw_size) {
  struct data_t *event = (struct data_t *)raw;
  if (opt_failed && event->ret >= 0) {
    return;
  }

  int fd_s, err;
  if (event->ret >= 0) {
    fd_s = event->ret;
    err = 0;
  } else {
    fd_s = -1;
    err = -event->ret;
  }

  int pid = event->id >> 32;
  printf("%-6d %-16s %4d %3d %s\n", pid, event->comm, fd_s, err, event->fname);
}

int main(int argc, char **argv) {
  parseArgs(argc, argv);

  int exitCode = 1;
  int *cpus = NULL;
  size_t numCpu = 0;
  if (getOnlineCpus(&cpus, &numCpu) < 0) {
    perror("Failure in getOnlineCpus()");
    goto error;
  }

  bpf_log_buf[0] = '\0';
  int hashMapFd = -1, eventsMapFd = -1, entryProgFd = -1, kprobeFd = -1,
      returnProgFd, kretprobeFd;
  struct perf_reader **readers = NULL;
  readers = malloc(numCpu * sizeof(struct perf_reader *));

  // On my system (Ubuntu 18.04.1 LTS), `uname -r` returns "4.15.0-33-generic".
  // KERNEL_VERSION(4, 15, 0) is 265984, but LINUX_VERSION_CODE is in
  // /usr/include/linux/version.h is 266002, so the values do not match.
  // Ideally, we would use uname(2) to compute kern_version at runtime so this
  // binary would not have to be rebuilt for a minor kernel upgrade, but if
  // kern_version does not match LINUX_VERSION_CODE exactly, then
  // bpf_prog_load(BPF_PROG_TYPE_KPROBE) will fail with EPERM:
  // https://github.com/torvalds/linux/blob/v4.15/kernel/bpf/syscall.c#L1140-L1142.
  // Note this issue has come up in the bcc project itself:
  // https://github.com/iovisor/bcc/commit/bfecc243fc8e822417836dd76a9b4028a5d8c2c9.
  unsigned int kern_version = LINUX_VERSION_CODE;

  // BPF_HASH
  const char *hashMapName = "hashMap name for debugging";
  hashMapFd = bpf_create_map(BPF_MAP_TYPE_HASH, hashMapName,
                             /* key_size */ sizeof(__u64),
                             /* value_size */ sizeof(struct val_t),
                             /* max_entries */ 10240,
                             /* map_flags */ 0);
  if (hashMapFd < 0) {
    perror("Failed to create BPF_HASH");
    goto error;
  }

  // Note that we could update the BPF_LD_MAP_FD() instruction
  // with the observed value if it is not 3, but then we have to
  // modify the code generated from the Python script.
  if (hashMapFd != 3) {
    fprintf(stderr,
            "Invariant violation: fd for BPF_HASH must be 3, but was %d.\n",
            hashMapFd);
    goto error;
  }

  // BPF_PERF_OUTPUT
  const char *perfMapName = "perfMap name for debugging";
  eventsMapFd = bpf_create_map(BPF_MAP_TYPE_PERF_EVENT_ARRAY, perfMapName,
                               /* key_size */ sizeof(int),
                               /* value_size */ sizeof(__u32),
                               /* max_entries */ numCpu,
                               /* map_flags */ 0);

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

  const char *prog_name_for_kprobe = "some kprobe";
  entryProgFd = bpf_prog_load(BPF_PROG_TYPE_KPROBE, prog_name_for_kprobe,
                              trace_entry_insns,
                              /* prog_len */ sizeof(trace_entry_insns),
                              /* license */ "GPL", kern_version,
                              /* log_level */ 1, bpf_log_buf, LOG_BUF_SIZE);
  if (entryProgFd == -1) {
    perror("Error calling bpf_prog_load() for kretprobe");
    goto error;
  }

  kprobeFd = bpf_attach_kprobe(entryProgFd, BPF_PROBE_ENTRY, "p_do_sys_open",
                               "do_sys_open",
                               /* fn_offset */ 0);
  if (kprobeFd < 0) {
    perror("Error calling bpf_attach_kprobe() for kprobe");
    goto error;
  }

  const char *prog_name_for_kretprobe = "some kretprobe";
  returnProgFd = bpf_prog_load(BPF_PROG_TYPE_KPROBE, prog_name_for_kretprobe,
                               trace_return_insns,
                               /* prog_len */ sizeof(trace_return_insns),
                               /* license */ "GPL", kern_version,
                               /* log_level */ 1, bpf_log_buf, LOG_BUF_SIZE);
  if (returnProgFd == -1) {
    perror("Error calling bpf_prog_load() for kretprobe");
    goto error;
  }

  kretprobeFd = bpf_attach_kprobe(returnProgFd, BPF_PROBE_RETURN,
                                  "r_do_sys_open", "do_sys_open",
                                  /* fn_offset */ 0);
  if (kretprobeFd < 0) {
    perror("Error calling bpf_attach_kprobe() for kretprobe");
    goto error;
  }

  // Open a perf buffer for each online CPU.
  // (This is what open_perf_buffer() in bcc/table.py does.)
  for (int cpuIndex = 0; cpuIndex < numCpu; cpuIndex++) {
    int cpu = cpus[cpuIndex];
    void *reader = bpf_open_perf_buffer(&perf_reader_raw_callback,
                                        /* lost_cb */ NULL,
                                        /* cb_cookie */ NULL,
                                        /* pid */ -1, cpu,
                                        /* page_cnt */ 64);
    if (reader == NULL) {
      fprintf(stderr, "Error calling bpf_open_perf_buffer().\n");
      goto error;
    }

    // The fd is owned by the reader, which will be cleaned up by
    // perf_reader_free().
    int perfReaderFd = perf_reader_fd((struct perf_reader *)reader);
    readers[cpu] = reader;

    int rc = bpf_update_elem(eventsMapFd, &cpu, &perfReaderFd, BPF_ANY);
    if (rc < 0) {
      perror("Error calling bpf_update_elem()");
      goto error;
    }
  }

  printHeader();

  // Loop and call perf_buffer_poll(), which has the side-effect of calling
  // perf_reader_raw_callback() on new events.
  while (1) {
    // From the implementation, this always appear to return 0.
    int rc = perf_reader_poll(numCpu, readers, -1);
    if (rc != 0) {
      fprintf(stderr, "Unexpected return value from perf_reader_poll(): %d\n.",
              rc);
    }
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
  // readers
  if (readers != NULL) {
    for (int i = 0; i < numCpu; i++) {
      struct perf_reader *reader = readers[i];
      if (reader != NULL) {
        perf_reader_free((void *)reader);
      }
    }
  }

  // kprobe
  if (kprobeFd != -1) {
    close(kprobeFd);
  }
  if (entryProgFd != -1) {
    close(entryProgFd);
  }

  // kretprobe
  if (kretprobeFd != -1) {
    close(kretprobeFd);
  }
  if (returnProgFd != -1) {
    close(returnProgFd);
  }

  // maps
  if (eventsMapFd != -1) {
    close(eventsMapFd);
  }
  if (hashMapFd != -1) {
    close(hashMapFd);
  }

  // cpus array allocated by getOnlineCpus().
  if (cpus != NULL) {
    free(cpus);
  }

  return exitCode;
}
