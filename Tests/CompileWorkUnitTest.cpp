#include "TestHelpers.h"

#include "../Cider/CiderExecutionKernel.h"
#include "../ImportExport/Importer.h"
#include "../Parser/parser.h"
#include "../QueryEngine/ArrowResultSet.h"
#include "../QueryEngine/CgenState.h"
#include "../QueryEngine/Execute.h"
#include "../QueryEngine/ExpressionRange.h"
#include "../QueryEngine/RelAlgExecutionUnit.h"
#include "../QueryEngine/ResultSetReductionJIT.h"
#include "../QueryRunner/QueryRunner.h"
#include "DistributedLoader.h"

#include <gtest/gtest.h>

#ifndef BASE_PATH
#define BASE_PATH "./tmp"
#endif

RelAlgExecutionUnit buildFakeRelAlgEU() {
  int table_id = 100;
  int column_id_0 = 1;
  int column_id_1 = 2;

  // input_descs
  std::vector<InputDescriptor> input_descs;
  InputDescriptor input_desc_0(table_id, 0);
  input_descs.push_back(input_desc_0);

  // input_col_descs
  std::list<std::shared_ptr<const InputColDescriptor>> input_col_descs;
  std::shared_ptr<const InputColDescriptor> input_col_desc_0 =
      std::make_shared<const InputColDescriptor>(column_id_0, table_id, 0);
  std::shared_ptr<const InputColDescriptor> input_col_desc_1 =
      std::make_shared<const InputColDescriptor>(column_id_1, table_id, 0);
  input_col_descs.push_back(input_col_desc_0);
  input_col_descs.push_back(input_col_desc_1);

  // simple_quals
  SQLTypes sqlTypes{SQLTypes::kBOOLEAN};
  SQLTypes subtypes{SQLTypes::kNULLT};
  SQLTypes dateSqlType{SQLTypes::kDATE};
  SQLTypes longTypes{SQLTypes::kBIGINT};

  SQLTypeInfo date_col_info(
      dateSqlType, 0, 0, false, EncodingType::kENCODING_DATE_IN_DAYS, 0, subtypes);
  std::shared_ptr<Analyzer::ColumnVar> col1 =
      std::make_shared<Analyzer::ColumnVar>(date_col_info, table_id, column_id_0, 0);
  SQLTypeInfo ti_boolean(
      sqlTypes, 0, 0, false, EncodingType::kENCODING_NONE, 0, subtypes);
  SQLTypeInfo ti_long(longTypes, 0, 0, false, EncodingType::kENCODING_NONE, 0, subtypes);

  std::shared_ptr<Analyzer::Expr> leftExpr =
      std::make_shared<Analyzer::UOper>(date_col_info, false, SQLOps::kCAST, col1);
  Datum d;
  d.bigintval = 757382400;
  std::shared_ptr<Analyzer::Expr> rightExpr =
      std::make_shared<Analyzer::Constant>(dateSqlType, false, d);
  std::shared_ptr<Analyzer::Expr> simple_qual_0 = std::make_shared<Analyzer::BinOper>(
      ti_boolean, false, SQLOps::kGE, SQLQualifier::kONE, leftExpr, rightExpr);
  std::list<std::shared_ptr<Analyzer::Expr>> simple_quals;
  simple_quals.push_back(simple_qual_0);

  std::vector<Analyzer::Expr*> target_exprs;
  Analyzer::ColumnVar* target_expr_0 =
      new Analyzer::ColumnVar(ti_long, table_id, column_id_1, 0);

  target_exprs.push_back(target_expr_0);

  std::list<std::shared_ptr<Analyzer::Expr>> groupby_exprs;
  groupby_exprs.push_back(nullptr);

  //   ra_exe_unit.input_descs = input_descs;
  //   ra_exe_unit.input_col_descs = input_col_descs;
  //   ra_exe_unit.simple_quals = simple_quals;
  //   ra_exe_unit.groupby_exprs = groupby_exprs;
  //   ra_exe_unit.target_exprs = target_exprs;

  RelAlgExecutionUnit ra_exe_unit{input_descs,
                                  input_col_descs,
                                  simple_quals,
                                  std::list<std::shared_ptr<Analyzer::Expr>>(),
                                  JoinQualsPerNestingLevel(),
                                  groupby_exprs,
                                  target_exprs};

  return ra_exe_unit;
}

