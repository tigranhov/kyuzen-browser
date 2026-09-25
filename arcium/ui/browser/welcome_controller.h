// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_WELCOME_CONTROLLER_H_
#define ARCIUM_UI_BROWSER_WELCOME_CONTROLLER_H_

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "arcium/browser/import/import_applier.h"
#include "arcium/browser/import/import_finder.h"
#include "arcium/browser/model/space.h"
#include "arcium/ui/welcome/welcome_model.h"
#include "arcium/ui/welcome/welcome_view.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/scoped_observation.h"
#include "components/search_engines/template_url_service.h"
#include "components/search_engines/template_url_service_observer.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/views/view_tracker.h"

class BrowserView;
class Profile;
class TemplateURL;

namespace arcium {

class SpaceSwitcher;

// The browser's side of the welcome: the card over the page area, and the
// real answers behind it -- Zen and Arc found under the home directory, the
// import applied to this profile's model, Chromium's search engines, the
// macOS default browser, and the step reached, kept in a pref so a quit
// brings the welcome back there.
//
// Exists only while the card is up. Looking for Zen and Arc starts once the
// card is on screen, on the thread pool; nothing is read before first paint.
class WelcomeController : public WelcomeModel,
                          public TemplateURLServiceObserver,
                          public ui::SelectFileDialog::Listener {
 public:
  WelcomeController(BrowserView* browser_view,
                    SpaceSwitcher* switcher,
                    WelcomeView::Mode mode,
                    WelcomeStep first,
                    base::OnceClosure done);
  WelcomeController(const WelcomeController&) = delete;
  WelcomeController& operator=(const WelcomeController&) = delete;
  ~WelcomeController() override;

  // Places the card over `page_area`, in the BrowserView's coordinates.
  // Called on every window layout.
  void Layout(const gfx::Rect& page_area);

  WelcomeView* view_for_testing() { return card(); }

  // WelcomeModel:
  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;
  bool searching() const override;
  const std::vector<WelcomeSource>& sources() const override;
  std::optional<size_t> chosen_source() const override;
  void ChooseSource(std::optional<size_t> index) override;
  void ChooseProfile(size_t index) override;
  bool kind_chosen(WelcomeKind kind) const override;
  void SetKindChosen(WelcomeKind kind, bool chosen) override;
  void PickFile() override;
  std::vector<WelcomeSpace> spaces() const override;
  void SetSeparateLogins(size_t space, bool separate) override;
  void ApplyImport() override;
  std::vector<std::u16string> engines() const override;
  size_t chosen_engine() const override;
  void ChooseEngine(size_t index) override;
  bool is_default_browser() const override;
  void MakeDefaultBrowser() override;
  bool sync_available() const override;
  std::optional<WelcomeSpace> arrived() const override;
  void StepReached(WelcomeStep step) override;
  void Finish() override;

  // TemplateURLServiceObserver:
  void OnTemplateURLServiceChanged() override;

  // ui::SelectFileDialog::Listener:
  void FileSelected(const ui::SelectedFileInfo& file, int index) override;
  void FileSelectionCanceled() override;

 private:
  // One browser's worth of what was found: several Zen profiles, one Arc,
  // or one picked file.
  struct Group {
    Group();
    Group(Group&&);
    Group& operator=(Group&&);
    ~Group();

    std::vector<FoundSource> found;
    size_t profile = 0;
    std::u16string detail;
  };

  void OnSourcesFound(std::vector<FoundSource> found);
  void OnFileRead(base::FilePath path, std::optional<FoundSource> found);
  void OnDefaultBrowserState(bool is_default);
  // The plan the first step has chosen, or null for Start fresh.
  const ImportPlan* ChosenPlan() const;
  // The choices the chosen plan starts from, keeping the kinds already
  // left behind.
  void ResetChoices();
  // The engines a person can choose: those Chromium prepopulates for this
  // country, in its order.
  std::vector<TemplateURL*> Engines() const;
  void Notify();
  WelcomeView* card();

  const raw_ptr<BrowserView> browser_view_;
  const raw_ptr<SpaceSwitcher> switcher_;
  const raw_ptr<Profile> profile_;
  const WelcomeView::Mode mode_;
  base::OnceClosure done_;
  // The card, or null once it is gone. Tracked rather than held, because a
  // closing window deletes its child views before it lets go of the sidebar
  // controller that owns this.
  views::ViewTracker card_;

  bool searching_ = true;
  std::vector<Group> groups_;
  std::vector<WelcomeSource> sources_;
  std::optional<size_t> chosen_;
  std::set<WelcomeKind> left_behind_;
  ImportChoices choices_;
  // Start fresh's own, so a toggle on an example space survives choosing Zen
  // and coming back.
  ImportChoices fresh_choices_;
  SpaceId arrived_;
  std::u16string arrived_origin_;
  bool default_browser_ = false;
  scoped_refptr<ui::SelectFileDialog> file_dialog_;

  base::ObserverList<Observer> observers_;
  base::ScopedObservation<TemplateURLService, TemplateURLServiceObserver>
      engines_observation_{this};
  base::WeakPtrFactory<WelcomeController> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_WELCOME_CONTROLLER_H_
