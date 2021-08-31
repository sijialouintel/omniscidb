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

#include "com_mapd_CiderJNI.h"
#include <stdio.h>
#include <string.h>
#include <random>

#include "Logger/Logger.h"
#include "QueryEngine/CiderArrowResultProvider.h"
#include "QueryEngine/CiderPrestoResultProvider.h"
#include "QueryEngine/CiderResultProvider.h"
#include "QueryEngine/Descriptors/RelAlgExecutionDescriptor.h"
#include "QueryRunner/QueryRunner.h"

#include "Embedded/DBEngine.h"

#include <arrow/api.h>
#include "Shared/ArrowUtil.h"

namespace util {

bool replace(std::string& str, const std::string& from, const std::string& to) {
  size_t start_pos = str.find(from);
  if (start_pos == std::string::npos)
    return false;
  str.replace(start_pos, from.length(), to);
  return true;
}

std::string random_string(std::size_t length) {
  const std::string CHARACTERS =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

  std::random_device random_device;
  std::mt19937 generator(random_device());
  std::uniform_int_distribution<> distribution(0, CHARACTERS.size() - 1);

  std::string random_string;

  for (std::size_t i = 0; i < length; ++i) {
    random_string += CHARACTERS[distribution(generator)];
  }

  return random_string;
}

std::pair<std::shared_ptr<arrow::Schema>, std::string> parse_to_schema(
    const std::string& json_schema) {
  std::vector<std::shared_ptr<arrow::Field>> fields;
  std::string table_name = "";
  rapidjson::Document d;
  if (d.Parse(json_schema).HasParseError() || !d.HasMember("Columns")) {
    LOG(ERROR) << "invalid json for schema!";
  }
  table_name = d["Table"].GetString();
  const rapidjson::Value& columns = d["Columns"];
  std::string schema;
  for (rapidjson::Value::ConstValueIterator v_iter = columns.Begin();
       v_iter != columns.End();
       ++v_iter) {
    const rapidjson::Value& field = *v_iter;
    for (rapidjson::Value::ConstMemberIterator m_iter = field.MemberBegin();
         m_iter != field.MemberEnd();
         ++m_iter) {
      const char* col_name = m_iter->name.GetString();
      const char* col_type = m_iter->value.GetString();
      std::string col_type_str(col_type);
      std::shared_ptr<arrow::DataType> type;
      if (col_type_str.compare("INT") == 0)
        type = arrow::int32();
      else if (col_type_str.compare("LONG") == 0)
        type = arrow::int64();
      else if (col_type_str.compare("FLOAT") == 0)
        type = arrow::float32();
      else if (col_type_str.compare("DOUBLE") == 0)
        type = arrow::float64();
      else
        LOG(WARNING) << "unsupported type: " << col_type_str;

      auto f = std::make_shared<arrow::Field>(std::string(col_name), type);
      fields.push_back(f);
    }
  }
  std::shared_ptr<arrow::Schema> s = std::make_shared<arrow::Schema>(fields);

  return std::make_pair(s, table_name);
}

void convertToArrowTable(std::shared_ptr<arrow::Table>& table,
                         int64_t* dataBuffers,
                         int64_t* nullBuffers,
                         const std::string& schema,
                         int rowCount,
                         std::string& table_name) {
  // todo:: null buffer???
  LOG(INFO) << "start convert " << rowCount;
  auto [s, table_name_] = parse_to_schema(schema);
  table_name = table_name_;
  std::vector<std::shared_ptr<arrow::Array>> arrays;
  for (int i = 0; i < s->fields().size(); i++) {
    LOG(INFO) << "convert " << i << " name: " << s->fields()[i]->name();
    arrow::Type::type dataType = s->fields()[i]->type()->id();
    int length = 0;
    switch (dataType) {
      case arrow::Type::INT32:
      case arrow::Type::FLOAT:
        length = 4;
        break;
      case arrow::Type::INT64:
      case arrow::Type::DOUBLE:
        length = 8;
        break;
      default:
        LOG(FATAL) << "Unsupported type";
        break;
    }

    auto dataBuffer =
        std::make_shared<arrow::Buffer>((uint8_t*)(dataBuffers[i]), rowCount * length);
    auto nullBuffer =
        std::make_shared<arrow::Buffer>((uint8_t*)(nullBuffers[i]), rowCount);
    long ptr = (long)(dataBuffers[i]);

    std::shared_ptr<arrow::Array> array;
    if (dataType == arrow::Type::INT32) {
      array = std::make_shared<arrow::Int32Array>(rowCount, dataBuffer);
      LOG(DEBUG1) << "convert result, data: " << array->ToString();
      for (int j = 0; j < rowCount; j++) {
        // LOG(INFO) << "data: " << *((int32_t*)ptr + j);
      }
    } else if (dataType == arrow::Type::INT64) {
      array = std::make_shared<arrow::Int64Array>(rowCount, dataBuffer);
    } else if (dataType == arrow::Type::FLOAT) {
      array = std::make_shared<arrow::FloatArray>(rowCount, dataBuffer);
    } else if (dataType == arrow::Type::DOUBLE) {
      array = std::make_shared<arrow::FloatArray>(rowCount, dataBuffer);
    } else {
      LOG(WARNING) << "not supported type!";
    }
    arrays.push_back(array);
  }

  table = arrow::Table::Make(s, arrays, rowCount);
  LOG(DEBUG1) << "convert done " << table->ToString();

  return;
}

}  // namespace util

