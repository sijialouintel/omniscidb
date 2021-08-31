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
#include "ArrowResultSet.h"

class CiderArrowResultProvider : public CiderResultProvider {
 public:

  std::any convert() {
    if (record_batch_) {
      return record_batch_;
    }
    if (col_names_.empty()) {
      col_names_ = getColumnNames(result_->getTargetsMeta());
    }
    auto converter =
        std::make_unique<ArrowResultSetConverter>(result_->getRows(), col_names_, -1);
    record_batch_ = converter->convertToArrow();
    return record_batch_;
  }

 private:
  std::shared_ptr<arrow::RecordBatch> record_batch_;
  std::vector<std::string> col_names_;

  std::vector<std::string> getColumnNames(const std::vector<TargetMetaInfo>& targets) const {
    std::vector<std::string> names;
    for (const auto& target : targets) {
      names.push_back(target.get_resname());
    }
    return names;
  }
};

