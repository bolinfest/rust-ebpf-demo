from bcc import ArgString, BPF
from binascii import hexlify
import struct

# define BPF program
bpf_text = """
#include <uapi/linux/ptrace.h>
#include <uapi/linux/limits.h>
#include <linux/sched.h>

int trace_entry(struct pt_regs *ctx, int dfd, const char __user *filename)
{
    bpf_trace_printk("hello from rust\\n", 17);
    return 0;
};
"""

b = BPF(text=bpf_text)
bytecode = b.dump_func("trace_entry")


def print_list_of_instructions():
    for index, b_offset in enumerate(xrange(0, len(bytecode), 8)):
        instruction = bytecode[b_offset : b_offset + 8]
        print("%2d %s" % (index, hexlify(instruction)))


bpf_insn_template = """\
  ((struct bpf_insn) {
      .code    = 0x%s,
      .dst_reg = BPF_REG_%d,
      .src_reg = BPF_REG_%d,
      .off     = %d,
      .imm     = %d,
  }),"""


def print_bpf_insn_struct():
    """Prints the eBPF bytecode as the equivalent bpf_insn[] in C."""
    print("struct bpf_insn prog[] = {")
    for b_offset in xrange(0, len(bytecode), 8):
        instruction = bytecode[b_offset : b_offset + 8]
        # u8
        opcode = instruction[0]

        src_and_dst = struct.unpack("B", instruction[1])[0]
        # The low-order nibble is dst.
        dst_reg = src_and_dst & 0x0F
        # The high-order nibble is src.
        src_reg = (src_and_dst & 0xF0) >> 4

        # s16
        offset = struct.unpack("h", instruction[2:4])[0]
        # s32
        imm = struct.unpack("i", instruction[4:8])[0]
        print(bpf_insn_template % (hexlify(opcode), dst_reg, src_reg, offset, imm))
    print("};")


# print_list_of_instructions()
print_bpf_insn_struct()
