/*
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

#pragma once

#include "CiderResultProvider.h"

struct PrestoResult {
  int8_t* valueBuffer;
  int8_t* nullBuffer;
  int32_t valueBufferLength;
  int32_t nullBufferLength;
};

class CiderPrestoResultProvider : public CiderResultProvider {
 public:

  void release() {
    const auto col_count = rows_->colCount();
    for (size_t i = 0; i < col_count; ++i) {
      delete [] prestoResultVector[i].valueBuffer;
      delete [] prestoResultVector[i].nullBuffer;
    }
  }

  std::any convert() {
    const auto row_count = rows_->rowCount();
    const auto col_count = rows_->colCount();
    prestoResultVector.resize(col_count);
    for (size_t i = 0; i < col_count; ++i) {
      const auto ct = rows_->getColType(i);
      switch (get_physical_type(ct)) {
        case kBOOLEAN:
          prestoResultVector[i].valueBuffer = (int8_t*)new bool[row_count];
          break;
        case kTINYINT:
          prestoResultVector[i].valueBuffer = (int8_t*)new int8_t[row_count];
          break;
        case kSMALLINT:
          prestoResultVector[i].valueBuffer = (int8_t*)new int16_t[row_count];
          break;
        case kINT:
          prestoResultVector[i].valueBuffer = (int8_t*)new int32_t[row_count];
          break;
        case kBIGINT:
          prestoResultVector[i].valueBuffer = (int8_t*)new int64_t[row_count];
          break;
        case kFLOAT:
          prestoResultVector[i].valueBuffer = (int8_t*)new float[row_count];
          break;
        case kDOUBLE:
          prestoResultVector[i].valueBuffer = (int8_t*)new double[row_count];
          break;
        default:
          throw std::runtime_error(ct.get_type_name() +
          " is not supported in Presto result sets.");
      }
      prestoResultVector[i].valueBufferLength = 0;
      prestoResultVector[i].nullBuffer = (int8_t*)new int8_t[row_count];
      prestoResultVector[i].nullBufferLength = row_count;
    }

    for (size_t i = 0; i < row_count; ++i) {
      auto row = rows_->getNextRow(true, true);
      if (row.empty()) {
        break;
      }
      for (size_t j = 0; j < col_count; ++j) {
        int8_t* valueBuffer = prestoResultVector[j].valueBuffer;
        int8_t* nullBuffer = prestoResultVector[j].nullBuffer;
        const auto ct = rows_->getColType(j);
        prestoResultVector[j].valueBufferLength += ct.get_size();
        switch (get_physical_type(ct)) {
          case kBOOLEAN: {
            int64_t value = getScalarValue<int64_t>(row[j]);
            reinterpret_cast<int8_t*>(valueBuffer)[i] = static_cast<int8_t>(value);
            nullBuffer[i] = value == NULL_BOOLEAN;
            break;
          }
          case kTINYINT: {
            int64_t value = getScalarValue<int64_t>(row[j]);
            reinterpret_cast<int8_t*>(valueBuffer)[i] = static_cast<int8_t>(value);
            nullBuffer[i] = value == NULL_TINYINT;
            break;
          }
          case kSMALLINT: {
            int64_t value = getScalarValue<int64_t>(row[j]);
            reinterpret_cast<int16_t*>(valueBuffer)[i] = static_cast<int16_t>(value);
            nullBuffer[i] = value == NULL_SMALLINT;
            break;
          }
          case kINT: {
            int64_t value = getScalarValue<int64_t>(row[j]);
            reinterpret_cast<int32_t*>(valueBuffer)[i] = static_cast<int32_t>(value);
            nullBuffer[i] = value == NULL_INT;
            break;
          }
          case kBIGINT: {
            int64_t value = getScalarValue<int64_t>(row[j]);
            reinterpret_cast<int64_t*>(valueBuffer)[i] = static_cast<int64_t>(value);
            nullBuffer[i] = value == NULL_BIGINT;
            break;
          }
          case kFLOAT: {
            float value = getScalarValue<float>(row[j]);
            reinterpret_cast<float*>(valueBuffer)[i] = static_cast<float>(value);
            nullBuffer[i] = value == NULL_FLOAT;
            break;
          }
          case kDOUBLE: {
            double value = getScalarValue<double>(row[j]);
            reinterpret_cast<double*>(valueBuffer)[i] = static_cast<double>(value);
            nullBuffer[i] = value == NULL_DOUBLE;
            break;
          }
          default:
            throw std::runtime_error(ct.get_type_name() +
                " is not supported in Presto result sets.");
        }
      }
    }

    return prestoResultVector;
  }

 private:
  template <typename T>
  T getScalarValue(const TargetValue& tv) {
    const auto scalar_value = boost::get<ScalarTargetValue>(&tv);
    CHECK(scalar_value);
    auto result = boost::get<T>(scalar_value);
    return *result;
  }

  inline SQLTypes get_physical_type(const SQLTypeInfo& sti) {
    auto logical_type = sti.get_type();
    if (IS_INTEGER(logical_type)) {
      switch (sti.get_size()) {
      case 1:
        return kTINYINT;
      case 2:
        return kSMALLINT;
      case 4:
        return kINT;
      case 8:
        return kBIGINT;
      default:
        CHECK(false);
      }
    }
    return logical_type;
  }

  std::vector<PrestoResult> prestoResultVector;
};

