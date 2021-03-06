// Copyright 2018 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//! [all code]

//! [bigtable includes]
#include "google/cloud/bigtable/table.h"
#include "google/cloud/bigtable/table_admin.h"
//! [bigtable includes]
#include <google/protobuf/text_format.h>
#include <deque>
#include <list>
#include <sstream>

namespace {

struct Usage {
  std::string msg;
};

char const* ConsumeArg(int& argc, char* argv[]) {
  if (argc < 2) {
    return nullptr;
  }
  char const* result = argv[1];
  std::copy(argv + 2, argv + argc, argv + 1);
  argc--;
  return result;
}

std::string command_usage;

void PrintUsage(int argc, char* argv[], std::string const& msg) {
  std::string const cmd = argv[0];
  auto last_slash = std::string(cmd).find_last_of('/');
  auto program = cmd.substr(last_slash + 1);
  std::cerr << msg << "\nUsage: " << program << " <command> [arguments]\n\n"
            << "Commands:\n"
            << command_usage << "\n";
}

void CreateTable(google::cloud::bigtable::TableAdmin admin, int argc,
                 char* argv[]) {
  if (argc != 2) {
    throw Usage{"create-table: <project-id> <instance-id> <table-id>"};
  }
  std::string const table_id = ConsumeArg(argc, argv);

  //! [create table] [START bigtable_create_table]
  [](google::cloud::bigtable::TableAdmin admin, std::string table_id) {
    auto schema = admin.CreateTable(
        table_id,
        google::cloud::bigtable::TableConfig(
            {{"fam", google::cloud::bigtable::GcRule::MaxNumVersions(10)},
             {"foo",
              google::cloud::bigtable::GcRule::MaxAge(std::chrono::hours(72))}},
            {}));
  }
  //! [create table] [END bigtable_create_table]
  (std::move(admin), table_id);
}

void ListTables(google::cloud::bigtable::TableAdmin admin, int argc,
                char* argv[]) {
  if (argc != 1) {
    throw Usage{"list-tables: <project-id> <instance-id>"};
  }

  //! [list tables] [START bigtable_list_tables]
  [](google::cloud::bigtable::TableAdmin admin) {
    auto tables =
        admin.ListTables(google::bigtable::admin::v2::Table::VIEW_UNSPECIFIED);

    if (!tables) {
      throw std::runtime_error(tables.status().message());
    }
    for (auto const& table : *tables) {
      std::cout << table.name() << "\n";
    }
  }
  //! [list tables] [END bigtable_list_tables]
  (std::move(admin));
}

void GetTable(google::cloud::bigtable::TableAdmin admin, int argc,
              char* argv[]) {
  if (argc != 2) {
    throw Usage{"get-table: <project-id> <instance-id> <table-id>"};
  }
  std::string const table_id = ConsumeArg(argc, argv);

  //! [get table]
  [](google::cloud::bigtable::TableAdmin admin, std::string table_id) {
    auto table =
        admin.GetTable(table_id, google::bigtable::admin::v2::Table::FULL);
    if (!table) {
      throw std::runtime_error(table.status().message());
    }
    std::cout << table->name() << "\n";
    for (auto const& family : table->column_families()) {
      std::string const& family_name = family.first;
      std::string gc_rule;
      google::protobuf::TextFormat::PrintToString(family.second.gc_rule(),
                                                  &gc_rule);
      std::cout << "\t" << family_name << "\t\t" << gc_rule << "\n";
    }
  }
  //! [get table]
  (std::move(admin), table_id);
}

void DeleteTable(google::cloud::bigtable::TableAdmin admin, int argc,
                 char* argv[]) {
  if (argc != 2) {
    throw Usage{"delete-table: <project-id> <instance-id> <table-id>"};
  }
  std::string const table_id = ConsumeArg(argc, argv);

  //! [delete table] [START bigtable_del_table]
  [](google::cloud::bigtable::TableAdmin admin, std::string table_id) {
    google::cloud::Status status = admin.DeleteTable(table_id);
    if (!status.ok()) {
      throw std::runtime_error(status.message());
    }
  }
  //! [delete table] [END bigtable_del_table]
  (std::move(admin), table_id);
}

void ModifyTable(google::cloud::bigtable::TableAdmin admin, int argc,
                 char* argv[]) {
  if (argc != 2) {
    throw Usage{"modify-table: <project-id> <instance-id> <table-id>"};
  }
  std::string const table_id = ConsumeArg(argc, argv);

  //! [modify table]
  [](google::cloud::bigtable::TableAdmin admin, std::string table_id) {
    auto schema = admin.ModifyColumnFamilies(
        table_id,
        {google::cloud::bigtable::ColumnFamilyModification::Drop("foo"),
         google::cloud::bigtable::ColumnFamilyModification::Update(
             "fam", google::cloud::bigtable::GcRule::Union(
                        google::cloud::bigtable::GcRule::MaxNumVersions(5),
                        google::cloud::bigtable::GcRule::MaxAge(
                            std::chrono::hours(24 * 7)))),
         google::cloud::bigtable::ColumnFamilyModification::Create(
             "bar", google::cloud::bigtable::GcRule::Intersection(
                        google::cloud::bigtable::GcRule::MaxNumVersions(3),
                        google::cloud::bigtable::GcRule::MaxAge(
                            std::chrono::hours(72))))});

    if (!schema) {
      throw std::runtime_error(schema.status().message());
    }
    std::string formatted;
    google::protobuf::TextFormat::PrintToString(*schema, &formatted);
    std::cout << "Schema modified to: " << formatted << "\n";
  }
  //! [modify table]
  (std::move(admin), table_id);
}

void CreateMaxAgeFamily(google::cloud::bigtable::TableAdmin admin, int argc,
                        char* argv[]) {
  if (argc != 3) {
    throw Usage{
        "create-max-age-family <project-id> <instance-id> <table-id>"
        " <family-name>"};
  }
  std::string const table_id = ConsumeArg(argc, argv);
  std::string const family_name = ConsumeArg(argc, argv);

  // [START bigtable_create_family_gc_max_age]
  namespace cbt = google::cloud::bigtable;
  [](cbt::TableAdmin admin, std::string table_id, std::string family_name) {
    auto schema = admin.ModifyColumnFamilies(
        table_id,
        {cbt::ColumnFamilyModification::Create(
            family_name, cbt::GcRule::MaxAge(std::chrono::hours(5 * 24)))});

    if (!schema) {
      throw std::runtime_error(schema.status().message());
    }
    std::string formatted;
    google::protobuf::TextFormat::PrintToString(*schema, &formatted);
    std::cout << "Schema modified to: " << formatted << "\n";
  }
  // [END bigtable_create_family_gc_max_age]
  (std::move(admin), table_id, family_name);
}

void CreateMaxVersionsFamily(google::cloud::bigtable::TableAdmin admin,
                             int argc, char* argv[]) {
  if (argc != 3) {
    throw Usage{
        "create-max-versions-family <project-id> <instance-id> <table-id>"
        " <family-name>"};
  }
  std::string const table_id = ConsumeArg(argc, argv);
  std::string const family_name = ConsumeArg(argc, argv);

  // [START bigtable_create_family_gc_max_versions]
  namespace cbt = google::cloud::bigtable;
  [](cbt::TableAdmin admin, std::string table_id, std::string family_name) {
    auto schema = admin.ModifyColumnFamilies(
        table_id, {cbt::ColumnFamilyModification::Create(
                      family_name, cbt::GcRule::MaxNumVersions(2))});

    if (!schema) {
      throw std::runtime_error(schema.status().message());
    }
    std::string formatted;
    google::protobuf::TextFormat::PrintToString(*schema, &formatted);
    std::cout << "Schema modified to: " << formatted << "\n";
  }
  // [END bigtable_create_family_gc_max_versions]
  (std::move(admin), table_id, family_name);
}

void CreateUnionFamily(google::cloud::bigtable::TableAdmin admin, int argc,
                       char* argv[]) {
  if (argc != 3) {
    throw Usage{
        "create-union-family: <project-id> <instance-id> <table-id>"
        " <family-name>"};
  }
  std::string const table_id = ConsumeArg(argc, argv);
  std::string const family_name = ConsumeArg(argc, argv);

  // [START bigtable_create_family_gc_union]
  namespace cbt = google::cloud::bigtable;
  [](cbt::TableAdmin admin, std::string table_id, std::string family_name) {
    auto schema = admin.ModifyColumnFamilies(
        table_id,
        {cbt::ColumnFamilyModification::Create(
            family_name, cbt::GcRule::Union(cbt::GcRule::MaxNumVersions(1),
                                            cbt::GcRule::MaxAge(
                                                5 * std::chrono::hours(24))))});

    if (!schema) {
      throw std::runtime_error(schema.status().message());
    }
    std::string formatted;
    google::protobuf::TextFormat::PrintToString(*schema, &formatted);
    std::cout << "Schema modified to: " << formatted << "\n";
  }
  // [END bigtable_create_family_gc_union]
  (std::move(admin), table_id, family_name);
}

void CreateIntersectionFamily(google::cloud::bigtable::TableAdmin admin,
                              int argc, char* argv[]) {
  if (argc != 3) {
    throw Usage{
        "create-intersection-family: <project-id> <instance-id> <table-id>"
        " <family-name>"};
  }
  std::string const table_id = ConsumeArg(argc, argv);
  std::string const family_name = ConsumeArg(argc, argv);

  // [START bigtable_create_family_gc_intersection]
  namespace cbt = google::cloud::bigtable;
  [](cbt::TableAdmin admin, std::string table_id, std::string family_name) {
    auto schema = admin.ModifyColumnFamilies(
        table_id, {cbt::ColumnFamilyModification::Create(
                      family_name,
                      cbt::GcRule::Intersection(
                          cbt::GcRule::MaxNumVersions(1),
                          cbt::GcRule::MaxAge(5 * std::chrono::hours(24))))});

    if (!schema) {
      throw std::runtime_error(schema.status().message());
    }
    std::string formatted;
    google::protobuf::TextFormat::PrintToString(*schema, &formatted);
    std::cout << "Schema modified to: " << formatted << "\n";
  }
  // [END bigtable_create_family_gc_intersection]
  (std::move(admin), table_id, family_name);
}

void CreateNestedFamily(google::cloud::bigtable::TableAdmin admin, int argc,
                        char* argv[]) {
  if (argc != 3) {
    throw Usage{
        "create-nested-family: <project-id> <instance-id> <table-id>"
        " <family-name>"};
  }
  std::string const table_id = ConsumeArg(argc, argv);
  std::string const family_name = ConsumeArg(argc, argv);

  // [START bigtable_create_family_gc_nested]
  namespace cbt = google::cloud::bigtable;
  [](cbt::TableAdmin admin, std::string table_id, std::string family_name) {
    auto schema = admin.ModifyColumnFamilies(
        table_id,
        {cbt::ColumnFamilyModification::Create(
            family_name,
            cbt::GcRule::Union(
                cbt::GcRule::MaxNumVersions(10),
                cbt::GcRule::Intersection(
                    cbt::GcRule::MaxNumVersions(1),
                    cbt::GcRule::MaxAge(5 * std::chrono::hours(24)))))});

    if (!schema) {
      throw std::runtime_error(schema.status().message());
    }
    std::string formatted;
    google::protobuf::TextFormat::PrintToString(*schema, &formatted);
    std::cout << "Schema modified to: " << formatted << "\n";
  }
  // [END bigtable_create_family_gc_nested]
  (std::move(admin), table_id, family_name);
}

void GetFamilyMetadata(google::cloud::bigtable::TableAdmin admin, int argc,
                       char* argv[]) {
  if (argc != 3) {
    throw Usage{
        "get-family-metadata: <project-id> <instance-id> <table-id>"
        " <family-name>"};
  }
  std::string const table_id = ConsumeArg(argc, argv);
  std::string const family_name = ConsumeArg(argc, argv);

  // [START bigtable_get_family_metadata]
  namespace cbt = google::cloud::bigtable;
  [](cbt::TableAdmin admin, std::string table_id, std::string family_name) {
    auto schema =
        admin.GetTable(table_id, google::bigtable::admin::v2::Table::FULL);

    if (!schema) {
      throw std::runtime_error(schema.status().message());
    }
    auto pos = schema->column_families().find(family_name);
    if (pos == schema->column_families().end()) {
      std::cout << "Cannot find family <" << family_name << "> in table\n";
      return;
    }
    std::string formatted;
    google::protobuf::TextFormat::PrintToString(pos->second, &formatted);
    std::cout << "Column family metadata for <" << family_name << "> is "
              << formatted << "\n";
  }
  // [END bigtable_get_family_metadata]
  (std::move(admin), table_id, family_name);
}

void DeleteColumnFamily(google::cloud::bigtable::TableAdmin admin, int argc,
                        char* argv[]) {
  if (argc != 3) {
    throw Usage{
        "delete-column-family: <project-id> <instance-id> <table-id>"
        " <family-name>"};
  }
  std::string const table_id = ConsumeArg(argc, argv);
  std::string const family_name = ConsumeArg(argc, argv);

  // [START bigtable_delete_family]
  namespace cbt = google::cloud::bigtable;
  [](cbt::TableAdmin admin, std::string table_id, std::string family_name) {
    auto schema = admin.ModifyColumnFamilies(
        table_id, {cbt::ColumnFamilyModification::Drop(family_name)});

    if (!schema) {
      throw std::runtime_error(schema.status().message());
    }
    std::string formatted;
    google::protobuf::TextFormat::PrintToString(*schema, &formatted);
    std::cout << "Schema modified to: " << formatted << "\n";
  }
  // [END bigtable_delete_family]
  (std::move(admin), table_id, family_name);
}

void CheckFamilyExists(google::cloud::bigtable::TableAdmin admin, int argc,
                       char* argv[]) {
  if (argc != 3) {
    throw Usage{
        "check-family-exists: <project-id> <instance-id> <table-id>"
        " <family-name>"};
  }
  std::string const table_id = ConsumeArg(argc, argv);
  std::string const family_name = ConsumeArg(argc, argv);

  // [START bigtable_check_family_exists]
  namespace cbt = google::cloud::bigtable;
  [](cbt::TableAdmin admin, std::string table_id, std::string family_name) {
    auto schema =
        admin.GetTable(table_id, google::bigtable::admin::v2::Table::FULL);

    if (!schema) {
      throw std::runtime_error(schema.status().message());
    }
    auto pos = schema->column_families().find(family_name);
    if (pos == schema->column_families().end()) {
      std::string msg =
          "The column family <" + family_name + "> does not exist";
      throw std::runtime_error(msg);
    }
    std::cout << "The column family <" << family_name << "> does exist\n";
  }
  // [END bigtable_check_family_exists]
  (std::move(admin), table_id, family_name);
}

void UpdateGcRule(google::cloud::bigtable::TableAdmin admin, int argc,
                  char* argv[]) {
  if (argc != 3) {
    throw Usage{
        "update-gc-rule: <project-id> <instance-id> <table-id>"
        " <family-name>"};
  }
  std::string const table_id = ConsumeArg(argc, argv);
  std::string const family_name = ConsumeArg(argc, argv);

  // [START bigtable_update_gc_rule]
  namespace cbt = google::cloud::bigtable;
  [](cbt::TableAdmin admin, std::string table_id, std::string family_name) {
    auto schema = admin.ModifyColumnFamilies(
        table_id, {cbt::ColumnFamilyModification::Update(
                      family_name, cbt::GcRule::MaxNumVersions(1))});

    if (!schema) {
      throw std::runtime_error(schema.status().message());
    }
    std::string formatted;
    google::protobuf::TextFormat::PrintToString(*schema, &formatted);
    std::cout << "Schema modified to: " << formatted << "\n";
  }
  // [END bigtable_update_gc_rule]
  (std::move(admin), table_id, family_name);
}

void DropAllRows(google::cloud::bigtable::TableAdmin admin, int argc,
                 char* argv[]) {
  if (argc != 2) {
    throw Usage{"drop-all-rows: <project-id> <instance-id> <table-id>"};
  }
  std::string const table_id = ConsumeArg(argc, argv);

  //! [drop all rows] [START bigtable_truncate_table]
  [](google::cloud::bigtable::TableAdmin admin, std::string table_id) {
    google::cloud::Status status = admin.DropAllRows(table_id);
    if (!status.ok()) {
      throw std::runtime_error(status.message());
    }
  }
  //! [drop all rows] [END bigtable_truncate_table]
  (std::move(admin), table_id);
}

void DropRowsByPrefix(google::cloud::bigtable::TableAdmin admin, int argc,
                      char* argv[]) {
  if (argc != 2) {
    throw Usage{"drop-rows-by-prefix: <project-id> <instance-id> <table-id>"};
  }
  std::string const table_id = ConsumeArg(argc, argv);

  //! [drop rows by prefix] [START bigtable_delete_rows_prefix]
  [](google::cloud::bigtable::TableAdmin admin, std::string table_id) {
    google::cloud::Status status =
        admin.DropRowsByPrefix(table_id, "key-00004");
    if (!status.ok()) {
      throw std::runtime_error(status.message());
    }
  }
  //! [drop rows by prefix] [END bigtable_delete_rows_prefix]
  (std::move(admin), table_id);
}

void WaitForConsistencyCheck(google::cloud::bigtable::TableAdmin admin,
                             int argc, char* argv[]) {
  if (argc != 2) {
    throw Usage{
        "wait-for-consistency-check: <project-id> <instance-id> <table-id>"};
  }
  std::string const table_id_param = ConsumeArg(argc, argv);

  //! [wait for consistency check]
  [](google::cloud::bigtable::TableAdmin admin, std::string table_id_param) {
    google::cloud::bigtable::TableId table_id(table_id_param);
    auto consistency_token(admin.GenerateConsistencyToken(table_id.get()));
    if (!consistency_token) {
      throw std::runtime_error(consistency_token.status().message());
    }
    auto result =
        admin.WaitForConsistencyCheck(table_id, *consistency_token).get();
    if (result && *result) {
      std::cout << "Table is consistent\n";
    } else {
      std::cout << "Table is not consistent\n";
    }
  }
  //! [wait for consistency check]
  (std::move(admin), table_id_param);
}

void CheckConsistency(google::cloud::bigtable::TableAdmin admin, int argc,
                      char* argv[]) {
  if (argc != 3) {
    throw Usage{
        "check-consistency: <project-id> <instance-id> <table-id> "
        "<consistency_token>"};
  }
  std::string const table_id_param = ConsumeArg(argc, argv);
  std::string const consistency_token_param = ConsumeArg(argc, argv);

  //! [check consistency]
  [](google::cloud::bigtable::TableAdmin admin, std::string table_id_param,
     std::string consistency_token_param) {
    google::cloud::bigtable::TableId table_id(table_id_param);
    google::cloud::bigtable::ConsistencyToken consistency_token(
        consistency_token_param);
    auto result = admin.CheckConsistency(table_id, consistency_token);
    if (!result) {
      throw std::runtime_error(result.status().message());
    }
    if (*result == google::cloud::bigtable::Consistency::kConsistent) {
      std::cout << "Table is consistent\n";
    } else {
      std::cout
          << "Table is not yet consistent, Please Try again Later with the "
             "same Token!";
    }
    std::cout << std::flush;
  }
  //! [check consistency]
  (std::move(admin), table_id_param, consistency_token_param);
}

void GenerateConsistencyToken(google::cloud::bigtable::TableAdmin admin,
                              int argc, char* argv[]) {
  if (argc != 2) {
    throw Usage{
        "generate-consistency-token: <project-id> <instance-id> <table-id>"};
  }
  std::string const table_id = ConsumeArg(argc, argv);

  //! [generate consistency token]
  [](google::cloud::bigtable::TableAdmin admin, std::string table_id) {
    auto token = admin.GenerateConsistencyToken(table_id);
    if (!token) {
      throw std::runtime_error(token.status().message());
    }
    std::cout << "generated token is : " << token->get() << "\n";
  }
  //! [generate consistency token]
  (std::move(admin), table_id);
}

void GetSnapshot(google::cloud::bigtable::TableAdmin admin, int argc,
                 char* argv[]) {
  if (argc != 3) {
    throw Usage{
        "get-snapshot: <project-id> <instance-id> <cluster-id> <snapshot-id>"};
  }
  std::string const cluster_id_str = ConsumeArg(argc, argv);
  std::string const snapshot_id_str = ConsumeArg(argc, argv);

  //! [get snapshot]
  [](google::cloud::bigtable::TableAdmin admin, std::string cluster_id_str,
     std::string snapshot_id_str) {
    google::cloud::bigtable::ClusterId cluster_id(cluster_id_str);
    google::cloud::bigtable::SnapshotId snapshot_id(snapshot_id_str);
    auto snapshot = admin.GetSnapshot(cluster_id, snapshot_id);
    if (!snapshot) {
      throw std::runtime_error(snapshot.status().message());
    }
    std::cout << "GetSnapshot name : " << snapshot->name() << "\n";
  }
  //! [get snapshot]
  (std::move(admin), cluster_id_str, snapshot_id_str);
}

void ListSnapshots(google::cloud::bigtable::TableAdmin admin, int argc,
                   char* argv[]) {
  if (argc != 2) {
    throw Usage{"list-snapshot: <project-id> <instance-id> <cluster-id>"};
  }
  std::string const cluster_id_str = ConsumeArg(argc, argv);

  //! [list snapshots]
  [](google::cloud::bigtable::TableAdmin admin, std::string cluster_id_str) {
    google::cloud::bigtable::ClusterId cluster_id(cluster_id_str);

    auto snapshot_list = admin.ListSnapshots(cluster_id);
    if (!snapshot_list) {
      throw std::runtime_error(snapshot_list.status().message());
    }
    std::cout << "Snapshot Name List\n";
    for (auto const& snapshot : *snapshot_list) {
      std::cout << "Snapshot Name:" << snapshot.name() << "\n";
    }
  }
  //! [list snapshots]
  (std::move(admin), cluster_id_str);
}

void DeleteSnapshot(google::cloud::bigtable::TableAdmin admin, int argc,
                    char* argv[]) {
  if (argc != 3) {
    throw Usage{
        "delete-snapshot: <project-id> <instance-id> <cluster-id> "
        "<snapshot-id>"};
  }
  std::string const cluster_id_str = ConsumeArg(argc, argv);
  std::string const snapshot_id_str = ConsumeArg(argc, argv);

  //! [delete snapshot]
  [](google::cloud::bigtable::TableAdmin admin, std::string cluster_id_str,
     std::string snapshot_id_str) {
    google::cloud::bigtable::ClusterId cluster_id(cluster_id_str);
    google::cloud::bigtable::SnapshotId snapshot_id(snapshot_id_str);
    google::cloud::Status status =
        admin.DeleteSnapshot(cluster_id, snapshot_id);
    if (!status.ok()) {
      throw std::runtime_error(status.message());
    }
  }
  //! [delete snapshot]
  (std::move(admin), cluster_id_str, snapshot_id_str);
}

void CreateTableFromSnapshot(google::cloud::bigtable::TableAdmin admin,
                             int argc, char* argv[]) {
  if (argc != 4) {
    throw Usage{
        "create-table-from-snapshot: <project-id> <instance-id> <cluster-id> "
        "<snapshot-id> <table-id>"};
  }
  std::string const cluster_id_str = ConsumeArg(argc, argv);
  std::string const snapshot_id_str = ConsumeArg(argc, argv);
  std::string const table_id = ConsumeArg(argc, argv);

  //! [create table from snapshot]
  [](google::cloud::bigtable::TableAdmin admin, std::string cluster_id_str,
     std::string snapshot_id_str, std::string table_id) {
    google::cloud::bigtable::ClusterId cluster_id(cluster_id_str);
    google::cloud::bigtable::SnapshotId snapshot_id(snapshot_id_str);
    auto future =
        admin.CreateTableFromSnapshot(cluster_id, snapshot_id, table_id);
    auto table = future.get();
    if (!table) {
      throw std::runtime_error(table.status().message());
    }
    std::cout << "Table created :" << table->name() << "\n";
  }
  //! [create table from snapshot]
  (std::move(admin), cluster_id_str, snapshot_id_str, table_id);
}

}  // anonymous namespace

