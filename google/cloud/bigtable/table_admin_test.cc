// Copyright 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "google/cloud/bigtable/table_admin.h"
#include "google/cloud/bigtable/grpc_error.h"
#include "google/cloud/bigtable/testing/mock_admin_client.h"
#include "google/cloud/internal/make_unique.h"
#include "google/cloud/status_or.h"
#include "google/cloud/testing_util/assert_ok.h"
#include "google/cloud/testing_util/chrono_literals.h"
#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>
#include <gmock/gmock.h>
#include <chrono>

namespace {
namespace btadmin = ::google::bigtable::admin::v2;
namespace bigtable = google::cloud::bigtable;
using namespace google::cloud::testing_util::chrono_literals;
using MockAdminClient = bigtable::testing::MockAdminClient;

std::string const kProjectId = "the-project";
std::string const kInstanceId = "the-instance";
std::string const kClusterId = "the-cluster";

/// A fixture for the bigtable::TableAdmin tests.
class TableAdminTest : public ::testing::Test {
 protected:
  void SetUp() override {
    using namespace ::testing;

    EXPECT_CALL(*client_, project()).WillRepeatedly(ReturnRef(kProjectId));
  }

  std::shared_ptr<MockAdminClient> client_ =
      std::make_shared<MockAdminClient>();
};

// A lambda to create lambdas.  Basically we would be rewriting the same
// lambda twice without this thing.
auto create_list_tables_lambda = [](std::string expected_token,
                                    std::string returned_token,
                                    std::vector<std::string> table_names) {
  return
      [expected_token, returned_token, table_names](
          grpc::ClientContext* ctx, btadmin::ListTablesRequest const& request,
          btadmin::ListTablesResponse* response) {
        auto const instance_name =
            "projects/" + kProjectId + "/instances/" + kInstanceId;
        EXPECT_EQ(instance_name, request.parent());
        EXPECT_EQ(btadmin::Table::FULL, request.view());
        EXPECT_EQ(expected_token, request.page_token());

        EXPECT_NE(nullptr, response);
        for (auto const& table_name : table_names) {
          auto& table = *response->add_tables();
          table.set_name(instance_name + "/tables/" + table_name);
          table.set_granularity(btadmin::Table::MILLIS);
        }
        // Return the right token.
        response->set_next_page_token(returned_token);
        return grpc::Status::OK;
      };
};

// A lambda to generate snapshot list.
auto create_list_snapshots_lambda =
    [](std::string expected_token, std::string returned_token,
       std::vector<std::string> snapshot_names) {
      return [expected_token, returned_token, snapshot_names](
                 grpc::ClientContext* ctx,
                 btadmin::ListSnapshotsRequest const& request,
                 btadmin::ListSnapshotsResponse* response) {
        auto cluster_name =
            "projects/" + kProjectId + "/instances/" + kInstanceId;
        cluster_name += "/clusters/" + kClusterId;
        EXPECT_EQ(cluster_name, request.parent());
        EXPECT_EQ(expected_token, request.page_token());

        EXPECT_NE(nullptr, response);
        for (auto const& snapshot_name : snapshot_names) {
          auto& snapshot = *response->add_snapshots();
          snapshot.set_name(cluster_name + "/snapshots/" + snapshot_name);
        }
        // Return the right token.
        response->set_next_page_token(returned_token);
        return grpc::Status::OK;
      };
    };

/**
 * Helper class to create the expectations for a simple RPC call.
 *
 * Given the type of the request and responses, this struct provides a function
 * to create a mock implementation with the right signature and checks.
 *
 * @tparam RequestType the protobuf type for the request.
 * @tparam ResponseType the protobuf type for the response.
 */
template <typename RequestType, typename ResponseType>
struct MockRpcFactory {
  using SignatureType = grpc::Status(grpc::ClientContext* ctx,
                                     RequestType const& request,
                                     ResponseType* response);

  /// Refactor the boilerplate common to most tests.
  static std::function<SignatureType> Create(std::string expected_request) {
    return std::function<SignatureType>(
        [expected_request](grpc::ClientContext* ctx, RequestType const& request,
                           ResponseType* response) {
          if (response == nullptr) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                "invalid call to MockRpcFactory::Create()");
          }
          RequestType expected;
          // Cannot use ASSERT_TRUE() here, it has an embedded "return;"
          EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
              expected_request, &expected));
          std::string delta;
          google::protobuf::util::MessageDifferencer differencer;
          differencer.ReportDifferencesToString(&delta);
          EXPECT_TRUE(differencer.Compare(expected, request)) << delta;

          return grpc::Status::OK;
        });
  }
};

/**
 * Helper class to create the expectations and check consistency over
 * multiple calls for a simple RPC call.
 *
 * Given the type of the request and responses, this struct provides a function
 * to create a mock implementation with the right signature and checks.
 *
 * @tparam RequestType the protobuf type for the request.
 * @tparam ResponseType the protobuf type for the response.
 */
template <typename RequestType, typename ResponseType>
struct MockRpcMultiCallFactory {
  using SignatureType = grpc::Status(grpc::ClientContext* ctx,
                                     RequestType const& request,
                                     ResponseType* response);

  /// Refactor the boilerplate common to most tests.
  static std::function<SignatureType> Create(std::string expected_request,
                                             bool expected_result) {
    return std::function<SignatureType>(
        [expected_request, expected_result](grpc::ClientContext* ctx,
                                            RequestType const& request,
                                            ResponseType* response) {
          if (response == nullptr) {
            return grpc::Status(
                grpc::StatusCode::INVALID_ARGUMENT,
                "invalid call to MockRpcMultiCallFactory::Create()");
          }
          RequestType expected;
          response->clear_consistent();
          // Cannot use ASSERT_TRUE() here, it has an embedded "return;"
          EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
              expected_request, &expected));
          std::string delta;
          google::protobuf::util::MessageDifferencer differencer;
          differencer.ReportDifferencesToString(&delta);
          EXPECT_TRUE(differencer.Compare(expected, request)) << delta;

          response->set_consistent(expected_result);

          return grpc::Status::OK;
        });
  }
};

}  // anonymous namespace

/// @test Verify basic functionality in the `bigtable::TableAdmin` class.
TEST_F(TableAdminTest, Default) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_EQ("the-instance", tested.instance_id());
  EXPECT_EQ("projects/the-project/instances/the-instance",
            tested.instance_name());
}

