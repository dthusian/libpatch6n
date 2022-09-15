#include "../platform_id.hh"
#if defined(PLATFORM_ARCH_AMD64) && defined(PLATFORM_OS_POSIX)

#include "../patch.hh"
#include <cstdint>
#include <cstring>
#include <vector>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>

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

constexpr uint8_t asm_nop_space[] = {
    // Two 6-byte NOPs
    0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00,
    0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00,
};

constexpr size_t asm_icall_size = 12;
void asm_icall(uint8_t* target, uint64_t reloc_addr) {
  target[0] = 0x48; // mov rax,<x>
  target[1] = 0xb8;
  *(reinterpret_cast<uint64_t*>(target + 2)) = reloc_addr;
  target[10] = 0xff; // call rax
  target[11] = 0xd0;
}

void asm_save_args(std::vector<uint8_t>& target, int n_args) {
  constexpr uint8_t asm_save_reg_args[6][2] = {
      {0x57}, // push rdi
      {0x56}, // push rsi
      {0x52}, // push rdx
      {0x51}, // push rcx
      {0x41, 0x50}, // push r8
      {0x41, 0x51}, // push r9
  };
  for(int i = std::min(n_args, 6) - 1; i >= 0; i--) {
    target.insert(target.end(), asm_save_reg_args[i], asm_save_reg_args[i] + (i >= 4 ? 2 : 1));
  }
}

void asm_restore_args(std::vector<uint8_t>& target, int n_args) {
  constexpr uint8_t asm_restore_reg_args[6][2] = {
      {0x5F}, // pop rdi
      {0x5E}, // pop rsi
      {0x5A}, // pop rdx
      {0x59}, // pop rcx
      {0x41, 0x58}, // pop r8
      {0x41, 0x59}  // pop r9
  };
  for(int i = 0; i < std::min(n_args, 6); i++) {
    target.insert(target.end(), asm_restore_reg_args[i], asm_restore_reg_args[i] + (i >= 4 ? 2 : 1));
  }
}

void asm_perform_mixin_head(std::vector<uint8_t>& target, const patch::mixin_def& def, int n_args) {
  constexpr uint8_t asm_sub_rsp_8[] = {
      0x48, 0x83, 0xEC, 0x08 // sub rsp,0x8
  };
  constexpr uint8_t asm_add_rsp_8[] = {
      0x48, 0x83, 0xC4, 0x08 // add rsp,0x8
  };
  constexpr uint8_t asm_restore_reg_args[6][5] = {
      {0x48, 0x8D, 0x7C, 0x24, 0x00},
      {0x48, 0x8D, 0x74, 0x24, 0xF8},
      {0x48, 0x8D, 0x54, 0x24, 0xF0},
      {0x48, 0x8D, 0x4C, 0x24, 0xE8},
      {0x4C, 0x8D, 0x44, 0x24, 0xE0},
      {0x4C, 0x8D, 0x4C, 0x24, 0xD8}
  };
  for(int i = 0; i < std::min(n_args, 6); i++) {
    target.insert(target.end(), asm_restore_reg_args[i], asm_restore_reg_args[i] + 5);
  }
  if(n_args % 2 == 0) {
    target.insert(target.end(), asm_sub_rsp_8, asm_sub_rsp_8 + sizeof(asm_sub_rsp_8));
  }
  target.insert(target.end(), asm_icall_size, 0);
  asm_icall(target.data() + target.size() - asm_icall_size, (intptr_t)def.injected);
  if(n_args % 2 == 0) {
    target.insert(target.end(), asm_add_rsp_8, asm_add_rsp_8 + sizeof(asm_add_rsp_8));
  }
}

struct patch::patch_data {
  void* target;
  void* exec_page;
  size_t exec_page_size;

  ~patch_data() {
    munmap(exec_page, exec_page_size);
  }
};

patch::patch_data* amd64_posix_patch(const patch::function_def& def) {
  auto* state = new patch::patch_data;
  auto* target_b = static_cast<uint8_t*>(def.target);
  // Check if the nop space was added
  size_t nop_offset = SIZE_MAX;
  for(size_t i = 0; i < asm_nop_space_search_size; i++) {
    int check = memcmp(target_b + i, asm_nop_space, sizeof(asm_nop_space));
    if(check == 0) {
      nop_offset = i;
      break;
    }
  }
  if(nop_offset == SIZE_MAX) {
    throw std::runtime_error("Function does not include nop space or is already patched");
  }

  // Construct the jump target
  std::vector<uint8_t> new_fn;
  asm_save_args(new_fn, def.n_args);
  for(auto& m_def : def.mixins) {
    asm_perform_mixin_head(new_fn, m_def, def.n_args);
  }
  asm_restore_args(new_fn, def.n_args);
  new_fn.push_back(0xc3); // ret

  // Copy the jump target to a new page
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
  memmove(target_b + sizeof(asm_nop_space), target_b, nop_offset);
  state->target = def.target;
  asm_icall(target_b, (intptr_t)state->exec_page);
  mprotect(reinterpret_cast<void*>(page_start), reinterpret_cast<uint64_t>(target_b) - page_start + 64, PROT_READ | PROT_EXEC);
  return state;
}

void amd64_posix_unpatch(patch::patch_data* state) {
  // Restore the jump stub
  auto* target_b = static_cast<uint8_t*>(state->target);
  uint64_t page_size = sysconf(_SC_PAGESIZE);
  uint64_t page_start = (reinterpret_cast<uint64_t>(target_b)) & setrb(page_size);
  mprotect(reinterpret_cast<void*>(page_start), reinterpret_cast<uint64_t>(target_b) - page_start + 64, PROT_READ | PROT_WRITE | PROT_EXEC);
  memcpy(target_b, asm_nop_space, sizeof(asm_nop_space));
  mprotect(reinterpret_cast<void*>(page_start), reinterpret_cast<uint64_t>(target_b) - page_start + 64, PROT_READ | PROT_EXEC);
  // Free state
  delete state;
}

patch::patch_data* patch::patch_function(const patch::function_def& def) {
  return amd64_posix_patch(def);
}

void patch::unpatch_function(patch::patch_data* state) {
  amd64_posix_unpatch(state);
}

#endif