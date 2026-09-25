// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/welcome_controller.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/profile_defaults.h"
#include "arcium/common/arcium_features.h"
#include "arcium/ui/browser/space_switcher.h"
#include "arcium/ui/browser/welcome_sources.h"
#include "arcium/ui/sidebar/profile_colors.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/path_service.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/ui/select_file_policy/chrome_select_file_policy.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_starter_pack_data.h"
#include "ui/compositor/layer.h"
#include "ui/shell_dialogs/selected_file_info.h"

namespace arcium {
namespace {

// Where to look for Zen and Arc: the home directory, or the one a test or a
// hand check names with --arcium-import-home, so neither ever reads the
// owner's own browsers.
base::FilePath ImportHome() {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(features::kImportHomeSwitch)) {
    return command_line.GetSwitchValuePath(features::kImportHomeSwitch);
  }
  return base::GetHomeDir();
}

bool& KindFlag(ImportChoices& choices, WelcomeKind kind) {
  switch (kind) {
    case WelcomeKind::kSpaces:
      return choices.spaces;
    case WelcomeKind::kPinned:
      return choices.pinned;
    case WelcomeKind::kFavorites:
      return choices.favorites;
    case WelcomeKind::kFolders:
      return choices.folders;
  }
}

}  // namespace

WelcomeController::Group::Group() = default;
WelcomeController::Group::Group(Group&&) = default;
WelcomeController::Group& WelcomeController::Group::operator=(Group&&) =
    default;
WelcomeController::Group::~Group() = default;

WelcomeController::WelcomeController(BrowserView* browser_view,
                                     SpaceSwitcher* switcher,
                                     WelcomeView::Mode mode,
                                     WelcomeStep first,
                                     base::OnceClosure done)
    : browser_view_(browser_view),
      switcher_(switcher),
      profile_(browser_view->GetProfile()),
      mode_(mode),
      done_(std::move(done)) {
  if (TemplateURLService* service =
          TemplateURLServiceFactory::GetForProfile(profile_)) {
    engines_observation_.Observe(service);
    service->Load();
  }
  base::MakeRefCounted<shell_integration::DefaultBrowserWorker>()
      ->StartCheckIsDefault(base::BindOnce(
          [](base::WeakPtr<WelcomeController> controller,
             shell_integration::DefaultWebClientState state) {
            if (controller) {
              controller->OnDefaultBrowserState(state ==
                                                shell_integration::IS_DEFAULT);
            }
          },
          weak_factory_.GetWeakPtr()));

  WelcomeView* card = browser_view_->AddChildView(
      std::make_unique<WelcomeView>(this, mode_, first));
  card_.SetView(card);
  // Over the page, as a peek is: the page draws through a layer of its own,
  // and a view without one paints beneath every layer in its parent.
  card->SetPaintToLayer();
  if (ui::Layer* layer = card->layer(); layer && layer->parent()) {
    layer->parent()->StackAtTop(layer);
  }

  // Looking starts once the card is up, never before the window's first
  // paint, and reads nothing on this thread.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&FindSources, ImportHome()),
      base::BindOnce(&WelcomeController::OnSourcesFound,
                     weak_factory_.GetWeakPtr()));
}

WelcomeController::~WelcomeController() {
  if (file_dialog_) {
    file_dialog_->ListenerDestroyed();
  }
  // The card observes this model; it goes first. When the window is closing
  // it has gone already.
  if (WelcomeView* card = this->card()) {
    card_.SetView(nullptr);
    browser_view_->RemoveChildViewT(card);
  }
}

void WelcomeController::Layout(const gfx::Rect& page_area) {
  if (WelcomeView* card = this->card()) {
    card->SetBoundsRect(page_area);
  }
}

void WelcomeController::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void WelcomeController::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

bool WelcomeController::searching() const {
  return searching_;
}

const std::vector<WelcomeSource>& WelcomeController::sources() const {
  return sources_;
}

std::optional<size_t> WelcomeController::chosen_source() const {
  return chosen_;
}

void WelcomeController::ChooseSource(std::optional<size_t> index) {
  if (index && *index >= groups_.size()) {
    return;
  }
  if (chosen_ != index) {
    chosen_ = index;
    ResetChoices();
  }
  Notify();
}

void WelcomeController::ChooseProfile(size_t index) {
  if (!chosen_ || index >= groups_[*chosen_].found.size()) {
    return;
  }
  Group& group = groups_[*chosen_];
  group.profile = index;
  sources_[*chosen_] = DescribeSource(group.found, index, group.detail);
  ResetChoices();
  Notify();
}

