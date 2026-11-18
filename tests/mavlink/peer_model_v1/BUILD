# Peer v1: ten sam kod peera co //tests/mavlink:mavlink_peer, model z ID wiadomosci <= 255 (v1; ara_runtime_lib ma stala nazwe
# regule, stad osobny pakiet). Wersje MAVLink lacza ustawia config daemona (version: 1).
load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "ara_runtime_lib")

ara_runtime_lib(
    name = "peer_runtime_v1",
    model_src = [
        "app_config.json",
        "service_Example.json",
        "type_Info.json",
        "type_ModeIn.json",
        "type_ModeOut.json",
        "type_NameData.json",
        "type_StatusData.json",
    ],
)

cc_binary(
    name = "mavlink_peer_v1",
    srcs = ["//tests/mavlink:peer/mavlink_peer.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":peer_runtime_v1",
        "@com_json//:json",
        "@srp_platform//ara/com:com_error_domain",
        "@srp_platform//ara/com/mavlink/client:daemon_client",
    ],
)
