
#ifndef PATCH6N_AMD64_HH
#define PATCH6N_AMD64_HH

#include "util.hh"

// <instr>_<r|m|i><r|m|i>_<spec a>_<spec b>

namespace asm_amd64 {
  enum class gpr : uint8_t {
    rax = 0,
    rbx = 1,
    rcx = 2,
    rdx = 3,
    rsp = 4,
    rbp = 5,
    rsi = 6,
    rdi = 7,
    r8 = 8,
    r9 = 9,
    r10 = 10,
    r11 = 11,
    r12 = 12,
    r13 = 13,
    r14 = 14,
    r15 = 15
  };

  constexpr uint8_t nop_12[] = {
      0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00,
      0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00
  };

  inline void mov_ri_rax_imm64(byte_ostream& b, uint64_t x) {
    b.put(0x48);
    b.put(0xb8);
    b.write(reinterpret_cast<uint8_t*>(&x), 8);
  }

  inline void call_r_rax(byte_ostream& b) {
    b.put(0xff);
    b.put(0xd0);
  }

  inline void push_r(byte_ostream& b, gpr reg) {
    auto regi = static_cast<uint8_t>(reg);
    if(regi >= 8) {
      b.put(0x41); // REX.B
    }
    b.put(0x50 | (regi & 0x7));
  }

  inline void pop_r(byte_ostream& b, gpr reg) {
    auto regi = static_cast<uint8_t>(reg);
    if(regi >= 8) {
      b.put(0x41); // REX.B
    }
    b.put(0x58 | (regi & 0x7));
  }

  inline void mov_rr(byte_ostream& b, gpr r1, gpr r2) {
    auto r1i = static_cast<uint8_t>(r1);
    auto r2i = static_cast<uint8_t>(r2);
    if(r1i >= 8 && r2i >= 8) {
      b.put(0x4d); // REX.WRB
    } else if(r1i >= 8) {
      b.put(0x4c); // REX.WR
    } else if(r2i >= 8) {
      b.put(0x49); // REX.WB
    } else {
      b.put(0x48); // REX.W
    }
    b.put(0x89);
    b.put(0xc0 | ((r1i & 0x7) << 3) | (r2i & 0x7));
  }

  inline void lea_rm_m_rsp_disp(byte_ostream& b, gpr reg, int32_t disp) {
    auto regi = static_cast<uint8_t>(reg);
    bool is_disp8 = disp < 128 && disp >= -128;
    b.put(0x48); // REX.W
    b.put(0x8d);
    b.put((is_disp8 ? 0x40 : 0x80) | (regi << 3) | 0x4);
    b.put(0x24);
    if(is_disp8) {
      b.put(disp & 0xff);
    } else {
      uint8_t dispa[4];
      *(reinterpret_cast<uint32_t*>(&dispa)) = disp;
      b.write(dispa, 4);
    }
  }

  inline void sub_ri_rsp_imm8(byte_ostream& b, int8_t disp) {
    b.put(0x48);
    b.put(0x83);
    b.put(0xec);
    b.put(*reinterpret_cast<uint8_t*>(&disp));
  }

  inline void add_ri_rsp_imm8(byte_ostream& b, int8_t disp) {
    b.put(0x48);
    b.put(0x83);
    b.put(0xc4);
    b.put(*reinterpret_cast<uint8_t*>(&disp));
  }

  inline void ret(byte_ostream& b) {
    b.put(0xc3);
  }
}

#endif //PATCH6N_AMD64_HH
