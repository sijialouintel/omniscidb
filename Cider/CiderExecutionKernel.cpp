/*
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

#include "CiderExecutionKernel.h"

#include "ImportExport/Importer.h"
#include "Parser/parser.h"
#include "QueryEngine/ArrowResultSet.h"
#include "QueryEngine/CgenState.h"
#include "QueryEngine/Descriptors/RelAlgExecutionDescriptor.h"
#include "QueryEngine/Execute.h"
#include "QueryEngine/ExpressionRange.h"
#include "QueryEngine/RelAlgExecutionUnit.h"
#include "QueryEngine/ResultSetReductionJIT.h"
#include "QueryRunner/QueryRunner.h"
#include "Shared/DateConverters.h"
#include "Shared/StringTransform.h"
#include "Shared/scope.h"
#include "SqliteConnector/SqliteConnector.h"

class CiderExecutionKernelImpl;

inline CiderExecutionKernelImpl* getImpl(CiderExecutionKernel* ptr) {
  return (CiderExecutionKernelImpl*)ptr;
}
inline const CiderExecutionKernelImpl* getImpl(const CiderExecutionKernel* ptr) {
  return (const CiderExecutionKernelImpl*)ptr;
}

class CiderExecutionKernelImpl : public CiderExecutionKernel {
 public:
  ~CiderExecutionKernelImpl() {
    executor_.reset();
  }

  CiderExecutionKernelImpl() {
    executor_ = Executor::getExecutor(Executor::UNITARY_EXECUTOR_ID);
  }
  void runWithDataMultiFrag(const int8_t*** multi_col_buffers,
                            const int64_t* num_rows,
                            int64_t** out,
                            int32_t* matched_num,
                            int32_t* err_code,
                            int64_t* init_agg_vals);

  void runWithData(const int8_t** col_buffers,
                   const int64_t* num_rows,
                   int64_t** out,
                   int32_t* matched_num,
                   int32_t* err_code,
                   int64_t* init_agg_vals);

  // TODO: remove Omnisci related class/header files, which may cause link issue.
  void compileWorkUnit(const RelAlgExecutionUnit& ra_exe_unit,
                       const std::vector<InputTableInfo>& query_infos);

  std::string getLlvmIR() override;

 private:
  std::shared_ptr<Executor> executor_;
  CompilationResult compilationResult_;
  bool compiled = false;
};

void CiderExecutionKernelImpl::runWithDataMultiFrag(const int8_t*** multi_col_buffers,
                                                    const int64_t* num_rows,
                                                    int64_t** out,
                                                    int32_t* matched_num,
                                                    int32_t* err_code,
                                                    int64_t* init_agg_vals) {
  // build input parameters
  PlanState::DeletedColumnsMap deleted_cols_map;
  const uint64_t num_fragments = 1;
  std::vector<int8_t> literal_vec =
      executor_->serializeLiterals(compilationResult_.literal_values, 0);
  uint64_t frag_row_offsets = 0;
  int32_t max_matched = *num_rows;          // FIXME:
  uint32_t num_tables = 1;                  // FIXME: only one table now, what about join
  int64_t* join_hash_tables_ptr = nullptr;  // FIXME:

  using agg_query = void (*)(const int8_t***,  // col_buffers
                             const uint64_t*,  // num_fragments
                             const int8_t*,    // literals
                             const int64_t*,   // num_rows
                             const uint64_t*,  // frag_row_offsets
                             const int32_t*,   // max_matched
                             int32_t*,         // total_matched
                             const int64_t*,   // init_agg_value
                             int64_t**,        // out
                             int32_t*,         // error_code
                             const uint32_t*,  // num_tables
                             const int64_t*);  // join_hash_tables_ptr

  std::shared_ptr<CpuCompilationContext> ccc =
      std::dynamic_pointer_cast<CpuCompilationContext>(compilationResult_.generated_code);
  reinterpret_cast<agg_query>(ccc->func())(multi_col_buffers,
                                           &num_fragments,
                                           literal_vec.data(),
                                           num_rows,
                                           &frag_row_offsets,
                                           &max_matched,
                                           matched_num,
                                           init_agg_vals,
                                           out,
                                           err_code,
                                           &num_tables,
                                           join_hash_tables_ptr);
}

void CiderExecutionKernelImpl::runWithData(const int8_t** col_buffers,
                                           const int64_t* num_rows,
                                           int64_t** out,
                                           int32_t* matched_num,
                                           int32_t* err_code,
                                           int64_t* init_agg_vals) {
  const int8_t*** multi_col_buffers = (const int8_t***)std::malloc(sizeof(int8_t**) * 1);
  multi_col_buffers[0] = col_buffers;
  runWithDataMultiFrag((const int8_t***)multi_col_buffers,
  num_rows,
                       out,
                       matched_num,
                       err_code,
                       init_agg_vals);
//  std::free(multi_col_buffers);
}

void CiderExecutionKernelImpl::compileWorkUnit(
    const RelAlgExecutionUnit& ra_exe_unit,
    const std::vector<InputTableInfo>& query_infos) {
  PlanState::DeletedColumnsMap deleted_cols_map;
  CompilationOptions co = CompilationOptions::defaults(ExecutorDeviceType::CPU);
  ExecutionOptions eo = ExecutionOptions::defaults();
  // TODO: remove.
  eo.output_columnar_hint = 0;
   CudaMgr_Namespace::CudaMgr* cuda_mgr = nullptr;
  bool allow_lazy_fetch{false};
  std::shared_ptr<RowSetMemoryOwner> row_set_mem_owner = nullptr;
  size_t max_groups_buffer_entry_guess{0};
  int8_t crt_min_byte_width{MAX_BYTE_WIDTH_SUPPORTED};
  bool has_cardinality_estimation{false};
  ColumnCacheMap column_cache;
  RenderInfo* render_info  = nullptr;

  CompilationResult compilation_result;
  std::unique_ptr<QueryMemoryDescriptor> query_mem_desc;

  std::tie(compilation_result, query_mem_desc) =
      executor_->compileWorkUnit(query_infos,
                                 deleted_cols_map,
                                 ra_exe_unit,
                                 co,
                                 eo,
                                cuda_mgr,
                                 allow_lazy_fetch,
                                 row_set_mem_owner,
                                 max_groups_buffer_entry_guess,
                                 crt_min_byte_width,
                                 has_cardinality_estimation,
                                 column_cache,
                                 render_info);
  compilationResult_ = compilation_result;
  compiled = true;
}

std::string CiderExecutionKernelImpl::getLlvmIR() {
  if (compiled) {
    return compilationResult_.llvm_ir;
  } else {
    return "";
  }
}

void CiderExecutionKernel::runWithDataMultiFrag(const int8_t*** multi_col_buffers,
                                                const int64_t* num_rows,
                                                int64_t** out,
                                                int32_t* matched_num,
                                                int32_t* err_code,
                                                int64_t* init_agg_vals) {
  CiderExecutionKernelImpl* kernel = getImpl(this);
  return kernel->runWithDataMultiFrag(
      multi_col_buffers, num_rows, out, matched_num, err_code, init_agg_vals);
}

void CiderExecutionKernel::runWithData(const int8_t** col_buffers,
                                       const int64_t* num_rows,
                                       int64_t** out,
                                       int32_t* matched_num,
                                       int32_t* err_code,
                                       int64_t* init_agg_vals) {
  CiderExecutionKernelImpl* kernel = getImpl(this);
  return kernel->runWithData(
      col_buffers, num_rows, out, matched_num, err_code, init_agg_vals);
}

void CiderExecutionKernel::compileWorkUnit(
    const RelAlgExecutionUnit& ra_exe_unit,
    const std::vector<InputTableInfo>& query_infos) {
  CiderExecutionKernelImpl* kernel = getImpl(this);
  return kernel->compileWorkUnit(ra_exe_unit, query_infos);
}

std::shared_ptr<CiderExecutionKernel> CiderExecutionKernel::create() {
  auto kernel = std::make_shared<CiderExecutionKernelImpl>();
  return kernel;
}

std::string CiderExecutionKernel::getLlvmIR() {
  CiderExecutionKernelImpl* kernel = getImpl(this) ;
  return 
  kernel->getLlvmIR();
}