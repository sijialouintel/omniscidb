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

#include "HashTableRecycler.h"

extern bool g_is_test_env;

bool HashTableRecycler::hasItemInCache(
    QueryPlanHash key,
    CacheItemType item_type,
    DeviceIdentifier device_identifier,
    std::lock_guard<std::mutex>& lock,
    std::optional<HashtableCacheMetaInfo> meta_info) const {
  if (!g_enable_data_recycler || !g_use_hashtable_cache ||
      key == EMPTY_HASHED_PLAN_DAG_KEY) {
    return false;
  }
  auto hashtable_cache = getCachedItemContainer(item_type, device_identifier);
  // hashtable cache of the *any* device type should be properly initialized
  CHECK(hashtable_cache);
  auto candidate_ht_it = std::find_if(
      hashtable_cache->begin(), hashtable_cache->end(), [&key](const auto& cached_item) {
        return cached_item.key == key;
      });
  if (candidate_ht_it != hashtable_cache->end()) {
    if (item_type == OVERLAPS_HT) {
      CHECK(candidate_ht_it->meta_info && candidate_ht_it->meta_info->overlaps_meta_info);
      CHECK(meta_info && meta_info->overlaps_meta_info);
      if (checkOverlapsHashtableBucketCompatability(
              *candidate_ht_it->meta_info->overlaps_meta_info,
              *meta_info->overlaps_meta_info)) {
        return true;
      }
    } else {
      return true;
    }
  }
  return false;
}

