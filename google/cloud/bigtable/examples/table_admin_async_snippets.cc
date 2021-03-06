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

#include "google/cloud/bigtable/table.h"
#include "google/cloud/bigtable/table_admin.h"
#include <google/protobuf/text_format.h>
#include <algorithm>

namespace {
namespace cbt = google::cloud::bigtable;

struct Usage {
  std::string msg;
};

std::string command_usage;

void PrintUsage(std::string const& cmd, std::string const& msg) {
  auto last_slash = std::string(cmd).find_last_of('/');
  auto program = cmd.substr(last_slash + 1);
  std::cerr << msg << "\nUsage: " << program << " <command> [arguments]\n\n"
            << "Commands:\n"
            << command_usage << "\n";
}

void AsyncCreateTable(cbt::TableAdmin admin, cbt::CompletionQueue cq,
                      std::vector<std::string> argv) {
  if (argv.size() != 2U) {
    throw Usage{"async-create-table: <project-id> <instance-id> <table-id>"};
  }

  //! [async create table]
  namespace cbt = google::cloud::bigtable;
  [](cbt::TableAdmin admin, cbt::CompletionQueue cq, std::string table_id) {
    google::cloud::future<
        google::cloud::StatusOr<google::bigtable::admin::v2::Table>>
        future = admin.AsyncCreateTable(
            cq, table_id,
            google::cloud::bigtable::TableConfig(
                {{"fam", google::cloud::bigtable::GcRule::MaxNumVersions(10)},
                 {"foo", google::cloud::bigtable::GcRule::MaxAge(
                             std::chrono::hours(72))}},
                {}));

    auto final = future.then(
        [](google::cloud::future<
            google::cloud::StatusOr<google::bigtable::admin::v2::Table>>
               f) {
          auto table = f.get();
          if (!table) {
            throw std::runtime_error(table.status().message());
          }
          std::cout << "Table created as " << table->name() << "\n";
          return google::cloud::Status();
        });
    final.get();  // block to keep sample small and correct.
  }
  //! [async create table]
  (std::move(admin), std::move(cq), argv[1]);
}

void AsyncGetTable(cbt::TableAdmin admin, cbt::CompletionQueue cq,
                   std::vector<std::string> argv) {
  if (argv.size() != 2U) {
    throw Usage{"async-get-table: <project-id> <instance-id> <table-id>"};
  }

  //! [async get table]
  namespace cbt = google::cloud::bigtable;
  [](cbt::TableAdmin admin, cbt::CompletionQueue cq, std::string table_id) {
    google::cloud::future<
        google::cloud::StatusOr<google::bigtable::admin::v2::Table>>
        future = admin.AsyncGetTable(cq, table_id,
                                     google::bigtable::admin::v2::Table::FULL);

    auto final = future.then(
        [](google::cloud::future<
            google::cloud::StatusOr<google::bigtable::admin::v2::Table>>
               f) {
          auto table = f.get();
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
          return google::cloud::Status();
        });

    final.get();
  }
  //! [async get table]
  (std::move(admin), std::move(cq), argv[1]);
}

void AsyncDeleteTable(cbt::TableAdmin admin, cbt::CompletionQueue cq,
                      std::vector<std::string> argv) {
  if (argv.size() != 2U) {
    throw Usage{"async-delete-table: <project-id> <instance-id> <table-id>"};
  }

  //! [async delete table]
  namespace cbt = google::cloud::bigtable;
  [](cbt::TableAdmin admin, cbt::CompletionQueue cq, std::string table_id) {
    google::cloud::future<google::cloud::Status> future =
        admin.AsyncDeleteTable(cq, table_id);

    auto final =
        future.then([table_id](google::cloud::future<google::cloud::Status> f) {
          auto status = f.get();
          if (!status.ok()) {
            throw std::runtime_error(status.message());
          }
          std::cout << "Successfully deleted table: " << table_id << "\n";
        });

    final.get();
  }
  //! [async delete table]
  (std::move(admin), std::move(cq), argv[1]);
}

void AsyncModifyTable(cbt::TableAdmin admin, cbt::CompletionQueue cq,
                      std::vector<std::string> argv) {
  if (argv.size() != 2U) {
    throw Usage{"async-modify-table: <project-id> <instance-id> <table-id>"};
  }

  //! [async modify table]
  [](cbt::TableAdmin admin, cbt::CompletionQueue cq, std::string table_id) {
    google::cloud::future<
        google::cloud::StatusOr<google::bigtable::admin::v2::Table>>
        future = admin.AsyncModifyColumnFamilies(
            cq, table_id,
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

    auto final = future.then(
        [](google::cloud::future<
            google::cloud::StatusOr<google::bigtable::admin::v2::Table>>
               f) {
          auto table = f.get();
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
          return google::cloud::Status();
        });

    final.get();
  }
  //! [async modify table]
  (std::move(admin), std::move(cq), argv[1]);
}

void AsyncDropRowsByPrefix(cbt::TableAdmin admin, cbt::CompletionQueue cq,
                           std::vector<std::string> argv) {
  if (argv.size() != 3U) {
    throw Usage{
        "async-drop-rows-by-prefix: <project-id> <instance-id> <table-id> "
        "<row-key>"};
  }

  //! [async drop rows by prefix]
  [](google::cloud::bigtable::TableAdmin admin, cbt::CompletionQueue cq,
     std::string table_id, std::string row_key) {
    google::cloud::future<google::cloud::Status> future =
        admin.AsyncDropRowsByPrefix(cq, table_id, row_key);
    auto final =
        future.then([row_key](google::cloud::future<google::cloud::Status> f) {
          auto status = f.get();
          if (!status.ok()) {
            throw std::runtime_error(status.message());
          }
          std::cout << "Successfully dropped rows with prefix " << row_key
                    << "\n";
        });

    final.get();
  }
  //! [async drop rows by prefix]
  (std::move(admin), std::move(cq), argv[1], argv[2]);
}

void AsyncDropAllRows(cbt::TableAdmin admin, cbt::CompletionQueue cq,
                      std::vector<std::string> argv) {
  if (argv.size() != 2U) {
    throw Usage{"async-drop-all-rows: <project-id> <instance-id> <table-id>"};
  }

  //! [async drop all rows]
  [](google::cloud::bigtable::TableAdmin admin, cbt::CompletionQueue cq,
     std::string table_id) {
    google::cloud::future<google::cloud::Status> future =
        admin.AsyncDropAllRows(cq, table_id);
    auto final =
        future.then([table_id](google::cloud::future<google::cloud::Status> f) {
          auto status = f.get();
          if (!status.ok()) {
            throw std::runtime_error(status.message());
          }
          std::cout << "Successfully dropped all rows for table_id " << table_id
                    << "\n";
        });

    final.get();
  }
  //! [async drop all rows]
  (std::move(admin), std::move(cq), argv[1]);
}
}  // anonymous namespace

int main(int argc, char* argv[]) try {
  using CommandType = std::function<void(
      google::cloud::bigtable::TableAdmin,
      google::cloud::bigtable::CompletionQueue, std::vector<std::string>)>;

  std::map<std::string, CommandType> commands = {
      {"async-create-table", &AsyncCreateTable},
      {"async-get-table", &AsyncGetTable},
      {"async-delete-table", &AsyncDeleteTable},
      {"async-modify-table", &AsyncModifyTable},
      {"async-drop-rows-by-prefix", &AsyncDropRowsByPrefix},
      {"async-drop-all-rows", &AsyncDropAllRows},
  };

  google::cloud::bigtable::CompletionQueue cq;

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
        kv.second(unused, cq, {});
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
    PrintUsage(argv[0], "Missing command and/or project-id/ or instance-id");
    return 1;
  }

  std::vector<std::string> args;
  args.emplace_back(argv[0]);
  std::string const command_name = argv[1];
  std::string const project_id = argv[2];
  std::string const instance_id = argv[3];
  std::transform(argv + 4, argv + argc, std::back_inserter(args),
                 [](char* x) { return std::string(x); });

  auto command = commands.find(command_name);
  if (commands.end() == command) {
    PrintUsage(argv[0], "Unknown command: " + command_name);
    return 1;
  }

  // Start a thread to run the completion queue event loop.
  std::thread runner([&cq] { cq.Run(); });

  // Connect to the Cloud Bigtable admin endpoint.
  google::cloud::bigtable::TableAdmin admin(
      google::cloud::bigtable::CreateDefaultAdminClient(
          project_id, google::cloud::bigtable::ClientOptions()),
      instance_id);

  command->second(admin, cq, args);

  // Shutdown the completion queue event loop and join the thread.
  cq.Shutdown();
  runner.join();

  return 0;
} catch (Usage const& ex) {
  PrintUsage(argv[0], ex.msg);
  return 1;
} catch (std::exception const& ex) {
  std::cerr << "Standard C++ exception raised: " << ex.what() << "\n";
  return 1;
}
