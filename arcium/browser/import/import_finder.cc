// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/import/import_finder.h"

#include <algorithm>
#include <map>
#include <utility>

#include "arcium/browser/import/arc_reader.h"
#include "arcium/browser/import/mozlz4.h"
#include "arcium/browser/import/zen_reader.h"
#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/values.h"

namespace arcium {
namespace {

// profiles.ini, containers.json and Arc's Local State are small.
constexpr size_t kMaxSmallFile = 16u * 1024u * 1024u;
// Arc's sidebar file carries two more copies of the sidebar for syncing, so
// a large sidebar makes a large file.
constexpr size_t kMaxArcFile = 128u * 1024u * 1024u;
// The first eight bytes of every mozLz4 file, the last one a zero.
constexpr std::string_view kMozLz4Magic("mozLz40\0", 8);

std::string ReadSmall(const base::FilePath& path) {
  std::string text;
  if (!base::ReadFileToStringWithMaxSize(path, &text, kMaxSmallFile)) {
    text.clear();
  }
  return text;
}

base::FilePath AppSupport(const base::FilePath& home) {
  return home.AppendASCII("Library").AppendASCII("Application Support");
}

std::optional<ImportPlan> ReadZenFile(const base::FilePath& file,
                                      std::string_view containers) {
  std::string bytes;
  if (!base::ReadFileToStringWithMaxSize(file, &bytes, kMaxMozLz4Size + 12)) {
    return std::nullopt;
  }
  const std::optional<std::string> json =
      DecodeMozLz4(base::as_byte_span(bytes));
  return json ? ReadZenSession(*json, containers) : std::nullopt;
}

// profiles.ini's sections in file order, each a set of keys.
using IniSection = std::map<std::string, std::string, std::less<>>;
std::vector<std::pair<std::string, IniSection>> ParseIni(
    std::string_view text) {
  std::vector<std::pair<std::string, IniSection>> sections;
  for (std::string_view line : base::SplitStringPiece(
           text, "\r\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (line.front() == '[' && line.back() == ']') {
      sections.emplace_back(std::string(line.substr(1, line.size() - 2)),
                            IniSection());
      continue;
    }
    const size_t equals = line.find('=');
    if (equals == std::string_view::npos || sections.empty() ||
        line.front() == ';' || line.front() == '#') {
      continue;
    }
    sections.back().second[std::string(
        base::TrimWhitespaceASCII(line.substr(0, equals), base::TRIM_ALL))] =
        std::string(
            base::TrimWhitespaceASCII(line.substr(equals + 1), base::TRIM_ALL));
  }
  return sections;
}

std::string_view KeyOr(const IniSection& section, std::string_view key) {
  const auto it = section.find(key);
  return it != section.end() ? std::string_view(it->second)
                             : std::string_view();
}

struct ZenProfile {
  std::string name;
  // profiles.ini's own spelling of its folder, which the install's default
  // repeats.
  std::string descriptor;
  bool marked_default = false;
  base::FilePath file;
  base::Time saved;
  ImportPlan plan;
};

// A profile's saved sidebar. Zen falls back to its clean-shutdown copy when
// the main file is missing or damaged, and so does this.
std::optional<ZenProfile> ReadZenProfile(const base::FilePath& folder) {
  const std::string containers =
      ReadSmall(folder.AppendASCII("containers.json"));
  for (const base::FilePath& file : {folder.AppendASCII("zen-sessions.jsonlz4"),
                                     folder.AppendASCII("zen-sessions-backup")
                                         .AppendASCII("clean.jsonlz4")}) {
    if (std::optional<ImportPlan> plan = ReadZenFile(file, containers)) {
      ZenProfile profile;
      profile.file = file;
      base::File::Info info;
      if (base::GetFileInfo(file, &info)) {
        profile.saved = info.last_modified;
      }
      profile.plan = std::move(*plan);
      return profile;
    }
  }
  return std::nullopt;
}

void FindZen(const base::FilePath& home, std::vector<FoundSource>& found) {
  const base::FilePath root = AppSupport(home).AppendASCII("zen");
  std::vector<std::string> install_defaults;
  std::vector<ZenProfile> profiles;
  for (const auto& [name, section] :
       ParseIni(ReadSmall(root.AppendASCII("profiles.ini")))) {
    const std::string descriptor(KeyOr(section, "Path"));
    if (base::StartsWith(name, "Install")) {
      install_defaults.emplace_back(KeyOr(section, "Default"));
      continue;
    }
    if (!base::StartsWith(name, "Profile") || descriptor.empty()) {
      continue;
    }
    base::FilePath folder = base::FilePath::FromUTF8Unsafe(descriptor);
    if (!folder.IsAbsolute()) {
      folder = root.Append(folder);
    }
    std::optional<ZenProfile> profile = ReadZenProfile(folder);
    if (!profile || profile->plan.spaces.empty()) {
      continue;
    }
    profile->name = KeyOr(section, "Name");
    profile->descriptor = descriptor;
    profile->marked_default = KeyOr(section, "Default") == "1";
    profiles.push_back(std::move(*profile));
  }
  // The profile Zen starts is its install's default. Several installs each
  // name one, and which is this Mac's Zen takes a hash to tell, so the one
  // saved last stands for it. Without an install, the older default mark.
  size_t first = 0;
  bool by_install = false;
  for (size_t i = 0; i < profiles.size(); ++i) {
    const bool install =
        std::ranges::find(install_defaults, profiles[i].descriptor) !=
        install_defaults.end();
    if (install && (!by_install || profiles[i].saved > profiles[first].saved)) {
      first = i;
      by_install = true;
    } else if (!by_install && profiles[i].marked_default &&
               !profiles[first].marked_default) {
      first = i;
    }
  }
  if (first != 0) {
    std::rotate(profiles.begin(), profiles.begin() + first,
                profiles.begin() + first + 1);
  }
  for (ZenProfile& profile : profiles) {
    FoundSource source;
    source.kind = ImportSourceKind::kZen;
    source.profile_name = std::move(profile.name);
    source.path = std::move(profile.file);
    source.plan = std::move(profile.plan);
    found.push_back(std::move(source));
  }
}

// The sidebar file names Arc's profiles only by folder; Arc's Chromium side
// keeps the names people see.
void NameArcProfiles(const base::FilePath& local_state, ImportPlan& plan) {
  if (plan.profiles.empty()) {
    return;
  }
  const std::optional<base::DictValue> state =
      base::JSONReader::ReadDict(ReadSmall(local_state), base::JSON_PARSE_RFC);
  const base::DictValue* cache =
      state ? state->FindDictByDottedPath("profile.info_cache") : nullptr;
  if (!cache) {
    return;
  }
  for (ImportProfile& profile : plan.profiles) {
    const base::DictValue* info = cache->FindDict(profile.name);
    const std::string* name = info ? info->FindString("name") : nullptr;
    if (name && !name->empty()) {
      profile.name = *name;
    }
  }
}

void FindArc(const base::FilePath& home, std::vector<FoundSource>& found) {
  const base::FilePath root = AppSupport(home).AppendASCII("Arc");
  std::optional<FoundSource> source =
      ReadSourceFile(root.AppendASCII("StorableSidebar.json"));
  if (!source || source->kind != ImportSourceKind::kArc ||
      source->plan.spaces.empty()) {
    return;
  }
  NameArcProfiles(root.AppendASCII("User Data").AppendASCII("Local State"),
                  source->plan);
  found.push_back(std::move(*source));
}

}  // namespace

FoundSource::FoundSource() = default;
FoundSource::FoundSource(const FoundSource&) = default;
FoundSource::FoundSource(FoundSource&&) = default;
FoundSource& FoundSource::operator=(const FoundSource&) = default;
FoundSource& FoundSource::operator=(FoundSource&&) = default;
FoundSource::~FoundSource() = default;

std::vector<FoundSource> FindSources(const base::FilePath& home) {
  std::vector<FoundSource> found;
  FindZen(home, found);
  FindArc(home, found);
  return found;
}

std::optional<FoundSource> ReadSourceFile(const base::FilePath& path) {
  std::string bytes;
  if (!base::ReadFileToStringWithMaxSize(path, &bytes, kMaxArcFile)) {
    return std::nullopt;
  }
  FoundSource source;
  source.path = path;
  if (base::StartsWith(bytes, kMozLz4Magic)) {
    const std::optional<std::string> json =
        DecodeMozLz4(base::as_byte_span(bytes));
    // A copied file may have left its containers behind; the separate
    // logins then keep a plain name.
    std::optional<ImportPlan> plan =
        json ? ReadZenSession(
                   *json,
                   ReadSmall(path.DirName().AppendASCII("containers.json")))
             : std::nullopt;
    if (!plan) {
      return std::nullopt;
    }
    source.kind = ImportSourceKind::kZen;
    source.plan = std::move(*plan);
    return source;
  }
  std::optional<ImportPlan> plan = ReadArcSidebar(bytes);
  if (!plan) {
    return std::nullopt;
  }
  source.kind = ImportSourceKind::kArc;
  source.plan = std::move(*plan);
  return source;
}

}  // namespace arcium
