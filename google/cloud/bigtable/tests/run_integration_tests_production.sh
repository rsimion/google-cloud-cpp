#!/usr/bin/env bash
# Copyright 2018 Google Inc.
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

set -eu

# Only run the data integration tests against production. The CI builds go over
# the bigtable quota if we run all of them. The CI build environment defines
# PROJECT_ID and INSTANCE_ID.
echo
echo "Running bigtable::Table integration test."
./data_integration_test "${PROJECT_ID}" "${INSTANCE_ID}"

echo
echo "Running bigtable::Filters integration tests."
./filters_integration_test "${PROJECT_ID}" "${INSTANCE_ID}"

echo
echo "Running Mutation (e.g. DeleteFromColumn, SetCell) integration tests."
./mutations_integration_test "${PROJECT_ID}" "${INSTANCE_ID}"

echo
echo "Running Table::Async* integration test."
./data_async_integration_test "${PROJECT_ID}" "${INSTANCE_ID}"
