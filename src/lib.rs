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
    // ::std::os::raw::c_char seems more appropriate than u8,
    // but converting msg using CStr::from_bytes_with_nul() seems to
    // mess things up.
    //
    // I tried using a combination of CString::from_vec_unchecked() and
    // as_ptr(), but I got this error when trying to generate the BPF
    // bytecode:
    //
    // error: <unknown>:0:0: in function kprobe__sys_clone i32 (i8*): only small returns supported
    //
    // Perhaps this is because std::os::raw::c_char is i8 instead of u8?
    transmute::<u64, fn(*const u8, i32) -> i32>(6)
  };

  let msg: [u8; 17] = [104u8, 101, 108, 108, 111, 32, 102, 114, 111, 109, 32, 114, 117, 115, 116, 10, 0]; //b"hello from rust\n\0"
  BPF_FUNC_trace_printk((&msg).as_ptr(), 17);
  return 0;
}