bool WelcomeController::kind_chosen(WelcomeKind kind) const {
  return !left_behind_.contains(kind);
}

void WelcomeController::SetKindChosen(WelcomeKind kind, bool chosen) {
  if (chosen) {
    left_behind_.erase(kind);
  } else {
    left_behind_.insert(kind);
  }
  KindFlag(choices_, kind) = chosen;
  Notify();
}

void WelcomeController::PickFile() {
  if (file_dialog_ &&
      file_dialog_->IsRunning(browser_view_->GetNativeWindow())) {
    return;
  }
  file_dialog_ = ui::SelectFileDialog::Create(
      this, std::make_unique<ChromeSelectFilePolicy>(nullptr));
  ui::SelectFileDialog::FileTypeInfo types;
  types.extensions = {
      {FILE_PATH_LITERAL("jsonlz4"), FILE_PATH_LITERAL("json")}};
  types.include_all_files = true;
  file_dialog_->SelectFile(ui::SelectFileDialog::SELECT_OPEN_FILE,
                           u"Import from Zen or Arc", base::FilePath(), &types,
                           1, base::FilePath::StringType(),
                           browser_view_->GetNativeWindow());
}

void WelcomeController::FileSelected(const ui::SelectedFileInfo& file,
                                     int index) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&ReadSourceFile, file.path()),
      base::BindOnce(&WelcomeController::OnFileRead, weak_factory_.GetWeakPtr(),
                     file.path()));
}

void WelcomeController::FileSelectionCanceled() {}

std::vector<WelcomeSpace> WelcomeController::spaces() const {
  if (const ImportPlan* plan = ChosenPlan()) {
    return SpacesToImport(*plan, choices_, u"from " + SourceName(plan->source));
  }
  return SpacesToImport(StartFreshPlan(), fresh_choices_, u"Example");
}

void WelcomeController::SetSeparateLogins(size_t space, bool separate) {
  const ImportPlan* plan = ChosenPlan();
  const ImportPlan fresh = StartFreshPlan();
  if (!plan) {
    plan = &fresh;
  }
  if (space >= plan->spaces.size()) {
    return;
  }
  ImportChoices& choices = ChosenPlan() ? choices_ : fresh_choices_;
  const std::string& key = plan->spaces[space].key;
  if (separate) {
    choices.separate_logins.insert(key);
  } else {
    choices.separate_logins.erase(key);
  }
  Notify();
}

void WelcomeController::ApplyImport() {
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContext(profile_);
  if (!state) {
    return;
  }
  ArciumModel& model = *state->model();
  const ImportPlan* chosen = ChosenPlan();
  const ImportPlan fresh = StartFreshPlan();
  ImportChoices choices = chosen ? choices_ : fresh_choices_;
  choices.profile_colors = static_cast<int>(ProfileColors().size());
  // Without spaces, everything lands in the space on screen.
  choices.into =
      switcher_ ? switcher_->active_space() : model.default_space_id();
  const ImportResult result =
      ApplyImportPlan(chosen ? *chosen : fresh, choices, model);
  if (result.first_space.is_valid()) {
    arrived_ = result.first_space;
    arrived_origin_ =
        chosen ? u"from " + SourceName(chosen->source) : u"Example";
  }
  // Onto the first space that arrived, and only then away with the empty
  // one a fresh install starts with: the window is never on a space that is
  // going.
  if (switcher_ && result.first_space.is_valid()) {
    switcher_->SwitchTo(result.first_space);
    if (result.empty_starter.is_valid()) {
      switcher_->DeleteSpace(result.empty_starter);
    }
  }
  Notify();
}

std::vector<std::u16string> WelcomeController::engines() const {
  std::vector<std::u16string> names;
  for (const TemplateURL* engine : Engines()) {
    names.push_back(engine->short_name());
  }
  return names;
}

size_t WelcomeController::chosen_engine() const {
  const std::vector<TemplateURL*> engines = Engines();
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(profile_);
  const TemplateURL* current =
      service ? service->GetDefaultSearchProvider() : nullptr;
  if (!current) {
    return engines.size();
  }
  // By Chromium's own number for the engine: the default is kept as a copy
  // of its own, never the same object as the one in the list.
  const auto it = std::ranges::find_if(engines, [&](const TemplateURL* engine) {
    return engine->prepopulate_id() == current->prepopulate_id();
  });
  return static_cast<size_t>(it - engines.begin());
}

void WelcomeController::ChooseEngine(size_t index) {
  const std::vector<TemplateURL*> engines = Engines();
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(profile_);
  if (!service || index >= engines.size() || index == chosen_engine() ||
      !service->CanMakeDefault(engines[index])) {
    return;
  }
  service->SetUserSelectedDefaultSearchProvider(engines[index]);
  // The service tells its observers, and this is one of them.
}