int main(int argc, char* argv[]) try {
  using CommandType =
      std::function<void(google::cloud::bigtable::TableAdmin, int, char* [])>;

  std::map<std::string, CommandType> commands = {
      {"create-table", &CreateTable},
      {"list-tables", &ListTables},
      {"get-table", &GetTable},
      {"delete-table", &DeleteTable},
      {"modify-table", &ModifyTable},
      {"create-max-age-family", &CreateMaxAgeFamily},
      {"create-max-versions-family", &CreateMaxVersionsFamily},
      {"create-union-family", &CreateUnionFamily},
      {"create-intersection-family", &CreateIntersectionFamily},
      {"create-nested-family", &CreateNestedFamily},
      {"get-family-metadata", &GetFamilyMetadata},
      {"delete-column-family", &DeleteColumnFamily},
      {"check-family-exists", &CheckFamilyExists},
      {"update-gc-rule", &UpdateGcRule},
      {"drop-all-rows", &DropAllRows},
      {"drop-rows-by-prefix", &DropRowsByPrefix},
      {"wait-for-consistency-check", &WaitForConsistencyCheck},
      {"check-consistency", &CheckConsistency},
      {"generate-consistency-token", &GenerateConsistencyToken},
      {"get-snapshot", &GetSnapshot},
      {"list-snapshot", &ListSnapshots},
      {"delete-snapshot", &DeleteSnapshot},
      {"create-table-from-snapshot", &CreateTableFromSnapshot},
  };

  {
    // Force each command to generate its Usage string, so we can provide a good
    // usage string for the whole program. We need to create an TableAdmin
    // object to do this, but that object is never used, it is passed to the
    // commands, without any calls made to it.
    google::cloud::bigtable::TableAdmin unused(
        google::cloud::bigtable::CreateDefaultAdminClient(
            "unused-project", google::cloud::bigtable::ClientOptions()),
        "Unused-instance");
    for (auto&& kv : commands) {
      try {
        int fake_argc = 0;
        kv.second(unused, fake_argc, argv);
      } catch (Usage const& u) {
        command_usage += "    ";
        command_usage += u.msg;
        command_usage += "\n";
      } catch (...) {
        // ignore other exceptions.
      }
    }
  }

  if (argc < 4) {
    PrintUsage(argc, argv, "Missing command and/or project-id/ or instance-id");
    return 1;
  }

  std::string const command_name = ConsumeArg(argc, argv);
  std::string const project_id = ConsumeArg(argc, argv);
  std::string const instance_id = ConsumeArg(argc, argv);

  auto command = commands.find(command_name);
  if (commands.end() == command) {
    PrintUsage(argc, argv, "Unknown command: " + command_name);
    return 1;
  }

  // Connect to the Cloud Bigtable admin endpoint.
  //! [connect admin]
  google::cloud::bigtable::TableAdmin admin(
      google::cloud::bigtable::CreateDefaultAdminClient(
          project_id, google::cloud::bigtable::ClientOptions()),
      instance_id);
  //! [connect admin]

  command->second(admin, argc, argv);

  return 0;
} catch (Usage const& ex) {
  PrintUsage(argc, argv, ex.msg);
  return 1;
} catch (std::exception const& ex) {
  std::cerr << "Standard C++ exception raised: " << ex.what() << "\n";
  return 1;
}
//! [all code]
