use std::mem::transmute;

#[no_mangle]
#[link_section = "license"]
pub static _license: [u8; 4] = [71u8, 80, 76, 0]; //b"GPL\0"

#[no_mangle]
#[link_section = "version"]
pub static _version: u32 = 0xFFFFFFFE;

#[no_mangle]
#[link_section = "kprobe/SyS_clone"]
#[allow(non_snake_case)]
pub extern "C" fn kprobe__sys_clone(_ctx: *mut u8) -> i32 {
  let BPF_FUNC_trace_printk = unsafe {
    transmute::<u64, fn(*const u8, i32) -> i32>(6)
  };

  let msg: [u8; 17] = [104u8, 101, 108, 108, 111, 32, 102, 114, 111, 109, 32, 114, 117, 115, 116, 10, 0]; //b"hello from Rust\0"
  BPF_FUNC_trace_printk((&msg).as_ptr(), 17);
  return 0;
}
