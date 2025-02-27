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

#pragma once

#include "DataRecycler.h"
#include "QueryEngine/JoinHashTable/HashJoin.h"
#include "QueryEngine/QueryHint.h"

struct QueryPlanMetaInfo {
  QueryPlan query_plan_dag;
  std::string inner_col_info_string;
};

struct OverlapsHashTableMetaInfo {
  size_t overlaps_max_table_size_bytes;
  double overlaps_bucket_threshold;
  std::vector<double> bucket_sizes;
};

struct HashtableCacheMetaInfo {
  std::optional<QueryPlanMetaInfo> query_plan_meta_info;
  std::optional<OverlapsHashTableMetaInfo> overlaps_meta_info;
  std::optional<RegisteredQueryHint> registered_query_hint;

  HashtableCacheMetaInfo()
      : query_plan_meta_info(std::nullopt)
      , overlaps_meta_info(std::nullopt)
      , registered_query_hint(std::nullopt){};
};

struct HashtableAccessPathInfo {
  QueryPlanHash hashed_query_plan_dag;
  HashtableCacheMetaInfo meta_info;
  std::unordered_set<size_t> table_keys;

  HashtableAccessPathInfo()
      : hashed_query_plan_dag(EMPTY_HASHED_PLAN_DAG_KEY)
      , meta_info(HashtableCacheMetaInfo())
      , table_keys({}){};
};

class HashTableRecycler
    : public DataRecycler<std::shared_ptr<HashTable>, HashtableCacheMetaInfo> {
 public:
  HashTableRecycler(CacheItemType hashtable_type, int num_gpus)
      : DataRecycler({hashtable_type},
                     g_hashtable_cache_total_bytes,
                     g_max_cacheable_hashtable_size_bytes,
                     num_gpus) {}

  std::shared_ptr<HashTable> getItemFromCache(
      QueryPlanHash key,
      CacheItemType item_type,
      DeviceIdentifier device_identifier,
      std::optional<HashtableCacheMetaInfo> meta_info = std::nullopt) override;

  void putItemToCache(
      QueryPlanHash key,
      std::shared_ptr<HashTable> item_ptr,
      CacheItemType item_type,
      DeviceIdentifier device_identifier,
      size_t item_size,
      size_t compute_time,
      std::optional<HashtableCacheMetaInfo> meta_info = std::nullopt) override;

  // nothing to do with hashtable recycler
  void initCache() override {}

  void clearCache() override;

  void markCachedItemAsDirty(size_t table_key,
                             std::unordered_set<QueryPlanHash>& key_set,
                             CacheItemType item_type,
                             DeviceIdentifier device_identifier) override;

  std::string toString() const override;

  bool checkOverlapsHashtableBucketCompatability(
      const OverlapsHashTableMetaInfo& candidate_bucket_dim,
      const OverlapsHashTableMetaInfo& target_bucket_dim) const;

  static HashtableAccessPathInfo getHashtableAccessPathInfo(
      const std::vector<InnerOuter>& inner_outer_pairs,
      const SQLOps op_type,
      const JoinType join_type,
      const HashTableBuildDagMap& hashtable_build_dag_map,
      Executor* executor);

  static std::string getJoinColumnInfoString(
      std::vector<const Analyzer::ColumnVar*>& inner_cols,
      std::vector<const Analyzer::ColumnVar*>& outer_cols,
      Executor* executor);

  static bool isSafeToCacheHashtable(const TableIdToNodeMap& table_id_to_node_map,
                                     bool need_dict_translation,
                                     const int table_id);

  // this function is required to test data recycler
  // specifically, it is tricky to get a hashtable cache key when we only know
  // a target query sql in test code
  // so this function utilizes an incorrect way to manipulate our hashtable recycler
  // but provides the cached hashtable for performing the test
  // a set "visited" contains cached hashtable keys that we have retrieved so far
  // based on that, this function iterates hashtable cache and return a cached one
  // when its hashtable cache key has not been visited yet
  // for instance, if we call this function with an empty "visited" key, we return
  // the first hashtable that its iterator visits
  std::tuple<QueryPlanHash,
             std::shared_ptr<HashTable>,
             std::optional<HashtableCacheMetaInfo>>
  getCachedHashtableWithoutCacheKey(std::set<size_t>& visited,
                                    CacheItemType hash_table_type,
                                    DeviceIdentifier device_identifier);

  void addQueryPlanDagForTableKeys(size_t hashed_query_plan_dag,
                                   const std::unordered_set<size_t>& table_keys);

  std::optional<std::unordered_set<size_t>> getMappedQueryPlanDagsWithTableKey(
      size_t table_key) const;

  void removeTableKeyInfoFromQueryPlanDagMap(size_t table_key);

  std::pair<bool, std::optional<HashJoinHint>> has_join_hint(
      std::optional<HashtableCacheMetaInfo> meta_info) {
    if (meta_info && meta_info->registered_query_hint) {
      auto join_hint = meta_info->registered_query_hint->hash_join;
      if (join_hint) {
        return std::make_pair(true, join_hint);
      }
    }
    return std::make_pair(false, std::nullopt);
  };

  bool compare_query_hints(const HashJoinHint& h1, const HashJoinHint& h2) {
    // caching hint option is ignored here since it only controls
    // the addition of new item to cache
    auto hashing = false;
    auto layout = false;
    if (h1.hashing && h2.hashing) {
      hashing = *h1.hashing == *h2.hashing;
    } else if (!h1.hashing && !h2.hashing) {
      hashing = true;
    }
    if (h1.layout && h2.layout) {
      layout = *h1.layout == *h2.layout;
    } else if (!h1.layout && !h2.layout) {
      layout = true;
    }
    return hashing && layout;
  }

 private:
  bool hasItemInCache(
      QueryPlanHash key,
      CacheItemType item_type,
      DeviceIdentifier device_identifier,
      std::lock_guard<std::mutex>& lock,
      std::optional<HashtableCacheMetaInfo> meta_info = std::nullopt) const override;

  void removeItemFromCache(
      QueryPlanHash key,
      CacheItemType item_type,
      DeviceIdentifier device_identifier,
      std::lock_guard<std::mutex>& lock,
      std::optional<HashtableCacheMetaInfo> meta_info = std::nullopt) override;

  void cleanupCacheForInsertion(
      CacheItemType item_type,
      DeviceIdentifier device_identifier,
      size_t required_size,
      std::lock_guard<std::mutex>& lock,
      std::optional<HashtableCacheMetaInfo> meta_info = std::nullopt) override;

  // we maintain the mapping between a hashed table_key -> a set of hashed query plan dag
  // only in hashtable recycler to minimize memory footprint
  // so other types of data recycler related to hashtable cache
  // i.e., hashing scheme recycler and overlaps tuning param recycler should use the
  // key_set when we retrieve it from here, see `markCachedItemAsDirty` function
  std::unordered_map<size_t, std::unordered_set<size_t>> table_key_to_query_plan_dag_map_;
};
