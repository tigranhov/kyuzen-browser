// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/session_rebuild_nudge.h"

#include <memory>
#include <utility>

#include "arcium/browser/tab_binding.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/supports_user_data.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"

namespace arcium {

namespace {

// The address of this object is the key; its value is never read.
const char kSessionRebuildNudgeKey[] = "arcium_session_rebuild_nudge";

class SessionRebuildNudge : public base::SupportsUserData::Data {
 public:
  explicit SessionRebuildNudge(Profile* profile) : profile_(profile) {}
  ~SessionRebuildNudge() override = default;

  void Request() {
    if (pending_) {
      return;
    }
    pending_ = true;
    // Posted, never inline: the caller is usually part way through a
    // tab-strip mutation, and ScheduleResetCommands walks every window's
    // every tab. Coalesced by `pending_` so closing twenty tabs costs one
    // rebuild rather than twenty.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&SessionRebuildNudge::Run, weak_factory_.GetWeakPtr()));
  }

  base::WeakPtr<SessionRebuildNudge> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  void Run() {
    pending_ = false;
    // IfExisting, not GetForProfile: a profile with session restore off, or
    // an incognito one, has no service and must not be given one just so
    // Arcium can ask it for a rebuild.
    SessionService* service =
        SessionServiceFactory::GetForProfileIfExisting(profile_);
    if (service) {
      service->ResetFromCurrentBrowsers();
    }
  }

  const raw_ptr<Profile> profile_;
  bool pending_ = false;
  base::WeakPtrFactory<SessionRebuildNudge> weak_factory_{this};
};

}  // namespace

void InstallSessionRebuildNudge(Profile* profile, TabBinding* binding) {
  auto* nudge = static_cast<SessionRebuildNudge*>(
      profile->GetUserData(kSessionRebuildNudgeKey));
  if (!nudge) {
    auto owned = std::make_unique<SessionRebuildNudge>(profile);
    nudge = owned.get();
    profile->SetUserData(kSessionRebuildNudgeKey, std::move(owned));
  }
  // A weak pointer, because the nudge and the binding are both user data on
  // the same profile and nothing orders their destruction.
  binding->SetChangedCallback(
      base::BindRepeating(&SessionRebuildNudge::Request, nudge->GetWeakPtr()));
}

}  // namespace arcium
