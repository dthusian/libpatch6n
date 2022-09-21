#include "../platform_id.hh"
#if defined(PLATFORM_ARCH_AMD64) && defined(PLATFORM_OS_POSIX)

#include "../patch.hh"
#include "../asm/amd64.hh"
#include <cstdint>
#include <cstring>
#include <vector>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
#include <stack>

namespace A = asm_amd64;

inline uint64_t setrb(uint64_t x) {
  x = x | (x << 1);
  x = x | (x << 2);
  x = x | (x << 4);
  x = x | (x << 8);
  x = x | (x << 16);
  x = x | (x << 32);
  return x;
}

constexpr size_t asm_nop_space_search_size = 32;

byte_ostream build_jmp_target(const patch::function_def& def) {
  // Terminology:
  // target-arg is an argument of patch_me
  // mixin-arg is a pointer argument to a target-arg

  // VERY IMPORTANT!
  // Calls to the generated asm WILL violate SYSV ABI!
  // Assume the stack is 16b aligned when the call instruction finishes (that is, 16b aligned + 8b when the call function executes)

  byte_ostream b;

  // Identify the arguments and their positions

  // Total size of the stack frame
  // Contains saved registers that contain target-args
  size_t stackframe_saved_args_size = 0;
  // Contains mixin-arguments
  size_t stackframe_mixin_args_size = 0;

  // Relative to the base of the stack frame (where rsp was when the function begin execution)
  std::vector<ptrdiff_t> arg_offsets;
  // Tracks what target-arguments were saved on the stack
  std::stack<int> saved_args_stack_contents;

  bool stack_is_padded = false;
  ptrdiff_t stack_args_consumed_bytes = 0;
  int gpr_args_consumed = 0; // target-args
  constexpr A::gpr int_args[] = {
      A::gpr::rdi,
      A::gpr::rsi,
      A::gpr::rdx,
      A::gpr::rcx,
      A::gpr::r8,
      A::gpr::r9
  };
  int i = 0;
  for(auto typ : def.args) {
    if(typ == patch::arg_type::vector) {
      throw std::runtime_error("Vector arguments unsupported for now");
    } else {
      // Integer argument
      if(gpr_args_consumed < 6) {
        // Argument is on a GPR, save it
        stackframe_saved_args_size += 8;
        A::push_r(b, int_args[gpr_args_consumed]);
        saved_args_stack_contents.push((uint8_t)int_args[i]);
        gpr_args_consumed++;
        // Negative because it's after the stackframe base
        arg_offsets.push_back(-gpr_args_consumed * 8);
      } else {
        // Stack argument
        // Positive because it's before the stackframe base
        arg_offsets.push_back(stack_args_consumed_bytes + 8); // Also add space for rip
        stack_args_consumed_bytes += 8;
      }
    }
    i++;
  }
  // Align the stack by calculating if it will be misaligned after pushing mixin-args
  if((stackframe_saved_args_size + (std::max((ssize_t)def.args.size() - 6, (ssize_t)0)) * 8) % 16 != 0) {
    stackframe_saved_args_size += 8;
    A::sub_ri_rsp_imm8(b, 8);
    stack_is_padded = true;
  }
  for(auto& mixin : def.mixins) {
    if(mixin.typ != patch::mixin_type::inject_head) {
      continue;
    }
    // Setup mixin-args
    int gpr_args_setup = 0; // measures how many mixin-args have gone into GPRs
    for(i = 0; i < def.args.size(); i++) {
      // Distance from current rsp to previous rsp (stackframe base) (rsp + rsp_disp_stackframe_base == stackframe_base)
      uint32_t rsp_disp_stackframe_base = (uint32_t)stackframe_mixin_args_size + (uint32_t)stackframe_saved_args_size;
      uint32_t rsp_disp_u32 = (uint32_t)arg_offsets[i] + rsp_disp_stackframe_base;
      int32_t rsp_disp_i32 = *reinterpret_cast<int32_t*>(&rsp_disp_u32);
      if(gpr_args_setup < 6) {
        // Load address of mixin-arg into register
        A::lea_rm_m_rsp_disp(b, int_args[gpr_args_setup], rsp_disp_i32);
        gpr_args_setup++;
      } else {
        // Address goes to stack
        A::lea_rm_m_rsp_disp(b, A::gpr::rax, rsp_disp_i32);
        A::push_r(b, A::gpr::rax);
        stackframe_mixin_args_size += 8;
      }
    }
    // Call the mixin
    A::mov_ri_rax_imm64(b, (intptr_t)mixin.injected);
    A::call_r_rax(b);
    // Clean the stack
    if(stackframe_mixin_args_size > 0) {
      //TODO allow more than 256/8 stack arguments
      A::sub_ri_rsp_imm8(b, (uint8_t)(stackframe_mixin_args_size));
    }
    stackframe_mixin_args_size = 0;
  }
  if(stack_is_padded) {
    A::add_ri_rsp_imm8(b, 8);
  }
  // Restore register args and prepare for calling the real function
  while(!saved_args_stack_contents.empty()) {
    A::pop_r(b, (A::gpr)((uint8_t)saved_args_stack_contents.top()));
    saved_args_stack_contents.pop();
  }
  A::ret(b);
  return b;
}

