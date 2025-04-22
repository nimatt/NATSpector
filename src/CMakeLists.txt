find_package(cnats CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(natspector
        main.cpp

        nats/NatsConnection.cpp
        nats/NatsConnection.h
        DataNode.h
        DataNode.cpp
)

target_link_libraries(
        natspector
        PRIVATE

        cnats::nats_static
        fmt::fmt
        nlohmann_json::nlohmann_json
)