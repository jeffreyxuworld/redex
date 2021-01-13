/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ConfigFiles.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "Debug.h"
#include "DexClass.h"

ConfigFiles::ConfigFiles(const Json::Value& config, const std::string& outdir)
    : m_json(config),
      outdir(outdir),
      m_proguard_map(config.get("proguard_map", "").asString()),
      m_printseeds(config.get("printseeds", "").asString()) {

  m_coldstart_class_filename = config.get("coldstart_classes", "").asString();
  if (m_coldstart_class_filename.empty()) {
    m_coldstart_class_filename =
        config.get("default_coldstart_classes", "").asString();
  }

  load_method_sorting_allowlisted_substrings();
  uint32_t instruction_size_bitwidth_limit =
      config.get("instruction_size_bitwidth_limit", 0).asUInt();
  always_assert_log(
      instruction_size_bitwidth_limit < 32,
      "instruction_size_bitwidth_limit must be between 0 and 31, actual: %u\n",
      instruction_size_bitwidth_limit);
  m_instruction_size_bitwidth_limit = instruction_size_bitwidth_limit;
}

ConfigFiles::ConfigFiles(const Json::Value& config) : ConfigFiles(config, "") {}

/**
 * This function relies on the g_redex.
 */
const std::unordered_set<DexType*>& ConfigFiles::get_no_optimizations_annos() {
  if (m_no_optimizations_annos.empty()) {
    Json::Value no_optimizations_anno;
    m_json.get("no_optimizations_annotations", Json::nullValue,
               no_optimizations_anno);
    if (!no_optimizations_anno.empty()) {
      for (auto const& config_anno_name : no_optimizations_anno) {
        std::string anno_name = config_anno_name.asString();
        DexType* anno = DexType::get_type(anno_name.c_str());
        if (anno) m_no_optimizations_annos.insert(anno);
      }
    }
  }
  return m_no_optimizations_annos;
}

/**
 * This function relies on the g_redex.
 */
const std::unordered_set<DexMethodRef*>& ConfigFiles::get_pure_methods() {
  if (m_pure_methods.empty()) {
    Json::Value pure_methods;
    m_json.get("pure_methods", Json::nullValue, pure_methods);
    if (!pure_methods.empty()) {
      for (auto const& method_name : pure_methods) {
        std::string name = method_name.asString();
        DexMethodRef* method = DexMethod::get_method(name);
        if (method) m_pure_methods.insert(method);
      }
    }
  }
  return m_pure_methods;
}

/**
 * Read an interdex list file and return as a vector of appropriately-formatted
 * classname strings.
 */
std::vector<std::string> ConfigFiles::load_coldstart_classes() {
  if (m_coldstart_class_filename.empty()) {
    return {};
  }
  const char* kClassTail = ".class";
  const size_t lentail = strlen(kClassTail);
  auto file = m_coldstart_class_filename.c_str();

  std::vector<std::string> coldstart_classes;

  std::ifstream input(file);
  if (!input) {
    fprintf(stderr,
            "[error] Can not open <coldstart_classes> file, path is %s\n",
            file);
    exit(EXIT_FAILURE);
  }
  std::string clzname;
  while (input >> clzname) {
    long position = clzname.length() - lentail;
    always_assert_log(position >= 0,
                      "Bailing, invalid class spec '%s' in interdex file %s\n",
                      clzname.c_str(), file);
    clzname.replace(position, lentail, ";");
    coldstart_classes.emplace_back(
        m_proguard_map.translate_class("L" + clzname));
  }
  return coldstart_classes;
}

/**
 * Read a map of {list_name : class_list} from json
 */
std::unordered_map<std::string, std::vector<std::string>>
ConfigFiles::load_class_lists() {
  std::unordered_map<std::string, std::vector<std::string>> lists;
  std::string class_lists_filename;
  this->m_json.get("class_lists", "", class_lists_filename);

  if (class_lists_filename.empty()) {
    return lists;
  }

  std::ifstream input(class_lists_filename);
  Json::Reader reader;
  Json::Value root;
  bool parsing_succeeded = reader.parse(input, root);
  always_assert_log(
      parsing_succeeded, "Failed to parse class list json from file: %s\n%s",
      class_lists_filename.c_str(), reader.getFormattedErrorMessages().c_str());

  for (Json::ValueIterator it = root.begin(); it != root.end(); ++it) {
    std::vector<std::string> class_list;
    Json::Value current_list = *it;
    for (Json::ValueIterator list_it = current_list.begin();
         list_it != current_list.end();
         ++list_it) {
      lists[it.key().asString()].push_back((*list_it).asString());
    }
  }

  lists["secondary_dex_head.list"] = get_coldstart_classes();

  return lists;
}

