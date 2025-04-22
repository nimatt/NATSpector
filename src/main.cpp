#include "DataNode.h"
#include "nats/NatsConnection.h"

#include <chrono>
#include <csignal>
#include <iostream>
#include <nlohmann/json.hpp>
#include <print>
#include <thread>
#include <variant>
#include <fmt/format.h>

namespace {
std::function<void(int)> signal_handler_callback;

auto get_padding(const size_t indent) -> std::string {
  std::string pad(indent * 2, ' ');
  return pad;
}

void print_data(const NATSpector::DataNode &val,
                int indent = 0,
                const std::string &prefix = "");

void print_array(const NATSpector::DataArray &val,
                 const int indent,
                 const std::string &prefix) {

  const auto pad = get_padding(indent);
  const size_t size{val.size()};
  if (size == 0) {
    std::print("{}[]", prefix);
    return;
  }

  const auto complex = std::holds_alternative<std::string>(val.at(0).value) ||
                       std::holds_alternative<NATSpector::DataArray>(
                           val.at(0).value) ||
                       std::holds_alternative<NATSpector::DataObject>(
                           val.at(0).value);
  const auto max_items = complex ? 5 : 15;
  const auto seperator = complex ? fmt::format("\n{}", pad) : ", ";
  if (complex) {
    std::print("\n{}", prefix.empty() ? pad : prefix);
  } else {
    std::print("{}[ ", prefix);
  }

  size_t i{0};
  for (const auto &elem : val) {
    print_data(elem, indent + 1, complex ? "- " : "");
    if (i > max_items) {
      std::print("{}...", seperator);
      break;
    }
    if (++i < size) {
      std::print("{}", seperator);
    }
  }

  if (!complex) {
    std::print(" ]");
  }
}

void print_object(const NATSpector::DataObject &val,
                  const int indent,
                  const std::string &prefix) {

  const auto pad = get_padding(indent);
  const size_t size{val.size()};
  if (size == 0) {
    std::print("{}", "{}");
  } else {
    if (prefix.empty()) {
      std::print("\n");
    }
    size_t i{0};
    for (const auto &[key, value] : val) {
      if (i == 0 && !prefix.empty()) {
        std::print("{}{}: ", prefix, key);
      } else {
        std::print("{}{}: ", pad, key);
      }
      print_data(value, indent + 1);
      if (++i < size) {
        std::print("\n");
      }
    }
  }
}

void print_data(const NATSpector::DataNode &val,
                const int indent,
                const std::string &prefix) {

  std::visit(
      [&]<typename NodeType>(const NodeType &v) {
        using T = std::decay_t<NodeType>;

        if constexpr (std::is_same_v<T, std::nullptr_t>) {
          std::print("{}\033[35mnull\033[0m", prefix);
        } else if constexpr (std::is_same_v<T, bool>) {
          std::print("{}\033[36m{}\033[0m", prefix, v ? "true" : "false");
        } else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<
                               T, double>) {
          std::print("{}\033[32m{}\033[0m", prefix, v);
        } else if constexpr (std::is_same_v<T, std::string>) {
          std::print("{}\033[34m'{}'\033[0m", prefix, v);
        } else if constexpr (std::is_same_v<T, NATSpector::DataArray>) {
          print_array(v, indent, prefix);
        } else if constexpr (std::is_same_v<T, NATSpector::DataObject>) {
          print_object(v, indent, prefix);
        }
      },
      val.value);
}

void signal_handler(const int signal) {
  if (signal_handler_callback) {
    signal_handler_callback(signal);
  }
}

auto print_headers(const NATSpector::NatsMessage &message) -> void {
  if (message.headers.empty()) {
    return;
  }

  std::println("\033[2;1mHeaders: \033[0m");
  for (const auto &[key, values] : message.headers) {
    std::print("  \033[33m{}\033[0m: ", key);
    for (const auto &value : values) {
      std::print("\033[2m{}\033[0m ", value);
    }
    std::println("");
  }
}

auto print_json_data(std::span<const std::byte> data) -> bool {
  try {
    const nlohmann::json json = nlohmann::json::parse(
        std::string{reinterpret_cast<const char *>(data.data()), data.size()});
    std::print("\033[2;1mData (JSON): \033[0m");
    std::string indent{"  "};

    print_data(NATSpector::DataNode::from_json(json), 1);
    std::print("\n");
    return true;
  } catch (...) {
    return false;
  }
}

auto print_data(const NATSpector::NatsMessage &message) -> void {
  if (message.data.empty()) {
    return;
  }

  if (message.data.front() == std::bit_cast<std::byte>('{')) {
    if (print_json_data(message.data)) {
      return;
    }
  }

  std::println("\033[2;1mData: \033[0m");
  std::println("  {}",
               std::string{reinterpret_cast<const char *>(message.data.data()),
                           message.data.size()});
}
} // namespace

int main(int argc, char **argv) {
  std::println("Welcome to NATSpector!");

  NATSpector::NatsConnection connection{};

  const auto result = connection.connect({}, {}, {}, {});

  if (result.has_value()) {
    std::println("Connected to {} ({})", result->url, result->id);
  } else {
    std::println("{}", result.error().reason);
  }

  const auto subject = argc > 1 ? std::string{argv[1]} : ">";

  connection.subscribe(subject,
                       [](const NATSpector::NatsMessage &message) {
                         auto now = std::chrono::floor<std::chrono::seconds>(
                             std::chrono::current_zone()->to_local(
                                 std::chrono::system_clock::now())
                             );
                         std::println(
                             "\n\033[2;1m[{}]\n\033[0m\033[36;1;4m{}\033[0m",
                             std::format("{:%Y-%m-%d %H:%M:%S}", now),
                             message.subject);

                         print_headers(message);
                         print_data(message);
                       });

  std::println("Subscribed to: {}", subject);

  std::jthread service{[&](const std::stop_token &stoppingToken) {
    while (!stoppingToken.stop_requested()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }};

  signal_handler_callback = [&](int) { service.request_stop(); };
  struct sigaction sa{};
  sa.sa_handler = signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);

  service.join();

  return 0;
}