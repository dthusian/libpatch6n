#include <iostream>
#include <libpatch6n/patch.hh>

void patch_me(int foo) {
  PATCHABLE;
  std::cout << "The number is: " << foo << "\n";
}

void inject_me(int* foo) {
  (*foo) *= 2;
}

int main() {
  std::cout << "libpatch6n tech test\n";
  auto* state = patch::patch_function(patch::function_def {
      .target = (void*) patch_me,
      .n_args = 1,
      .mixins = {
          patch::mixin_def {
              .typ = patch::mixin_type::inject_head,
              .injected = (void*)inject_me
          }
      }
  });
  patch_me(5);
  patch::unpatch_function(state);
  patch_me(5);
  return 0;
}