#include "DataNode.h"

#include <nlohmann/json.hpp>

namespace NATSpector {
auto DataNode::from_json(const nlohmann::json &data) -> DataNode { // NOLINT(*-no-recursion)
  if (data.is_null()) {
    return { nullptr };
  }
  if (data.is_boolean()) {
    return { data.get<bool>() };
  }
  if (data.is_number_integer()) {
    return { data.get<int64_t>() };
  }
  if (data.is_number_float()) {
    return { data.get<double>() };
  }
  if (data.is_string()) {
    return { data.get<std::string>() };
  }
  if (data.is_array()) {
    DataArray arr;
    for (auto& el : data) {
      arr.push_back(from_json(el));
    }
    return { std::move(arr) };
  }
  if (data.is_object()) {
    DataObject obj;
    for (auto& [key, val] : data.items()) {
      obj.insert_or_assign(key, from_json(val));
    }
    return { std::move(obj) };
  }
  throw std::runtime_error("Unknown JSON type");
}
} // namespace NATSpector
