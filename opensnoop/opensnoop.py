import os
from bcc import (
    ArgString,
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

struct val_t {
    u64 id;
    char comm[TASK_COMM_LEN];
    const char *fname;
};

struct data_t {
    u64 id;
    u64 ts;
    int ret;
    char comm[TASK_COMM_LEN];
    char fname[NAME_MAX];
};

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


def gen_bytecode(fn_name, filter_value=""):
    bpf = BPF(text=bpf_text_template.replace("FILTER", filter_value))
    bytecode = bpf.dump_func(fn_name)
    bpf.cleanup()  # Reset fds before next BPF is created.
    return bytecode


trace_entry_no_filter_bytecode = gen_bytecode("trace_entry")
trace_return_bytecode = gen_bytecode("trace_return")

PLACEHOLDER_TID = 123456
trace_entry_tid_filter_bytecode = gen_bytecode(
    "trace_entry", "if (tid != %d) { return 0; }" % PLACEHOLDER_TID
)

PLACEHOLDER_PID = 654321
trace_entry_pid_filter_bytecode = gen_bytecode(
    "trace_entry", "if (pid != %d) { return 0; }" % PLACEHOLDER_PID
)

trace_entry_code = generate_c_function(
    "generate_trace_entry", trace_return_bytecode + trace_entry_no_filter_bytecode
)
trace_entry_tid_code = generate_c_function(
    "generate_trace_entry_tid",
    trace_return_bytecode + trace_entry_tid_filter_bytecode,
    placeholder={"param_type": "int", "param_name": "tid", "imm": PLACEHOLDER_TID},
)
trace_entry_pid_code = generate_c_function(
    "generate_trace_entry_pid",
    trace_return_bytecode + trace_entry_pid_filter_bytecode,
    placeholder={"param_type": "int", "param_name": "pid", "imm": PLACEHOLDER_PID},
)

generated_header = os.path.join(
    os.path.dirname(os.path.realpath(__file__)), "generated_bytecode.h"
)
with open(generated_header, "w") as f:
    f.write(
        """\
// GENERATED FILE: See opensnoop.py.
#include <bcc/libbpf.h>
#include <stdlib.h>

#define TRACE_RETURN_NUM_INSTRUCTIONS %d

%s
%s
%s"""
        % (
            len(trace_return_bytecode) / 8,
            trace_entry_code,
            trace_entry_tid_code,
            trace_entry_pid_code,
        )
    )