/// @test Verify that `bigtable::TableAdmin::ListTables` works in the easy case.
TEST_F(TableAdminTest, ListTables) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, kInstanceId);
  auto mock_list_tables = create_list_tables_lambda("", "", {"t0", "t1"});
  EXPECT_CALL(*client_, ListTables(_, _, _)).WillOnce(Invoke(mock_list_tables));

  // After all the setup, make the actual call we want to test.
  auto actual = tested.ListTables(btadmin::Table::FULL);
  ASSERT_STATUS_OK(actual);
  auto const& v = *actual;
  std::string instance_name = tested.instance_name();
  ASSERT_EQ(2UL, v.size());
  EXPECT_EQ(instance_name + "/tables/t0", v[0].name());
  EXPECT_EQ(instance_name + "/tables/t1", v[1].name());
}

/// @test Verify that `bigtable::TableAdmin::ListTables` handles failures.
TEST_F(TableAdminTest, ListTablesRecoverableFailures) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  auto mock_recoverable_failure = [](grpc::ClientContext* ctx,
                                     btadmin::ListTablesRequest const& request,
                                     btadmin::ListTablesResponse* response) {
    return grpc::Status(grpc::StatusCode::UNAVAILABLE, "try-again");
  };
  auto batch0 = create_list_tables_lambda("", "token-001", {"t0", "t1"});
  auto batch1 = create_list_tables_lambda("token-001", "", {"t2", "t3"});
  EXPECT_CALL(*client_, ListTables(_, _, _))
      .WillOnce(Invoke(mock_recoverable_failure))
      .WillOnce(Invoke(batch0))
      .WillOnce(Invoke(mock_recoverable_failure))
      .WillOnce(Invoke(mock_recoverable_failure))
      .WillOnce(Invoke(batch1));

  // After all the setup, make the actual call we want to test.
  auto actual = tested.ListTables(btadmin::Table::FULL);
  ASSERT_STATUS_OK(actual);
  auto const& v = *actual;
  std::string instance_name = tested.instance_name();
  ASSERT_EQ(4UL, v.size());
  EXPECT_EQ(instance_name + "/tables/t0", v[0].name());
  EXPECT_EQ(instance_name + "/tables/t1", v[1].name());
  EXPECT_EQ(instance_name + "/tables/t2", v[2].name());
  EXPECT_EQ(instance_name + "/tables/t3", v[3].name());
}

/**
 * @test Verify that `bigtable::TableAdmin::ListTables` handles unrecoverable
 * failures.
 */
TEST_F(TableAdminTest, ListTablesUnrecoverableFailures) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, ListTables(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "uh oh")));

  EXPECT_FALSE(tested.ListTables(btadmin::Table::FULL));
}

/**
 * @test Verify that `bigtable::TableAdmin::ListTables` handles too many
 * recoverable failures.
 */
TEST_F(TableAdminTest, ListTablesTooManyFailures) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;

  bigtable::TableAdmin tested(
      client_, "the-instance", bigtable::LimitedErrorCountRetryPolicy(3),
      bigtable::ExponentialBackoffPolicy(10_ms, 10_min));
  auto mock_recoverable_failure = [](grpc::ClientContext* ctx,
                                     btadmin::ListTablesRequest const& request,
                                     btadmin::ListTablesResponse* response) {
    return grpc::Status(grpc::StatusCode::UNAVAILABLE, "try-again");
  };
  EXPECT_CALL(*client_, ListTables(_, _, _))
      .WillRepeatedly(Invoke(mock_recoverable_failure));

  EXPECT_FALSE(tested.ListTables(btadmin::Table::FULL));
}

/// @test Verify that `bigtable::TableAdmin::Create` works in the easy case.
TEST_F(TableAdminTest, CreateTableSimple) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;

  bigtable::TableAdmin tested(client_, "the-instance");

  std::string expected_text = R"""(
      parent: 'projects/the-project/instances/the-instance'
table_id: 'new-table'
table {
  column_families {
    key: 'f1'
    value { gc_rule { max_num_versions: 1 }}
  }
  column_families {
    key: 'f2'
    value { gc_rule { max_age { seconds: 1 }}}
  }
  granularity: TIMESTAMP_GRANULARITY_UNSPECIFIED
}
initial_splits { key: 'a' }
initial_splits { key: 'c' }
initial_splits { key: 'p' }
)""";
  auto mock_create_table =
      MockRpcFactory<btadmin::CreateTableRequest, btadmin::Table>::Create(
          expected_text);
  EXPECT_CALL(*client_, CreateTable(_, _, _))
      .WillOnce(Invoke(mock_create_table));

  // After all the setup, make the actual call we want to test.
  using GC = bigtable::GcRule;
  bigtable::TableConfig config(
      {{"f1", GC::MaxNumVersions(1)}, {"f2", GC::MaxAge(1_s)}},
      {"a", "c", "p"});
  auto table = tested.CreateTable("new-table", std::move(config));
  EXPECT_STATUS_OK(table);
}

/**
 * @test Verify that `bigtable::TableAdmin::CreateTable` supports
 * only one try and let client know request status.
 */
TEST_F(TableAdminTest, CreateTableFailure) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, CreateTable(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "uh oh")));

  EXPECT_FALSE(tested.CreateTable("other-table", bigtable::TableConfig()));
}

/**
 * @test Verify that Copy Constructor and assignment operator
 * copies all properties.
 */
TEST_F(TableAdminTest, CopyConstructibleAssignableTest) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-copy-instance");
  bigtable::TableAdmin table_admin(tested);

  EXPECT_EQ(tested.instance_id(), table_admin.instance_id());
  EXPECT_EQ(tested.instance_name(), table_admin.instance_name());
  EXPECT_EQ(tested.project(), table_admin.project());

  bigtable::TableAdmin table_admin_assign(client_, "the-assign-instance");
  EXPECT_NE(tested.instance_id(), table_admin_assign.instance_id());
  EXPECT_NE(tested.instance_name(), table_admin_assign.instance_name());

  table_admin_assign = tested;
  EXPECT_EQ(tested.instance_id(), table_admin_assign.instance_id());
  EXPECT_EQ(tested.instance_name(), table_admin_assign.instance_name());
  EXPECT_EQ(tested.project(), table_admin_assign.project());
}

/**
 * @test Verify that Copy Constructor and assignment operator copies
 * all properties including policies applied.
 */
