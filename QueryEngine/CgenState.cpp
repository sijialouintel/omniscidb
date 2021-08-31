/*
 * Copyright 2021 OmniSci, Inc.
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

#include "CgenState.h"
#include "OutputBufferInitialization.h"

#include <llvm/IR/InstIterator.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Transforms/Utils/Cloning.h>

extern std::unique_ptr<llvm::Module> g_rt_module;
#ifdef ENABLE_GEOS
extern std::unique_ptr<llvm::Module> g_rt_geos_module;
#endif

llvm::ConstantInt* CgenState::inlineIntNull(const SQLTypeInfo& type_info) {
  auto type = type_info.get_type();
  if (type_info.is_string()) {
    switch (type_info.get_compression()) {
      case kENCODING_DICT:
        return llInt(static_cast<int32_t>(inline_int_null_val(type_info)));
      case kENCODING_NONE:
        return llInt(int64_t(0));
      default:
        CHECK(false);
    }
  }
  switch (type) {
    case kBOOLEAN:
      return llInt(static_cast<int8_t>(inline_int_null_val(type_info)));
    case kTINYINT:
      return llInt(static_cast<int8_t>(inline_int_null_val(type_info)));
    case kSMALLINT:
      return llInt(static_cast<int16_t>(inline_int_null_val(type_info)));
    case kINT:
      return llInt(static_cast<int32_t>(inline_int_null_val(type_info)));
    case kBIGINT:
    case kTIME:
    case kTIMESTAMP:
    case kDATE:
    case kINTERVAL_DAY_TIME:
    case kINTERVAL_YEAR_MONTH:
      return llInt(inline_int_null_val(type_info));
    case kDECIMAL:
    case kNUMERIC:
      return llInt(inline_int_null_val(type_info));
    case kARRAY:
      return llInt(int64_t(0));
    default:
      abort();
  }
}

llvm::ConstantFP* CgenState::inlineFpNull(const SQLTypeInfo& type_info) {
  CHECK(type_info.is_fp());
  switch (type_info.get_type()) {
    case kFLOAT:
      return llFp(NULL_FLOAT);
    case kDOUBLE:
      return llFp(NULL_DOUBLE);
    default:
      abort();
  }
}

llvm::Constant* CgenState::inlineNull(const SQLTypeInfo& ti) {
  return ti.is_fp() ? static_cast<llvm::Constant*>(inlineFpNull(ti))
                    : static_cast<llvm::Constant*>(inlineIntNull(ti));
}

std::pair<llvm::ConstantInt*, llvm::ConstantInt*> CgenState::inlineIntMaxMin(
    const size_t byte_width,
    const bool is_signed) {
  int64_t max_int{0}, min_int{0};
  if (is_signed) {
    std::tie(max_int, min_int) = inline_int_max_min(byte_width);
  } else {
    uint64_t max_uint{0}, min_uint{0};
    std::tie(max_uint, min_uint) = inline_uint_max_min(byte_width);
    max_int = static_cast<int64_t>(max_uint);
    CHECK_EQ(uint64_t(0), min_uint);
  }
  switch (byte_width) {
    case 1:
      return std::make_pair(::ll_int(static_cast<int8_t>(max_int), context_),
                            ::ll_int(static_cast<int8_t>(min_int), context_));
    case 2:
      return std::make_pair(::ll_int(static_cast<int16_t>(max_int), context_),
                            ::ll_int(static_cast<int16_t>(min_int), context_));
    case 4:
      return std::make_pair(::ll_int(static_cast<int32_t>(max_int), context_),
                            ::ll_int(static_cast<int32_t>(min_int), context_));
    case 8:
      return std::make_pair(::ll_int(max_int, context_), ::ll_int(min_int, context_));
    default:
      abort();
  }
}

llvm::Value* CgenState::castToTypeIn(llvm::Value* val, const size_t dst_bits) {
  auto src_bits = val->getType()->getScalarSizeInBits();
  if (src_bits == dst_bits) {
    return val;
  }
  if (val->getType()->isIntegerTy()) {
    return ir_builder_.CreateIntCast(
        val, get_int_type(dst_bits, context_), src_bits != 1);
  }
  // real (not dictionary-encoded) strings; store the pointer to the payload
  if (val->getType()->isPointerTy()) {
    return ir_builder_.CreatePointerCast(val, get_int_type(dst_bits, context_));
  }

  CHECK(val->getType()->isFloatTy() || val->getType()->isDoubleTy());

  llvm::Type* dst_type = nullptr;
  switch (dst_bits) {
    case 64:
      dst_type = llvm::Type::getDoubleTy(context_);
      break;
    case 32:
      dst_type = llvm::Type::getFloatTy(context_);
      break;
    default:
      CHECK(false);
  }

  return ir_builder_.CreateFPCast(val, dst_type);
}

void CgenState::maybeCloneFunctionRecursive(llvm::Function* fn) {
  CHECK(fn);
  if (!fn->isDeclaration()) {
    return;
  }

  // Get the implementation from the runtime module.
  auto func_impl = g_rt_module->getFunction(fn->getName());
  CHECK(func_impl) << fn->getName().str();

  if (func_impl->isDeclaration()) {
    return;
  }

  auto DestI = fn->arg_begin();
  for (auto arg_it = func_impl->arg_begin(); arg_it != func_impl->arg_end(); ++arg_it) {
    DestI->setName(arg_it->getName());
    vmap_[&*arg_it] = &*DestI++;
  }

  llvm::SmallVector<llvm::ReturnInst*, 8> Returns;  // Ignore returns cloned.
  llvm::CloneFunctionInto(fn, func_impl, vmap_, /*ModuleLevelChanges=*/true, Returns);

  for (auto it = llvm::inst_begin(fn), e = llvm::inst_end(fn); it != e; ++it) {
    if (llvm::isa<llvm::CallInst>(*it)) {
      auto& call = llvm::cast<llvm::CallInst>(*it);
      maybeCloneFunctionRecursive(call.getCalledFunction());
    }
  }
}

