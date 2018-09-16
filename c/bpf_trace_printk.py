from bcc import ArgString, BPF
from debug import print_bpf_insn_struct, print_list_of_instructions

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

b = BPF(text=bpf_text)
bytecode = b.dump_func("trace_entry")

print_list_of_instructions(bytecode)
# print_bpf_insn_struct(bytecode)