TEST_F(TableAdminTest, CopyConstructibleAssignablePolicyTest) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;

  bigtable::TableAdmin tested(
      client_, "the-construct-instance",
      bigtable::LimitedErrorCountRetryPolicy(3),
      bigtable::ExponentialBackoffPolicy(10_ms, 10_min));
  // Copy Constructor
  bigtable::TableAdmin table_admin(tested);
  // Create New Instance
  bigtable::TableAdmin table_admin_assign(client_, "the-assign-instance");
  // Copy assignable
  table_admin_assign = table_admin;

  EXPECT_CALL(*client_, GetTable(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::UNAVAILABLE, "try-again")));

  EXPECT_FALSE(table_admin.GetTable("other-table"));
  EXPECT_FALSE(table_admin_assign.GetTable("other-table"));
}

/// @test Verify that `bigtable::TableAdmin::GetTable` works in the easy case.
TEST_F(TableAdminTest, GetTableSimple) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;

  bigtable::TableAdmin tested(client_, "the-instance");
  std::string expected_text = R"""(
      name: 'projects/the-project/instances/the-instance/tables/the-table'
      view: SCHEMA_VIEW
)""";
  auto mock = MockRpcFactory<btadmin::GetTableRequest, btadmin::Table>::Create(
      expected_text);
  EXPECT_CALL(*client_, GetTable(_, _, _))
      .WillOnce(
          Return(grpc::Status(grpc::StatusCode::UNAVAILABLE, "try-again")))
      .WillOnce(Invoke(mock));

  // After all the setup, make the actual call we want to test.
  tested.GetTable("the-table");
}

/**
 * @test Verify that `bigtable::TableAdmin::GetTable` reports unrecoverable
 * failures.
 */
TEST_F(TableAdminTest, GetTableUnrecoverableFailures) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, GetTable(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::NOT_FOUND, "uh oh")));

  // After all the setup, make the actual call we want to test.
  EXPECT_FALSE(tested.GetTable("other-table"));
}

/**
 * @test Verify that `bigtable::TableAdmin::GetTable` works with too many
 * recoverable failures.
 */
TEST_F(TableAdminTest, GetTableTooManyFailures) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;

  bigtable::TableAdmin tested(
      client_, "the-instance", bigtable::LimitedErrorCountRetryPolicy(3),
      bigtable::ExponentialBackoffPolicy(10_ms, 10_min));
  EXPECT_CALL(*client_, GetTable(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::UNAVAILABLE, "try-again")));

  // After all the setup, make the actual call we want to test.
  EXPECT_FALSE(tested.GetTable("other-table"));
}

/// @test Verify that bigtable::TableAdmin::DeleteTable works as expected.
TEST_F(TableAdminTest, DeleteTable) {
  using namespace ::testing;
  using google::protobuf::Empty;

  bigtable::TableAdmin tested(client_, "the-instance");
  std::string expected_text = R"""(
      name: 'projects/the-project/instances/the-instance/tables/the-table'
)""";
  auto mock =
      MockRpcFactory<btadmin::DeleteTableRequest, Empty>::Create(expected_text);
  EXPECT_CALL(*client_, DeleteTable(_, _, _)).WillOnce(Invoke(mock));

  // After all the setup, make the actual call we want to test.
  EXPECT_STATUS_OK(tested.DeleteTable("the-table"));
}

/**
 * @test Verify that `bigtable::TableAdmin::DeleteTable` supports
 * only one try and let client know request status.
 */
TEST_F(TableAdminTest, DeleteTableFailure) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, DeleteTable(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "uh oh")));

  // After all the setup, make the actual call we want to test.
  EXPECT_FALSE(tested.DeleteTable("other-table").ok());
}

/**
 * @test Verify that bigtable::TableAdmin::ModifyColumnFamilies works as
 * expected.
 */
TEST_F(TableAdminTest, ModifyColumnFamilies) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;
  using google::protobuf::Empty;

  bigtable::TableAdmin tested(client_, "the-instance");
  std::string expected_text = R"""(
      name: 'projects/the-project/instances/the-instance/tables/the-table'
modifications {
  id: 'foo'
  create { gc_rule { max_age { seconds: 172800 }}}
}
modifications {
  id: 'bar'
  update { gc_rule { max_age { seconds: 86400 }}}
}
)""";
  auto mock = MockRpcFactory<btadmin::ModifyColumnFamiliesRequest,
                             btadmin::Table>::Create(expected_text);
  EXPECT_CALL(*client_, ModifyColumnFamilies(_, _, _)).WillOnce(Invoke(mock));

  // After all the setup, make the actual call we want to test.
  using M = bigtable::ColumnFamilyModification;
  using GC = bigtable::GcRule;
  auto actual = tested.ModifyColumnFamilies(
      "the-table",
      {M::Create("foo", GC::MaxAge(48_h)), M::Update("bar", GC::MaxAge(24_h))});
}

/**
 * @test Verify that `bigtable::TableAdmin::ModifyColumnFamilies` makes only one
 * RPC attempt and reports errors on failure.
 */
TEST_F(TableAdminTest, ModifyColumnFamiliesFailure) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, ModifyColumnFamilies(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "uh oh")));

  using M = bigtable::ColumnFamilyModification;
  using GC = bigtable::GcRule;
  std::vector<M> changes{M::Create("foo", GC::MaxAge(48_h)),
                         M::Update("bar", GC::MaxAge(24_h))};

  EXPECT_FALSE(tested.ModifyColumnFamilies("other-table", std::move(changes)));
}

/// @test Verify that bigtable::TableAdmin::DropRowsByPrefix works as expected.
TEST_F(TableAdminTest, DropRowsByPrefix) {
  using namespace ::testing;
  using google::protobuf::Empty;

  bigtable::TableAdmin tested(client_, "the-instance");
  std::string expected_text = R"""(
      name: 'projects/the-project/instances/the-instance/tables/the-table'
      row_key_prefix: 'foobar'
)""";
  auto mock = MockRpcFactory<btadmin::DropRowRangeRequest, Empty>::Create(
      expected_text);
  EXPECT_CALL(*client_, DropRowRange(_, _, _)).WillOnce(Invoke(mock));

  // After all the setup, make the actual call we want to test.
  EXPECT_STATUS_OK(tested.DropRowsByPrefix("the-table", "foobar"));
}

/**
 * @test Verify that `bigtable::TableAdmin::DropRowsByPrefix` makes only one
 * RPC attempt and reports errors on failure.
 */
TEST_F(TableAdminTest, DropRowsByPrefixFailure) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, DropRowRange(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "uh oh")));

  EXPECT_FALSE(tested.DropRowsByPrefix("other-table", "prefix").ok());
}

