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

#include <any>

#include "QueryEngine/Descriptors/RelAlgExecutionDescriptor.h"

class CiderResultProvider {
 public:
  CiderResultProvider() : crt_row_idx_(0) {}

  const std::shared_ptr<ResultSet>& getRows() const {
      return rows_;
  }

  std::vector<TargetValue> getNextRow(const bool translate_strings,
                                      const bool decimal_to_double) const {
    if (crt_row_idx_ == rows_->rowCount()) {
      return {};
    }
    crt_row_idx_++;
    return rows_->getNextRow(translate_strings, decimal_to_double);
  }

  std::vector<TargetValue> getRowAt(const size_t index) const {
    if (index >= rows_->rowCount()) {
      return {};
    }
    return rows_->getRowAt(index);
  }

  bool hasNext() {
    if (crt_row_idx_ < rows_->rowCount()) {
      return true;
    }
    return false;
  }

  size_t next(size_t required_rows) {
    if (crt_row_idx_ + required_rows > rows_->rowCount()) {
      required_rows = std::min<size_t>(required_rows, rows_->rowCount() - crt_row_idx_);
    }
    return required_rows;
  }

  bool registerResultSet(std::shared_ptr<ResultSet> result) {
    result_ = nullptr;
    rows_ = result;
    crt_row_idx_ = 0;
    return true;
  }

  bool registerExecutionResult(std::shared_ptr<ExecutionResult> result) {
    result_ = result;
    rows_ = result->getRows();
    crt_row_idx_ = 0;
    return true;
  }

  virtual std::any convert() {
    return nullptr;
  }

  virtual void release() {}

  std::shared_ptr<ExecutionResult> result_; //FIXME : do we need this??
  std::shared_ptr<ResultSet> rows_;
  mutable size_t crt_row_idx_;
};
