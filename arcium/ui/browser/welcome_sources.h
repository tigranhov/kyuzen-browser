// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_WELCOME_SOURCES_H_
#define ARCIUM_UI_BROWSER_WELCOME_SOURCES_H_

#include <optional>
#include <string>
#include <vector>

#include "arcium/browser/import/import_applier.h"
#include "arcium/browser/import/import_finder.h"
#include "arcium/browser/import/import_plan.h"
#include "arcium/browser/model/space.h"
#include "arcium/ui/welcome/welcome_model.h"

namespace arcium {

class ArciumModel;

// The welcome's reading of what the import found and of what it made: which
// browsers, which spaces, and how the space someone lands in looks. Kept
// apart from the controller, which holds the browser's services, so all of it
// can be tested without a browser.

// The step the welcome opens at on this launch, from the stored step and
// whether this launch found no model file: nothing once it is done, the step
// reached when one is stored, the first step on a fresh install, and nothing
// for someone updating from a version that had no welcome.
std::optional<WelcomeStep> WelcomeResumeStep(int stored,
                                             bool model_file_absent);

// "Zen" or "Arc".
std::u16string SourceName(ImportSourceKind kind);

// What was found, one group per browser: every Zen profile that holds spaces
// under one Zen, in the order found (the default first), then Arc.
std::vector<std::vector<FoundSource>> GroupByBrowser(
    std::vector<FoundSource> found);

// A browser as the first step shows it: its name, `detail` under it, its
// profiles when there are several to choose from, and what the chosen one
// holds.
WelcomeSource DescribeSource(const std::vector<FoundSource>& group,
                             size_t profile,
                             const std::u16string& detail);

// The plan's spaces as the first two steps list them, with the logins
// `choices` keeps apart.
std::vector<WelcomeSpace> SpacesToImport(const ImportPlan& plan,
                                         const ImportChoices& choices,
                                         const std::u16string& origin);

// The two example spaces Start fresh makes, Personal and Work, holding
// nothing.
ImportPlan StartFreshPlan();

// `space` as the sidebar now draws it: its name, and the first rows of its
// pinned section, folders before loose pages as the sidebar orders them.
WelcomeSpace SpaceAsArrived(const ArciumModel& model,
                            SpaceId space,
                            const std::u16string& origin);

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_WELCOME_SOURCES_H_
