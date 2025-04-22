#pragma once
#include <nlohmann/json_fwd.hpp>
#include <memory>
#include <string>
#include <map>
#include <variant>
#include <vector>

namespace NATSpector {

struct DataNode;

using DataArray = std::vector<DataNode>;
using DataObject = std::map<std::string, DataNode>;

struct DataNode {
  using ValueType = std::variant<std::nullptr_t,
                                 bool,
                                 int64_t,
                                 double,
                                 std::string,
                                 DataArray,
                                 DataObject>;

  ValueType value;

  template <typename T> bool is() const {
    return std::holds_alternative<T>(value);
  }

  template <typename T> T &get() { return std::get<T>(value); }

  template <typename T> const T &get() const { return std::get<T>(value); }

  static auto from_json(const nlohmann::json& data) -> DataNode;
};
} // namespace NATSpector
