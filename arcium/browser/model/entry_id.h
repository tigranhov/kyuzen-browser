// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_MODEL_ENTRY_ID_H_
#define ARCIUM_BROWSER_MODEL_ENTRY_ID_H_

#include <compare>
#include <string>

#include "base/uuid.h"

namespace arcium {

// A stable identity for a persistent entity. Distinct types for entries and
// folders so one cannot be passed where the other is meant.
template <typename Tag>
class TypedId {
 public:
  TypedId() = default;

  static TypedId Generate() {
    return TypedId(base::Uuid::GenerateRandomV4().AsLowercaseString());
  }

  // Returns an invalid id when `value` is not a well-formed UUID, so a
  // corrupt file cannot inject an id that collides with a generated one.
  static TypedId FromString(const std::string& value) {
    return base::Uuid::ParseLowercase(value).is_valid() ? TypedId(value)
                                                        : TypedId();
  }

  bool is_valid() const { return !value_.empty(); }
  const std::string& value() const { return value_; }

  friend bool operator==(const TypedId&, const TypedId&) = default;
  friend auto operator<=>(const TypedId&, const TypedId&) = default;

 private:
  explicit TypedId(std::string value) : value_(std::move(value)) {}

  std::string value_;
};

struct EntryIdTag;
struct FolderIdTag;
struct SpaceIdTag;

using EntryId = TypedId<EntryIdTag>;
using FolderId = TypedId<FolderIdTag>;
using SpaceId = TypedId<SpaceIdTag>;

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_ENTRY_ID_H_
