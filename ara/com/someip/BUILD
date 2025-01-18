cc_library(
    name = "someip_lib",
    srcs = [
        "event_proxy.cc",
        "method_proxy.cc",
        "service_skeleton.cc",
        "someip_handler.cc",
    ],
    hdrs = [
        "event_proxy.h",
        "event_skeleton.h",
        "method_proxy.h",
        "service_proxy.h",
        "service_skeleton.h",
        "someip_handler.h",
    ],
    visibility = ["@srp_platform//visibility:public"],
    deps = [
        "@srp_platform//ara/com:com_error_domain",
        "@srp_platform//ara/com/someip:someip_data",
        "@srp_platform//ara/com/someip/bindings:bindings_lib",
        "@srp_platform//ara/core:Result",
        "@srp_platform//ara/core:instance_specifier",
        "@srp_platform//ara/core:model_db_lib",
        "@srp_platform//core/data:data_conv_lib",
    ],
)

cc_library(
    name = "someip_data",
    srcs = [
        "someip_frame.cc",
        "someip_frame_builder.cc",
        "someip_sd_frame_builder.cc",
    ],
    hdrs = [
        "EndpointOption.h",
        "EventEntry.h",
        "HeaderStructure.h",
        "ServiceEntry.h",
        "network_endpoint.h",
        "someip_frame.h",
        "someip_frame_builder.h",
        "someip_sd_frame_builder.h",
    ],
    visibility = ["@srp_platform//visibility:public"],
    deps = [
        "@srp_platform//ara/com:com_error_domain",
        "@srp_platform//ara/com/someip:someip_enums",
        "@srp_platform//ara/core:Result",
        "@srp_platform//ara/core:instance_specifier",
        "@srp_platform//core/data:data_conv_lib",
    ],
)

cc_library(
    name = "someip_enums",
    hdrs = [
        "message_code.h",
        "message_type.h",
    ],
    visibility = ["@srp_platform//visibility:public"],
)
