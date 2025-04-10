find_package(cnats CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

add_executable(NATSpector
        main.cpp

        nats/NatsConnection.cpp
        nats/NatsConnection.h
)

target_link_libraries(
        NATSpector
        PRIVATE

        cnats::nats_static
        fmt::fmt
)