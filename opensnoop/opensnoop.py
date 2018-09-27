import os
from bcc import (
    BPF,
    DEBUG_LLVM_IR,
    DEBUG_PREPROCESSOR,
    DEBUG_SOURCE,
    DEBUG_BPF_REGISTER_STATE,
)
from debug import generate_c_function

# define BPF program
bpf_text_template = """
#include <uapi/linux/ptrace.h>
#include <uapi/linux/limits.h>
#include <linux/sched.h>
#include "opensnoop.h"

BPF_HASH(infotmp, u64, struct val_t);
BPF_PERF_OUTPUT(events);

int trace_entry(struct pt_regs *ctx, int dfd, const char __user *filename)
{
    struct val_t val = {};
    u64 id = bpf_get_current_pid_tgid();
    u32 pid = id >> 32; // PID is higher part
    u32 tid = id;       // Cast and get the lower part

    FILTER
    if (bpf_get_current_comm(&val.comm, sizeof(val.comm)) == 0) {
        val.id = id;
        val.fname = filename;
        infotmp.update(&id, &val);
    }

    return 0;
};

int trace_return(struct pt_regs *ctx)
{
    u64 id = bpf_get_current_pid_tgid();
    struct val_t *valp;
    struct data_t data = {};

    u64 tsp = bpf_ktime_get_ns();

    valp = infotmp.lookup(&id);
    if (valp == 0) {
        // missed entry
        return 0;
    }
    bpf_probe_read(&data.comm, sizeof(data.comm), valp->comm);
    bpf_probe_read(&data.fname, sizeof(data.fname), (void *)valp->fname);
    data.id = valp->id;
    data.ts = tsp;
    data.ret = PT_REGS_RC(ctx);

    events.perf_submit(ctx, &data, sizeof(data));
    infotmp.delete(&id);

    return 0;
}
"""


def gen_c(name, bpf_fn, filter_value="", placeholder=None):
    bpf = BPF(text=bpf_text_template.replace("FILTER", filter_value))
    bytecode = bpf.dump_func(bpf_fn)
    bpf.cleanup()  # Reset fds before next BPF is created.
    return generate_c_function(name, bytecode, placeholder=placeholder)


PLACEHOLDER_TID = 123456
PLACEHOLDER_PID = 654321

# Note that we cannot call gen_c() while another file is open
# (such as generated_bytecode.h) or else it will throw off the
# file descriptor numbers in the generated code.
c_file = (
    """\
// GENERATED FILE: See opensnoop.py.
#include <bcc/libbpf.h>
#include <stdlib.h>

"""
    + gen_c("generate_trace_entry", "trace_entry")
    + gen_c(
        "generate_trace_entry_tid",
        "trace_entry",
        filter_value="if (tid != %d) { return 0; }" % PLACEHOLDER_TID,
        placeholder={"param_type": "int", "param_name": "tid", "imm": PLACEHOLDER_TID},
    )
    + gen_c(
        "generate_trace_entry_pid",
        "trace_entry",
        filter_value="if (pid != %d) { return 0; }" % PLACEHOLDER_PID,
        placeholder={"param_type": "int", "param_name": "pid", "imm": PLACEHOLDER_PID},
    )
    + gen_c("generate_trace_return", "trace_return")
)

__dir = os.path.dirname(os.path.realpath(__file__))
with open(os.path.join(__dir, "generated_bytecode.h"), "w") as f:
    f.write(c_file)