/// @test Verify that bigtable::TableAdmin::DropRowsByPrefix works as expected.
TEST_F(TableAdminTest, DropAllRows) {
  using namespace ::testing;
  using google::protobuf::Empty;

  bigtable::TableAdmin tested(client_, "the-instance");
  std::string expected_text = R"""(
      name: 'projects/the-project/instances/the-instance/tables/the-table'
      delete_all_data_from_table: true
)""";
  auto mock = MockRpcFactory<btadmin::DropRowRangeRequest, Empty>::Create(
      expected_text);
  EXPECT_CALL(*client_, DropRowRange(_, _, _)).WillOnce(Invoke(mock));

  // After all the setup, make the actual call we want to test.
  EXPECT_STATUS_OK(tested.DropAllRows("the-table"));
}

/**
 * @test Verify that `bigtable::TableAdmin::DropAllRows` makes only one
 * RPC attempt and reports errors on failure.
 */
TEST_F(TableAdminTest, DropAllRowsFailure) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, DropRowRange(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "uh oh")));

  // After all the setup, make the actual call we want to test.
  EXPECT_FALSE(tested.DropAllRows("other-table").ok());
}

/**
 * @test Verify that `bigtagble::TableAdmin::GenerateConsistencyToken` works as
 * expected.
 */
TEST_F(TableAdminTest, GenerateConsistencyTokenSimple) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  std::string expected_text = R"""(
      name: 'projects/the-project/instances/the-instance/tables/the-table'
)""";
  auto mock = MockRpcFactory<
      btadmin::GenerateConsistencyTokenRequest,
      btadmin::GenerateConsistencyTokenResponse>::Create(expected_text);
  EXPECT_CALL(*client_, GenerateConsistencyToken(_, _, _))
      .WillOnce(Invoke(mock));

  // After all the setup, make the actual call we want to test.
  tested.GenerateConsistencyToken("the-table");
}

/**
 * @test Verify that `bigtable::TableAdmin::GenerateConsistencyToken` makes only
 * one RPC attempt and reports errors on failure.
 */
TEST_F(TableAdminTest, GenerateConsistencyTokenFailure) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, GenerateConsistencyToken(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "uh oh")));

  // After all the setup, make the actual call we want to test.
  EXPECT_FALSE(tested.GenerateConsistencyToken("other-table"));
}

/**
 * @test Verify that `bigtagble::TableAdmin::CheckConsistency` works as
 * expected.
 */
TEST_F(TableAdminTest, CheckConsistencySimple) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  std::string expected_text = R"""(
      name: 'projects/the-project/instances/the-instance/tables/the-table'
      consistency_token: 'test-token'
)""";
  auto mock =
      MockRpcFactory<btadmin::CheckConsistencyRequest,
                     btadmin::CheckConsistencyResponse>::Create(expected_text);
  EXPECT_CALL(*client_, CheckConsistency(_, _, _)).WillOnce(Invoke(mock));

  bigtable::TableId table_id("the-table");
  bigtable::ConsistencyToken consistency_token("test-token");
  // After all the setup, make the actual call we want to test.
  auto result = tested.CheckConsistency(table_id, consistency_token);
  ASSERT_STATUS_OK(result);
}

/**
 * @test Verify that `bigtable::TableAdmin::CheckConsistency` makes only
 * one RPC attempt and reports errors on failure.
 */
TEST_F(TableAdminTest, CheckConsistencyFailure) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, CheckConsistency(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "uh oh")));

  bigtable::TableId table_id("other-table");
  bigtable::ConsistencyToken consistency_token("test-token");
  // After all the setup, make the actual call we want to test.
  EXPECT_FALSE(tested.CheckConsistency(table_id, consistency_token));
}

/**
 * @test Verify that `bigtagble::TableAdmin::CheckConsistency` works as
 * expected, with multiple asynchronous calls.
 */
TEST_F(TableAdminTest, AsyncCheckConsistencySimple) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;

  bigtable::TableAdmin tested(client_, "the-async-instance");
  std::string expected_text = R"""(
      name: 'projects/the-project/instances/the-async-instance/tables/the-async-table'
      consistency_token: 'test-async-token'
)""";

  auto mock_for_false = MockRpcMultiCallFactory<
      btadmin::CheckConsistencyRequest,
      btadmin::CheckConsistencyResponse>::Create(expected_text, false);
  auto mock_for_true = MockRpcMultiCallFactory<
      btadmin::CheckConsistencyRequest,
      btadmin::CheckConsistencyResponse>::Create(expected_text, true);

  EXPECT_CALL(*client_, CheckConsistency(_, _, _))
      .WillOnce(Invoke(mock_for_false))
      .WillOnce(Invoke(mock_for_false))
      .WillOnce(Invoke(mock_for_false))
      .WillOnce(Invoke(mock_for_false))
      .WillOnce(Invoke(mock_for_true));

  bigtable::TableId table_id("the-async-table");
  bigtable::ConsistencyToken consistency_token("test-async-token");
  // After all the setup, make the actual call we want to test.
  std::future<google::cloud::StatusOr<bool>> result =
      tested.WaitForConsistencyCheck(table_id, consistency_token);
  EXPECT_STATUS_OK(result.get());
}

/**
 * @test Verify that `bigtable::TableAdmin::CheckConsistency` makes only
 * one RPC attempt and reports errors on failure.
 */
TEST_F(TableAdminTest, AsyncCheckConsistencyFailure) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;

  bigtable::TableAdmin tested(client_, "the-async-instance");
  EXPECT_CALL(*client_, CheckConsistency(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "uh oh")));

  bigtable::TableId table_id("other-async-table");
  bigtable::ConsistencyToken consistency_token("test-async-token");

  std::future<google::cloud::StatusOr<bool>> result =
      tested.WaitForConsistencyCheck(table_id, consistency_token);
  EXPECT_FALSE(result.get());
}

/**
 * @test Verify that `bigtable::TableAdmin::GetSnapshot` works in the easy case.
 */
