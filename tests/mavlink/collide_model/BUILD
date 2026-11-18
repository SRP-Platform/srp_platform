# Aplikacja z uslugą o msg_id zajętym przez inną definicję (peer_model: Example.Status = 42000): MavlinkDaemon
# musi odrzucic jej rejestracje (kConflictMsgId). Ten sam kod peera co //tests/mavlink:mavlink_peer.
load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "ara_runtime_lib")

ara_runtime_lib(
    name = "collide_runtime",
    model_src = [
        "app_config.json",
        "service_Clash.json",
    ],
)

cc_binary(
    name = "mavlink_peer_collide",
    testonly = True,
    srcs = ["//tests/mavlink:peer/mavlink_peer.cc"],
    visibility = ["//tests/mavlink:__pkg__"],
    deps = [
        ":collide_runtime",
        "@com_json//:json",
        "@srp_platform//ara/com:com_error_domain",
        "@srp_platform//ara/com/mavlink/client:daemon_client",
    ],
)