void ConfigFiles::load_method_sorting_allowlisted_substrings() {
  const auto json_cfg = get_json_config();
  Json::Value json_result;
  json_cfg.get("method_sorting_allowlisted_substrings", Json::nullValue,
               json_result);
  if (!json_result.empty()) {
    for (auto const& json_element : json_result) {
      m_method_sorting_allowlisted_substrings.insert(json_element.asString());
    }
  }
}

void ConfigFiles::ensure_agg_method_stats_loaded() {
  std::vector<std::string> csv_filenames;
  get_json_config().get("agg_method_stats_files", {}, csv_filenames);
  if (csv_filenames.empty() || m_method_profiles.is_initialized()) {
    return;
  }
  m_method_profiles.initialize(csv_filenames);
}

void ConfigFiles::load_inliner_config(inliner::InlinerConfig* inliner_config) {
  Json::Value config;
  m_json.get("inliner", Json::nullValue, config);
  if (config.empty()) {
    m_json.get("MethodInlinePass", Json::nullValue, config);
  }
  if (config.empty()) {
    fprintf(stderr, "WARNING: No inliner config\n");
    return;
  }
  JsonWrapper jw(config);
  jw.get("virtual", true, inliner_config->virtual_inline);
  jw.get("true_virtual_inline", false, inliner_config->true_virtual_inline);
  jw.get("throws", false, inliner_config->throws_inline);
  jw.get("enforce_method_size_limit",
         true,
         inliner_config->enforce_method_size_limit);
  jw.get("use_constant_propagation_for_callee_size", true,
         inliner_config->use_constant_propagation_for_callee_size);
  jw.get("use_cfg_inliner", true, inliner_config->use_cfg_inliner);
  jw.get("multiple_callers", false, inliner_config->multiple_callers);
  jw.get("inline_small_non_deletables",
         true,
         inliner_config->inline_small_non_deletables);
  jw.get("run_const_prop", false, inliner_config->run_const_prop);
  jw.get("run_cse", false, inliner_config->run_cse);
  jw.get("run_copy_prop", false, inliner_config->run_copy_prop);
  jw.get("run_local_dce", false, inliner_config->run_local_dce);
  jw.get("run_dedup_blocks", false, inliner_config->run_dedup_blocks);
  jw.get("debug", false, inliner_config->debug);
  jw.get("blocklist", {}, inliner_config->m_blocklist);
  jw.get("caller_blocklist", {}, inliner_config->m_caller_blocklist);
  jw.get("intradex_white_list", {}, inliner_config->m_intradex_white_list);

  std::vector<std::string> no_inline_annos;
  jw.get("no_inline_annos", {}, no_inline_annos);
  for (const auto& type_s : no_inline_annos) {
    auto type = DexType::get_type(type_s.c_str());
    if (type != nullptr) {
      inliner_config->m_no_inline_annos.emplace(type);
    } else {
      fprintf(stderr, "WARNING: Cannot find no_inline annotation %s\n",
              type_s.c_str());
    }
  }

  std::vector<std::string> force_inline_annos;
  jw.get("force_inline_annos", {}, force_inline_annos);
  for (const auto& type_s : force_inline_annos) {
    auto type = DexType::get_type(type_s.c_str());
    if (type != nullptr) {
      inliner_config->m_force_inline_annos.emplace(type);
    } else {
      fprintf(stderr, "WARNING: Cannot find force_inline annotation %s\n",
              type_s.c_str());
    }
  }
}

void ConfigFiles::load(const Scope& scope) {
  get_inliner_config();
  m_inliner_config->populate(scope);
}

bool ConfigFiles::force_single_dex() const {
  return m_json.get("force_single_dex", false);
}