TEST_F(TableAdminTest, GetSnapshotSimple) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;

  bigtable::TableAdmin tested(client_, "the-instance");
  std::string expected_text = R"""(
      name: 'projects/the-project/instances/the-instance/clusters/the-cluster/snapshots/random-snapshot'
)""";
  auto mock =
      MockRpcFactory<btadmin::GetSnapshotRequest, btadmin::Snapshot>::Create(
          expected_text);
  EXPECT_CALL(*client_, GetSnapshot(_, _, _))
      .WillOnce(
          Return(grpc::Status(grpc::StatusCode::UNAVAILABLE, "try-again")))
      .WillOnce(Invoke(mock));
  bigtable::ClusterId cluster_id("the-cluster");
  bigtable::SnapshotId snapshot_id("random-snapshot");
  tested.GetSnapshot(cluster_id, snapshot_id);
}

/**
 * @test Verify that `bigtable::TableAdmin::GetSnapshot` reports unrecoverable
 * failures.
 */
TEST_F(TableAdminTest, GetSnapshotUnrecoverableFailures) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, GetSnapshot(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::NOT_FOUND, "No snapshot.")));
  bigtable::ClusterId cluster_id("other-cluster");
  bigtable::SnapshotId snapshot_id("other-snapshot");
  // After all the setup, make the actual call we want to test.
  EXPECT_FALSE(tested.GetSnapshot(cluster_id, snapshot_id));
}

/**
 * @test Verify that `bigtable::TableAdmin::GetSnapshot` works with too many
 * recoverable failures.
 */
TEST_F(TableAdminTest, GetSnapshotTooManyFailures) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;

  bigtable::TableAdmin tested(
      client_, "the-instance", bigtable::LimitedErrorCountRetryPolicy(3),
      bigtable::ExponentialBackoffPolicy(10_ms, 10_min));
  EXPECT_CALL(*client_, GetSnapshot(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::UNAVAILABLE, "try-again")));
  bigtable::ClusterId cluster_id("other-cluster");
  bigtable::SnapshotId snapshot_id("other-snapshot");
  // After all the setup, make the actual call we want to test.
  EXPECT_FALSE(tested.GetSnapshot(cluster_id, snapshot_id));
}

/// @test Verify that bigtable::TableAdmin::DeleteSnapshot works as expected.
TEST_F(TableAdminTest, DeleteSnapshotSimple) {
  using namespace ::testing;
  using google::protobuf::Empty;

  bigtable::TableAdmin tested(client_, "the-instance");
  std::string expected_text = R"""(
      name: 'projects/the-project/instances/the-instance/clusters/the-cluster/snapshots/random-snapshot'
)""";
  auto mock = MockRpcFactory<btadmin::DeleteSnapshotRequest, Empty>::Create(
      expected_text);
  EXPECT_CALL(*client_, DeleteSnapshot(_, _, _)).WillOnce(Invoke(mock));

  // After all the setup, make the actual call we want to test.
  bigtable::ClusterId cluster_id("the-cluster");
  bigtable::SnapshotId snapshot_id("random-snapshot");
  EXPECT_STATUS_OK(tested.DeleteSnapshot(cluster_id, snapshot_id));
}

/**
 * @test Verify that `bigtable::TableAdmin::DeleteSnapshot` supports
 * only one try and let client know request status.
 */
TEST_F(TableAdminTest, DeleteSnapshotFailure) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, DeleteSnapshot(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "uh oh")));
  bigtable::ClusterId cluster_id("other-cluster");
  bigtable::SnapshotId snapshot_id("other-snapshot");

  // After all the setup, make the actual call we want to test.
  EXPECT_FALSE(tested.DeleteSnapshot(cluster_id, snapshot_id).ok());
}

/// @test Verify that bigtable::TableAdmin::SnapshotTable works as expected.
TEST_F(TableAdminTest, SnapshotTableSimple) {
  using ::testing::_;
  using ::testing::Invoke;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, SnapshotTable(_, _, _))
      .WillOnce(Invoke([](grpc::ClientContext*,
                          btadmin::SnapshotTableRequest const& request,
                          google::longrunning::Operation* response) {
        return grpc::Status::OK;
      }));

  std::string expected_text = R"""(
              name: 'projects/the-project/instances/the-instance/clusters/the-cluster/snapshots/random-snapshot'
        )""";

  btadmin::Snapshot expected;
  ASSERT_TRUE(
      google::protobuf::TextFormat::ParseFromString(expected_text, &expected));
  EXPECT_CALL(*client_, GetOperation(_, _, _))
      .WillOnce(Invoke([](grpc::ClientContext*,
                          google::longrunning::GetOperationRequest const&,
                          google::longrunning::Operation* operation) {
        operation->set_done(false);
        return grpc::Status::OK;
      }))
      .WillOnce(Invoke([](grpc::ClientContext*,
                          google::longrunning::GetOperationRequest const&,
                          google::longrunning::Operation* operation) {
        operation->set_done(false);
        return grpc::Status::OK;
      }))
      .WillOnce(Invoke(
          [&expected](grpc::ClientContext*,
                      google::longrunning::GetOperationRequest const& request,
                      google::longrunning::Operation* operation) {
            operation->set_done(true);
            auto any =
                google::cloud::internal::make_unique<google::protobuf::Any>();
            any->PackFrom(expected);
            operation->set_allocated_response(any.release());
            return grpc::Status::OK;
          }));

  bigtable::ClusterId cluster_id("the-cluster");
  bigtable::SnapshotId snapshot_id("random-snapshot");
  bigtable::TableId table_id("the-table");
  auto future = tested.SnapshotTable(cluster_id, snapshot_id, table_id, 100_s);
  auto actual = future.get();
  EXPECT_STATUS_OK(actual);

  std::string delta;
  google::protobuf::util::MessageDifferencer differencer;
  differencer.ReportDifferencesToString(&delta);
  EXPECT_TRUE(differencer.Compare(expected, *actual)) << delta;
}

/// @test Verify that `bigtable::TableAdmin::SnapshotTable` works.
TEST_F(TableAdminTest, SnapshotTableImmediatelyReady) {
  using ::testing::_;
  using ::testing::Invoke;

  bigtable::TableAdmin tested(client_, "the-instance");
  std::string expected_text = R"""(
      name: 'projects/the-project/instances/the-instance/clusters/the-cluster/snapshots/random-snapshot'
)""";

  btadmin::Snapshot expected;
  ASSERT_TRUE(
      google::protobuf::TextFormat::ParseFromString(expected_text, &expected));
  EXPECT_CALL(*client_, SnapshotTable(_, _, _))
      .WillOnce(Invoke([&expected](grpc::ClientContext*,
                                   btadmin::SnapshotTableRequest const& request,
                                   google::longrunning::Operation* response) {
        response->set_done(true);
        response->set_name("operation-name");
        auto any =
            google::cloud::internal::make_unique<google::protobuf::Any>();
        any->PackFrom(expected);
        response->set_allocated_response(any.release());
        return grpc::Status::OK;
      }));

  EXPECT_CALL(*client_, GetOperation(_, _, _)).Times(0);

  bigtable::ClusterId cluster_id("the-cluster");
  bigtable::SnapshotId snapshot_id("random-snapshot");
  bigtable::TableId table_id("the-table");
  auto future = tested.SnapshotTable(cluster_id, snapshot_id, table_id, 100_s);
  auto actual = future.get();
  EXPECT_STATUS_OK(actual);

  std::string delta;
  google::protobuf::util::MessageDifferencer differencer;
  differencer.ReportDifferencesToString(&delta);
  EXPECT_TRUE(differencer.Compare(expected, *actual)) << delta;
}

