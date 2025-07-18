load("//tensorflow:strict.default.bzl", "py_strict_library")
load("//tensorflow:tensorflow.default.bzl", "tf_custom_op_py_strict_library", "tf_py_strict_test")

package(
    # copybara:uncomment default_applicable_licenses = ["//tensorflow:license"],
    default_visibility = [
        "//tensorflow:internal",
    ],
    licenses = ["notice"],
)

py_strict_library(
    name = "failure_handling_lib",
    srcs = [
        "failure_handling.py",
    ],
    deps = [
        ":check_preemption_py",
        ":failure_handling_util",
        "//tensorflow/python/checkpoint",
        "//tensorflow/python/checkpoint:checkpoint_context",
        "//tensorflow/python/checkpoint:checkpoint_management",
        "//tensorflow/python/distribute:distribute_lib",
        "//tensorflow/python/distribute:multi_worker_util",
        "//tensorflow/python/eager:context",
        "//tensorflow/python/framework:constant_op",
        "//tensorflow/python/framework:dtypes",
        "//tensorflow/python/framework:errors",
        "//tensorflow/python/lib/io:file_io",
        "//tensorflow/python/ops:variables",
        "//tensorflow/python/platform:gfile",
        "//tensorflow/python/platform:tf_logging",
        "//tensorflow/python/util:deprecation",
        "//tensorflow/python/util:tf_contextlib",
        "//tensorflow/python/util:tf_export",
        "//tensorflow/tools/docs:doc_controls",
    ],
)

py_strict_library(
    name = "failure_handling_util",
    srcs = [
        "failure_handling_util.py",
    ],
    deps = [
        "//tensorflow/python/eager:context",
        "//tensorflow/python/platform:tf_logging",
        "@pypi_requests//:pkg",
        "@six_archive//:six",
    ],
)

py_strict_library(
    name = "preemption_watcher",
    srcs = ["preemption_watcher.py"],
    deps = [
        ":failure_handling_util",
        "//tensorflow/python/eager:context",
        "//tensorflow/python/eager:monitoring",
        "//tensorflow/python/framework:errors",
        "//tensorflow/python/util:tf_export",
        "@absl_py//absl/logging",
    ],
)

tf_py_strict_test(
    name = "failure_handler_test",
    timeout = "long",
    srcs = ["failure_handler_test.py"],
    shard_count = 8,
    tags = [
        "no_mac",
        "no_pip",  # TODO(b/266520226)
        "no_windows",  # TODO(b/197981388)
    ],
    deps = [
        ":failure_handling_lib",
        ":failure_handling_util",
        "//tensorflow/python/checkpoint",
        "//tensorflow/python/checkpoint:checkpoint_management",
        "//tensorflow/python/distribute:collective_all_reduce_strategy",
        "//tensorflow/python/distribute:combinations",
        "//tensorflow/python/distribute:distribute_lib",
        "//tensorflow/python/distribute:mirrored_strategy",
        "//tensorflow/python/distribute:multi_process_runner",
        "//tensorflow/python/distribute:multi_worker_test_base",
        "//tensorflow/python/distribute:multi_worker_util",
        "//tensorflow/python/distribute:one_device_strategy",
        "//tensorflow/python/distribute:test_util",
        "//tensorflow/python/eager:def_function",
        "//tensorflow/python/framework:config",
        "//tensorflow/python/framework:constant_op",
        "//tensorflow/python/framework:errors",
        "//tensorflow/python/module",
        "//tensorflow/python/ops:variables",
        "//tensorflow/python/platform:client_testlib",
        "//tensorflow/python/platform:gfile",
        "//tensorflow/python/platform:tf_logging",
        "//tensorflow/python/training:server_lib",
        "@absl_py//absl/testing:parameterized",
    ],
)

tf_py_strict_test(
    name = "gce_failure_handler_test",
    srcs = ["gce_failure_handler_test.py"],
    shard_count = 32,
    tags = [
        "no_mac",  # Fails on CI but works fine locally.
        "no_pip",  # TODO(b/266520226)
        "noasan",  # TODO(b/226154233): Flaky test
    ],
    deps = [
        ":failure_handling_lib",
        ":failure_handling_util",
        "//tensorflow/python/checkpoint",
        "//tensorflow/python/checkpoint:checkpoint_management",
        "//tensorflow/python/distribute:collective_all_reduce_strategy",
        "//tensorflow/python/distribute:combinations",
        "//tensorflow/python/distribute:mirrored_strategy",
        "//tensorflow/python/distribute:multi_process_runner",
        "//tensorflow/python/distribute:multi_worker_test_base",
        "//tensorflow/python/distribute:multi_worker_util",
        "//tensorflow/python/distribute:one_device_strategy",
        "//tensorflow/python/distribute:test_util",
        "//tensorflow/python/eager:def_function",
        "//tensorflow/python/framework:config",
        "//tensorflow/python/framework:constant_op",
        "//tensorflow/python/module",
        "//tensorflow/python/ops:variables",
        "//tensorflow/python/platform:client_testlib",
        "//tensorflow/python/platform:gfile",
        "//tensorflow/python/platform:tf_logging",
        "//tensorflow/python/training:server_lib",
        "@absl_py//absl/testing:parameterized",
        "@pypi_dill//:pkg",  # build_cleaner: keep
    ],
)

tf_custom_op_py_strict_library(
    name = "check_preemption_py",
    kernels = [
        "//tensorflow/core/distributed_runtime/preemption:check_preemption_op_kernel",
        "//tensorflow/core/distributed_runtime/preemption:check_preemption_op_op_lib",
    ],
    deps = [
        "//tensorflow/core/distributed_runtime/preemption:gen_check_preemption_op",
        "//tensorflow/python/framework:for_generated_wrappers",
    ],
)
