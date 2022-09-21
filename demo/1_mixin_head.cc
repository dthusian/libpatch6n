#include <iostream>
#include <libpatch6n/patch.hh>

void patch_me(int foo) {
  PATCHABLE;
  std::cout << "target: The number is: " << foo << "\n";
}

void inject_me(int* foo) {
  std::cout << "mixin: recieved number: " << *foo << "\n";
  (*foo) *= 2;
  std::cout << "mixin: The number is now: " << *foo << "\n";
}

int main() {
  std::cout << "Demo 1: Mixin Head\n";
  auto* state = patch::patch_function(patch::function_def {
      .target = (void*) patch_me,
      .args = {
          patch::arg_type::integer
      },
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