/// @test Failures while polling in `bigtable::TableAdmin::SnapshotTable`.
TEST_F(TableAdminTest, SnapshotTablePollRecoverableFailures) {
  using ::testing::_;
  using ::testing::Invoke;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, SnapshotTable(_, _, _))
      .WillOnce(Invoke([](grpc::ClientContext*,
                          btadmin::SnapshotTableRequest const& request,
                          google::longrunning::Operation* response) {
        return grpc::Status::OK;
      }));

  std::string expected_text = R"""(
      name: 'projects/the-project/instances/the-instance/clusters/the-cluster/snapshots/random-snapshot'
)""";

  btadmin::Snapshot expected;
  ASSERT_TRUE(
      google::protobuf::TextFormat::ParseFromString(expected_text, &expected));
  EXPECT_CALL(*client_, GetOperation(_, _, _))
      .WillOnce(Invoke([](grpc::ClientContext*,
                          google::longrunning::GetOperationRequest const&,
                          google::longrunning::Operation*) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "try-again");
      }))
      .WillOnce(Invoke([](grpc::ClientContext*,
                          google::longrunning::GetOperationRequest const&,
                          google::longrunning::Operation*) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "try-again");
      }))
      .WillOnce(Invoke(
          [&expected](grpc::ClientContext*,
                      google::longrunning::GetOperationRequest const& request,
                      google::longrunning::Operation* operation) {
            operation->set_done(true);
            auto any =
                google::cloud::internal::make_unique<google::protobuf::Any>();
            any->PackFrom(expected);
            operation->set_allocated_response(any.release());
            return grpc::Status::OK;
          }));

  bigtable::ClusterId cluster_id("the-cluster");
  bigtable::SnapshotId snapshot_id("random-snapshot");
  bigtable::TableId table_id("the-table");
  auto future = tested.SnapshotTable(cluster_id, snapshot_id, table_id, 100_s);
  auto actual = future.get();
  EXPECT_STATUS_OK(actual);

  std::string delta;
  google::protobuf::util::MessageDifferencer differencer;
  differencer.ReportDifferencesToString(&delta);
  EXPECT_TRUE(differencer.Compare(expected, *actual)) << delta;
}

/// @test Failure when polling exhausted for
/// `bigtable::TableAdmin::SnapshotTable`.
TEST_F(TableAdminTest, SnapshotTablePollingExhausted) {
  using ::testing::_;
  using ::testing::Invoke;

  bigtable::TableAdmin tested(
      client_, "the-instance",
      bigtable::GenericPollingPolicy<bigtable::LimitedErrorCountRetryPolicy,
                                     bigtable::ExponentialBackoffPolicy>(
          bigtable::LimitedErrorCountRetryPolicy(3),
          bigtable::ExponentialBackoffPolicy(10_ms, 10_min)));
  EXPECT_CALL(*client_, SnapshotTable(_, _, _))
      .WillOnce(Invoke([](grpc::ClientContext*,
                          btadmin::SnapshotTableRequest const& request,
                          google::longrunning::Operation* response) {
        return grpc::Status::OK;
      }));

  EXPECT_CALL(*client_, GetOperation(_, _, _))
      .WillRepeatedly(Invoke([](grpc::ClientContext*,
                                google::longrunning::GetOperationRequest const&,
                                google::longrunning::Operation* operation) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "try-again");
      }));

  bigtable::ClusterId cluster_id("the-cluster");
  bigtable::SnapshotId snapshot_id("random-snapshot");
  bigtable::TableId table_id("the-table");

  auto future = tested.SnapshotTable(cluster_id, snapshot_id, table_id, 100_s);
  EXPECT_FALSE(future.get());
}

/// @test `bigtable::TableAdmin::SnapshotTable` call has permanent failure.
TEST_F(TableAdminTest, SnapshotTablePermanentFailure) {
  using ::testing::_;
  using ::testing::Invoke;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, SnapshotTable(_, _, _))
      .WillOnce(Invoke([](grpc::ClientContext*,
                          btadmin::SnapshotTableRequest const& request,
                          google::longrunning::Operation* response) {
        return grpc::Status::OK;
      }));

  EXPECT_CALL(*client_, GetOperation(_, _, _))
      .WillRepeatedly(Invoke([](grpc::ClientContext*,
                                google::longrunning::GetOperationRequest const&,
                                google::longrunning::Operation* operation) {
        return grpc::Status(grpc::StatusCode::UNKNOWN, "try-again");
      }));

  bigtable::ClusterId cluster_id("the-cluster");
  bigtable::SnapshotId snapshot_id("random-snapshot");
  bigtable::TableId table_id("the-table");

  auto future = tested.SnapshotTable(cluster_id, snapshot_id, table_id, 100_s);
  EXPECT_FALSE(future.get());
}

/// @test Failures in `bigtable::TableAdmin::SnapshotTable`.
TEST_F(TableAdminTest, SnapshotTableRequestFailure) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, SnapshotTable(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "uh oh")));

  bigtable::ClusterId cluster_id("the-cluster");
  bigtable::SnapshotId snapshot_id("random-snapshot");
  bigtable::TableId table_id("the-table");
  auto future = tested.SnapshotTable(cluster_id, snapshot_id, table_id, 100_s);
  EXPECT_FALSE(future.get());
}