llvm::Value* CgenState::emitCall(const std::string& fname,
                                 const std::vector<llvm::Value*>& args) {
  // Get the function reference from the query module.
  auto func = module_->getFunction(fname);
  CHECK(func);
  // If the function called isn't external, clone the implementation from the runtime
  // module.
  maybeCloneFunctionRecursive(func);

  return ir_builder_.CreateCall(func, args);
}

void CgenState::emitErrorCheck(llvm::Value* condition,
                               llvm::Value* errorCode,
                               std::string label) {
  needs_error_check_ = true;
  auto check_ok = llvm::BasicBlock::Create(context_, label + "_ok", current_func_);
  auto check_fail = llvm::BasicBlock::Create(context_, label + "_fail", current_func_);
  ir_builder_.CreateCondBr(condition, check_ok, check_fail);
  ir_builder_.SetInsertPoint(check_fail);
  ir_builder_.CreateRet(errorCode);
  ir_builder_.SetInsertPoint(check_ok);
}

namespace {

struct GpuFunctionDefinition {
  GpuFunctionDefinition(const std::string& name) : name(name) {}
  std::string name;

  virtual ~GpuFunctionDefinition() {}

  virtual llvm::FunctionCallee getFunction(llvm::Module* module,
                                           llvm::LLVMContext& context) const = 0;
};

struct GpuPowerFunction : public GpuFunctionDefinition {
  GpuPowerFunction() : GpuFunctionDefinition("power") {}

  llvm::FunctionCallee getFunction(llvm::Module* module,
                                   llvm::LLVMContext& context) const final {
    return module->getOrInsertFunction(name,
                                       /*ret_type=*/llvm::Type::getDoubleTy(context),
                                       /*args=*/llvm::Type::getDoubleTy(context),
                                       llvm::Type::getDoubleTy(context));
  }
};

struct GpuAtanFunction : public GpuFunctionDefinition {
  GpuAtanFunction() : GpuFunctionDefinition("Atan") {}

  llvm::FunctionCallee getFunction(llvm::Module* module,
                                   llvm::LLVMContext& context) const final {
    return module->getOrInsertFunction(name,
                                       /*ret_type=*/llvm::Type::getDoubleTy(context),
                                       /*args=*/llvm::Type::getDoubleTy(context));
  }
};

struct GpuLogFunction : public GpuFunctionDefinition {
  GpuLogFunction() : GpuFunctionDefinition("ln") {}

