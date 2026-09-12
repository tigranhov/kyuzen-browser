// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_CLEAR_DATA_WARNING_H_
#define ARCIUM_UI_BROWSER_CLEAR_DATA_WARNING_H_

#include <optional>

#include "base/functional/callback_forward.h"

namespace content {
class WebContents;
}

namespace arcium {

// Asks, before Chrome's Clear browsing data removes anything, whether the
// removal should reach the spaces that keep their own logins. Chrome's own
// removal covers the shared logins and nothing else, so without this a
// "clear everything" would quietly leave those spaces signed in.
//
// Returns true when the question has been put and `resume` will run once it
// is answered -- the caller must do nothing more until then. Returns false
// when there is nothing to ask: only one profile exists, or this page has
// just been answered and is calling back to do the removal.
//
// Whichever answer is given, the removal goes ahead; there is no way to
// stop it, because the settings page reports a deletion as soon as it hears
// back. "Clear them all" also empties each space's own storage, in the
// background, as the answer is taken.
bool AskWhichProfilesToClear(content::WebContents* settings_page,
                             base::OnceClosure resume);

// Answers the question without a dialog. std::nullopt puts the dialog back.
void SetClearDataWarningAnswerForTesting(
    std::optional<bool> clear_every_profile);

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_CLEAR_DATA_WARNING_H_
