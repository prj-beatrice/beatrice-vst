// Copyright (c) 2024-2025 Project Beatrice and Contributors

#ifndef BEATRICE_VST_LOAD_H_
#define BEATRICE_VST_LOAD_H_

#include <windows.h>

#include <filesystem>
#include <memory>
#include <string>

namespace beatrice::vst {

inline static auto GetContentsPath() -> std::filesystem::path {
  auto module_path_str = std::string();
  module_path_str.resize(2048);
  auto* hmodule = HMODULE();
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCSTR>(&GetContentsPath), &hmodule);
  const auto siz =
      GetModuleFileNameA(hmodule, std::to_address(module_path_str.begin()),
                         module_path_str.size());
  module_path_str.resize(siz);
  const auto module_path = std::filesystem::path(module_path_str);
  return module_path.parent_path().parent_path();
}

}  // namespace beatrice::vst

#endif  // BEATRICE_VST_LOAD_H_
