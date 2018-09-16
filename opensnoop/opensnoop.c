#include <linux/bpf.h>
#include <linux/version.h>
#include <stdio.h>
#include <string.h>
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

struct val_t {
  __u64 id;
  __u64 ts;
  char comm[TASK_COMM_LEN];
  const char *fname;
};

int main(int argc, char **argv) {
  int exitCode = 1;
  bpf_log_buf[0] = '\0';
  int progFd = -1;
  union bpf_attr attr;
  memset(&attr, 0, sizeof(attr));

  attr.map_type = BPF_MAP_TYPE_HASH;
  attr.key_size = sizeof(__u64);
  attr.value_size = sizeof(struct val_t);
  attr.max_entries = 10240; // Default value for BPF_HASH().
  int hashMapFd = syscall(__NR_bpf, BPF_MAP_CREATE, &attr, sizeof(attr));
  if (hashMapFd < 0) {
    perror("Failed to create HASH_MAP");
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

      // When BCC generates the bytecode for this function,
      // it inserts a "load double-word" instruction of:
      //
      //   18 11 00 00 03 00 00 00 00 00 00 00 00 00 00 00
      //
      // This corresponds to the two bpf_insn below:
      //
      // ((struct bpf_insn){
      //     .code = 0x18,
      //     .dst_reg = BPF_REG_1,
      //     .src_reg = BPF_REG_1,
      //     .off = 0,
      //     .imm = 3,
      // }),
      // ((struct bpf_insn){
      //     .code = 0x0,
      //     .dst_reg = BPF_REG_0,
      //     .src_reg = BPF_REG_0,
      //     .off = 0,
      //     .imm = 0,
      // }),
      //
      // Note this has a weird case where the src and dst are
      // the same register. There appears to be some special logic
      // where a "load double-word" instruction with src_reg=1
      // flags the imm value as a file descriptor for a BPF map, which
      // is imperative for program verification.
      //
      // As a workaround, we replace this with our own
      // "load double-word" instruction with the following changes:
      // - src_reg is changed to BPF_REG_0.
      // - imm is changed to to the fd of the appropriate BPF map.
      //
      // Note that because our replacement is using the same
      // number of instructions as the original bytecode, we do
      // not have to update the offsets in the existing jump instructions.
      //
      // This Go program from Cloudflare does a similar thing
      // where they build the bytecode programmatically, though they
      // use a helper library. Here is the use of a helper function
      // named BPFILdMapFd
      // (https://github.com/cloudflare/cloudflare-blog/blob/765d4b663d74b0a77a646a1810b3f8ec3d64ea03/2018-03-ebpf/ebpf.go#L105):
      //
      //   // r1 must point to map
      //   ebpf.BPFILdMapFd(ebpf.Reg1, mapFd),
      //
      // And here is the code in the ebpf library that implements
      // that helper function
      // (https://github.com/newtools/ebpf/blob/bccd8019d64f716dd3647e82950ae38c7291fc77/types.go#L1354-L1369):
      //
      //   // BPFILdMapFd loads a user space fd into a BPF program as a
      //   reference to a
      //   // specific eBPF map.
      //   func BPFILdMapFd(dst Register, imm int) Instruction {
      //     return BPFILdImm64Raw(dst, 1, int64(imm))
      //   }
      //
      //   func BPFILdImm64(dst Register, imm int64) Instruction {
      //     return BPFILdImm64Raw(dst, 0, imm)
      //   }
      //
      //   func BPFILdImm64Raw(dst, src Register, imm int64) Instruction {
      //     return Instruction{
      //       OpCode:      LdDW,
      //       DstRegister: dst,
      //       SrcRegister: src,
      //       Constant:    imm,
      //     }
      //   }
      //
      // If you look at the code that turns an LdDW Instruction into the binary
      // format for the kernel, you can verify that this is serialized as two
      // instructions:
      // https://github.com/newtools/ebpf/blob/bccd8019d64f716dd3647e82950ae38c7291fc77/types.go#L1354.
      ((struct bpf_insn){
          .code = 0x18,
          .dst_reg = BPF_REG_1,
          .src_reg = 1, // BPF_PSEUDO_MAP_FD; see BPF_LD_MAP_FD() in libbpf.
          .off = 0,
          .imm = hashMapFd,
      }),
      ((struct bpf_insn){
          .code = 0x0,
          .dst_reg = BPF_REG_0,
          .src_reg = BPF_REG_0,
          .off = 0,
          .imm = hashMapFd < 0 ? 0xFFFF : 0,
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

  exitCode = 0;
  goto cleanup;

error:
  // If there is anything in the bpf_log_buf, print it
  // as it may be helpful in debugging.
  if (bpf_log_buf[0] != '\0') {
    fprintf(stderr, "%s", bpf_log_buf);
  }

cleanup:
  if (progFd != -1) {
    close(progFd);
  }
  close(hashMapFd);
  return exitCode;
}
