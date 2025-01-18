// Copyright (c) 2024-2025 Project Beatrice and Contributors

#include "common/parameter_state.h"

#include <memory>
#include <string>

namespace beatrice::common {

ParameterState::ParameterState(const ParameterState& rhs) {
  for (const auto& [param_id, value] : rhs.states_) {
    std::visit(
        [this, param_id](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, int> || std::is_same_v<T, double>) {
            // NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
            SetValue(param_id, value);
          } else if constexpr (std::is_same_v<T,  // NOLINT(readability/braces)
                                              std::unique_ptr<std::u8string>>) {
            // NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
            SetValue(param_id, *value);
          } else {
            assert(false);
          }
        },
        value);
  }
}

auto ParameterState::operator=(const ParameterState& rhs) -> ParameterState& {
  states_.clear();
  for (const auto& [param_id, value] : rhs.states_) {
    std::visit(
        [this, param_id](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, int> || std::is_same_v<T, double>) {
            // NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
            SetValue(param_id, value);
          } else if constexpr (std::is_same_v<T,  // NOLINT(readability/braces)
                                              std::unique_ptr<std::u8string>>) {
            // NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
            SetValue(param_id, *value);
          } else {
            assert(false);
          }
        },
        value);
  }
  return *this;
}

void ParameterState::SetDefaultValues(const ParameterSchema& schema) {
  for (const auto& [param_id, param] : schema) {
    std::visit(
        [this, param_id](const auto& param) {
          // NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
          SetValue(param_id, param.GetDefaultValue());
        },
        param);
  }
}

auto ParameterState::GetValue(const ParameterID param_id) const
    -> const ParameterState::Value& {
  return states_.at(param_id);
}

auto ParameterState::Read(std::istream& is) -> ErrorCode {
  while (true) {
    ParameterID param_id;
    int type_index;
    if (is.read(reinterpret_cast<char*>(&param_id), sizeof(ParameterID))
            .eof()) {
      return ErrorCode::kFileTooSmall;
    }
    if (is.read(reinterpret_cast<char*>(&type_index), sizeof(int)).eof()) {
      return ErrorCode::kFileTooSmall;
    }
    switch (type_index) {
      case 0: {
        int value;
        if (is.read(reinterpret_cast<char*>(&value), sizeof(int)).eof()) {
          return ErrorCode::kFileTooSmall;
        }
        SetValue(param_id, value);
        break;
      }
      case 1: {
        double value;
        if (is.read(reinterpret_cast<char*>(&value), sizeof(double)).eof()) {
          return ErrorCode::kFileTooSmall;
        }
        SetValue(param_id, value);
        break;
      }
      case 2: {
        int siz;
        if (is.read(reinterpret_cast<char*>(&siz), sizeof(int)).eof()) {
          return ErrorCode::kFileTooSmall;
        }
        std::u8string value;
        value.resize(siz);
        if (is.read(reinterpret_cast<char*>(value.data()), siz).eof()) {
          return ErrorCode::kFileTooSmall;
        }
        SetValue(param_id, value);
        break;
      }
      default:
        assert(false);
        return ErrorCode::kUnknownError;
    }
    if (is.peek() == EOF && is.eof()) {
      return ErrorCode::kSuccess;
    }
  }
}

auto ParameterState::ReadOrSetDefault(std::istream& is,
                                      const ParameterSchema& schema)
    -> ErrorCode {
  states_.clear();
  SetDefaultValues(schema);
  return Read(is);
}

auto ParameterState::Write(std::ostream& os) const -> ErrorCode {
  for (const auto& [param_id, value] : states_) {
    const auto type_index = static_cast<int>(value.index());
    os.write(reinterpret_cast<const char*>(&param_id), sizeof(ParameterID));
    os.write(reinterpret_cast<const char*>(&type_index), sizeof(int));
    if (const auto* const p = std::get_if<int>(&value)) {
      os.write(reinterpret_cast<const char*>(p), sizeof(int));
    } else if (const auto* const p = std::get_if<double>(&value)) {
      os.write(reinterpret_cast<const char*>(p), sizeof(double));
    } else if (const auto* const pp =
                   std::get_if<std::unique_ptr<std::u8string>>(&value)) {
      const auto& p = *pp;
      const auto siz = static_cast<int>(p->size());
      os.write(reinterpret_cast<const char*>(&siz), sizeof(int));
      os.write(reinterpret_cast<const char*>(p->c_str()), siz);
    } else {
      assert(false);
    }
  }
  return ErrorCode::kSuccess;
}
}  // namespace beatrice::common