/// @test Failures while polling in `bigtable::TableAdmin::SnapshotTable`.
TEST_F(TableAdminTest, SnapshotTablePollUnrecoverableFailure) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, SnapshotTable(_, _, _))
      .WillOnce(Invoke([](grpc::ClientContext*,
                          btadmin::SnapshotTableRequest const& request,
                          google::longrunning::Operation* response) {
        return grpc::Status::OK;
      }));

  EXPECT_CALL(*client_, GetOperation(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "uh oh")));
  bigtable::ClusterId cluster_id("the-cluster");
  bigtable::SnapshotId snapshot_id("random-snapshot");
  bigtable::TableId table_id("the-table");
  auto future = tested.SnapshotTable(cluster_id, snapshot_id, table_id, 100_s);
  EXPECT_FALSE(future.get());
}

/// @test Polling in `bigtable::TableAdmin::SnapshotTable` returns failure.
TEST_F(TableAdminTest, SnapshotTablePollReturnsFailure) {
  using ::testing::_;
  using ::testing::Invoke;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, SnapshotTable(_, _, _))
      .WillOnce(Invoke([](grpc::ClientContext*,
                          btadmin::SnapshotTableRequest const& request,
                          google::longrunning::Operation* response) {
        return grpc::Status::OK;
      }));

  EXPECT_CALL(*client_, GetOperation(_, _, _))
      .WillOnce(Invoke([](grpc::ClientContext*,
                          google::longrunning::GetOperationRequest const&,
                          google::longrunning::Operation* operation) {
        operation->set_done(false);
        return grpc::Status::OK;
      }))
      .WillOnce(Invoke([](grpc::ClientContext*,
                          google::longrunning::GetOperationRequest const&,
                          google::longrunning::Operation* operation) {
        operation->set_done(false);
        return grpc::Status::OK;
      }))
      .WillOnce(
          Invoke([](grpc::ClientContext*,
                    google::longrunning::GetOperationRequest const& request,
                    google::longrunning::Operation* operation) {
            operation->set_done(true);
            auto error =
                google::cloud::internal::make_unique<google::rpc::Status>();
            error->set_code(grpc::StatusCode::FAILED_PRECONDITION);
            error->set_message("something is broken");
            operation->set_allocated_error(error.release());
            return grpc::Status::OK;
          }));

  bigtable::ClusterId cluster_id("the-cluster");
  bigtable::SnapshotId snapshot_id("random-snapshot");
  bigtable::TableId table_id("the-table");

  auto future = tested.SnapshotTable(cluster_id, snapshot_id, table_id, 100_s);
  EXPECT_FALSE(future.get());
}

/**
 * @test Verify that `bigtable::TableAdmin::ListSnapshots` works in the easy
 * case.
 */
TEST_F(TableAdminTest, ListSnapshots_Simple) {
  using namespace ::testing;
  bigtable::TableAdmin tested(client_, kInstanceId);
  auto mock_list_snapshots = create_list_snapshots_lambda("", "", {"s0", "s1"});
  EXPECT_CALL(*client_, ListSnapshots(_, _, _))
      .WillOnce(Invoke(mock_list_snapshots));

  bigtable::ClusterId cluster_id("the-cluster");
  auto actual_snapshots = tested.ListSnapshots(cluster_id);
  EXPECT_STATUS_OK(actual_snapshots);
  ASSERT_EQ(2UL, actual_snapshots->size());
  std::string instance_name = tested.instance_name();
  EXPECT_EQ(instance_name + "/clusters/the-cluster/snapshots/s0",
            (*actual_snapshots)[0].name());
  EXPECT_EQ(instance_name + "/clusters/the-cluster/snapshots/s1",
            (*actual_snapshots)[1].name());
}

/**
 * @test Verify that `bigtable::TableAdmin::ListSnapshots` handles failures.
 */
TEST_F(TableAdminTest, ListSnapshots_RecoverableFailure) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;

  bigtable::TableAdmin tested(client_, "the-instance");
  auto mock_recoverable_failure =
      [](grpc::ClientContext* ctx, btadmin::ListSnapshotsRequest const& request,
         btadmin::ListSnapshotsResponse* response) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "try-again");
      };

  auto list0 = create_list_snapshots_lambda("", "token-001", {"s0", "s1"});
  auto list1 = create_list_snapshots_lambda("token-001", "", {"s2", "s3"});
  EXPECT_CALL(*client_, ListSnapshots(_, _, _))
      .WillOnce(Invoke(mock_recoverable_failure))
      .WillOnce(Invoke(list0))
      .WillOnce(Invoke(mock_recoverable_failure))
      .WillOnce(Invoke(list1));

  bigtable::ClusterId cluster_id("the-cluster");
  auto actual_snapshots = tested.ListSnapshots(cluster_id);
  EXPECT_STATUS_OK(actual_snapshots);
  ASSERT_EQ(4UL, actual_snapshots->size());
  std::string instance_name = tested.instance_name();
  EXPECT_EQ(instance_name + "/clusters/the-cluster/snapshots/s0",
            (*actual_snapshots)[0].name());
  EXPECT_EQ(instance_name + "/clusters/the-cluster/snapshots/s1",
            (*actual_snapshots)[1].name());
  EXPECT_EQ(instance_name + "/clusters/the-cluster/snapshots/s2",
            (*actual_snapshots)[2].name());
  EXPECT_EQ(instance_name + "/clusters/the-cluster/snapshots/s3",
            (*actual_snapshots)[3].name());
}

/**
 * @test Verify that `bigtable::TableAdmin::ListSnapshots` handles unrecoverable
 * failure.
 */
TEST_F(TableAdminTest, ListSnapshots_UnrecoverableFailures) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, ListSnapshots(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "uh-oh")));

  bigtable::ClusterId cluster_id("other-cluster");
  EXPECT_FALSE(tested.ListSnapshots(cluster_id));
}

/**
 * @test Verify that `bigtable::TableAdmin::CreateTableFromSnapshot` works in
 * the easy case.
 */
