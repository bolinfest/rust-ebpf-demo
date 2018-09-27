import struct
from bcc import ArgString, BPF

# define BPF program
bpf_text = """
#include <uapi/linux/ptrace.h>
#include <uapi/linux/limits.h>
#include <linux/sched.h>

int trace_entry(struct pt_regs *ctx, int dfd, const char __user *filename)
{
    bpf_trace_printk("hello from rust\\n");
    return 0;
};
"""

def parse_instruction(instruction):
    # u8
    opcode = struct.unpack("B", instruction[0])[0]

    src_and_dst = struct.unpack("B", instruction[1])[0]
    # The low-order nibble is dst.
    dst_reg = src_and_dst & 0x0F
    # The high-order nibble is src.
    src_reg = (src_and_dst & 0xF0) >> 4

    # s16
    offset = struct.unpack("h", instruction[2:4])[0]
    # s32
    imm = struct.unpack("i", instruction[4:8])[0]
    return (opcode, dst_reg, src_reg, offset, imm)


bpf_insn_template = """\
  ((struct bpf_insn) {
      .code    = 0x%x,
      .dst_reg = BPF_REG_%d,
      .src_reg = BPF_REG_%d,
      .off     = %d,
      .imm     = %d,
  }),"""


def print_bpf_insn_struct(bytecode):
    """Prints the eBPF bytecode as the equivalent bpf_insn[] in C."""
    print("struct bpf_insn prog[] = {")
    for b_offset in xrange(0, len(bytecode), 8):
        instruction = bytecode[b_offset : b_offset + 8]
        opcode, dst_reg, src_reg, offset, imm = parse_instruction(instruction)
        print(bpf_insn_template % (opcode, dst_reg, src_reg, offset, imm))
    print("};")


bpf = BPF(text=bpf_text)
bytecode = bpf.dump_func("trace_entry")
print_bpf_insn_struct(bytecode)
# bpf.attach_kprobe(event="do_sys_open", fn_name="trace_entry")
