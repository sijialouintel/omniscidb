/*
 * Copyright 2019 OmniSci, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file VeloxFunctionsTest.cpp
 * @brief Test suite for Velox functions
 */

#include <QueryEngine/ResultSet.h>
#include <gtest/gtest.h>
#include <boost/format.hpp>
#include <boost/locale/generator.hpp>

#include "Catalog/Catalog.h"

#include "../QueryRunner/QueryRunner.h"
#include "TestHelpers.h"

#ifndef BASE_PATH
#define BASE_PATH "./tmp"
#endif

extern bool g_enable_experimental_string_functions;
using QR = QueryRunner::QueryRunner;

namespace {
inline void run_ddl_statement(const std::string& input_str) {
  QR::get()->runDDLStatement(input_str);
}

TargetValue run_simple_agg(const std::string& query_str) {
  auto rows = QR::get()->runSQL(query_str, ExecutorDeviceType::CPU, false);
  auto crt_row = rows->getNextRow(true, true);
  CHECK_EQ(size_t(1), crt_row.size()) << query_str;
  return crt_row[0];
}

inline auto sql(const std::string& sql_stmt) {
  return QueryRunner::QueryRunner::get()->runSQL(
      sql_stmt, ExecutorDeviceType::CPU, true, true);
}

inline auto multi_sql(const std::string& sql_stmts) {
  return QueryRunner::QueryRunner::get()
      ->runMultipleStatements(sql_stmts, ExecutorDeviceType::CPU)
      .back();
}

class AssertValueEqualsVisitor : public boost::static_visitor<> {
 public:
  AssertValueEqualsVisitor(const size_t& row, const size_t& column)
      : row(row), column(column) {}

  template <typename T, typename U>
  void operator()(const T& expected, const U& actual) const {
    FAIL() << boost::format(
                  "Values are of different types. Expected result set value: %s is of "
                  "type: %s while actual result set value: %s is of type: %s. At row: "
                  "%d, column: %d") %
                  expected % typeid(expected).name() % actual % typeid(actual).name() %
                  row % column;
  }

  template <typename T>
  void operator()(const T& expected, const T& actual) const {
    EXPECT_EQ(expected, actual) << boost::format("At row: %d, column: %d") % row % column;
  }

 private:
  size_t row;
  size_t column;
};

template <>
void AssertValueEqualsVisitor::operator()<NullableString>(
    const NullableString& expected,
    const NullableString& actual) const {
  boost::apply_visitor(AssertValueEqualsVisitor(row, column), expected, actual);
}

void assert_value_equals(ScalarTargetValue& expected,
                         ScalarTargetValue& actual,
                         const size_t& row,
                         const size_t& column) {
  boost::apply_visitor(AssertValueEqualsVisitor(row, column), expected, actual);
}

void compare_result_set(
    const std::vector<std::vector<ScalarTargetValue>>& expected_result_set,
    const std::shared_ptr<ResultSet>& actual_result_set) {
  auto row_count = actual_result_set->rowCount(false);
  ASSERT_EQ(expected_result_set.size(), row_count)
      << "Returned result set does not have the expected number of rows";

  if (row_count == 0) {
    return;
  }

  auto expected_column_count = expected_result_set[0].size();
  auto column_count = actual_result_set->colCount();
  ASSERT_EQ(expected_column_count, column_count)
      << "Returned result set does not have the expected number of columns";
  ;

  for (size_t r = 0; r < row_count; ++r) {
    auto row = actual_result_set->getNextRow(true, true);
    for (size_t c = 0; c < column_count; c++) {
      auto column_value = boost::get<ScalarTargetValue>(row[c]);
      auto expected_column_value = expected_result_set[r][c];
      assert_value_equals(expected_column_value, column_value, r, c);
    }
  }
}
}  // namespace

const char* test_table_schema = R"(
    CREATE TABLE test_table (
        i32 INT,
        i64 BIGINT,
        d DOUBLE,
        f FLOAT,
        str TEXT
    ) WITH (FRAGMENT_SIZE=2);
)";

/**
 * @brief Class used for setting up and tearing down tables and records that are required
 */
class VeloxFunctionTest : public testing::Test {
 public:
  void SetUp() override {
    ASSERT_NO_THROW(run_ddl_statement("drop table if exists test_table;"););
    ASSERT_NO_THROW(run_ddl_statement(test_table_schema));
    ASSERT_NO_THROW(sql("insert into test_table values(1, 100000000, 1.1, 1.11111111, 'CN');"));
    ASSERT_NO_THROW(sql("insert into test_table values(2, 200000000, 8.0, 2.22222222, 'USs');"));
    ASSERT_NO_THROW(sql("insert into test_table values(3, 300000000, 3.1, 3.33333333, 'UKkk');"));
    ASSERT_NO_THROW(multi_sql(R"(
        drop table if exists lower_function_test;
        create table lower_function_test(first_name text, last_name text encoding none, age integer, country_code text);
        insert into lower_function_test values('JOHN', 'SMITH', 25, 'us');
        insert into lower_function_test values('ABCDEFGHIJKLMNOPQRSTUVWXYZ', 'Banks', 30, 'Us');
        insert into lower_function_test values('JOHN', 'Wilson', 20, 'cA');
        insert into lower_function_test values('Sue', 'Smith', 25, 'CA');
      )"););
  }

  void TearDown() override {
    ASSERT_NO_THROW(multi_sql(R"(
        drop table test_table;
        drop table lower_function_test;
      )"););
  }
};

TEST_F(VeloxFunctionTest, Cbrt) {
  ASSERT_EQ(2.0,
      TestHelpers::v<double>(run_simple_agg(
          "SELECT cbrt(d) FROM test_table where i32 = 2")));
}

TEST_F(VeloxFunctionTest, Lower) {
  auto result_set = sql("select lower(first_name) from lower_function_test;");
  std::vector<std::vector<ScalarTargetValue>> expected_result_set{
      {"john"}, {"abcdefghijklmnopqrstuvwxyz"}, {"john"}, {"sue"}};
  compare_result_set(expected_result_set, result_set);
}

int main(int argc, char** argv) {
  TestHelpers::init_logger_stderr_only(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  QueryRunner::QueryRunner::init(BASE_PATH);
  g_enable_experimental_string_functions = true;

  // Use system locale setting by default (as done in the server).
  boost::locale::generator generator;
  std::locale::global(generator.generate(""));

  int err{0};
  try {
    err = RUN_ALL_TESTS();
  } catch (const std::exception& e) {
    LOG(ERROR) << e.what();
  }

  g_enable_experimental_string_functions = false;
  QueryRunner::QueryRunner::reset();
  return err;
}
