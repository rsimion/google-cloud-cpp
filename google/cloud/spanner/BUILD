# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

package(default_visibility = ["//visibility:public"])

licenses(["notice"])  # Apache 2.0

load(
    ":spanner_client_version.bzl",
    "SPANNER_CLIENT_VERSION_MAJOR",
    "SPANNER_CLIENT_VERSION_MINOR",
    "SPANNER_CLIENT_VERSION_PATCH",
)

genrule(
    name = "generate_version_info",
    srcs = ["version_info.h.in"],
    outs = ["version_info.h"],
    cmd = """
sed -e 's/@SPANNER_CLIENT_VERSION_MAJOR@/%s/' \
    -e 's/@SPANNER_CLIENT_VERSION_MINOR@/%s/' \
    -e 's/@SPANNER_CLIENT_VERSION_PATCH@/%s/' < $< > $@
""" % (
        SPANNER_CLIENT_VERSION_MAJOR,
        SPANNER_CLIENT_VERSION_MINOR,
        SPANNER_CLIENT_VERSION_PATCH,
    ),
)

load(":spanner_client.bzl", "spanner_client_hdrs", "spanner_client_srcs")

cc_library(
    name = "spanner_client",
    srcs = spanner_client_srcs,
    hdrs = spanner_client_hdrs + ["version_info.h"],
    deps = [
        "//google/cloud:google_cloud_cpp_common",
        "@com_github_googleapis_googleapis//:spanner_protos",
    ],
)

load(":spanner_client_unit_tests.bzl", "spanner_client_unit_tests")

[cc_test(
    name = "spanner_client_" + test.replace("/", "_").replace(".cc", ""),
    srcs = [test],
    deps = [
        ":spanner_client",
        "//google/cloud:google_cloud_cpp_common",
        "//google/cloud:google_cloud_cpp_testing",
        "@com_google_googletest//:gtest",
    ],
) for test in spanner_client_unit_tests]

cc_binary(
    name = "spanner_tool",
    srcs = ["spanner_tool.cc"],
    deps = [
        ":spanner_client",
    ],
)