RelAlgExecutionUnit buildFakeAggRelAlgEU() {
  int table_id = 100;
  int column_id_0 = 1;
  int column_id_1 = 2;

  // input_descs
  std::vector<InputDescriptor> input_descs;
  InputDescriptor input_desc_0(table_id, 0);
  input_descs.push_back(input_desc_0);

  // input_col_descs
  std::list<std::shared_ptr<const InputColDescriptor>> input_col_descs;
  std::shared_ptr<const InputColDescriptor> input_col_desc_0 =
      std::make_shared<const InputColDescriptor>(column_id_0, table_id, 0);
  std::shared_ptr<const InputColDescriptor> input_col_desc_1 =
      std::make_shared<const InputColDescriptor>(column_id_1, table_id, 0);
  input_col_descs.push_back(input_col_desc_0);
  input_col_descs.push_back(input_col_desc_1);

  // simple_quals
  SQLTypes sqlTypes{SQLTypes::kBOOLEAN};
  SQLTypes subtypes{SQLTypes::kNULLT};
  SQLTypes dateSqlType{SQLTypes::kDATE};
  SQLTypes longTypes{SQLTypes::kBIGINT};

  SQLTypeInfo date_col_info(
      dateSqlType, 0, 0, false, EncodingType::kENCODING_DATE_IN_DAYS, 0, subtypes);
  std::shared_ptr<Analyzer::ColumnVar> col1 =
      std::make_shared<Analyzer::ColumnVar>(date_col_info, table_id, column_id_0, 0);
  SQLTypeInfo ti_boolean(
      sqlTypes, 0, 0, false, EncodingType::kENCODING_NONE, 0, subtypes);
  SQLTypeInfo ti_long(longTypes, 0, 0, false, EncodingType::kENCODING_NONE, 0, subtypes);

  std::shared_ptr<Analyzer::Expr> leftExpr =
      std::make_shared<Analyzer::UOper>(date_col_info, false, SQLOps::kCAST, col1);
  Datum d;
  d.bigintval = 757382400;
  std::shared_ptr<Analyzer::Expr> rightExpr =
      std::make_shared<Analyzer::Constant>(dateSqlType, false, d);
  std::shared_ptr<Analyzer::Expr> simple_qual_0 = std::make_shared<Analyzer::BinOper>(
      ti_boolean, false, SQLOps::kGE, SQLQualifier::kONE, leftExpr, rightExpr);
  std::list<std::shared_ptr<Analyzer::Expr>> simple_quals;
  simple_quals.push_back(simple_qual_0);

  std::vector<Analyzer::Expr*> target_exprs;
  std::shared_ptr<Analyzer::ColumnVar> col_expr_0 =
      std::make_shared<Analyzer::ColumnVar>(ti_long, table_id, column_id_1, 0);
  Analyzer::AggExpr* target_expr_0 =
      new Analyzer::AggExpr(ti_long, kSUM, col_expr_0, false, nullptr);

  target_exprs.push_back(target_expr_0);

  // notice! size should be 0
  std::list<std::shared_ptr<Analyzer::Expr>> groupby_exprs;
  //  groupby_exprs.push_back(nullptr);

  //   ra_exe_unit.input_descs = input_descs;
  //   ra_exe_unit.input_col_descs = input_col_descs;
  //   ra_exe_unit.simple_quals = simple_quals;
  //   ra_exe_unit.groupby_exprs = groupby_exprs;
  //   ra_exe_unit.target_exprs = target_exprs;

  RelAlgExecutionUnit ra_exe_unit{input_descs,
                                  input_col_descs,
                                  simple_quals,
                                  std::list<std::shared_ptr<Analyzer::Expr>>(),
                                  JoinQualsPerNestingLevel(),
                                  groupby_exprs,
                                  target_exprs};

  return ra_exe_unit;
}

// build parameters, for test only.
std::vector<InputTableInfo> buildQueryInfo() {
  std::vector<InputTableInfo> query_infos;
  Fragmenter_Namespace::FragmentInfo fi_0;
  fi_0.fragmentId = 0;
  fi_0.shadowNumTuples = 20;
  fi_0.physicalTableId = 100;
  fi_0.setPhysicalNumTuples(20);

  Fragmenter_Namespace::TableInfo ti_0;
  ti_0.fragments = {fi_0};
  ti_0.setPhysicalNumTuples(20);

  InputTableInfo iti_0{100, ti_0};
  query_infos.push_back(iti_0);

  return query_infos;
}

int8_t** build_input_buf() {
  int8_t** col_buffer_frag_0 =
      (int8_t**)std::malloc(sizeof(int8_t*) * 2);  // we have two columns

  int32_t* date_col_id_0 = (int32_t*)std::malloc(sizeof(int32_t) * 10);
  int64_t* long_col_id_1 = (int64_t*)std::malloc(sizeof(int64_t) * 10);

  for (int i = 0; i < 5; i++) {
    date_col_id_0[i] = 8777;
    long_col_id_1[i] = i;
  }

  for (int i = 5; i < 10; i++) {
    date_col_id_0[i] = 8700;
    long_col_id_1[i] = i;
  }

  col_buffer_frag_0[0] = reinterpret_cast<int8_t*>(date_col_id_0);
  col_buffer_frag_0[1] = reinterpret_cast<int8_t*>(long_col_id_1);
  return col_buffer_frag_0;
}

