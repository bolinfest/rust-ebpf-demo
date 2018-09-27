#include <linux/bpf.h>
#include <stdio.h>

/*
 * Simple program to verify that nibbles are serialized correctly.
 * Recommended test:
 *
 * clang write_bpf_insn.c
 * ./a.out | xxd
 *
 * You should see:
 *
 * 00000000: 187e feca 6f6d 2072                      .~..om r
 */
int main(int argc, char **argv) {
  /*
   * bpf_insn is defined as follows where the second
   * and third fields are nibbles:
   *
   * struct bpf_insn {
   *   __u8 code;       // opcode
   *   __u8 dst_reg:4;  // dest register
   *   __u8 src_reg:4;  // source register
   *   __s16 off;       // signed offset
   *   __s32 imm;       // signed immediate constant
   * };
   */
  struct bpf_insn instruction = (struct bpf_insn){
      .code = 0x18,
      // Note that if you try to change this to 0x1e, you will
      // get a compilation warning:
      //
      // warning: implicit truncation from 'int' to bit-field changes value from 30 to 14
      .dst_reg = 0x0e,
      .src_reg = 0x07,
      .off = 0xcafe,
      .imm = 0x72206d6f,
  };

  fwrite(&instruction, sizeof(instruction), 1, stdout);

  // struct example {
  //   char f1 : 3;
  //   char f2 : 3;
  //   __u8 f3 : 2;
  // };
  // struct example ex = (struct example){
  //     .f1 = -1,
  //     .f2 = 2,
  //     .f3 = 3,
  // };
  // fwrite(&ex, sizeof(ex), 1, stdout);
}
