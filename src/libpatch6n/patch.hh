
#ifndef PATCH6N_PATCH_HH
#define PATCH6N_PATCH_HH

#include <map>
#include <cstdint>
#include <unordered_set>
#include <vector>
#include "patchable.hh"

namespace patch {
  enum class mixin_type {
    inject_head,
    inject_ret,
    replace,
  };

  struct mixin_def {
    mixin_type typ;
    void* injected;
  };

  struct function_def {
    void* target;
    int n_args;
    std::vector<mixin_def> mixins;
  };

  struct patch_data;

  patch_data* patch_function(const function_def&);
  void unpatch_function(patch_data*);
}

#endif //PATCH6N_PATCH_HH
