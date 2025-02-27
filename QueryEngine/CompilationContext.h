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

#pragma once

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/JITEventListener.h>
#include <llvm/IR/Module.h>

#include <memory>

class CompilationContext {
 public:
  virtual ~CompilationContext() {}
};

struct CompilationOptions;

class ExecutionEngineWrapper {
 public:
  ExecutionEngineWrapper();
  ExecutionEngineWrapper(llvm::ExecutionEngine* execution_engine);
  ExecutionEngineWrapper(llvm::ExecutionEngine* execution_engine,
                         const CompilationOptions& co);

  ExecutionEngineWrapper(const ExecutionEngineWrapper& other) = delete;
  ExecutionEngineWrapper(ExecutionEngineWrapper&& other) = default;

  ExecutionEngineWrapper& operator=(const ExecutionEngineWrapper& other) = delete;
  ExecutionEngineWrapper& operator=(ExecutionEngineWrapper&& other) = default;

  ExecutionEngineWrapper& operator=(llvm::ExecutionEngine* execution_engine);

  llvm::ExecutionEngine* get() { return execution_engine_.get(); }
  const llvm::ExecutionEngine* get() const { return execution_engine_.get(); }

  llvm::ExecutionEngine& operator*() { return *execution_engine_; }
  const llvm::ExecutionEngine& operator*() const { return *execution_engine_; }

  llvm::ExecutionEngine* operator->() { return execution_engine_.get(); }
  const llvm::ExecutionEngine* operator->() const { return execution_engine_.get(); }

 private:
  std::unique_ptr<llvm::ExecutionEngine> execution_engine_;
  std::unique_ptr<llvm::JITEventListener> intel_jit_listener_;
};

class CpuCompilationContext : public CompilationContext {
 public:
  CpuCompilationContext(ExecutionEngineWrapper&& execution_engine)
      : execution_engine_(std::move(execution_engine)) {}

  void setFunctionPointer(llvm::Function* function) {
    func_ = execution_engine_->getPointerToFunction(function);
    CHECK(func_);
  }

  void* func() const { return func_; }

  using TableFunctionEntryPointPtr = int32_t (*)(const int8_t* mgr_ptr,
                                                 const int8_t** input_cols,
                                                 const int64_t* input_row_count,
                                                 int64_t** out,
                                                 int64_t* output_row_count);
  TableFunctionEntryPointPtr table_function_entry_point() const {
    return (TableFunctionEntryPointPtr)func_;
  }

 private:
  void* func_{nullptr};
  ExecutionEngineWrapper execution_engine_;
};
