#include "./NatsConnection.h"

#include <fmt/format.h>
#include <mutex>
#include <nats/nats.h>

namespace NATSpector {
namespace {
std::filesystem::path get_env_variable_path(const std::string &name) {
  if (const char *envValue = std::getenv(name.c_str())) {
    return std::filesystem::path{envValue};
  }
  return std::filesystem::path{}; // Environment variable not set
}

void on_message(natsConnection *,
               natsSubscription *,
               natsMsg *msg,
               void *closure) {
  if (closure != nullptr) {
    const auto handler = static_cast<NatsMessageHandler *>(closure);

    (*handler)(NatsMessage{.topic = natsMsg_GetSubject(msg)});
  }

  natsMsg_Destroy(msg);
}
} // namespace

NatsSubscription::NatsSubscription(
    std::unique_ptr<natsSubscription, std::function<void(natsSubscription *)>>
        &&subscription,
    std::string topic,
    NatsMessageHandler &&handler)
    : topic{std::move(topic)}, handler{std::move(handler)},
      subscription_{std::move(subscription)} {}

NatsConnection::NatsConnection() : NatsConnection{NATS_OK} {}

NatsConnection::NatsConnection(const natsStatus initialStatus)
    : status_{initialStatus}, connection_{nullptr, natsConnection_Destroy},
      options_{nullptr, natsOptions_Destroy} {}

NatsConnection::NatsConnection(
    std::unique_ptr<natsConnection, std::function<void(natsConnection *)>>
        &&connection,
    const natsStatus initialStatus)
    : NatsConnection(initialStatus) {
  connection_ = std::move(connection);
}

void NatsConnection::close_handler(natsConnection *conn, void *closure) {
  auto *connection = static_cast<NatsConnection *>(closure);
  std::lock_guard<std::mutex> lock(connection->mtx);
  connection->closed = true;
  connection->cv.notify_one();
}

auto NatsConnection::connect(std::string url,
                             std::filesystem::path caCertificatePath,
                             std::filesystem::path clientCertificatePath,
                             std::filesystem::path clientCertificateKeyPath)
    -> std::expected<NatsServer, NatsConnectionFailure> {
  if (url.empty()) {
    url = std::string{"nats://127.0.0.1:4222"};
  }
  if (caCertificatePath.empty()) {
    caCertificatePath = get_env_variable_path("NATS_CA");
  }
  if (clientCertificatePath.empty()) {
    clientCertificatePath = get_env_variable_path("NATS_CERT");
  }
  if (clientCertificateKeyPath.empty()) {
    clientCertificateKeyPath = get_env_variable_path("NATS_KEY");
  }

  status_ = natsOptions_Create(std::inout_ptr(options_));
  if (status_ != NATS_OK) {
    return std::unexpected<NatsConnectionFailure>(
        {status_,
         fmt::format("Error creating NATS options: {}",
                     natsStatus_GetText(status_))});
  }

  natsOptions_SetAllowReconnect(options_.get(), true);
  natsOptions_SetMaxReconnect(options_.get(), -1);
  natsOptions_SetReconnectWait(options_.get(), 1000);

  status_ = natsOptions_SetClosedCB(options_.get(), close_handler, this);
  if (status_ != NATS_OK) {
    throw std::runtime_error(fmt::format("Error setting closed callback: {}",
                                         natsStatus_GetText(status_)));
  }

  // Set the server URL (update with your mTLS-enabled NATS server URL)
  status_ = natsOptions_SetURL(options_.get(), url.c_str());
  if (status_ != NATS_OK) {
    return std::unexpected<NatsConnectionFailure>(
        {status_,
         fmt::format("Error setting NATS server URL: {}",
                     natsStatus_GetText(status_))});
  }

  // Configure mTLS
  status_ = natsOptions_SetSecure(options_.get(), true);
  if (status_ != NATS_OK) {
    return std::unexpected<NatsConnectionFailure>(
        {status_,
         fmt::format("Error enabling TLS: {}", natsStatus_GetText(status_))});
  }

  // Set CA certificate for server verification
  status_ = natsOptions_LoadCATrustedCertificates(options_.get(),
                                                  caCertificatePath.c_str());
  if (status_ != NATS_OK) {
    return std::unexpected<NatsConnectionFailure>(
        {status_,
         fmt::format("Error loading CA certificates: {}",
                     natsStatus_GetText(status_))});
  }

  // Set client certificate and private key for mTLS
  status_ = natsOptions_LoadCertificatesChain(options_.get(),
                                              clientCertificatePath.c_str(),
                                              clientCertificateKeyPath.c_str());
  if (status_ != NATS_OK) {
    return std::unexpected<NatsConnectionFailure>(
        {status_,
         fmt::format("Error setting client certificate and key: {}",
                     natsStatus_GetText(status_))});
  }

  status_ = natsConnection_Connect(std::inout_ptr(connection_), options_.get());
  if (status_ != NATS_OK) {
    return std::unexpected<NatsConnectionFailure>(
        {status_,
         fmt::format("Error connecting to NATS server: {}",
                     natsStatus_GetText(status_))});
  }

  std::array<char, 100> idBuffer{};
  natsConnection_GetConnectedServerId(
      connection_.get(), idBuffer.data(), idBuffer.size());
  std::array<char, 100> urlBuffer{};
  natsConnection_GetConnectedUrl(
      connection_.get(), urlBuffer.data(), urlBuffer.size());

  return NatsServer{
      .id = std::string{idBuffer.data()},
      .url = std::string{urlBuffer.data()},
  };
}

NatsConnection::~NatsConnection() {
  if (connection_ == nullptr) {
    return;
  }
  if (status_ != NATS_OK) {
    return;
  }

  natsConnection_Drain(connection_.get());

  // Wait until the close handler notifies that the connection_ is closed
  std::unique_lock<std::mutex> lock(mtx);
  cv.wait(lock, [this] { return closed; });

  connection_ = nullptr;
  options_ = nullptr;
}

auto NatsConnection::is_valid() const -> bool {
  return !closed && status_ == NATS_OK && connection_ != nullptr;
}

auto NatsConnection::subscribe(const std::string &topic,
                               NatsMessageHandler handler) -> void {
  if (!is_valid()) {
    throw std::invalid_argument("Invalid connection");
  }

  auto subscription = std::make_shared<NatsSubscription>(
      std::unique_ptr<natsSubscription,
                      std::function<void(natsSubscription *)>>{
          nullptr, natsSubscription_Destroy},
      topic,
      std::move(handler));
  natsConnection_Subscribe(std::out_ptr(subscription->subscription_),
                           connection_.get(),
                           topic.c_str(),
                           on_message,
                           &subscription->handler);

  subscriptions_.push_back(std::move(subscription));
}
} // namespace NATSpector