std::shared_ptr<HashTable> HashTableRecycler::getItemFromCache(
    QueryPlanHash key,
    CacheItemType item_type,
    DeviceIdentifier device_identifier,
    std::optional<HashtableCacheMetaInfo> meta_info) {
  if (!g_enable_data_recycler || !g_use_hashtable_cache ||
      key == EMPTY_HASHED_PLAN_DAG_KEY) {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(getCacheLock());
  auto hashtable_cache = getCachedItemContainer(item_type, device_identifier);
  auto candidate_ht = getCachedItemWithoutConsideringMetaInfo(
      key, item_type, device_identifier, *hashtable_cache, lock);
  if (candidate_ht) {
    bool can_return_cached_item = true;
    auto join_hint = has_join_hint(meta_info);
    auto candidate_join_hint = has_join_hint(candidate_ht->meta_info);
    // if query hint is not given, we try to recycle it regardless of the existence of
    // candidate join hint otherwise, we only recycle it iff two hints are matched
    if (join_hint.first &&
        (!candidate_join_hint.first ||
         (candidate_join_hint.first &&
          !compare_query_hints(*join_hint.second, *candidate_join_hint.second)))) {
      VLOG(1) << "Fail to recycle a cached hash table (unmatched query hint)";
      can_return_cached_item = false;
    }
    if (item_type == OVERLAPS_HT) {
      // we have to check hashtable metainfo for overlaps join hashtable
      CHECK(candidate_ht->meta_info->overlaps_meta_info);
      CHECK(meta_info->overlaps_meta_info);
      if (!checkOverlapsHashtableBucketCompatability(
              *candidate_ht->meta_info->overlaps_meta_info,
              *meta_info->overlaps_meta_info)) {
        can_return_cached_item = false;
      }
    }
    if (can_return_cached_item) {
      CHECK(!candidate_ht->isDirty());
      candidate_ht->item_metric->incRefCount();
      VLOG(1) << "[" << DataRecyclerUtil::toStringCacheItemType(item_type) << ", "
              << DataRecyclerUtil::getDeviceIdentifierString(device_identifier)
              << "] Recycle hash table";
      return candidate_ht->cached_item;
    }
  }
  return nullptr;
}

void HashTableRecycler::putItemToCache(QueryPlanHash key,
                                       std::shared_ptr<HashTable> item_ptr,
                                       CacheItemType item_type,
                                       DeviceIdentifier device_identifier,
                                       size_t item_size,
                                       size_t compute_time,
                                       std::optional<HashtableCacheMetaInfo> meta_info) {
  if (!g_enable_data_recycler || !g_use_hashtable_cache ||
      key == EMPTY_HASHED_PLAN_DAG_KEY) {
    return;
  }
  std::lock_guard<std::mutex> lock(getCacheLock());
  auto has_cached_ht = hasItemInCache(key, item_type, device_identifier, lock, meta_info);
  if (has_cached_ht) {
    // check to see whether the cached one is in a dirty status
    auto hashtable_cache = getCachedItemContainer(item_type, device_identifier);
    auto candidate_it =
        std::find_if(hashtable_cache->begin(),
                     hashtable_cache->end(),
                     [&key](const auto& cached_item) { return cached_item.key == key; });
    bool valid_candidate = false;
    if (candidate_it != hashtable_cache->end()) {
      auto join_hint = has_join_hint(meta_info);
      auto candidate_join_hint = has_join_hint(candidate_it->meta_info);
      if (!join_hint.first ||
          (join_hint.first && candidate_join_hint.first &&
           compare_query_hints(*join_hint.second, *candidate_join_hint.second))) {
        valid_candidate = true;
      }
      if (valid_candidate && item_type == OVERLAPS_HT) {
        // we have to check hashtable metainfo for overlaps join hashtable
        CHECK(candidate_it->meta_info && candidate_it->meta_info->overlaps_meta_info);
        CHECK(meta_info && meta_info->overlaps_meta_info);
        if (checkOverlapsHashtableBucketCompatability(
                *candidate_it->meta_info->overlaps_meta_info,
                *meta_info->overlaps_meta_info)) {
          valid_candidate = true;
        }
      }
      if (!valid_candidate || candidate_it->isDirty()) {
        // remove invalid or the dirty hash table from the cache and make a room for the
        // new one
        removeItemFromCache(
            key, item_type, device_identifier, lock, candidate_it->meta_info);
        has_cached_ht = false;
      }
    }
  }

  if (!has_cached_ht) {
    // check cache's space availability
    auto& metric_tracker = getMetricTracker(item_type);
    auto cache_status = metric_tracker.canAddItem(device_identifier, item_size);
    if (cache_status == CacheAvailability::UNAVAILABLE) {
      // hashtable is too large
      return;
    } else if (cache_status == CacheAvailability::AVAILABLE_AFTER_CLEANUP) {
      // we need to cleanup some cached hashtables to make a room to insert this hashtable
      // here we try to cache the new one anyway since we don't know the importance of
      // this hashtable yet and if it is not that frequently reused it is removed
      // in a near future
      auto required_size = metric_tracker.calculateRequiredSpaceForItemAddition(
          device_identifier, item_size);
      cleanupCacheForInsertion(item_type, device_identifier, required_size, lock);
    }
    // put hashtable's metric to metric tracker
    auto new_cache_metric_ptr = metric_tracker.putNewCacheItemMetric(
        key, device_identifier, item_size, compute_time);
    CHECK_EQ(item_size, new_cache_metric_ptr->getMemSize());
    metric_tracker.updateCurrentCacheSize(
        device_identifier, CacheUpdateAction::ADD, item_size);
    // put hashtable to cache
    VLOG(1) << "[" << DataRecyclerUtil::toStringCacheItemType(item_type) << ", "
            << DataRecyclerUtil::getDeviceIdentifierString(device_identifier)
            << "] Put item to cache";
    auto hashtable_cache = getCachedItemContainer(item_type, device_identifier);
    hashtable_cache->emplace_back(key, item_ptr, new_cache_metric_ptr, meta_info);
  }
  // we have a cached hashtable in a clean status
  return;
}

void HashTableRecycler::removeItemFromCache(
    QueryPlanHash key,
    CacheItemType item_type,
    DeviceIdentifier device_identifier,
    std::lock_guard<std::mutex>& lock,
    std::optional<HashtableCacheMetaInfo> meta_info) {
  if (!g_enable_data_recycler || !g_use_hashtable_cache ||
      key == EMPTY_HASHED_PLAN_DAG_KEY) {
    return;
  }
  auto& cache_metrics = getMetricTracker(item_type);
  // remove cached item from the cache
  auto cache_metric = cache_metrics.getCacheItemMetric(key, device_identifier);
  CHECK(cache_metric);
  auto hashtable_size = cache_metric->getMemSize();
  auto hashtable_container = getCachedItemContainer(item_type, device_identifier);
  auto filter = [key](auto const& item) { return item.key == key; };
  auto itr =
      std::find_if(hashtable_container->cbegin(), hashtable_container->cend(), filter);
  if (itr == hashtable_container->cend()) {
    return;
  } else {
    hashtable_container->erase(itr);
  }
  // remove cache metric
  cache_metrics.removeCacheItemMetric(key, device_identifier);
  // update current cache size
  cache_metrics.updateCurrentCacheSize(
      device_identifier, CacheUpdateAction::REMOVE, hashtable_size);
  return;
}

void HashTableRecycler::cleanupCacheForInsertion(
    CacheItemType item_type,
    DeviceIdentifier device_identifier,
    size_t required_size,
    std::lock_guard<std::mutex>& lock,
    std::optional<HashtableCacheMetaInfo> meta_info) {
  // sort the vector based on the importance of the cached items (by # referenced, size
  // and compute time) and then remove unimportant cached items
  int elimination_target_offset = 0;
  size_t removed_size = 0;
  auto& metric_tracker = getMetricTracker(item_type);
  auto actual_space_to_free = metric_tracker.getTotalCacheSize() / 2;
  if (!g_is_test_env && required_size < actual_space_to_free) {
    // remove enough items to avoid too frequent cache cleanup
    // we do not apply thin to test code since test scenarios are designed to
    // specific size of items and their caches
    required_size = actual_space_to_free;
  }
  metric_tracker.sortCacheInfoByQueryMetric(device_identifier);
  auto cached_item_metrics = metric_tracker.getCacheItemMetrics(device_identifier);
  sortCacheContainerByQueryMetric(item_type, device_identifier);

  // collect targets to eliminate
  for (auto& metric : cached_item_metrics) {
    auto target_size = metric->getMemSize();
    ++elimination_target_offset;
    removed_size += target_size;
    if (removed_size > required_size) {
      break;
    }
  }

  // eliminate targets in 1) cache container and 2) their metrics
  removeCachedItemFromBeginning(item_type, device_identifier, elimination_target_offset);
  metric_tracker.removeMetricFromBeginning(device_identifier, elimination_target_offset);

  // update the current cache size after this cleanup
  metric_tracker.updateCurrentCacheSize(
      device_identifier, CacheUpdateAction::REMOVE, removed_size);
}

void HashTableRecycler::clearCache() {
  std::lock_guard<std::mutex> lock(getCacheLock());
  for (auto& item_type : getCacheItemType()) {
    getMetricTracker(item_type).clearCacheMetricTracker();
    auto item_cache = getItemCache().find(item_type)->second;
    for (auto& kv : *item_cache) {
      kv.second->clear();
    }
  }
  table_key_to_query_plan_dag_map_.clear();
}

void HashTableRecycler::markCachedItemAsDirty(size_t table_key,
                                              std::unordered_set<QueryPlanHash>& key_set,
                                              CacheItemType item_type,
                                              DeviceIdentifier device_identifier) {
  if (!g_enable_data_recycler || !g_use_hashtable_cache || key_set.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(getCacheLock());
  auto hashtable_cache = getCachedItemContainer(item_type, device_identifier);
  for (auto key : key_set) {
    markCachedItemAsDirtyImpl(key, *hashtable_cache);
  }
  // after marking all cached hashtable having the given "table_key" as its one of input,
  // we remove the mapping between the table_key -> hashed_query_plan_dag
  // since we do not need to care about "already marked" item in the cache
  removeTableKeyInfoFromQueryPlanDagMap(table_key);
}

std::string HashTableRecycler::toString() const {
  std::ostringstream oss;
  oss << "A current status of the Hashtable Recycler:\n";
  for (auto& item_type : getCacheItemType()) {
    oss << "\t" << DataRecyclerUtil::toStringCacheItemType(item_type);
    auto& metric_tracker = getMetricTracker(item_type);
    oss << "\n\t# cached hashtables:\n";
    auto item_cache = getItemCache().find(item_type)->second;
    for (auto& cache_container : *item_cache) {
      oss << "\t\tDevice"
          << DataRecyclerUtil::getDeviceIdentifierString(cache_container.first)
          << ", # hashtables: " << cache_container.second->size() << "\n";
      for (auto& ht : *cache_container.second) {
        oss << "\t\t\tHT] " << ht.item_metric->toString() << "\n";
      }
    }
    oss << "\t" << metric_tracker.toString() << "\n";
  }
  return oss.str();
}

bool HashTableRecycler::checkOverlapsHashtableBucketCompatability(
    const OverlapsHashTableMetaInfo& candidate,
    const OverlapsHashTableMetaInfo& target) const {
  if (candidate.bucket_sizes.size() != target.bucket_sizes.size()) {
    return false;
  }
  for (size_t i = 0; i < candidate.bucket_sizes.size(); i++) {
    if (std::abs(target.bucket_sizes[i] - candidate.bucket_sizes[i]) > 1e-4) {
      return false;
    }
  }
  auto threshold_check =
      candidate.overlaps_bucket_threshold == target.overlaps_bucket_threshold;
  auto hashtable_size_check =
      candidate.overlaps_max_table_size_bytes == target.overlaps_max_table_size_bytes;
  return threshold_check && hashtable_size_check;
}

std::string HashTableRecycler::getJoinColumnInfoString(
    std::vector<const Analyzer::ColumnVar*>& inner_cols,
    std::vector<const Analyzer::ColumnVar*>& outer_cols,
    Executor* executor) {
  std::vector<std::string> join_cols_info;
  join_cols_info.push_back(
      executor->getQueryPlanDagCache().translateColVarsToInfoString(inner_cols, false));
  join_cols_info.push_back(
      executor->getQueryPlanDagCache().translateColVarsToInfoString(outer_cols, false));
  return boost::join(join_cols_info, "|");
}

bool HashTableRecycler::isSafeToCacheHashtable(
    const TableIdToNodeMap& table_id_to_node_map,
    bool need_dict_translation,
    const int table_id) {
  // if hashtable is built from subquery's resultset we need to check
  // 1) whether resulset rows can have inconsistency, e.g., rows can randomly be
  // permutated per execution and 2) whether it needs dictionary translation for hashtable
  // building to recycle the hashtable safely
  auto getNodeByTableId =
      [&table_id_to_node_map](const int table_id) -> const RelAlgNode* {
    auto it = table_id_to_node_map.find(table_id);
    if (it != table_id_to_node_map.end()) {
      return it->second;
    }
    return nullptr;
  };
  bool found_sort_node = false;
  bool found_project_node = false;
  if (table_id < 0) {
    auto origin_table_id = table_id * -1;
    auto inner_node = getNodeByTableId(origin_table_id);
    if (!inner_node) {
      // we have to keep the node info of temporary resultset
      // so in this case we are not safe to recycle the hashtable
      return false;
    }
    // it is not safe to recycle the hashtable when
    // this resultset may have resultset ordering inconsistency and/or
    // need dictionary translation for hashtable building
    auto sort_node = dynamic_cast<const RelSort*>(inner_node);
    if (sort_node) {
      found_sort_node = true;
    } else {
      auto project_node = dynamic_cast<const RelProject*>(inner_node);
      if (project_node) {
        found_project_node = true;
      }
    }
  }
  return !(found_sort_node || (found_project_node && need_dict_translation));
}

HashtableAccessPathInfo HashTableRecycler::getHashtableAccessPathInfo(
    const std::vector<InnerOuter>& inner_outer_pairs,
    const SQLOps op_type,
    const JoinType join_type,
    const HashTableBuildDagMap& hashtable_build_dag_map,
    Executor* executor) {
  std::vector<const Analyzer::ColumnVar*> inner_cols_vec, outer_cols_vec;
  std::vector<std::string> join_qual_info;
  for (auto& join_col_pair : inner_outer_pairs) {
    inner_cols_vec.push_back(join_col_pair.first);
    // extract inner join col's id
    // b/c when the inner col comes from a subquery's resulset,
    // table id / rte_index can be different even if we have the same
    // subquery's semantic, i.e., project col A from table T
    join_qual_info.push_back(executor->getQueryPlanDagCache().getJoinColumnsInfoString(
        join_col_pair.first, JoinColumnSide::kDirect, true));
    join_qual_info.push_back(::toString(op_type));
    join_qual_info.push_back(::toString(join_type));
    auto outer_col_var = dynamic_cast<const Analyzer::ColumnVar*>(join_col_pair.second);
    if (outer_col_var) {
      outer_cols_vec.push_back(outer_col_var);
    } else {
      auto cvs = executor->getQueryPlanDagCache().collectColVars(join_col_pair.second);
      std::copy(cvs.begin(), cvs.end(), std::back_inserter(outer_cols_vec));
    }
    join_qual_info.push_back(join_col_pair.first->get_type_info().toString());
    if (join_col_pair.first->get_type_info().is_dict_encoded_string() && outer_col_var) {
      // add comp param for dict encoded string
      join_qual_info.push_back(executor->getQueryPlanDagCache().getJoinColumnsInfoString(
          outer_col_var, JoinColumnSide::kDirect, true));
      join_qual_info.push_back(outer_col_var->get_type_info().toString());
    }
  }
  auto inner_join_cols_info = boost::join(join_qual_info, "|");
  auto join_cols_info = getJoinColumnInfoString(inner_cols_vec, outer_cols_vec, executor);
  HashtableCacheMetaInfo meta_info;
  HashtableAccessPathInfo access_path_info;
  auto it = hashtable_build_dag_map.find(join_cols_info);
  if (it != hashtable_build_dag_map.end()) {
    std::vector<std::string> hashtable_access_path_info;
    hashtable_access_path_info.push_back(it->second.inner_cols_access_path);
    hashtable_access_path_info.push_back(inner_join_cols_info);
    if (inner_cols_vec.front()->get_type_info().is_dict_encoded_string()) {
      hashtable_access_path_info.push_back(it->second.outer_cols_access_path);
    }
    auto hashtable_access_path = boost::join(hashtable_access_path_info, "|");
    QueryPlanMetaInfo query_plan_meta_info;
    query_plan_meta_info.query_plan_dag = hashtable_access_path;
    query_plan_meta_info.inner_col_info_string = inner_join_cols_info;
    HashtableCacheMetaInfo meta_info;
    meta_info.query_plan_meta_info = query_plan_meta_info;
    access_path_info.hashed_query_plan_dag = boost::hash_value(hashtable_access_path);
    access_path_info.meta_info = meta_info;
    access_path_info.table_keys = it->second.inputTableKeys;
  }
  return access_path_info;
}

std::tuple<QueryPlanHash,
           std::shared_ptr<HashTable>,
           std::optional<HashtableCacheMetaInfo>>
HashTableRecycler::getCachedHashtableWithoutCacheKey(std::set<size_t>& visited,
                                                     CacheItemType hash_table_type,
                                                     DeviceIdentifier device_identifier) {
  std::lock_guard<std::mutex> lock(getCacheLock());
  auto hashtable_cache = getCachedItemContainer(hash_table_type, device_identifier);
  for (auto& ht : *hashtable_cache) {
    if (!visited.count(ht.key)) {
      return std::make_tuple(ht.key, ht.cached_item, ht.meta_info);
    }
  }
  return std::make_tuple(EMPTY_HASHED_PLAN_DAG_KEY, nullptr, std::nullopt);
}

void HashTableRecycler::addQueryPlanDagForTableKeys(
    size_t hashed_query_plan_dag,
    const std::unordered_set<size_t>& table_keys) {
  std::lock_guard<std::mutex> lock(getCacheLock());
  for (auto table_key : table_keys) {
    auto it = table_key_to_query_plan_dag_map_.find(table_key);
    if (it != table_key_to_query_plan_dag_map_.end()) {
      it->second.insert(hashed_query_plan_dag);
    } else {
      std::unordered_set<size_t> query_plan_dags{hashed_query_plan_dag};
      table_key_to_query_plan_dag_map_.emplace(table_key, query_plan_dags);
    }
  }
}

std::optional<std::unordered_set<size_t>>
HashTableRecycler::getMappedQueryPlanDagsWithTableKey(size_t table_key) const {
  std::lock_guard<std::mutex> lock(getCacheLock());
  auto it = table_key_to_query_plan_dag_map_.find(table_key);
  return it != table_key_to_query_plan_dag_map_.end() ? std::make_optional(it->second)
                                                      : std::nullopt;
}

void HashTableRecycler::removeTableKeyInfoFromQueryPlanDagMap(size_t table_key) {
  // this function is called when marking cached item for the given table_key as dirty
  // and when we do that we already acquire the cache lock so we skip to lock in this func
  table_key_to_query_plan_dag_map_.erase(table_key);
}
