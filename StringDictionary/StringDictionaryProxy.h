/*
 * Copyright 2017 MapD Technologies, Inc.
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

#ifndef STRINGDICTIONARY_STRINGDICTIONARYPROXY_H
#define STRINGDICTIONARY_STRINGDICTIONARYPROXY_H

#include "../Shared/mapd_shared_mutex.h"
#include "StringDictionary.h"

#include <map>
#include <string>
#include <tuple>
#include <vector>

// used to access a StringDictionary when transient strings are involved
class StringDictionaryProxy {
  friend bool operator==(const StringDictionaryProxy& sdp1,
                         const StringDictionaryProxy& sdp2);
  friend bool operator!=(const StringDictionaryProxy& sdp1,
                         const StringDictionaryProxy& sdp2);

 public:
  StringDictionaryProxy(std::shared_ptr<StringDictionary> sd,
                        const int32_t string_dict_id,
                        const int64_t generation);

  int32_t getOrAdd(const std::string& str) noexcept;
  StringDictionary* getDictionary() const noexcept;
  int64_t getGeneration() const noexcept;
  int32_t getOrAddTransient(const std::string& str);
  int32_t getIdOfString(const std::string& str) const;
  int32_t getIdOfStringNoGeneration(
      const std::string& str) const;  // disregard generation, only used by QueryRenderer
  std::string getString(int32_t string_id) const;
  std::pair<const char*, size_t> getStringBytes(int32_t string_id) const noexcept;
  size_t storageEntryCount() const;
  void updateGeneration(const int64_t generation) noexcept;

  std::vector<int32_t> getLike(const std::string& pattern,
                               const bool icase,
                               const bool is_simple,
                               const char escape) const;

  std::vector<int32_t> getCompare(const std::string& pattern,
                                  const std::string& comp_operator) const;

  std::vector<int32_t> getRegexpLike(const std::string& pattern, const char escape) const;

  const std::map<int32_t, std::string> getTransientMapping() const {
    return transient_int_to_str_;
  }

 private:
  std::shared_ptr<StringDictionary> string_dict_;
  const int32_t string_dict_id_;
  std::map<int32_t, std::string> transient_int_to_str_;
  std::map<std::string, int32_t> transient_str_to_int_;
  int64_t generation_;
  mutable mapd_shared_mutex rw_mutex_;
};
#endif  // STRINGDICTIONARY_STRINGDICTIONARYPROXY_H
