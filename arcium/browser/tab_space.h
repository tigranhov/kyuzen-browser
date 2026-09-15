// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_TAB_SPACE_H_
#define ARCIUM_BROWSER_TAB_SPACE_H_

#include <map>
#include <string>

#include "arcium/browser/model/entry_id.h"
#include "base/memory/scoped_refptr.h"
#include "components/sessions/core/session_id.h"
#include "components/tabs/public/tab_interface.h"

class GURL;

namespace content {
class BrowserContext;
class SiteInstance;
class WebContents;
}  // namespace content

namespace sessions {
class CommandStorageManager;
}

namespace tabs {
class TabInterface;
}

namespace arcium {

class ArciumModel;
class TabBinding;

// The two facts a tab carries about itself, ridden through the session file
// the same way kEntryIdExtraDataKey is.
inline constexpr char kSpaceIdExtraDataKey[] = "arcium.space_id";
inline constexpr char kTabKeyExtraDataKey[] = "arcium.tab_key";

// The profile of the tab's space when the session was written. Read by
// restore, which creates the tab -- and so fixes its storage -- before the
// model file has been read.
inline constexpr char kProfileIdExtraDataKey[] = "arcium.profile_id";

// The space a tab was tagged with, or an invalid SpaceId when it carries no
// tag. Not the space it is drawn in — SpaceOfTab is that, and it consults the
// entry binding first.
SpaceId SpaceTagOf(content::WebContents* web_contents);

void SetSpaceTag(content::WebContents* web_contents, SpaceId id);

// The stable id a tab carries across a restart, generated on first ask so
// every tab ends up with one.
TabKey KeyOf(content::WebContents* web_contents);

// The key if one was set or generated, or an invalid TabKey otherwise. Never
// generates: a comparison over every tab in a strip must not hand out a UUID
// to a tab nothing has ever asked about.
TabKey ExistingKeyOf(content::WebContents* web_contents);

// Sets the key directly, for the restore path, which is handed a key read
// off disk rather than one it is minting itself.
void SetTabKey(content::WebContents* web_contents, TabKey key);

// Copies the two facts above onto the contents replacing another in the same
// tab: a discard done to save memory, or a prerender swapping in. Both live
// on the WebContents, so a replacement starts carrying neither, and a tab
// that lost its tag reads as belonging to the model's first space -- it would
// be drawn in the wrong space, written to the session file under the wrong
// profile, and sent by the partition guard into storage it never belonged in.
// Never overwrites a tag the new contents already carries: one put there
// deliberately by whoever built it is the better answer, the same rule
// TagInsertedTabs follows.
void CarryTabIdentityTo(content::WebContents* from, content::WebContents* to);

// The space `handle` is drawn in. An entry's space wins over the tab's own
// tag, and a tag naming no space falls back to the model's first. A loose
// page is drawn in no space, and gets an invalid SpaceId.
SpaceId SpaceOfTab(const ArciumModel& model,
                   const TabBinding& binding,
                   tabs::TabHandle handle);

// Restore, first half's counterpart for the two facts above. Reads
// kSpaceIdExtraDataKey and kTabKeyExtraDataKey out of `extra_data` and, for
// each that parses, sets it on `web_contents`. A key that fails to parse
// leaves the corresponding fact untouched.
void RestoreTabSpaceData(content::WebContents* web_contents,
                         const std::map<std::string, std::string>& extra_data);

// The storage a restored tab belongs in, from the profile its session
// recorded: `chromium_choice` unless that profile has storage of its own.
// Restore runs before the model file has been read, so the session is the
// only thing that can answer this.
scoped_refptr<content::SiteInstance> SiteInstanceForRestoredTab(
    content::BrowserContext* context,
    const GURL& url,
    const std::map<std::string, std::string>& extra_data,
    scoped_refptr<content::SiteInstance> chromium_choice);

// The profile a restored tab's storage belongs to: the default profile when
// `extra_data` names none or names something that is not an id, which is
// also what a session written before profiles existed means.
ProfileId ProfileIdFromExtraData(
    const std::map<std::string, std::string>& extra_data);

// Save. Appends the resolved space and the tab's key as rebuild commands for
// `web_contents`, or nothing when it names no tab.
void AppendTabSpaceCommands(
    sessions::CommandStorageManager* command_storage_manager,
    SessionID tab_id,
    content::WebContents* web_contents,
    const ArciumModel& model,
    const TabBinding& binding);

// Save, the in-session half. Writes the same two facts into `extra_data`.
void PopulateTabSpaceExtraData(tabs::TabInterface* tab,
                               const ArciumModel& model,
                               const TabBinding& binding,
                               std::map<std::string, std::string>* extra_data);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_TAB_SPACE_H_