  llvm::FunctionCallee getFunction(llvm::Module* module,
                                   llvm::LLVMContext& context) const final {
    return module->getOrInsertFunction(name,
                                       /*ret_type=*/llvm::Type::getDoubleTy(context),
                                       /*args=*/llvm::Type::getDoubleTy(context));
  }
};

struct GpuTanFunction : public GpuFunctionDefinition {
  GpuTanFunction() : GpuFunctionDefinition("Tan") {}

  llvm::FunctionCallee getFunction(llvm::Module* module,
                                   llvm::LLVMContext& context) const final {
    return module->getOrInsertFunction(name,
                                       /*ret_type=*/llvm::Type::getDoubleTy(context),
                                       /*args=*/llvm::Type::getDoubleTy(context));
  }
};

struct GpuExpFunction : public GpuFunctionDefinition {
  GpuExpFunction() : GpuFunctionDefinition("Exp") {}

  llvm::FunctionCallee getFunction(llvm::Module* module,
                                   llvm::LLVMContext& context) const final {
    return module->getOrInsertFunction(name,
                                       /*ret_type=*/llvm::Type::getDoubleTy(context),
                                       /*args=*/llvm::Type::getDoubleTy(context));
  }
};

static const std::unordered_map<std::string, std::shared_ptr<GpuFunctionDefinition>>
    gpu_replacement_functions{{"pow", std::make_shared<GpuPowerFunction>()},
                              {"atan", std::make_shared<GpuAtanFunction>()},
                              {"log", std::make_shared<GpuLogFunction>()},
                              {"tan", std::make_shared<GpuTanFunction>()},
                              {"exp", std::make_shared<GpuExpFunction>()}};

}  // namespace

std::vector<std::string> CgenState::gpuFunctionsToReplace(llvm::Function* fn) {
  std::vector<std::string> ret;

  CHECK(fn);
  CHECK(!fn->isDeclaration());

  for (auto& basic_block : *fn) {
    auto& inst_list = basic_block.getInstList();
    for (auto inst_itr = inst_list.begin(); inst_itr != inst_list.end(); ++inst_itr) {
      if (auto call_inst = llvm::dyn_cast<llvm::CallInst>(inst_itr)) {
        auto called_fcn = call_inst->getCalledFunction();
        CHECK(called_fcn);

        if (gpu_replacement_functions.find(called_fcn->getName().str()) !=
            gpu_replacement_functions.end()) {
          ret.emplace_back(called_fcn->getName());
        }
      }
    }
  }
  return ret;
}

void CgenState::replaceFunctionForGpu(const std::string& fcn_to_replace,
                                      llvm::Function* fn) {
  CHECK(fn);
  CHECK(!fn->isDeclaration());

  auto map_it = gpu_replacement_functions.find(fcn_to_replace);
  if (map_it == gpu_replacement_functions.end()) {
    throw QueryMustRunOnCpu("Codegen failed: Could not find replacement functon for " +
                            fcn_to_replace +
                            " to run on gpu. Query step must run in cpu mode.");
  }
  const auto& gpu_fcn_obj = map_it->second;
  CHECK(gpu_fcn_obj);
  const auto& gpu_fcn_name = gpu_fcn_obj->name;
  VLOG(1) << "Replacing " << fcn_to_replace << " with " << gpu_fcn_name
          << " for parent function " << fn->getName().str();

  for (auto& basic_block : *fn) {
    auto& inst_list = basic_block.getInstList();
    for (auto inst_itr = inst_list.begin(); inst_itr != inst_list.end(); ++inst_itr) {
      if (auto call_inst = llvm::dyn_cast<llvm::CallInst>(inst_itr)) {
        auto called_fcn = call_inst->getCalledFunction();
        CHECK(called_fcn);

        if (called_fcn->getName() == fcn_to_replace) {
          std::vector<llvm::Value*> args;
          std::vector<llvm::Type*> arg_types;
          for (auto& arg : call_inst->args()) {
            arg_types.push_back(arg.get()->getType());
            args.push_back(arg.get());
          }
          auto gpu_func = gpu_fcn_obj->getFunction(module_, context_);
          CHECK(gpu_func);
          auto gpu_func_type = gpu_func.getFunctionType();
          CHECK(gpu_func_type);
          CHECK_EQ(gpu_func_type->getReturnType(), called_fcn->getReturnType());
          llvm::ReplaceInstWithInst(call_inst,
                                    llvm::CallInst::Create(gpu_func, args, ""));
          return;
        }
      }
    }
  }
}