void release_input_buf(int8_t** col_buffers) {
  std::free(col_buffers[0]);
  std::free(col_buffers[1]);
  std::free(col_buffers);
}

int64_t** build_out_buf() {
  int64_t** out = (int64_t**)std::malloc(sizeof(int64_t**) * 1);
  int64_t* out_col_0 = (int64_t*)std::malloc(sizeof(int64_t*) * 10);
  std::memset(out_col_0, 0, sizeof(int64_t*) * 10);
  out[0] = out_col_0;
  return out;
}

void release_output_buf(int64_t** out_buffers) {
  std::free(out_buffers[0]);
  std::free(out_buffers);
}

TEST(APITest, case1) {
  auto kernel = CiderExecutionKernel::create();

  // build compile input parameters
  RelAlgExecutionUnit ra_exe_unit = buildFakeRelAlgEU();
  std::vector<InputTableInfo> query_infos = buildQueryInfo();

  kernel->compileWorkUnit(ra_exe_unit, query_infos);

  //  std::cout << "compile IR : " << kernel->getLlvmIR() << std::endl;

  // build data input parameters
  int8_t** col_buffers = build_input_buf();
  int64_t num_rows = 10;
  int64_t** out_buffers = build_out_buf();
  int32_t matched_num = 0;
  int32_t err_code = 0;
  int64_t init_agg_vals = 0;

  kernel->runWithData((const int8_t**)col_buffers,
                      &num_rows,
                      out_buffers,
                      &matched_num,
                      &err_code,
                      &init_agg_vals);

  // check result
  std::cout << "total match " << matched_num << " rows" << std::endl;

  // release buffers
  release_input_buf(col_buffers);
  release_output_buf(out_buffers);
}

TEST(APITest, case2) {
  auto kernel = CiderExecutionKernel::create();

  // build compile input parameters
  RelAlgExecutionUnit ra_exe_unit = buildFakeAggRelAlgEU();
  std::vector<InputTableInfo> query_infos = buildQueryInfo();

  kernel->compileWorkUnit(ra_exe_unit, query_infos);

  //  std::cout << "compile IR : " << kernel->getLlvmIR() << std::endl;

  // build data input parameters
  int8_t** col_buffers = build_input_buf();
  int64_t num_rows = 10;
  int64_t** out_buffers = build_out_buf();
  int32_t matched_num = 0;
  int32_t err_code = 0;
  int64_t init_agg_vals = 10;

  kernel->runWithData((const int8_t**)col_buffers,
                      &num_rows,
                      out_buffers,
                      &matched_num,
                      &err_code,
                      &init_agg_vals);

  // check result
  std::cout << "total match " << matched_num << " rows" << std::endl;
  // should be 20(10 + 0 + 1 + 2 + 3 + 4)
  std::cout << "sum result " << out_buffers[0][0] << std::endl;

  // release buffers
  release_input_buf(col_buffers);
  release_output_buf(out_buffers);
}

int main(int argc, char** argv) {
  int err = 0;
  TestHelpers::init_logger_stderr_only(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  namespace po = boost::program_options;

  po::options_description desc("Options");

  desc.add_options()("disable-shared-mem-group-by",
                     po::value<bool>(&g_enable_smem_group_by)
                         ->default_value(g_enable_smem_group_by)
                         ->implicit_value(false),
                     "Enable/disable using GPU shared memory for GROUP BY.");
  desc.add_options()("enable-columnar-output",
                     po::value<bool>(&g_enable_columnar_output)
                         ->default_value(g_enable_columnar_output)
                         ->implicit_value(true),
                     "Enable/disable using columnar output format.");
  desc.add_options()("enable-bump-allocator",
                     po::value<bool>(&g_enable_bump_allocator)
                         ->default_value(g_enable_bump_allocator)
                         ->implicit_value(true),
                     "Enable the bump allocator for projection queries on GPU.");

  logger::LogOptions log_options(argv[0]);
  log_options.severity_ = logger::Severity::DEBUG1;
  log_options.severity_clog_ = logger::Severity::DEBUG1;
  log_options.set_options();

  logger::init(log_options);

  try {
    err = RUN_ALL_TESTS();
  } catch (const std::exception& e) {
    LOG(ERROR) << e.what();
  }

  return err;
}
