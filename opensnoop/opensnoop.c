#include "generated_trace_entry.h"
#include "generated_trace_return.h"
#include "libbpf_wrapper.h"
#include <bcc/perf_reader.h>
#include <errno.h>
#include <limits.h>
#include <linux/version.h>
#include <stdio.h>
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

// TODO: Do get_online_cpus() instead of hardcoding to 8.
#define NUM_CPU 8

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

void perf_reader_raw_callback(void *cb_cookie, void *raw, int raw_size) {
  struct data_t *event = (struct data_t *)raw;
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
  int exitCode = 1;
  bpf_log_buf[0] = '\0';
  int hashMapFd = -1, eventsMapFd = -1, entryProgFd = -1, kprobeFd = -1,
      returnProgFd, kretprobeFd;
  struct perf_reader *readers[NUM_CPU];
  memset(readers, 0, NUM_CPU * sizeof(struct perf_reader *));

  // TODO: Read this from uname(2) rather than hardcoding it here or else
  // the compiled version of this program cannot run on another machine.
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
                               /* max_entries */ NUM_CPU,
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
  for (int cpu = 0; cpu < NUM_CPU; cpu++) {
    // TODO: Verify these are the right CPU numbers?
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

  printf("%-6s %-16s %4s %3s %s\n", "PID", "COMM", "FD", "ERR", "PATH");

  // Loop and call perf_buffer_poll(), which has the side-effect of calling
  // perf_reader_raw_callback() on new events.
  while (1) {
    // From the implementation, this always appear to return 0.
    int rc = perf_reader_poll(NUM_CPU, readers, -1);
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
  for (int i = 0; i < NUM_CPU; i++) {
    struct perf_reader *reader = readers[i];
    if (reader != NULL) {
      perf_reader_free((void *)reader);
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
  return exitCode;
}
