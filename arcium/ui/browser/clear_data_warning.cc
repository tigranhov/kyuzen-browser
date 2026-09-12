// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/clear_data_warning.h"

#include <utility>
#include <vector>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/arcium_profile.h"
#include "arcium/ui/browser/profile_actions.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/utf_string_conversions.h"
#include "components/constrained_window/constrained_window_views.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"
#include "ui/base/models/dialog_model.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"

namespace arcium {

namespace {

std::optional<bool>& TestAnswer() {
  // Not a base::NoDestructor: std::optional<bool> is trivially destructible,
  // so NoDestructor's own static_assert refuses it -- a plain function-local
  // static is exactly what it tells callers to use instead.
  static std::optional<bool> answer;
  return answer;
}

// Marks the settings page that has just been answered, so the removal it
// then asks for is let through instead of asking again.
class AlreadyAsked : public content::WebContentsUserData<AlreadyAsked> {
 public:
  ~AlreadyAsked() override = default;

  // One removal per answer: a later clear on the same page is a new
  // question.
  static bool TakeFrom(content::WebContents* contents) {
    if (!FromWebContents(contents)) {
      return false;
    }
    contents->RemoveUserData(UserDataKey());
    return true;
  }

 private:
  friend class content::WebContentsUserData<AlreadyAsked>;
  explicit AlreadyAsked(content::WebContents* contents)
      : content::WebContentsUserData<AlreadyAsked>(*contents) {}

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(AlreadyAsked);

void Answer(base::WeakPtr<content::WebContents> settings_page,
            base::OnceClosure resume,
            bool clear_every_profile) {
  if (!settings_page) {
    return;
  }
  if (clear_every_profile) {
    content::BrowserContext* context = settings_page->GetBrowserContext();
    ArciumProfileState* state =
        ArciumProfileState::GetForBrowserContextIfExists(context);
    if (state) {
      for (const ArciumProfile& profile : state->model()->profiles()) {
        if (profile.id != DefaultProfileId()) {
          // Chrome's own removal, which is about to run, covers Default.
          ClearArciumProfileData(context, profile.id, base::DoNothing());
        }
      }
    }
  }
  AlreadyAsked::CreateForWebContents(settings_page.get());
  std::move(resume).Run();
}

}  // namespace

bool AskWhichProfilesToClear(content::WebContents* settings_page,
                             base::OnceClosure resume) {
  if (!settings_page || AlreadyAsked::TakeFrom(settings_page)) {
    return false;
  }
  ArciumProfileState* state = ArciumProfileState::GetForBrowserContextIfExists(
      settings_page->GetBrowserContext());
  if (!state || state->model()->profiles().size() <= 1) {
    return false;
  }
  if (TestAnswer().has_value()) {
    Answer(settings_page->GetWeakPtr(), std::move(resume), *TestAnswer());
    return true;
  }

  auto split = base::SplitOnceCallback(
      base::BindOnce(&Answer, settings_page->GetWeakPtr(), std::move(resume)));
  std::unique_ptr<ui::DialogModel> dialog =
      ui::DialogModel::Builder()
          .SetTitle(u"Some spaces keep their own logins")
          .AddParagraph(ui::DialogModelLabel(
              u"This can sign you out of every account in every space, "
              u"including the ones that stay signed in separately. There is "
              u"no way to undo it."))
          .AddOkButton(base::BindOnce(std::move(split.first), false),
                       ui::DialogModel::Button::Params().SetLabel(
                           u"Clear shared logins only"))
          .AddCancelButton(base::BindOnce(std::move(split.second), true),
                           ui::DialogModel::Button::Params().SetLabel(
                               u"Clear every space too"))
          .Build();
  constrained_window::ShowWebModal(std::move(dialog), settings_page);
  return true;
}

void SetClearDataWarningAnswerForTesting(
    std::optional<bool> clear_every_profile) {
  TestAnswer() = clear_every_profile;
}

}  // namespace arcium
