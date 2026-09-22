// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_UPDATE_UPDATE_CONTROLLER_H_
#define ARCIUM_BROWSER_UPDATE_UPDATE_CONTROLLER_H_

#include <memory>

#include "arcium/browser/update/update_status.h"
#include "arcium/browser/update/updater_backend.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/prefs/pref_change_registrar.h"

class PrefService;

namespace arcium {

// Turns the reader's setting into instructions for the updater, and reports
// back what the updater is doing. One per browser process.
class UpdateController : public UpdateStatus {
 public:
  UpdateController(PrefService* local_state,
                   std::unique_ptr<UpdaterBackend> backend);
  UpdateController(const UpdateController&) = delete;
  UpdateController& operator=(const UpdateController&) = delete;
  ~UpdateController() override;

  // UpdateStatus:
  State CurrentState() const override;
  bool RelaunchIsPending() const override;
  void CheckNow() override;

  // Stops following the setting. Called at shutdown, before the setting's
  // store goes away, because the controller itself is kept to the end.
  void Shutdown();

 private:
  void ApplySetting();
  void OnStateChanged(State state);

  raw_ptr<PrefService> local_state_;
  std::unique_ptr<UpdaterBackend> backend_;
  PrefChangeRegistrar registrar_;
  State state_ = State::kIdle;
  base::WeakPtrFactory<UpdateController> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_UPDATE_UPDATE_CONTROLLER_H_
