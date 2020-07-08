/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "ConcurrentContainers.h"
#include "DexClass.h"
#include "DexStore.h"
#include "FrameworkApi.h"
#include "TypeUtil.h"

// Helper class that checks if it's safe to use a type/method/field in
// - the context of a particular store, and
// - any context where we can only assume a particular min-sdk.
//
// Types/methods/fields directly contained in the min-sdk are fine. We also
// check that any declaring types, array element types, super types, implemented
// interface types, return types, argument types, field types are valid for the
// given min-sdk.
//
// All functions are thread-safe.
class RefChecker {
 public:
  RefChecker() = delete;
  RefChecker(const RefChecker&) = delete;
  RefChecker& operator=(const RefChecker&) = delete;
  explicit RefChecker(XStoreRefs* xstores,
                      size_t store_idx,
                      const api::AndroidSDK* min_sdk_api)
      : m_xstores(xstores),
        m_store_idx(store_idx),
        m_min_sdk_api(min_sdk_api) {}

  bool check_type(const DexType* type) const;

  bool check_method(const DexMethod* method) const;

  bool check_field(const DexField* field) const;

 private:
  XStoreRefs* m_xstores;
  size_t m_store_idx;
  const api::AndroidSDK* m_min_sdk_api;

  enum class MaybeBoolean : uint8_t { FALSE, TRUE, UNKNOWN };
  static MaybeBoolean to_maybe_boolean(bool value) {
    return value ? MaybeBoolean::TRUE : MaybeBoolean::FALSE;
  }
  static bool from_maybe_boolean(MaybeBoolean maybe_boolean) {
    always_assert(maybe_boolean != MaybeBoolean::UNKNOWN);
    return maybe_boolean != MaybeBoolean::FALSE;
  }
  mutable ConcurrentMap<const DexType*, MaybeBoolean> m_type_cache;
  mutable ConcurrentMap<const DexMethod*, MaybeBoolean> m_method_cache;
  mutable ConcurrentMap<const DexField*, MaybeBoolean> m_field_cache;

  bool check_type_internal(const DexType* type) const;

  bool check_method_internal(const DexMethod* method) const;

  bool check_field_internal(const DexField* field) const;
};