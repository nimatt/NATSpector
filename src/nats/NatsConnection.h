#pragma once

#include <condition_variable>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <nats/nats.h>

namespace NATSpector {
struct NatsServer {
  std::string id{};
  std::string url{};
};

struct NatsConnectionFailure {
  natsStatus status;
  std::string reason;
};

class NatsConnection {
public:
  explicit NatsConnection();
  NatsConnection(const NatsConnection &) = delete;
  NatsConnection(NatsConnection &&other) noexcept = delete;
  auto operator=(const NatsConnection &) -> NatsConnection & = delete;
  auto operator=(NatsConnection &&other) noexcept -> NatsConnection & = delete;

  ~NatsConnection();

  auto connect(std::string url,
               std::filesystem::path caCertificatePath,
               std::filesystem::path clientCertificatePath,
               std::filesystem::path clientCertificateKeyPath)
      -> std::expected<NatsServer, NatsConnectionFailure>;

  [[nodiscard]] auto is_valid() const -> bool;

private:
  explicit NatsConnection(natsStatus initialStatus);
  explicit NatsConnection(
      std::unique_ptr<natsConnection, std::function<void(natsConnection *)>>
          &&connection,
      natsStatus initialStatus);

  static void close_handler(natsConnection *conn, void *closure);

  natsStatus status_;
  std::unique_ptr<natsConnection, std::function<void(natsConnection *)>>
      connection_;
  std::unique_ptr<natsOptions, std::function<void(natsOptions *)>> options_;

  bool closed{false};

  std::mutex mtx{};
  std::condition_variable cv{};
};
} // namespace NATSpector