TEST_F(TableAdminTest, CreateTableFromSnapshot_Simple) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;

  bigtable::TableAdmin tested(client_, "the-instance");

  EXPECT_CALL(*client_, CreateTableFromSnapshot(_, _, _))
      .WillOnce(
          Invoke([](grpc::ClientContext*,
                    btadmin::CreateTableFromSnapshotRequest const& request,
                    google::longrunning::Operation* response) {
            auto const project_name =
                "projects/" + kProjectId + "/instances/the-instance";
            EXPECT_EQ(project_name, request.parent());
            EXPECT_EQ("table-1", request.table_id());
            EXPECT_EQ(
                project_name + "/clusters/other-cluster/snapshots/snapshot-1",
                request.source_snapshot());

            return grpc::Status::OK;
          }));

  std::string expected_text = R"(
        name: 'the-instance'
  )";
  auto mock_successs = [](grpc::ClientContext* ctx,
                          google::longrunning::GetOperationRequest const&,
                          google::longrunning::Operation* operation) {
    operation->set_done(false);
    return grpc::Status::OK;
  };

  btadmin::Table expected;
  ASSERT_TRUE(
      google::protobuf::TextFormat::ParseFromString(expected_text, &expected));
  EXPECT_CALL(*client_, GetOperation(_, _, _))
      .WillOnce(Invoke(mock_successs))
      .WillOnce(Invoke(mock_successs))
      .WillOnce(Invoke(
          [&expected](grpc::ClientContext*,
                      google::longrunning::GetOperationRequest const& request,
                      google::longrunning::Operation* operation) {
            operation->set_done(true);
            auto any =
                google::cloud::internal::make_unique<google::protobuf::Any>();
            any->PackFrom(expected);
            operation->set_allocated_response(any.release());
            return grpc::Status::OK;
          }));

  std::string table_id = "table-1";
  auto future = tested.CreateTableFromSnapshot(
      bigtable::ClusterId("other-cluster"), bigtable::SnapshotId("snapshot-1"),
      table_id);

  auto actual = future.get();
  std::string delta;
  google::protobuf::util::MessageDifferencer differencer;
  differencer.ReportDifferencesToString(&delta);
  EXPECT_TRUE(differencer.Compare(expected, *actual)) << delta;
}

/**
 * @test Verify that `bigtable::TableAdmin::CreateTableFromSnapshot` handles
 * unrecoverable failure.
 */
TEST_F(TableAdminTest, CreateTableFromSnapshot_UnrecoverableFailures) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, CreateTableFromSnapshot(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "uh-oh")));

  std::string table_id = "table-1";
  auto future = tested.CreateTableFromSnapshot(
      bigtable::ClusterId("other-cluster"), bigtable::SnapshotId("snapshot-1"),
      table_id);
  // After all the setup, make the actual call we want to test.
  EXPECT_FALSE(future.get());
}

/// @test Polling in `bigtable::TableAdmin::CreateTableFromSnapshot` returns
/// failure.
TEST_F(TableAdminTest, CreateTableFromSnapshot_PollReturnsFailure) {
  using ::testing::_;
  using ::testing::Invoke;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, CreateTableFromSnapshot(_, _, _))
      .WillOnce(
          Invoke([](grpc::ClientContext*,
                    btadmin::CreateTableFromSnapshotRequest const& request,
                    google::longrunning::Operation* response) {
            return grpc::Status::OK;
          }));

  EXPECT_CALL(*client_, GetOperation(_, _, _))
      .WillOnce(Invoke([](grpc::ClientContext*,
                          google::longrunning::GetOperationRequest const&,
                          google::longrunning::Operation* operation) {
        operation->set_done(false);
        return grpc::Status::OK;
      }))
      .WillOnce(Invoke([](grpc::ClientContext*,
                          google::longrunning::GetOperationRequest const&,
                          google::longrunning::Operation* operation) {
        operation->set_done(false);
        return grpc::Status::OK;
      }))
      .WillOnce(
          Invoke([](grpc::ClientContext*,
                    google::longrunning::GetOperationRequest const& request,
                    google::longrunning::Operation* operation) {
            operation->set_done(true);
            auto error =
                google::cloud::internal::make_unique<google::rpc::Status>();
            error->set_code(grpc::StatusCode::FAILED_PRECONDITION);
            error->set_message("something is broken");
            operation->set_allocated_error(error.release());
            return grpc::Status::OK;
          }));

  std::string table_id = "table-1";
  auto future = tested.CreateTableFromSnapshot(
      bigtable::ClusterId("other-cluster"), bigtable::SnapshotId("snapshot-1"),
      table_id);
  // After all the setup, make the actual call we want to test.
  EXPECT_FALSE(future.get());
}

/// @test Polling in `bigtable::TableAdmin::CreateTableFromSnapshot` returns
/// exhaust polling policy failure.
TEST_F(TableAdminTest, CreateTableFromSnapshot_ExhaustPollingPolicyFailure) {
  using ::testing::_;
  using ::testing::Invoke;
  using namespace google::cloud::testing_util::chrono_literals;

  bigtable::TableAdmin tested(
      client_, "the-instance",
      bigtable::GenericPollingPolicy<bigtable::LimitedErrorCountRetryPolicy,
                                     bigtable::ExponentialBackoffPolicy>(
          bigtable::LimitedErrorCountRetryPolicy(3),
          bigtable::ExponentialBackoffPolicy(10_ms, 10_min)));

  EXPECT_CALL(*client_, CreateTableFromSnapshot(_, _, _))
      .WillOnce(
          Invoke([](grpc::ClientContext*,
                    btadmin::CreateTableFromSnapshotRequest const& request,
                    google::longrunning::Operation* response) {
            return grpc::Status::OK;
          }));

  EXPECT_CALL(*client_, GetOperation(_, _, _))
      .WillOnce(Invoke([](grpc::ClientContext*,
                          google::longrunning::GetOperationRequest const&,
                          google::longrunning::Operation* operation) {
        operation->set_done(false);
        return grpc::Status::OK;
      }))
      .WillOnce(Invoke([](grpc::ClientContext*,
                          google::longrunning::GetOperationRequest const&,
                          google::longrunning::Operation* operation) {
        operation->set_done(false);
        return grpc::Status::OK;
      }))
      .WillOnce(Invoke([](grpc::ClientContext*,
                          google::longrunning::GetOperationRequest const&,
                          google::longrunning::Operation* operation) {
        operation->set_done(false);
        return grpc::Status::OK;
      }))
      .WillOnce(
          Invoke([](grpc::ClientContext*,
                    google::longrunning::GetOperationRequest const& request,
                    google::longrunning::Operation* operation) {
            operation->set_done(true);
            auto error =
                google::cloud::internal::make_unique<google::rpc::Status>();
            error->set_code(grpc::StatusCode::UNKNOWN);
            error->set_message("Polling policy exhausted");
            operation->set_allocated_error(error.release());
            return grpc::Status::OK;
          }));

  std::string table_id = "table-1";
  auto future = tested.CreateTableFromSnapshot(
      bigtable::ClusterId("other-cluster"), bigtable::SnapshotId("snapshot-1"),
      table_id);
  // After all the setup, make the actual call we want to test.
  EXPECT_FALSE(future.get());
}