struct patch::patch_data {
  void* target;
  void* exec_page;
  size_t exec_page_size;

  ~patch_data() {
    munmap(exec_page, exec_page_size);
  }
};

patch::patch_data* amd64_sysvabi_posix_patch(const patch::function_def& def) {
  auto* state = new patch::patch_data;
  auto* target_b = static_cast<uint8_t*>(def.target);
  // Check if the nop space was added
  size_t nop_offset = SIZE_MAX;
  for(size_t i = 0; i < asm_nop_space_search_size; i++) {
    int check = memcmp(target_b + i, A::nop_12, sizeof(A::nop_12));
    if(check == 0) {
      nop_offset = i;
      break;
    }
  }
  if(nop_offset == SIZE_MAX) {
    throw std::runtime_error("Function does not include nop space or is already patched");
  }

  // Construct the jump target
  byte_buf new_fn = build_jmp_target(def).str();

  // Copy the jump target to a new executable page
  state->exec_page = mmap(nullptr, new_fn.size(), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if(state->exec_page == MAP_FAILED) {
    throw std::runtime_error("mmap() failed");
  }
  state->exec_page_size = new_fn.size();
  memcpy(state->exec_page, new_fn.data(), new_fn.size());
  mprotect(state->exec_page, state->exec_page_size, PROT_READ | PROT_EXEC);

  // Move the function prologue and add the jump stub
  uint64_t page_size = sysconf(_SC_PAGESIZE);
  uint64_t page_start = (reinterpret_cast<uint64_t>(target_b)) & setrb(page_size);
  mprotect(reinterpret_cast<void*>(page_start), reinterpret_cast<uint64_t>(target_b) - page_start + 64, PROT_READ | PROT_WRITE | PROT_EXEC);
  for(int i = (int)nop_offset - 1; i >= 0; i--) {
    target_b[i + sizeof(A::nop_12)] = target_b[i];
  }
  state->target = def.target;
  byte_ostream jmp_stub;
  A::mov_ri_rax_imm64(jmp_stub, (intptr_t)state->exec_page);
  A::call_r_rax(jmp_stub);
  byte_buf jmp_stub_b = jmp_stub.str();
  for(int i = 0; i < sizeof(asm_amd64::nop_12); i++) {
    target_b[i] = jmp_stub_b[i];
  }
  mprotect(reinterpret_cast<void*>(page_start), reinterpret_cast<uint64_t>(target_b) - page_start + 64, PROT_READ | PROT_EXEC);
  return state;
}

void amd64_sysvabi_posix_unpatch(patch::patch_data* state) {
  // Restore the jump stub
  auto* target_b = static_cast<uint8_t*>(state->target);
  uint64_t page_size = sysconf(_SC_PAGESIZE);
  uint64_t page_start = (reinterpret_cast<uint64_t>(target_b)) & setrb(page_size);
  mprotect(reinterpret_cast<void*>(page_start), reinterpret_cast<uint64_t>(target_b) - page_start + 64, PROT_READ | PROT_WRITE | PROT_EXEC);
  memcpy(target_b, A::nop_12, sizeof(A::nop_12));
  mprotect(reinterpret_cast<void*>(page_start), reinterpret_cast<uint64_t>(target_b) - page_start + 64, PROT_READ | PROT_EXEC);
  // Free state
  delete state;
}

patch::patch_data* patch::patch_function(const patch::function_def& def) {
  return amd64_sysvabi_posix_patch(def);
}

void patch::unpatch_function(patch::patch_data* state) {
  amd64_sysvabi_posix_unpatch(state);
}

#endif