/*
 * Class:     com_mapd_CiderJNI
 * Method:    init
 * Signature: ()J
 */
JNIEXPORT jlong JNICALL Java_com_mapd_CiderJNI_init(JNIEnv* env, jclass cls) {
  logger::LogOptions log_options("");
  log_options.severity_ = logger::Severity::DEBUG4;
  log_options.severity_clog_ = logger::Severity::DEBUG4;
  logger::init(log_options);

  std::string opt_str = "/tmp/" + util::random_string(10) + " --calcite-port 5555";
  // todo: this is a shared_ptr, will it dispose?
  std::shared_ptr<EmbeddedDatabase::DBEngine>* pDbe =
      new std::shared_ptr<EmbeddedDatabase::DBEngine>;
  *pDbe = EmbeddedDatabase::DBEngine::create(opt_str);

  return reinterpret_cast<jlong>(pDbe);
}

/*
 * Class:     com_mapd_CiderJNI
 * Method:    close
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_com_mapd_CiderJNI_close(JNIEnv* env,
                                                    jclass cls,
                                                    jlong dbePtr) {
  std::shared_ptr<EmbeddedDatabase::DBEngine>* pDbe =
      reinterpret_cast<std::shared_ptr<EmbeddedDatabase::DBEngine>*>(dbePtr);
  (*pDbe).reset();
  delete pDbe;
}

/*
 * Class:     com_mapd_CiderJNI
 * Method:    processBlocks
 * Signature: (Ljava/lang/String;Ljava/lang/String;[J[J[J[JI)I
 */
JNIEXPORT jint JNICALL Java_com_mapd_CiderJNI_processBlocks(JNIEnv* env,
                                                            jclass cls,
                                                            jlong dbePtr,
                                                            jstring sql,
                                                            jstring schema,
                                                            jlongArray dataValues,
                                                            jlongArray dataNulls,
                                                            jlongArray resultValues,
                                                            jlongArray resultNulls,
                                                            jint rowCount) {
  if (dbePtr == 0) {
    LOG(FATAL) << "ERROR: NULLPTR ";
  }
  std::shared_ptr<EmbeddedDatabase::DBEngine>* pDbe =
      reinterpret_cast<std::shared_ptr<EmbeddedDatabase::DBEngine>*>(dbePtr);

  jsize dataValuesLen = env->GetArrayLength(dataValues);
  jsize dataNullsLen = env->GetArrayLength(dataNulls);

  jlong* dataValuesPtr = env->GetLongArrayElements(dataValues, 0);
  jlong* dataNullsPtr = env->GetLongArrayElements(dataNulls, 0);

  jsize resultValuesLen = env->GetArrayLength(resultValues);
  jsize resultNullsLen = env->GetArrayLength(resultNulls);

  jlong* resultValuesPtr = env->GetLongArrayElements(resultValues, 0);
  jlong* resultNullsPtr = env->GetLongArrayElements(resultNulls, 0);

  std::shared_ptr<arrow::Table> arrowTable;
  const char* schemaPtr = env->GetStringUTFChars(schema, nullptr);
  std::string tableSchema(schemaPtr);
  std::string table_name;
  util::convertToArrowTable(
      arrowTable, dataValuesPtr, dataNullsPtr, schemaPtr, rowCount, table_name);
  std::string random_table_name = table_name + util::random_string(10);

  const char* sqlPtr = env->GetStringUTFChars(sql, nullptr);
  std::string queryInfo = "execute relalg " + std::string(sqlPtr);
  // std::string queryInfo = "select a from " + table_name + " where c > 13;";

  auto ret = util::replace(queryInfo, table_name, random_table_name);
  if (!ret) {
    LOG(FATAL) << "table name not match!";
  }
  LOG(DEBUG1) << "table " << table_name << "   random " << random_table_name;

  (*pDbe)->importArrowTable(random_table_name, arrowTable);

  LOG(DEBUG1) << "import table done.";

  auto res = (*pDbe)->executeRA(queryInfo);

  LOG(DEBUG1) << "execute done.";

  int count = res->getRowCount();
  LOG(DEBUG1) << "return rows: " << count;

  // converting result
  auto rp = std::make_shared<CiderPrestoResultProvider>();
  rp->registerResultSet(res->getResultSet());
  std::vector<PrestoResult> result =
      std::any_cast<std::vector<PrestoResult>>(rp->convert());
  CHECK(resultValuesLen == result.size());
  LOG(INFO) << "copy result: " << resultValuesLen;
  for (int i = 0; i < resultValuesLen; i++) {
    int8_t* valueSrc = result[i].valueBuffer;
    int8_t* valueDst = (int8_t*)resultValuesPtr[i];
    int valueBufferLength = result[i].valueBufferLength;
    LOG(INFO) << "copy length: " << valueBufferLength;
    memcpy(valueDst, valueSrc, valueBufferLength);
    // int8_t* nullSrc = result[i].nullBuffer;
    int8_t* nullDst = (int8_t*)resultNullsPtr[i];
    int nullBufferLength = result[i].nullBufferLength;
    // memcpy(nullDst, nullSrc, nullBufferLength);
    memset(nullDst, 0x01, nullBufferLength);
  }

  rp->release();
  LOG(DEBUG1) << "convert done : " << count;

  return count;
}
