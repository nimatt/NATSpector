#include "nats/NatsConnection.h"

#include <print>
#include <csignal>
#include <thread>

std::function<void(int)> signalHandlerCallback;

void signalHandler(const int signal) {
  if (signalHandlerCallback) {
    signalHandlerCallback(signal);
  }
}

int main() {
  std::println("Welcome to NATSpector!");

  NATSpector::NatsConnection connection{};

  const auto result = connection.connect({}, {}, {}, {});

  if (result.has_value()) {
    std::println("Connected to {} ({})", result->url, result->id);
  } else {
    std::println("{}", result.error().reason);
  }

  connection.subscribe(">", [](const NATSpector::NatsMessage &message) {
    std::println("message: {}", message.topic);
  });

  std::jthread service{[&](const std::stop_token &stoppingToken) {
    while (!stoppingToken.stop_requested()) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }};

  signalHandlerCallback = [&](int) { service.request_stop(); };
  struct sigaction sa {};
  sa.sa_handler = signalHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);

  service.join();

  return 0;
}