bool WelcomeController::is_default_browser() const {
  return default_browser_;
}

void WelcomeController::MakeDefaultBrowser() {
  // macOS asks the person; the answer arrives when they have given it.
  base::MakeRefCounted<shell_integration::DefaultBrowserWorker>()
      ->StartSetAsDefault(base::BindOnce(
          [](base::WeakPtr<WelcomeController> controller,
             shell_integration::DefaultWebClientState state) {
            if (controller) {
              controller->OnDefaultBrowserState(state ==
                                                shell_integration::IS_DEFAULT);
            }
          },
          weak_factory_.GetWeakPtr()));
}

bool WelcomeController::sync_available() const {
  // Until folder sync exists.
  return false;
}

std::optional<WelcomeSpace> WelcomeController::arrived() const {
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContextIfExists(profile_);
  if (!arrived_.is_valid() || !state || !state->model()->GetSpace(arrived_)) {
    return std::nullopt;
  }
  return SpaceAsArrived(*state->model(), arrived_, arrived_origin_);
}

void WelcomeController::StepReached(WelcomeStep step) {
  // The command box's import is not the welcome, and leaves its progress
  // alone.
  if (mode_ == WelcomeView::Mode::kWelcome) {
    profile_->GetPrefs()->SetInteger(kWelcomeStepPref,
                                     static_cast<int>(step) + 1);
  }
}

void WelcomeController::Finish() {
  if (mode_ == WelcomeView::Mode::kWelcome) {
    profile_->GetPrefs()->SetInteger(kWelcomeStepPref, kWelcomeDone);
  }
  // Called from a click on the card; the card must outlive that click, so
  // the owner lets go of this controller at the next turn.
  if (done_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                             std::move(done_));
  }
}

void WelcomeController::OnTemplateURLServiceChanged() {
  Notify();
}

void WelcomeController::OnSourcesFound(std::vector<FoundSource> found) {
  searching_ = false;
  for (std::vector<FoundSource>& browser : GroupByBrowser(std::move(found))) {
    Group group;
    group.found = std::move(browser);
    group.detail = u"Found on this Mac";
    sources_.push_back(DescribeSource(group.found, 0, group.detail));
    groups_.push_back(std::move(group));
  }
  // What was found comes preselected; with nothing found, Start fresh is.
  // A file picked while looking stays chosen.
  if (!chosen_ && !groups_.empty()) {
    chosen_ = 0;
    ResetChoices();
  }
  Notify();
}

void WelcomeController::OnFileRead(base::FilePath path,
                                   std::optional<FoundSource> found) {
  if (!found) {
    return;
  }
  Group group;
  group.found.push_back(std::move(*found));
  group.detail = path.BaseName().LossyDisplayName();
  sources_.push_back(DescribeSource(group.found, 0, group.detail));
  groups_.push_back(std::move(group));
  chosen_ = groups_.size() - 1;
  ResetChoices();
  Notify();
}

void WelcomeController::OnDefaultBrowserState(bool is_default) {
  default_browser_ = is_default;
  Notify();
}

const ImportPlan* WelcomeController::ChosenPlan() const {
  if (!chosen_ || *chosen_ >= groups_.size()) {
    return nullptr;
  }
  const Group& group = groups_[*chosen_];
  return &group.found[std::min(group.profile, group.found.size() - 1)].plan;
}

void WelcomeController::ResetChoices() {
  const ImportPlan* plan = ChosenPlan();
  if (!plan) {
    return;
  }
  choices_ = DefaultChoices(*plan);
  for (WelcomeKind kind : left_behind_) {
    KindFlag(choices_, kind) = false;
  }
}

std::vector<TemplateURL*> WelcomeController::Engines() const {
  std::vector<TemplateURL*> engines;
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(profile_);
  if (!service || !service->loaded()) {
    return engines;
  }
  // Not filtered by CanMakeDefault, which says no to the engine that is
  // already the default: the list would never hold the one ticked.
  for (TemplateURL* engine : service->GetTemplateURLs()) {
    if (engine->prepopulate_id() > 0 &&
        engine->starter_pack_id() ==
            template_url_starter_pack_data::StarterPackId::kNone) {
      engines.push_back(engine);
    }
  }
  return engines;
}

WelcomeView* WelcomeController::card() {
  return static_cast<WelcomeView*>(card_.view());
}

void WelcomeController::Notify() {
  for (Observer& observer : observers_) {
    observer.OnWelcomeChanged();
  }
}

}  // namespace arcium
