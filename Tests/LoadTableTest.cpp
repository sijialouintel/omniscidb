/*
 * Copyright 2020 OmniSci, Inc.
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

#include <arrow/api.h>
#include <arrow/io/memory.h>
#include <arrow/ipc/api.h>
#include <arrow/ipc/writer.h>
#include <gtest/gtest.h>
#include <boost/filesystem.hpp>

#ifdef HAVE_AWS_S3
#include "AwsHelpers.h"
#include "DataMgr/OmniSciAwsSdk.h"
#include "Shared/ThriftTypesConvert.h"
#endif  // HAVE_AWS_S3
#include "Shared/ArrowUtil.h"
#include "Tests/DBHandlerTestHelpers.h"
#include "Tests/TestHelpers.h"

#ifndef BASE_PATH
#define BASE_PATH "./tmp"
#endif

class LoadTableTest : public DBHandlerTestFixture {
 protected:
  void SetUp() override {
    DBHandlerTestFixture::SetUp();
    sql("DROP TABLE IF EXISTS load_test");
    sql("DROP TABLE IF EXISTS geo_load_test");
    sql("CREATE TABLE geo_load_test(i1 INTEGER, ls LINESTRING DEFAULT 'LINESTRING(0 0, 1 "
        "1)', s TEXT, mp MULTIPOLYGON, "
        "nns TEXT not null)");
    sql("CREATE TABLE load_test(i1 INTEGER, s TEXT DEFAULT 'default str' ENCODING "
        "DICT(8), nns TEXT not null)");
    initData();
  }

  void TearDown() override {
    sql("DROP TABLE IF EXISTS load_test");
    sql("DROP TABLE IF EXISTS geo_load_test");
    DBHandlerTestFixture::TearDown();
  }

  TStringValue getNullSV() const {
    TStringValue v;
    v.is_null = true;
    v.str_val = "";
    return v;
  }

  TStringValue getSV(const std::string& value) const {
    TStringValue v;
    v.is_null = false;
    v.str_val = value;
    return v;
  }

  const std::string LINESTRING = "LINESTRING (0 0,1 1,1 2)";
  const std::string DEFAULT_LINESTRING = "LINESTRING (0 0,1 1)";
  const std::string MULTIPOLYGON =
      "MULTIPOLYGON (((0 0,4 0,4 4,0 4,0 0),"
      "(1 1,1 2,2 2,2 1,1 1)),((-1 -1,-2 -1,-2 -2,-1 -2,-1 -1)))";
  TColumn i1_column, s_column, nns_column, ls_column, mp_column;
  TDatum i1_datum, s_datum, nns_datum, ls_datum, mp_datum;
  std::shared_ptr<arrow::Field> i1_field, s_field, nns_field, ls_field, mp_field;

 private:
  void initData() {
    i1_column.nulls = s_column.nulls = nns_column.nulls = ls_column.nulls =
        mp_column.nulls = {false};
    i1_column.data.int_col = {1};
    s_column.data.str_col = {"s"};
    nns_column.data.str_col = {"nns"};
    ls_column.data.str_col = {LINESTRING};
    mp_column.data.str_col = {MULTIPOLYGON};

    i1_datum.is_null = s_datum.is_null = nns_datum.is_null = ls_datum.is_null =
        mp_datum.is_null = false;
    i1_datum.val.int_val = 1;
    s_datum.val.str_val = "s";
    nns_datum.val.str_val = "nns";
    ls_datum.val.str_val = LINESTRING;
    mp_datum.val.str_val = MULTIPOLYGON;

    i1_field = arrow::field("i1", arrow::int32());
    s_field = arrow::field("s", arrow::utf8());
    nns_field = arrow::field("nns", arrow::utf8());
    ls_field = arrow::field("ls", arrow::utf8());
    mp_field = arrow::field("mp", arrow::utf8());
  }
};

TEST_F(LoadTableTest, AllColumnsNoGeo) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  TStringRow row;
  row.cols = {getSV("1"), getSV("s"), getSV("nns")};
  handler->load_table(session, "load_test", {row}, {});
  sqlAndCompareResult("SELECT * FROM load_test", {{i(1), "s", "nns"}});
}

TEST_F(LoadTableTest, AllColumns) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  TStringRow row;
  row.cols = {
      getSV("1"), getSV(LINESTRING), getSV("s"), getSV(MULTIPOLYGON), getSV("nns")};
  handler->load_table(session, "geo_load_test", {row}, {});
  sqlAndCompareResult("SELECT * FROM geo_load_test",
                      {{i(1), LINESTRING, "s", MULTIPOLYGON, "nns"}});
}

TEST_F(LoadTableTest, AllColumnsReordered) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"mp", "nns", "ls", "i1", "s"};
  TStringRow row;
  row.cols = {
      getSV(MULTIPOLYGON), getSV("nns"), getSV(LINESTRING), getSV("1"), getSV("s")};
  handler->load_table(session, "geo_load_test", {row}, column_names);
  sqlAndCompareResult("SELECT mp, nns, ls, i1, s FROM geo_load_test",
                      {{MULTIPOLYGON, "nns", LINESTRING, i(1), "s"}});
}

TEST_F(LoadTableTest, SomeColumnsReordered) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"mp", "nns", "ls"};
  TStringRow row;
  row.cols = {getSV(MULTIPOLYGON), getSV("nns"), getSV(LINESTRING)};
  handler->load_table(session, "geo_load_test", {row}, column_names);
  sqlAndCompareResult("SELECT mp, nns, ls, i1, s FROM geo_load_test",
                      {{MULTIPOLYGON, "nns", LINESTRING, nullptr, nullptr}});
}

TEST_F(LoadTableTest, OmitNotNullableColumn) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"mp", "ls"};
  TStringRow row;
  row.cols = {getSV(MULTIPOLYGON), getSV(LINESTRING)};
  executeLambdaAndAssertException(
      [&]() { handler->load_table(session, "geo_load_test", {row}, column_names); },
      "TException - service has thrown: TOmniSciException(error_msg="
      "Column 'nns' cannot be omitted due to NOT NULL constraint)");
}

TEST_F(LoadTableTest, OmitGeoColumn) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"i1", "s", "nns", "ls"};
  TStringRow row;
  row.cols = {getSV("1"), getSV("s"), getSV("nns"), getSV(LINESTRING)};
  handler->load_table(session, "geo_load_test", {row}, column_names);
  sqlAndCompareResult("SELECT i1, s, nns, mp, ls FROM geo_load_test",
                      {{i(1), "s", "nns", (void*)0, LINESTRING}});
}

TEST_F(LoadTableTest, DuplicateColumns) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"mp", "ls", "mp", "nns"};
  TStringRow row;
  row.cols = {getSV(MULTIPOLYGON), getSV(LINESTRING), getSV(MULTIPOLYGON), getSV("nns")};
  executeLambdaAndAssertException(
      [&]() { handler->load_table(session, "geo_load_test", {row}, column_names); },
      "TException - service has thrown: TOmniSciException(error_msg="
      "Column mp is mentioned multiple times)");
}

TEST_F(LoadTableTest, UnexistingColumn) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"mp", "ls", "mp2", "nns"};
  TStringRow row;
  row.cols = {getSV(MULTIPOLYGON), getSV(LINESTRING), getSV(MULTIPOLYGON), getSV("nns")};
  executeLambdaAndAssertException(
      [&]() { handler->load_table(session, "geo_load_test", {row}, column_names); },
      "TException - service has thrown: TOmniSciException(error_msg="
      "Column mp2 does not exist)");
}

TEST_F(LoadTableTest, ColumnNumberMismatch) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"mp", "ls", "i1", "nns"};
  TStringRow row;
  row.cols = {getSV(MULTIPOLYGON), getSV(LINESTRING), getSV("nns")};
  executeLambdaAndAssertException(
      [&]() { handler->load_table(session, "geo_load_test", {row}, column_names); },
      "TException - service has thrown: TOmniSciException(error_msg="
      "Number of columns specified does not match the number of columns given (3 vs 4))");
}

TEST_F(LoadTableTest, NoColumns) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  executeLambdaAndAssertException(
      [&]() { handler->load_table(session, "geo_load_test", {}, {}); },
      "TException - service has thrown: TOmniSciException(error_msg="
      "No rows to insert)");
}

TEST_F(LoadTableTest, DefaultString) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"i1", "nns"};
  TStringRow row;
  row.cols = {getSV("1"), getSV("nns")};
  handler->load_table(session, "load_test", {row}, column_names);
  sqlAndCompareResult("SELECT i1, s, nns FROM load_test", {{i(1), "default str", "nns"}});
}

TEST_F(LoadTableTest, DefaultGeo) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"i1", "s", "nns", "mp"};
  TStringRow row;
  row.cols = {getSV("1"), getSV("s"), getSV("nns"), getSV(MULTIPOLYGON)};
  handler->load_table(session, "geo_load_test", {row}, column_names);
  sqlAndCompareResult("SELECT i1, s, nns, mp, ls FROM geo_load_test",
                      {{i(1), "s", "nns", MULTIPOLYGON, DEFAULT_LINESTRING}});
}

TEST_F(LoadTableTest, BinaryAllColumnsNoGeo) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  TRow row;
  row.cols = {i1_datum, s_datum, nns_datum};
  handler->load_table_binary(session, "load_test", {row}, {});
  sqlAndCompareResult("SELECT * FROM load_test", {{i(1), "s", "nns"}});
}

TEST_F(LoadTableTest, DictOutOfBounds) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<TRow> rows;
  for (int i = 0; i < 300; i++) {
    TRow row;
    TDatum str_datum;
    str_datum.is_null = false;
    i1_datum.val.int_val = 1;
    str_datum.val.str_val = std::to_string(i);

    row.cols = {i1_datum, str_datum, nns_datum};
    rows.emplace_back(row);
  }
  executeLambdaAndAssertPartialException(
      [&]() { handler->load_table_binary(session, "load_test", rows, {}); },
      "has exceeded its limit of 8 bits (255 unique values). There was an attempt to add "
      "the new string '255'. Table will need to be recreated with larger String "
      "Dictionary Capacity");

  sqlAndCompareResult("SELECT count(*) FROM load_test", {{i(0)}});
}

// TODO(max): load_table_binary doesn't support tables with geo columns yet
TEST_F(LoadTableTest, DISABLED_BinaryAllColumns) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  TRow row;
  row.cols = {i1_datum, ls_datum, s_datum, mp_datum, nns_datum};
  handler->load_table_binary(session, "geo_load_test", {row}, {});
  sqlAndCompareResult("SELECT * FROM geo_load_test",
                      {{i(1), LINESTRING, "s", MULTIPOLYGON, "nns"}});
}

TEST_F(LoadTableTest, BinaryAllColumnsReorderedNoGeo) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"nns", "i1", "s"};
  TRow row;
  row.cols = {nns_datum, i1_datum, s_datum};
  handler->load_table_binary(session, "load_test", {row}, column_names);
  sqlAndCompareResult("SELECT i1, s, nns FROM load_test", {{i(1), "s", "nns"}});
}

TEST_F(LoadTableTest, BinarySomeColumnsReorderedNoGeo) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"nns", "s"};
  TRow row;
  row.cols = {nns_datum, s_datum};
  handler->load_table_binary(session, "load_test", {row}, column_names);
  sqlAndCompareResult("SELECT i1, s, nns FROM load_test", {{nullptr, "s", "nns"}});
}

TEST_F(LoadTableTest, BinaryOmitNotNullableColumnNoGeo) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"i1", "s"};
  TRow row;
  row.cols = {i1_datum, s_datum};
  executeLambdaAndAssertException(
      [&]() { handler->load_table_binary(session, "load_test", {row}, column_names); },
      "TException - service has thrown: TOmniSciException(error_msg="
      "Column 'nns' cannot be omitted due to NOT NULL constraint)");
}

TEST_F(LoadTableTest, BinaryDuplicateColumnsNoGeo) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"nns", "i1", "i1"};
  TRow row;
  row.cols = {nns_datum, i1_datum, i1_datum};
  executeLambdaAndAssertException(
      [&]() { handler->load_table_binary(session, "load_test", {row}, column_names); },
      "TException - service has thrown: TOmniSciException(error_msg="
      "Column i1 is mentioned multiple times)");
}

TEST_F(LoadTableTest, BinaryUnexistingColumnNoGeo) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"nns", "i1", "i2"};
  TRow row;
  row.cols = {nns_datum, i1_datum, i1_datum};
  executeLambdaAndAssertException(
      [&]() { handler->load_table_binary(session, "load_test", {row}, column_names); },
      "TException - service has thrown: TOmniSciException(error_msg="
      "Column i2 does not exist)");
}

TEST_F(LoadTableTest, BinaryColumnNumberMismatchNoGeo) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"nns", "i1", "s"};
  TRow row;
  row.cols = {nns_datum, i1_datum};
  executeLambdaAndAssertException(
      [&]() { handler->load_table_binary(session, "load_test", {row}, column_names); },
      "TException - service has thrown: TOmniSciException(error_msg="
      "Number of columns specified does not match the number of columns given "
      "(2 vs 3))");
}

TEST_F(LoadTableTest, BinaryNoColumns) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  executeLambdaAndAssertException(
      [&]() { handler->load_table_binary(session, "load_test", {}, {}); },
      "TException - service has thrown: TOmniSciException(error_msg="
      "No rows to insert)");
}

TEST_F(LoadTableTest, BinaryDefaultString) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"i1", "nns"};
  TRow row;
  row.cols = {i1_datum, nns_datum};
  handler->load_table_binary(session, "load_test", {row}, column_names);
  sqlAndCompareResult("SELECT * FROM load_test", {{i(1), "default str", "nns"}});
}

TEST_F(LoadTableTest, ColumnarAllColumnsNoGeo) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  handler->load_table_binary_columnar(
      session, "load_test", {i1_column, s_column, nns_column}, {});
  sqlAndCompareResult("SELECT * FROM load_test", {{i(1), "s", "nns"}});
}

TEST_F(LoadTableTest, ColumnarAllColumns) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  handler->load_table_binary_columnar(
      session,
      "geo_load_test",
      {i1_column, ls_column, s_column, mp_column, nns_column},
      {});
  sqlAndCompareResult("SELECT * FROM geo_load_test",
                      {{i(1), LINESTRING, "s", MULTIPOLYGON, "nns"}});
}

TEST_F(LoadTableTest, ColumnarAllColumnsReordered) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"mp", "nns", "ls", "i1", "s"};
  handler->load_table_binary_columnar(
      session,
      "geo_load_test",
      {mp_column, nns_column, ls_column, i1_column, s_column},
      column_names);
  sqlAndCompareResult("SELECT mp, nns, ls, i1, s FROM geo_load_test",
                      {{MULTIPOLYGON, "nns", LINESTRING, i(1), "s"}});
}

TEST_F(LoadTableTest, ColumnarSomeColumnsReordered) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"mp", "nns", "ls"};
  handler->load_table_binary_columnar(
      session, "geo_load_test", {mp_column, nns_column, ls_column}, column_names);
  sqlAndCompareResult("SELECT mp, nns, ls, i1, s FROM geo_load_test",
                      {{MULTIPOLYGON, "nns", LINESTRING, nullptr, nullptr}});
}

TEST_F(LoadTableTest, ColumnarOmitNotNullableColumn) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"mp", "ls"};
  executeLambdaAndAssertException(
      [&]() {
        handler->load_table_binary_columnar(
            session, "geo_load_test", {mp_column, ls_column}, column_names);
      },
      "Column 'nns' cannot be omitted due to NOT NULL constraint");
}

TEST_F(LoadTableTest, ColumnarOmitGeoColumn) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"i1", "s", "nns", "ls"};
  handler->load_table_binary_columnar(session,
                                      "geo_load_test",
                                      {i1_column, s_column, nns_column, ls_column},
                                      column_names);
  sqlAndCompareResult("SELECT i1, s, nns, mp, ls FROM geo_load_test",
                      {{i(1), "s", "nns", (void*)0, LINESTRING}});
}

TEST_F(LoadTableTest, ColumnarDuplicateColumns) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"mp", "ls", "mp"};
  executeLambdaAndAssertException(
      [&]() {
        handler->load_table_binary_columnar(
            session, "geo_load_test", {mp_column, ls_column, mp_column}, column_names);
      },
      "Column mp is mentioned multiple times");
}

TEST_F(LoadTableTest, ColumnarUnexistingColumn) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"mp", "ls", "mp2"};
  executeLambdaAndAssertException(
      [&]() {
        handler->load_table_binary_columnar(
            session, "geo_load_test", {mp_column, ls_column, mp_column}, column_names);
      },
      "Column mp2 does not exist");
}

TEST_F(LoadTableTest, ColumnarColumnNumberMismatch) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"mp", "ls", "i1"};
  executeLambdaAndAssertException(
      [&]() {
        handler->load_table_binary_columnar(
            session, "geo_load_test", {mp_column, ls_column}, column_names);
      },
      "Number of columns specified does not match the number of columns given (2 vs 3)");
}

TEST_F(LoadTableTest, ColumnarNoColumns) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  executeLambdaAndAssertException(
      [&]() { handler->load_table_binary_columnar(session, "geo_load_test", {}, {}); },
      "No columns to insert");
}

TEST_F(LoadTableTest, ColumnarDefaultStr) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"i1", "nns"};
  handler->load_table_binary_columnar(
      session, "load_test", {i1_column, nns_column}, column_names);
  sqlAndCompareResult("SELECT * FROM load_test", {{i(1), "default str", "nns"}});
}

TEST_F(LoadTableTest, ColumnarDefaultGeo) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::vector<std::string> column_names{"i1", "s", "mp", "nns"};
  handler->load_table_binary_columnar(session,
                                      "geo_load_test",
                                      {i1_column, s_column, mp_column, nns_column},
                                      column_names);
  sqlAndCompareResult("SELECT * FROM geo_load_test",
                      {{i(1), DEFAULT_LINESTRING, "s", MULTIPOLYGON, "nns"}});
}

// A small helper to build Arrow stream for load_table_binary_arrow
class ArrowStreamBuilder {
 public:
  ArrowStreamBuilder(const std::shared_ptr<arrow::Schema>& schema) : schema_(schema) {}

  std::string finish() {
    CHECK(columns_.size() == schema_->fields().size());
    size_t length = columns_.empty() ? 0 : columns_[0]->length();
    auto records = arrow::RecordBatch::Make(schema_, length, columns_);
    auto out_stream = *arrow::io::BufferOutputStream::Create();
    auto stream_writer = *arrow::ipc::MakeStreamWriter(out_stream.get(), schema_);
    ARROW_THROW_NOT_OK(stream_writer->WriteRecordBatch(*records));
    ARROW_THROW_NOT_OK(stream_writer->Close());
    auto buffer = *out_stream->Finish();
    columns_.clear();
    return buffer->ToString();
  }

  void appendInt32(const std::vector<int32_t>& values,
                   const std::vector<bool>& is_null = {}) {
    append<arrow::Int32Builder, int32_t>(values, is_null);
  }
  void appendString(const std::vector<std::string>& values,
                    const std::vector<bool>& is_null = {}) {
    append<arrow::StringBuilder, std::string>(values, is_null);
  }

 private:
  template <typename Builder, typename T>
  void append(const std::vector<T>& values, const std::vector<bool>& is_null) {
    Builder builder;
    CHECK(is_null.empty() || values.size() == is_null.size());
    for (size_t i = 0; i < values.size(); ++i) {
      if (!is_null.empty() && is_null[i]) {
        ARROW_THROW_NOT_OK(builder.AppendNull());
      } else {
        ARROW_THROW_NOT_OK(builder.Append(values[i]));
      }
    }
    columns_.push_back(nullptr);
    ARROW_THROW_NOT_OK(builder.Finish(&columns_.back()));
  }

  std::shared_ptr<arrow::Schema> schema_;
  std::vector<std::shared_ptr<arrow::Array>> columns_;
};

TEST_F(LoadTableTest, ArrowAllColumnsNoGeo) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  auto schema = arrow::schema({i1_field, s_field, nns_field});
  ArrowStreamBuilder builder(schema);
  builder.appendInt32({1});
  builder.appendString({"s"});
  builder.appendString({"nns"});
  handler->load_table_binary_arrow(session, "load_test", builder.finish(), false);
  sqlAndCompareResult("SELECT * FROM load_test", {{i(1), "s", "nns"}});
}

// TODO (max) load_table_binary_arrow doesn't support tables with geocolumns properly yet
TEST_F(LoadTableTest, DISABLED_ArrowAllColumns) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  auto schema = arrow::schema({i1_field, ls_field, s_field, mp_field, nns_field});
  ArrowStreamBuilder builder(schema);
  builder.appendInt32({1});
  builder.appendString({LINESTRING});
  builder.appendString({"s"});
  builder.appendString({MULTIPOLYGON});
  builder.appendString({"nns"});
  handler->load_table_binary_arrow(session, "geo_load_test", builder.finish(), false);
  sqlAndCompareResult("SELECT * FROM geo_load_test",
                      {{i(1), LINESTRING, "s", MULTIPOLYGON, "nns"}});
}

TEST_F(LoadTableTest, ArrowAllColumnsReorderedNoGeo) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  auto schema = arrow::schema({nns_field, i1_field, s_field});
  ArrowStreamBuilder builder(schema);
  builder.appendString({"nns"});
  builder.appendInt32({1});
  builder.appendString({"s"});
  handler->load_table_binary_arrow(session, "load_test", builder.finish(), true);
  sqlAndCompareResult("SELECT i1, s, nns FROM load_test", {{i(1), "s", "nns"}});
}

TEST_F(LoadTableTest, ArrowSomeColumnsReorderedNoGeo) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  auto schema = arrow::schema({nns_field, s_field});
  ArrowStreamBuilder builder(schema);
  builder.appendString({"nns"});
  builder.appendString({"s"});
  handler->load_table_binary_arrow(session, "load_test", builder.finish(), true);
  sqlAndCompareResult("SELECT i1, s, nns FROM load_test", {{nullptr, "s", "nns"}});
}

TEST_F(LoadTableTest, ArrowOmitNotNullableColumnNoGeo) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  auto schema = arrow::schema({i1_field, s_field});
  ArrowStreamBuilder builder(schema);
  builder.appendInt32({1});
  builder.appendString({"s"});
  executeLambdaAndAssertException(
      [&]() {
        handler->load_table_binary_arrow(session, "load_test", builder.finish(), true);
      },
      "Column 'nns' cannot be omitted due to NOT NULL constraint");
}

TEST_F(LoadTableTest, ArrowDuplicateColumnsNoGeo) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  auto schema = arrow::schema({nns_field, i1_field, i1_field});
  ArrowStreamBuilder builder(schema);
  builder.appendString({"nns"});
  builder.appendInt32({1});
  builder.appendInt32({1});
  executeLambdaAndAssertException(
      [&]() {
        handler->load_table_binary_arrow(session, "load_test", builder.finish(), true);
      },
      "Column i1 is mentioned multiple times");
}

TEST_F(LoadTableTest, ArrowUnexistingColumnNoGeo) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  std::shared_ptr<arrow::Field> i2_field = arrow::field("i2", arrow::int32());
  auto bad_schema = arrow::schema({nns_field, i1_field, i2_field});
  ArrowStreamBuilder builder(bad_schema);
  builder.appendString({"nns"});
  builder.appendInt32({1});
  builder.appendInt32({2});
  executeLambdaAndAssertException(
      [&]() {
        handler->load_table_binary_arrow(session, "load_test", builder.finish(), true);
      },
      "Column i2 does not exist");
}

TEST_F(LoadTableTest, ArrowNoColumns) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  auto schema = arrow::schema({});
  ArrowStreamBuilder builder(schema);
  executeLambdaAndAssertException(
      [&]() {
        handler->load_table_binary_arrow(session, "load_test", builder.finish(), false);
      },
      "No columns to insert");
}

TEST_F(LoadTableTest, ArrowDefaultStr) {
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  auto schema = arrow::schema({i1_field, nns_field});
  ArrowStreamBuilder builder(schema);
  builder.appendInt32({1});
  builder.appendString({"nns"});
  handler->load_table_binary_arrow(session, "load_test", builder.finish(), true);
  sqlAndCompareResult("SELECT * FROM load_test", {{i(1), "default str", "nns"}});
}

class ImportGeoTableTest : public DBHandlerTestFixture {
 protected:
  void SetUp() override {
    DBHandlerTestFixture::SetUp();
    sql("DROP TABLE IF EXISTS import_geo_table_test");
  }

  const std::string getGeoFileName() const {
    auto geo_file_name =
        boost::filesystem::absolute(
            boost::filesystem::path(
                "../../Tests/ImportGeoTableTest/datafiles/geospatial_poly.geojson"))
            .string();
    return geo_file_name;
  }

  const TCopyParams getCopyParams() const {
    TCopyParams copy_params;
    copy_params.file_type = TFileType::GEO;
    return copy_params;
  }

  const TCreateParams getCreateParams() const {
    TCreateParams create_params;
    create_params.is_replicated = false;
    return create_params;
  }

  TColumnType getScalarColumnType(const std::string& name,
                                  const TDatumType::type type) const {
    TColumnType ct;
    ct.col_name = name;
    ct.col_type.type = type;
    ct.col_type.encoding = TEncodingType::type::NONE;
    ct.col_type.nullable = true;
    ct.col_type.is_array = false;
    ct.col_type.precision = 0;
    ct.col_type.scale = 0;
    ct.col_type.comp_param = 0;
    ct.col_type.size = 0;
    ct.is_reserved_keyword = false;
    ct.src_name = name;
    ct.is_system = false;
    ct.is_physical = false;
    ct.col_id = 0;
    return ct;
  };

  TColumnType getPolyColumnType(const std::string& name) const {
    TColumnType ct;
    ct.col_name = name;
    ct.col_type.type = TDatumType::type::POLYGON;
    ct.col_type.encoding = TEncodingType::type::GEOINT;
    ct.col_type.nullable = true;
    ct.col_type.is_array = false;
    ct.col_type.precision = 23;  // aka subtype = SQLTypes::kGEOMETRY (not the same as
                                 // TDatumType::type::GEOMETRY)
    ct.col_type.scale = 4326;    // aka output_srid = WGS84
    ct.col_type.comp_param = 32;
    ct.col_type.size = 0;
    ct.is_reserved_keyword = false;
    ct.src_name = name;
    ct.is_system = false;
    ct.is_physical = false;
    ct.col_id = 0;
    return ct;
  };

  void TearDown() override {
    sql("DROP TABLE IF EXISTS import_geo_table_test");
    DBHandlerTestFixture::TearDown();
  }
};

TEST_F(ImportGeoTableTest, ImportGeoTableAuto) {
  // geo import with empty row descriptor
  // will automatically create table
  // equivalent to COPY FROM WITH (source_type='geo_file')
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  TRowDescriptor row_descriptor;
  EXPECT_NO_THROW(handler->import_geo_table(session,
                                            "import_geo_table_test",
                                            getGeoFileName(),
                                            getCopyParams(),
                                            row_descriptor,
                                            getCreateParams()));
  sqlAndCompareResult("SELECT count(*) FROM import_geo_table_test", {{i(10)}});
  sqlAndCompareResult("SELECT trip FROM import_geo_table_test WHERE rowid=0", {{0.0f}});
}

TEST_F(ImportGeoTableTest, ImportGeoTableExplicit) {
  // geo import with explicit row descriptor (e.g. Immerse import)
  // must create table first
  // correct types
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  TRowDescriptor row_descriptor{getScalarColumnType("trip", TDatumType::type::FLOAT),
                                getPolyColumnType("omnisci_geo")};
  EXPECT_NO_THROW(handler->create_table(session,
                                        "import_geo_table_test",
                                        row_descriptor,
                                        TFileType::type::GEO,
                                        getCreateParams()));
  EXPECT_NO_THROW(handler->import_geo_table(session,
                                            "import_geo_table_test",
                                            getGeoFileName(),
                                            getCopyParams(),
                                            row_descriptor,
                                            getCreateParams()));
  sqlAndCompareResult("SELECT count(*) FROM import_geo_table_test", {{i(10)}});
  sqlAndCompareResult("SELECT trip FROM import_geo_table_test WHERE rowid=0", {{0.0f}});
}

TEST_F(ImportGeoTableTest, ImportGeoTableOverride) {
  // geo import with explicit row descriptor (e.g. Immerse import)
  // must create table first
  // type of column 'trip' overridden from FLOAT to INT (valid)
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  TRowDescriptor row_descriptor{getScalarColumnType("trip", TDatumType::type::INT),
                                getPolyColumnType("omnisci_geo")};
  EXPECT_NO_THROW(handler->create_table(session,
                                        "import_geo_table_test",
                                        row_descriptor,
                                        TFileType::type::GEO,
                                        getCreateParams()));
  EXPECT_NO_THROW(handler->import_geo_table(session,
                                            "import_geo_table_test",
                                            getGeoFileName(),
                                            getCopyParams(),
                                            row_descriptor,
                                            getCreateParams()));
  sqlAndCompareResult("SELECT count(*) FROM import_geo_table_test", {{i(10)}});
  sqlAndCompareResult("SELECT trip FROM import_geo_table_test WHERE rowid=0", {{i(0)}});
}

TEST_F(ImportGeoTableTest, ImportGeoTableTypeMismatch1) {
  // geo import with explicit row descriptor (e.g. Immerse import)
  // must create table first
  // types of columns swapped (possible in Immerse for now)
  // import will not fail, but should reject all rows
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  TRowDescriptor row_descriptor{
      getPolyColumnType("trip"),
      getScalarColumnType("omnisci_geo", TDatumType::type::FLOAT)};
  EXPECT_NO_THROW(handler->create_table(session,
                                        "import_geo_table_test",
                                        row_descriptor,
                                        TFileType::type::GEO,
                                        getCreateParams()));
  EXPECT_THROW(handler->import_geo_table(session,
                                         "import_geo_table_test",
                                         getGeoFileName(),
                                         getCopyParams(),
                                         row_descriptor,
                                         getCreateParams()),
               TOmniSciException);
  sqlAndCompareResult("SELECT count(*) FROM import_geo_table_test", {{i(0)}});
}

TEST_F(ImportGeoTableTest, ImportGeoTableFailTypeMismatch2) {
  // geo import with explicit row descriptor (e.g. Immerse import)
  // must create table first
  // column types valid but columns swapped (possible in Immerse for now)
  // import will not fail, but should reject all rows
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  TRowDescriptor row_descriptor{
      getScalarColumnType("omnisci_geo", TDatumType::type::FLOAT),
      getPolyColumnType("trip")};
  EXPECT_NO_THROW(handler->create_table(session,
                                        "import_geo_table_test",
                                        row_descriptor,
                                        TFileType::type::GEO,
                                        getCreateParams()));
  EXPECT_THROW(handler->import_geo_table(session,
                                         "import_geo_table_test",
                                         getGeoFileName(),
                                         getCopyParams(),
                                         row_descriptor,
                                         getCreateParams()),
               TOmniSciException);
  sqlAndCompareResult("SELECT count(*) FROM import_geo_table_test", {{i(0)}});
}

TEST_F(ImportGeoTableTest, ImportGeoTableFailNoGeoColumns) {
  // geo import with explicit row descriptor (e.g. Immerse import)
  // must create table first
  // no geo columns in row descriptor
  // import should fail
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  TRowDescriptor row_descriptor{getScalarColumnType("trip", TDatumType::type::FLOAT)};
  EXPECT_NO_THROW(handler->create_table(session,
                                        "import_geo_table_test",
                                        row_descriptor,
                                        TFileType::type::GEO,
                                        getCreateParams()));
  EXPECT_THROW(handler->import_geo_table(session,
                                         "import_geo_table_test",
                                         getGeoFileName(),
                                         getCopyParams(),
                                         row_descriptor,
                                         getCreateParams()),
               TOmniSciException);
  sqlAndCompareResult("SELECT count(*) FROM import_geo_table_test", {{i(0)}});
}

TEST_F(ImportGeoTableTest, ImportGeoTableFailTooManyGeoColumns) {
  // geo import with explicit row descriptor (e.g. Immerse import)
  // must create table first
  // more than one geo column in row descriptor
  // import should fail
  auto* handler = getDbHandlerAndSessionId().first;
  auto& session = getDbHandlerAndSessionId().second;
  TRowDescriptor row_descriptor{getScalarColumnType("trip", TDatumType::type::FLOAT),
                                getPolyColumnType("omnisci_geo1"),
                                getPolyColumnType("omnisci_geo2")};
  EXPECT_NO_THROW(handler->create_table(session,
                                        "import_geo_table_test",
                                        row_descriptor,
                                        TFileType::type::GEO,
                                        getCreateParams()));
  EXPECT_THROW(handler->import_geo_table(session,
                                         "import_geo_table_test",
                                         getGeoFileName(),
                                         getCopyParams(),
                                         row_descriptor,
                                         getCreateParams()),
               TOmniSciException);
  sqlAndCompareResult("SELECT count(*) FROM import_geo_table_test", {{i(0)}});
}

#ifdef HAVE_AWS_S3
class ThriftDetectServerPrivilegeTest : public DBHandlerTestFixture {
 protected:
  inline const static std::string PUBLIC_S3_FILE =
      "s3://omnisci-fsi-test-public/FsiDataFiles/0.csv";
  inline const static std::string PRIVATE_S3_FILE =
      "s3://omnisci-fsi-test/FsiDataFiles/0.csv";
  inline const static std::string AWS_DUMMY_CREDENTIALS_DIR =
      to_string(BASE_PATH) + "/aws";
  inline static std::map<std::string, std::string> aws_environment_;

  static void SetUpTestSuite() {
    DBHandlerTestFixture::SetUpTestSuite();
    omnisci_aws_sdk::init_sdk();
    g_allow_s3_server_privileges = true;
    aws_environment_ = unset_aws_env();
    create_stub_aws_profile(AWS_DUMMY_CREDENTIALS_DIR);
  }

  static void TearDownTestSuite() {
    DBHandlerTestFixture::TearDownTestSuite();
    omnisci_aws_sdk::shutdown_sdk();
    g_allow_s3_server_privileges = false;
    restore_aws_env(aws_environment_);
    boost::filesystem::remove_all(AWS_DUMMY_CREDENTIALS_DIR);
  }

  std::string detectTable(const std::string& file_name,
                          const std::string& s3_access_key = "",
                          const std::string& s3_secret_key = "",
                          const std::string& s3_session_token = "",
                          const std::string& s3_region = "us-west-1") {
    const auto& db_handler_and_session_id = getDbHandlerAndSessionId();
    TDetectResult thrift_result;
    TCopyParams copy_params;
    // Setting S3 credentials through copy params simulates
    // environment variables configured on the omnisql client
    copy_params.s3_access_key = s3_access_key;
    copy_params.s3_secret_key = s3_secret_key;
    copy_params.s3_session_token = s3_session_token;
    copy_params.s3_region = s3_region;
    db_handler_and_session_id.first->detect_column_types(
        thrift_result, db_handler_and_session_id.second, file_name, copy_params);
    std::stringstream oss;
    for (const auto& tct : thrift_result.row_set.row_desc) {
      oss << tct.col_name;
    }
    oss << "\n";
    for (const auto& tct : thrift_result.row_set.row_desc) {
      oss << type_info_from_thrift(tct.col_type).get_type_name();
    }
    oss << "\n";
    for (const auto& row : thrift_result.row_set.rows) {
      for (const auto& col : row.cols) {
        oss << col.val.str_val;
      }
      oss << "\n";
    }
    oss << "\nCREATE TABLE your_table_name(";
    for (size_t i = 0; i < thrift_result.row_set.row_desc.size(); ++i) {
      const auto tct = thrift_result.row_set.row_desc[i];
      oss << (i ? ", " : "") << tct.col_name << " "
          << type_info_from_thrift(tct.col_type).get_type_name();
      if (type_info_from_thrift(tct.col_type).is_string()) {
        oss << " ENCODING DICT";
      }
      if (type_info_from_thrift(tct.col_type).is_array()) {
        oss << "[";
        if (type_info_from_thrift(tct.col_type).get_size() > 0) {
          oss << type_info_from_thrift(tct.col_type).get_size();
        }
        oss << "]";
      }
    }
    oss << ");\n";

    return oss.str();
  }
};

TEST_F(ThriftDetectServerPrivilegeTest, S3_Public_without_credentials) {
  set_aws_profile(AWS_DUMMY_CREDENTIALS_DIR, false);
  const auto result = detectTable(PUBLIC_S3_FILE);
  ASSERT_EQ(result, "i\nSMALLINT\n0\n\nCREATE TABLE your_table_name(i SMALLINT);\n");
}

TEST_F(ThriftDetectServerPrivilegeTest, S3_Private_without_credentials) {
  if (is_valid_aws_role()) {
    GTEST_SKIP();
  }
  set_aws_profile(AWS_DUMMY_CREDENTIALS_DIR, false);
  EXPECT_THROW(detectTable(PRIVATE_S3_FILE), TOmniSciException);
}

TEST_F(ThriftDetectServerPrivilegeTest, S3_Private_with_invalid_specified_credentials) {
  if (!is_valid_aws_key(aws_environment_)) {
    GTEST_SKIP();
  }
  set_aws_profile(AWS_DUMMY_CREDENTIALS_DIR, false);
  EXPECT_THROW(detectTable(PRIVATE_S3_FILE, "invalid_access_key", "invalid_secret_key"),
               TOmniSciException);
}

TEST_F(ThriftDetectServerPrivilegeTest, S3_Private_with_valid_specified_credentials) {
  if (!is_valid_aws_key(aws_environment_)) {
    GTEST_SKIP();
  }
  const auto aws_access_key_id = aws_environment_.find("AWS_ACCESS_KEY_ID")->second;
  const auto aws_secret_access_key =
      aws_environment_.find("AWS_SECRET_ACCESS_KEY")->second;
  set_aws_profile(AWS_DUMMY_CREDENTIALS_DIR, false);
  const auto result =
      detectTable(PRIVATE_S3_FILE, aws_access_key_id, aws_secret_access_key);
  ASSERT_EQ(result, "i\nSMALLINT\n0\n\nCREATE TABLE your_table_name(i SMALLINT);\n");
}

TEST_F(ThriftDetectServerPrivilegeTest, S3_Private_with_env_credentials) {
  if (!is_valid_aws_key(aws_environment_)) {
    GTEST_SKIP();
  }
  restore_aws_keys(aws_environment_);
  set_aws_profile(AWS_DUMMY_CREDENTIALS_DIR, false);
  const auto result = detectTable(PRIVATE_S3_FILE);
  ASSERT_EQ(result, "i\nSMALLINT\n0\n\nCREATE TABLE your_table_name(i SMALLINT);\n");
  unset_aws_keys();
}

TEST_F(ThriftDetectServerPrivilegeTest, S3_Private_with_profile_credentials) {
  if (!is_valid_aws_key(aws_environment_)) {
    GTEST_SKIP();
  }
  set_aws_profile(AWS_DUMMY_CREDENTIALS_DIR, true, aws_environment_);
  const auto result = detectTable(PRIVATE_S3_FILE);
  ASSERT_EQ(result, "i\nSMALLINT\n0\n\nCREATE TABLE your_table_name(i SMALLINT);\n");
}

TEST_F(ThriftDetectServerPrivilegeTest, S3_Private_with_role_credentials) {
  if (!is_valid_aws_role()) {
    GTEST_SKIP();
  }
  set_aws_profile(AWS_DUMMY_CREDENTIALS_DIR, false);
  const auto result = detectTable(PRIVATE_S3_FILE);
  ASSERT_EQ(result, "i\nSMALLINT\n0\n\nCREATE TABLE your_table_name(i SMALLINT);\n");
}

class ThriftImportServerPrivilegeTest : public ThriftDetectServerPrivilegeTest {
 protected:
  void SetUp() override {
    ThriftDetectServerPrivilegeTest::SetUp();
    sql("DROP TABLE IF EXISTS import_test_table;");
    sql("CREATE TABLE import_test_table(i SMALLINT);");
  }

  void TearDown() override {
    ThriftDetectServerPrivilegeTest::TearDown();
    sql("DROP TABLE IF EXISTS import_test_table;");
  }

  void importTable(const std::string& file_name,
                   const std::string& table_name,
                   const std::string& s3_access_key = "",
                   const std::string& s3_secret_key = "",
                   const std::string& s3_session_token = "",
                   const std::string& s3_region = "us-west-1") {
    const auto& db_handler_and_session_id = getDbHandlerAndSessionId();
    TCopyParams copy_params;
    // Setting S3 credentials through copy params simulates
    // environment variables configured on the omnisql client
    copy_params.s3_access_key = s3_access_key;
    copy_params.s3_secret_key = s3_secret_key;
    copy_params.s3_session_token = s3_session_token;
    copy_params.s3_region = s3_region;
    db_handler_and_session_id.first->import_table(
        db_handler_and_session_id.second, table_name, file_name, copy_params);
  }
};

TEST_F(ThriftImportServerPrivilegeTest, S3_Public_without_credentials) {
  set_aws_profile(AWS_DUMMY_CREDENTIALS_DIR, false);
  EXPECT_NO_THROW(importTable(PUBLIC_S3_FILE, "import_test_table"));
  sqlAndCompareResult("SELECT * FROM import_test_table", {{i(0)}});
}

TEST_F(ThriftImportServerPrivilegeTest, S3_Private_without_credentials) {
  if (is_valid_aws_role()) {
    GTEST_SKIP();
  }
  set_aws_profile(AWS_DUMMY_CREDENTIALS_DIR, false);
  EXPECT_THROW(importTable(PRIVATE_S3_FILE, "import_test_table"), TOmniSciException);
}

TEST_F(ThriftImportServerPrivilegeTest, S3_Private_with_invalid_specified_credentials) {
  if (!is_valid_aws_key(aws_environment_)) {
    GTEST_SKIP();
  }
  set_aws_profile(AWS_DUMMY_CREDENTIALS_DIR, false);
  EXPECT_THROW(importTable(PRIVATE_S3_FILE,
                           "import_test_table",
                           "invalid_access_key",
                           "invalid_secret_key"),
               TOmniSciException);
}

TEST_F(ThriftImportServerPrivilegeTest, S3_Private_with_valid_specified_credentials) {
  if (!is_valid_aws_key(aws_environment_)) {
    GTEST_SKIP();
  }
  const auto aws_access_key_id = aws_environment_.find("AWS_ACCESS_KEY_ID")->second;
  const auto aws_secret_access_key =
      aws_environment_.find("AWS_SECRET_ACCESS_KEY")->second;
  set_aws_profile(AWS_DUMMY_CREDENTIALS_DIR, false);
  EXPECT_NO_THROW(importTable(
      PRIVATE_S3_FILE, "import_test_table", aws_access_key_id, aws_secret_access_key));
  sqlAndCompareResult("SELECT * FROM import_test_table", {{i(0)}});
}

TEST_F(ThriftImportServerPrivilegeTest, S3_Private_with_env_credentials) {
  if (!is_valid_aws_key(aws_environment_)) {
    GTEST_SKIP();
  }
  restore_aws_keys(aws_environment_);
  set_aws_profile(AWS_DUMMY_CREDENTIALS_DIR, false);
  EXPECT_NO_THROW(importTable(PRIVATE_S3_FILE, "import_test_table"));
  sqlAndCompareResult("SELECT * FROM import_test_table", {{i(0)}});
  unset_aws_keys();
}

TEST_F(ThriftImportServerPrivilegeTest, S3_Private_with_profile_credentials) {
  if (!is_valid_aws_key(aws_environment_)) {
    GTEST_SKIP();
  }
  set_aws_profile(AWS_DUMMY_CREDENTIALS_DIR, true, aws_environment_);
  EXPECT_NO_THROW(importTable(PRIVATE_S3_FILE, "import_test_table"));
  sqlAndCompareResult("SELECT * FROM import_test_table", {{i(0)}});
}

TEST_F(ThriftImportServerPrivilegeTest, S3_Private_with_role_credentials) {
  if (!is_valid_aws_role()) {
    GTEST_SKIP();
  }
  set_aws_profile(AWS_DUMMY_CREDENTIALS_DIR, false);
  EXPECT_NO_THROW(importTable(PRIVATE_S3_FILE, "import_test_table"));
  sqlAndCompareResult("SELECT * FROM import_test_table", {{i(0)}});
}

#endif  // HAVE_AWS_S3

int main(int argc, char** argv) {
  TestHelpers::init_logger_stderr_only(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  int err{0};
  try {
    err = RUN_ALL_TESTS();
  } catch (const std::exception& e) {
    LOG(ERROR) << e.what();
  }
  return err;
}
