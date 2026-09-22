# Stage 5a split view implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Two pages share the screen in one Arcium window, put there by four
gestures, drawn as a joined pair in the sidebar, confined to one space, and
still there after a relaunch.

**Architecture:** Chromium's native split already does the panes, the divider
and the model; Arcium writes only the decisions it cannot know -- who may
split with whom, how the sidebar says so, and what is written down. One new
controller per window plus one new view, both owned by
`BrowserSidebarController`. No new patch: every entry point is an Arcium view
or command, and the drop zone rides on the hooks patches 0050 and 0070 already
provide.

**Tech Stack:** Chromium Views in C++, `TabStripModel`'s split API
(`AddToNewSplit`, `RemoveSplit`, `GetSplitForTab`, `GetForegroundTabs`,
`OnSplitTabChanged`), Arcium's `ArciumModel` JSON model file.

**Spec:** `docs/superpowers/specs/2026-09-22-stage-5a-split-view-design.md`

## Global Constraints

- All Arcium code lives in `arcium/`. Upstream files change only through a
  numbered patch in `patches/`, and a patch is a hook that carries no logic.
  **This stage adds no patch.** If a task appears to need one, stop and say so.
- Views C++ for anything always visible. Never WebUI.
- No Swift, AppKit, Cocoa or platform-specific UI.
- One window, one `Browser`, one `TabStripModel`.
- Files under ~500 lines. Split a file that grows past it.
- Test-driven: write the failing test, watch it fail, then write the code.
  Mutate the code to confirm a passing test can fail.
- `scripts/format` after every code change. `git cl format` does not work here.
- Build with `scripts/build dev arcium_unittests` (and `arcium_browsertests`).
  Before any build, check `pgrep -f siso` and that no browser is running out of
  `out/dev`.
- The browser suite takes over the screen for minutes: it is skipped by
  default. Run the tests a change touches; ask whether the machine is free
  before running the whole suite; say in the report what was skipped.
- Commit by explicit path, never `git add -A`. Commit messages say why, carry
  no task numbers, and end with
  `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.
- Nothing is published. Releases happen only when the owner asks.
- Exact values fixed by the design: two panes only; the shortcut is
  **Cmd+Option+S**; the model file version goes from **5 to 6**; the box
  command id is **`kBoxCommandSplit = -4`**.

---

### Task 1: The rule and the controller

**Files:**
- Create: `arcium/ui/browser/split_controller.h`
- Create: `arcium/ui/browser/split_controller.cc`
- Create: `arcium/test/split_controller_unittest.cc`
- Modify: `arcium/ui/browser/BUILD.gn` (add both sources, alphabetically)
- Modify: `arcium/test/BUILD.gn` (add the unit test, alphabetically)
- Modify: `arcium/ui/browser/browser_sidebar_controller.h`,
  `arcium/ui/browser/browser_sidebar_controller.cc` (own it, like `peek_`)

**Interfaces:**
- Consumes: `SpaceSwitcher::SpaceOfTabAt`, `arcium::IsLoosePage` (Stage 4b's
  marker; find its exact name in `arcium/browser/loose_page.h`).
- Produces: the class below. Tasks 3, 4, 5 and 6 all call it and add nothing
  to it except `SplitWithActive(EntryId)` in Task 3.

- [ ] **Step 1: Write the failing test**

Create `arcium/test/split_controller_unittest.cc`. Model the fixture on
`arcium/test/space_switcher_unittest.cc` -- read it first; it builds a
`BrowserWithTestWindowTest` with an `ArciumModel`, a `TabBinding` and a
`SpaceSwitcher`, and `arcium/test/space_test_util.h` appends tabs already
tagged with a space.

```cpp
TEST_F(SplitControllerTest, TwoTabsOfOneSpaceMayShareTheScreen) {
  const SpaceId space = FirstSpace();
  AddTabInSpace(strip(), profile(), GURL("https://a.test/"), space);
  AddTabInSpace(strip(), profile(), GURL("https://b.test/"), space);

  EXPECT_TRUE(controller().CanSplit(0, 1));
}

TEST_F(SplitControllerTest, TabsOfDifferentSpacesMayNot) {
  const SpaceId other = AddSpace(u"Work");
  AddTabInSpace(strip(), profile(), GURL("https://a.test/"), FirstSpace());
  AddTabInSpace(strip(), profile(), GURL("https://b.test/"), other);

  EXPECT_FALSE(controller().CanSplit(0, 1));
}

TEST_F(SplitControllerTest, ATabAlreadySharingMayNotShareAgain) {
  const SpaceId space = FirstSpace();
  AddTabInSpace(strip(), profile(), GURL("https://a.test/"), space);
  AddTabInSpace(strip(), profile(), GURL("https://b.test/"), space);
  AddTabInSpace(strip(), profile(), GURL("https://c.test/"), space);
  ASSERT_TRUE(controller().SplitWithActive(1));

  EXPECT_FALSE(controller().CanSplit(0, 2));
}

TEST_F(SplitControllerTest, SplittingPutsBothTabsInFront) {
  const SpaceId space = FirstSpace();
  AddTabInSpace(strip(), profile(), GURL("https://a.test/"), space);
  AddTabInSpace(strip(), profile(), GURL("https://b.test/"), space);
  strip()->ActivateTabAt(0);

  ASSERT_TRUE(controller().SplitWithActive(1));

  EXPECT_EQ(2u, strip()->GetForegroundTabs().size());
  EXPECT_TRUE(controller().ActiveIsSplit());
}

TEST_F(SplitControllerTest, UnsplittingLeavesBothTabsOpen) {
  const SpaceId space = FirstSpace();
  AddTabInSpace(strip(), profile(), GURL("https://a.test/"), space);
  AddTabInSpace(strip(), profile(), GURL("https://b.test/"), space);
  ASSERT_TRUE(controller().SplitWithActive(1));

  controller().Unsplit();

  EXPECT_FALSE(controller().ActiveIsSplit());
  EXPECT_EQ(2, strip()->count());
}
```

- [ ] **Step 2: Run it and watch it fail**

```bash
scripts/build dev arcium_unittests && out/dev/arcium_unittests --gtest_filter='SplitControllerTest.*'
```

Expected: does not compile -- `split_controller.h` does not exist.

- [ ] **Step 3: Write the header**

```cpp
// arcium/ui/browser/split_controller.h
#ifndef ARCIUM_UI_BROWSER_SPLIT_CONTROLLER_H_
#define ARCIUM_UI_BROWSER_SPLIT_CONTROLLER_H_

#include <optional>

#include "arcium/browser/model/entry_id.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/split_tabs/split_tab_id.h"

class TabStripModel;

namespace arcium {

class SpaceSwitcher;

// One window's split view (R5.1): the decisions Chromium's own split cannot
// make. Chromium owns the two panes, the divider and the collection in the
// strip; this owns who may share a screen with whom, and ends a split that
// Arcium's own rules would otherwise break.
//
// Two panes, never more: SplitTabLayout has two values and MultiContentsView
// holds two contents views. Three and four are deferred, see the design.
class SplitController {
 public:
  SplitController(TabStripModel* tab_strip_model, SpaceSwitcher* switcher);
  SplitController(const SplitController&) = delete;
  SplitController& operator=(const SplitController&) = delete;
  ~SplitController();

  // Whether the tabs at these two indices may share the screen. False for a
  // bad index, for two spaces, for a loose page, and for a tab already in a
  // split. `switcher` null -- the playground, a window with no sidebar --
  // means one space, so only the other rules apply.
  bool CanSplit(int index_a, int index_b) const;

  // Puts the tab at `index` beside the active tab. False when refused, and
  // then nothing happened.
  bool SplitWithActive(int index);

  // Ends the split the active tab is in. Both tabs stay open.
  void Unsplit();

  // Ends the split the tab at `index` is in, if any. Called before a tab
  // leaves its space -- see Task 6 -- because a split across two spaces must
  // never exist even for one turn of the loop.
  void EndSplitFor(int index);

  bool ActiveIsSplit() const;
  std::optional<split_tabs::SplitTabId> SplitOfActive() const;

 private:
  raw_ptr<TabStripModel> tab_strip_model_;
  raw_ptr<SpaceSwitcher> switcher_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_SPLIT_CONTROLLER_H_
```

- [ ] **Step 4: Write the implementation**

```cpp
// arcium/ui/browser/split_controller.cc
#include "arcium/ui/browser/split_controller.h"

#include <vector>

#include "arcium/browser/loose_page.h"
#include "arcium/ui/browser/space_switcher.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/split_tabs/split_tab_visual_data.h"

namespace arcium {

SplitController::SplitController(TabStripModel* tab_strip_model,
                                 SpaceSwitcher* switcher)
    : tab_strip_model_(tab_strip_model), switcher_(switcher) {}

SplitController::~SplitController() = default;

bool SplitController::CanSplit(int a, int b) const {
  if (!tab_strip_model_ || a == b) {
    return false;
  }
  if (!tab_strip_model_->ContainsIndex(a) ||
      !tab_strip_model_->ContainsIndex(b)) {
    return false;
  }
  // A pane can only be replaced by first ending the split it is in. Chromium's
  // own keyboard path makes the same check.
  if (tab_strip_model_->GetSplitForTab(a) ||
      tab_strip_model_->GetSplitForTab(b)) {
    return false;
  }
  // A peek and the page in an outside-link window are in no space, so the
  // space test below would already refuse them; said outright so the refusal
  // is deliberate rather than a side effect.
  if (IsLoosePage(tab_strip_model_->GetWebContentsAt(a)) ||
      IsLoosePage(tab_strip_model_->GetWebContentsAt(b))) {
    return false;
  }
  if (!switcher_) {
    return true;  // No spaces to disagree about.
  }
  return switcher_->SpaceOfTabAt(a) == switcher_->SpaceOfTabAt(b);
}

bool SplitController::SplitWithActive(int index) {
  const int active = tab_strip_model_ ? tab_strip_model_->active_index() : -1;
  if (active < 0 || !CanSplit(active, index)) {
    return false;
  }
  std::vector<int> indices = {active, index};
  std::sort(indices.begin(), indices.end());  // AddToNewSplit requires it.
  tab_strip_model_->AddToNewSplit(
      indices, split_tabs::SplitTabVisualData(),
      split_tabs::SplitTabCreatedSource::kDragAndDropTab);
  return true;
}

void SplitController::Unsplit() {
  if (const std::optional<split_tabs::SplitTabId> id = SplitOfActive()) {
    tab_strip_model_->RemoveSplit(*id);
  }
}

void SplitController::EndSplitFor(int index) {
  if (!tab_strip_model_ || !tab_strip_model_->ContainsIndex(index)) {
    return;
  }
  if (const std::optional<split_tabs::SplitTabId> id =
          tab_strip_model_->GetSplitForTab(index)) {
    tab_strip_model_->RemoveSplit(*id);
  }
}

bool SplitController::ActiveIsSplit() const {
  return SplitOfActive().has_value();
}

std::optional<split_tabs::SplitTabId> SplitController::SplitOfActive() const {
  const int active = tab_strip_model_ ? tab_strip_model_->active_index() : -1;
  return active < 0 ? std::nullopt : tab_strip_model_->GetSplitForTab(active);
}

}  // namespace arcium
```

Check `arcium/browser/loose_page.h` for the real predicate's name and
signature before writing this; if it takes a `TabInterface` rather than a
`WebContents`, adapt the two calls and nothing else.

`SplitWithActive` hard-codes `kDragAndDropTab` here; Task 4 and Task 5 add a
source argument when they need a different one. Do not add it before then.

- [ ] **Step 5: Wire it into the window and run the tests**

In `browser_sidebar_controller.h` add `#include` and
`std::unique_ptr<SplitController> split_;` beside `peek_`, plus
`SplitController* split() { return split_.get(); }`. Construct it where `peek_`
is constructed, with `browser_view_->browser()->tab_strip_model()` and
`space_switcher_.get()`.

```bash
scripts/format && scripts/build dev arcium_unittests && out/dev/arcium_unittests --gtest_filter='SplitControllerTest.*'
```

Expected: 5 tests pass.

- [ ] **Step 6: Prove the tests can fail**

Temporarily make `CanSplit` return `true` unconditionally, rebuild, and
confirm `TabsOfDifferentSpacesMayNot` and `ATabAlreadySharingMayNotShareAgain`
fail. Revert the mutation.

- [ ] **Step 7: Commit**

```bash
git add arcium/ui/browser/split_controller.h arcium/ui/browser/split_controller.cc arcium/ui/browser/BUILD.gn arcium/test/split_controller_unittest.cc arcium/test/BUILD.gn arcium/ui/browser/browser_sidebar_controller.h arcium/ui/browser/browser_sidebar_controller.cc
git commit
```

Message: why a split needs a rule of its own -- two spaces on one screen would
put two sets of logins there and give the sidebar a page it cannot draw.

---

### Task 2: The sidebar says which two rows share the screen

**Files:**
- Modify: `arcium/ui/sidebar/sidebar_model.h` (a field on `SidebarRow`)
- Modify: `arcium/ui/browser/sidebar_tab_model.cc`,
  `arcium/ui/browser/sidebar_tab_model_entries.cc` (fill it; foreground rows)
- Modify: `arcium/ui/sidebar/tab_row_view.h`, `.cc` (the mark)
- Modify: `arcium/ui/sidebar/tab_list_view.cc` (the bracket)
- Modify: `arcium/ui/sidebar/sidebar_colors.h` (one colour id if one is needed)
- Create: `arcium/test/split_rows_unittest.cc`
- Modify: `arcium/test/BUILD.gn`

**Interfaces:**
- Consumes: Task 1's controller is *not* used here -- the model reads
  `TabStripModel::GetSplitForTab` directly, because the sidebar model already
  holds the strip and a row build must not reach through the window.
- Produces: `SidebarRow::split`, which Task 5 reads to filter the box's rows.

- [ ] **Step 1: Write the failing test**

Create `arcium/test/split_rows_unittest.cc`, fixture as in
`arcium/test/sidebar_tab_model_unittest.cc`.

```cpp
TEST_F(SplitRowsTest, BothTabsOfASplitReadAsCurrent) {
  AddTabInSpace(strip(), profile(), GURL("https://a.test/"), FirstSpace());
  AddTabInSpace(strip(), profile(), GURL("https://b.test/"), FirstSpace());
  strip()->ActivateTabAt(0);
  ASSERT_TRUE(controller().SplitWithActive(1));

  const std::vector<SidebarRow> rows = model().rows();
  ASSERT_EQ(2u, rows.size());
  EXPECT_TRUE(rows[0].is_active);
  EXPECT_TRUE(rows[1].is_active);
  EXPECT_TRUE(rows[0].split.has_value());
  EXPECT_EQ(rows[0].split, rows[1].split);
}

TEST_F(SplitRowsTest, ARowOutsideTheSplitIsNotCurrent) {
  AddTabInSpace(strip(), profile(), GURL("https://a.test/"), FirstSpace());
  AddTabInSpace(strip(), profile(), GURL("https://b.test/"), FirstSpace());
  AddTabInSpace(strip(), profile(), GURL("https://c.test/"), FirstSpace());
  strip()->ActivateTabAt(0);
  ASSERT_TRUE(controller().SplitWithActive(1));

  const std::vector<SidebarRow> rows = model().rows();
  const SidebarRow& third = RowForUrl(rows, GURL("https://c.test/"));
  EXPECT_FALSE(third.is_active);
  EXPECT_FALSE(third.split.has_value());
}

TEST_F(SplitRowsTest, EndingASplitClearsBothMarks) {
  AddTabInSpace(strip(), profile(), GURL("https://a.test/"), FirstSpace());
  AddTabInSpace(strip(), profile(), GURL("https://b.test/"), FirstSpace());
  ASSERT_TRUE(controller().SplitWithActive(1));
  controller().Unsplit();

  for (const SidebarRow& row : model().rows()) {
    EXPECT_FALSE(row.split.has_value());
  }
}
```

- [ ] **Step 2: Run it and watch it fail**

```bash
scripts/build dev arcium_unittests && out/dev/arcium_unittests --gtest_filter='SplitRowsTest.*'
```

Expected: does not compile -- `SidebarRow` has no `split`.

- [ ] **Step 3: Add the field**

In `arcium/ui/sidebar/sidebar_model.h`, inside `SidebarRow`, after
`is_unloaded`:

```cpp
  // The split this row's tab shares the screen in, absent when it shares with
  // nothing. Two rows carrying the same value are the two halves of one
  // split, and both read as current: `is_active` is membership of the
  // foreground, which is one tab normally and two in a split.
  std::optional<split_tabs::SplitTabId> split;
```

Add `#include <optional>` and
`#include "components/split_tabs/split_tab_id.h"`, and
`//components/split_tabs` to `arcium/ui/sidebar/BUILD.gn`'s deps if it is not
already reachable.

- [ ] **Step 4: Fill it, and widen what "active" means**

In `sidebar_tab_model.cc` replace

```cpp
  row.is_active = index == tab_strip_model_->active_index();
```

with

```cpp
  row.split = tab_strip_model_->GetSplitForTab(index);
  // The foreground is one tab normally and both halves of a split when the
  // active tab is in one, which is exactly what a row means by current.
  row.is_active = IsForeground(index);
```

and make the same two changes in `sidebar_tab_model_entries.cc:178`, where the
line is spelled with `row.tab_index`. Add the private helper:

```cpp
bool SidebarTabModel::IsForeground(int index) const {
  if (index < 0 || !tab_strip_model_->ContainsIndex(index)) {
    return false;
  }
  tabs::TabInterface* const tab = tab_strip_model_->GetTabAtIndex(index);
  for (tabs::TabInterface* front : tab_strip_model_->GetForegroundTabs()) {
    if (front == tab) {
      return true;
    }
  }
  return false;
}
```

`is_unloaded` keeps its `!row.is_active` guard and so now excludes both halves,
which is right: both are on screen.

- [ ] **Step 5: Rebuild rows when a split changes**

`SidebarTabModel` is already a `TabStripModelObserver`. Add:

```cpp
void SidebarTabModel::OnSplitTabChanged(const SplitTabChange& change) {
  // Forming or breaking a split changes which rows read as current, and the
  // strip sends this instead of a selection change when it happens.
  ScheduleNotify();
}
```

using whatever the existing coalescing call is named in this file -- find it
in `OnTabStripModelChanged` and use the same one, so a split change still
costs one notification per run-loop turn and no timer.

- [ ] **Step 6: Run the tests**

```bash
scripts/format && scripts/build dev arcium_unittests && out/dev/arcium_unittests --gtest_filter='SplitRowsTest.*:SidebarTabModelTest.*:UnloadedRowsTest.*'
```

Expected: the three new tests pass and no existing row test regresses. If an
existing test asserted that exactly one row is active, it is now wrong in the
split case only; read it before changing it, and change the test only if it
constructs a split.

- [ ] **Step 7: Draw it**

In `tab_row_view.cc`'s `UpdateVisuals`, show a small two-pane glyph in the
row's trailing area when `row_.split` is set, next to where the audio
indicator goes; hide it otherwise. Add the vector icon beside the existing
ones in `arcium/ui/sidebar/icons`, following whatever `kAudioIcon` does.

In `tab_list_view.cc`, when two adjacent laid-out rows carry the same
`split`, paint a 2px rounded bar down their shared left edge in
`kColorArciumRowTextActive`, and in that case the glyph is redundant -- pass
the rows a flag saying the bracket has it covered. Rows in different sections
are never adjacent, and those keep the glyph.

Iterate on this in the playground, not the browser:

```bash
scripts/playground
```

The playground's fake model needs a way to say "these two rows are split" --
add a setter beside its existing ones, and one snapshot check.

- [ ] **Step 8: Commit**

```bash
git add arcium/ui/sidebar/sidebar_model.h arcium/ui/browser/sidebar_tab_model.cc arcium/ui/browser/sidebar_tab_model.h arcium/ui/browser/sidebar_tab_model_entries.cc arcium/ui/sidebar/tab_row_view.cc arcium/ui/sidebar/tab_row_view.h arcium/ui/sidebar/tab_list_view.cc arcium/test/split_rows_unittest.cc arcium/test/BUILD.gn
git commit
```

Message: why both rows read as current -- the sidebar is the only tab list, so
a split it does not mark is a split with no way to tell which two pages are on
screen.

---

### Task 3: Dragging a row onto the page

**Files:**
- Create: `arcium/ui/browser/split_drop_view.h`, `.cc`
- Modify: `arcium/ui/browser/browser_sidebar_controller.cc` (create, lay out)
- Modify: `arcium/ui/browser/split_controller.h`, `.cc`
  (`SplitWithActive(EntryId)`)
- Modify: `arcium/ui/browser/BUILD.gn`
- Modify: `arcium/test/split_controller_unittest.cc`

**Interfaces:**
- Consumes: `RowDragData::Read`, `RowDragData::Format` from
  `arcium/ui/sidebar/row_drag_data.h`; `RowDragSession` from
  `arcium/ui/sidebar/row_drag_session.h`; `SidebarTabModel::ActivateEntry`.
- Produces: nothing other tasks use.

- [ ] **Step 1: Write the failing test**

Add to `arcium/test/split_controller_unittest.cc`:

```cpp
TEST_F(SplitControllerTest, ASplitWithAColdEntryOpensItFirst) {
  const EntryId entry = PinEntry(GURL("https://pinned.test/"));  // no tab
  AddTabInSpace(strip(), profile(), GURL("https://a.test/"), FirstSpace());
  ASSERT_EQ(1, strip()->count());

  EXPECT_TRUE(controller().SplitWithActive(entry));

  EXPECT_EQ(2, strip()->count());
  EXPECT_EQ(2u, strip()->GetForegroundTabs().size());
}
```

`PinEntry` is the fixture helper that creates a pinned entry with no tab;
`arcium/test/sidebar_tab_model_unittest.cc` has one -- reuse its shape.

- [ ] **Step 2: Run it and watch it fail**

```bash
scripts/build dev arcium_unittests && out/dev/arcium_unittests --gtest_filter='SplitControllerTest.ASplitWithAColdEntry*'
```

Expected: does not compile.

- [ ] **Step 3: Add the entry overload**

`split_controller.h`:

```cpp
  // Puts `id`'s page beside the active tab, opening it first when the entry
  // is cold. False when refused. Needs the sidebar model, which is what knows
  // how to open an entry; null in a window without one.
  bool SplitWithActive(EntryId id);
```

`split_controller.cc`:

```cpp
bool SplitController::SplitWithActive(EntryId id) {
  if (!model_ || !tab_strip_model_) {
    return false;
  }
  // The tab that is on screen now, remembered before opening anything:
  // ActivateEntry puts the entry's tab in front, and splitting it with
  // itself is not a split.
  const int active = tab_strip_model_->active_index();
  if (active < 0) {
    return false;
  }
  const tabs::TabHandle keep = tab_strip_model_->GetTabAtIndex(active)
                                   ->GetHandle();
  model_->ActivateEntry(id);  // Opens a cold entry; a no-op for a warm one.
  const int opened = tab_strip_model_->active_index();
  const int previous = tab_strip_model_->GetIndexOfTab(keep.Get());
  if (opened < 0 || previous < 0 || !CanSplit(previous, opened)) {
    return false;
  }
  std::vector<int> indices = {previous, opened};
  std::sort(indices.begin(), indices.end());
  tab_strip_model_->AddToNewSplit(
      indices, split_tabs::SplitTabVisualData(),
      split_tabs::SplitTabCreatedSource::kDragAndDropTab);
  return true;
}
```

Give the controller a `raw_ptr<SidebarTabModel> model_` constructor argument
and pass `model_.get()` where it is built.

- [ ] **Step 4: Run the test**

```bash
scripts/format && scripts/build dev arcium_unittests && out/dev/arcium_unittests --gtest_filter='SplitControllerTest.*'
```

Expected: 6 tests pass.

- [ ] **Step 5: Write the drop zone**

```cpp
// arcium/ui/browser/split_drop_view.h -- sketch; fill in the views boilerplate
// the way peek_view.h does.
class SplitDropView : public views::View, public RowDragSession::Observer {
 public:
  using DropCallback = base::RepeatingCallback<void(RowDragData, bool right)>;
  SplitDropView(RowDragSession* session, DropCallback on_drop);
  // Places it over the page; called from LayoutSidebar.
  void Layout(const gfx::Rect& page_area);

  // views::View:
  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* types) override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  views::View::DropCallback GetDropCallback(
      const ui::DropTargetEvent& event) override;

  // RowDragSession::Observer:
  void OnRowDragInFlightChanged() override;
};
```

Rules the implementation must keep:

- `SetVisible(session->in_flight())` and nothing else decides visibility, so
  the view takes no events and costs no hit test while nobody drags.
- `GetDropFormats` offers only `RowDragData::Format()`; `CanDrop` returns
  whether `RowDragData::Read` succeeded. A drag from outside Arcium is
  refused, which leaves Chromium's own link zone to it.
- `OnDragUpdated` returns `ui::DragDropTypes::DRAG_MOVE` and highlights the
  half the pointer is over. `right` is `event.x() >= width() / 2`.
- It is laid out over the page area only, from `LayoutSidebar`, so it never
  covers the sidebar column and the existing row-into-Pinned drag is
  untouched.
- It paints to its own layer, above the page's. This is Stage 4b's peek
  defect: a view without a layer paints beneath every layer inside its
  parent's, and the page draws through one, so a zone without
  `SetPaintToLayer()` is invisible.

In `browser_sidebar_controller.cc`, create it beside `peek_`, add
`split_drop_->Layout(FullPageArea())` to `LayoutSidebar`, and give the
controller `FullPageArea()` -- `PageArea()` returns the *active* contents
container, which in a split is one pane; the zone wants both, so use
`browser_view_->multi_contents_view()`'s bounds converted the same way.

The drop callback:

```cpp
void BrowserSidebarController::OnSplitDrop(RowDragData data, bool right) {
  const bool formed = data.is_entry()
                          ? split_->SplitWithActive(data.entry_id)
                          : split_->SplitWithActive(data.tab_index);
  if (formed && !right) {
    // Dropped on the left half, so the dragged page belongs on the left.
    split_->ReverseIfNeeded(/*dragged_on_right=*/false);
  }
}
```

Add `ReverseIfNeeded` to the controller as a thin call to
`TabStripModel::ReverseTabsInSplit` when the dragged tab is not on the side it
was dropped on. A folder drag (`data.is_folder()`) is refused in `CanDrop`.

- [ ] **Step 6: Check it by eye**

```bash
scripts/build dev && scripts/run
```

Drag a Today row onto the right half of the page; two pages appear. Drag a
cold favourite; it opens and joins. Drag a Today row up into Pinned; it still
pins. Confirm the zone is invisible when nothing is being dragged.

- [ ] **Step 7: Commit**

```bash
git add arcium/ui/browser/split_drop_view.h arcium/ui/browser/split_drop_view.cc arcium/ui/browser/split_controller.h arcium/ui/browser/split_controller.cc arcium/ui/browser/browser_sidebar_controller.h arcium/ui/browser/browser_sidebar_controller.cc arcium/ui/browser/BUILD.gn arcium/test/split_controller_unittest.cc
git commit
```

Message: why the zone is Arcium's own rather than Chromium's -- our rows ride
in a private clipboard format that Chromium's drop target has no reason to
accept, and a row naming a cold entry has no URL to hand it anyway.

---

### Task 4: The row menu and the keyboard shortcut

**Files:**
- Modify: `arcium/ui/sidebar/row_context_menu.h`, `.cc`
- Modify: `arcium/ui/sidebar/sidebar_model.h` (two virtuals)
- Modify: `arcium/ui/browser/sidebar_tab_model.h`, `.cc` (implement them)
- Modify: `arcium/ui/browser/browser_sidebar_controller.cc` (the accelerator)
- Modify: `arcium/test/sidebar_views_unittest.cc` (menu contents)
- Modify: `arcium/test/split_controller_unittest.cc`

**Interfaces:**
- Consumes: Task 1's `CanSplit`, `SplitWithActive`, `Unsplit`.
- Produces: `SidebarModel::SplitRowWithCurrentPage(int tab_index)` and
  `SplitEntryWithCurrentPage(EntryId)`, plus `CanSplitRow(...)`, which the
  fake model in the playground must also implement.

- [ ] **Step 1: Write the failing test**

In `arcium/test/sidebar_views_unittest.cc`, beside the existing menu tests:

```cpp
TEST_F(RowContextMenuTest, ATodayRowOffersToSplitWithThePageOnScreen) {
  // Two tabs in one space, the first on screen, the menu on the second.
  EXPECT_THAT(MenuLabelsForRow(SecondTodayRow()),
              testing::Contains(u"Split with current page"));
}

TEST_F(RowContextMenuTest, TheRowOnScreenDoesNotOfferToSplitWithItself) {
  EXPECT_THAT(MenuLabelsForRow(ActiveRow()),
              testing::Not(testing::Contains(u"Split with current page")));
}
```

And in `split_controller_unittest.cc`:

```cpp
TEST_F(SplitControllerTest, TheShortcutFormsAndThenEndsASplit) {
  AddTabInSpace(strip(), profile(), GURL("https://a.test/"), FirstSpace());
  AddTabInSpace(strip(), profile(), GURL("https://b.test/"), FirstSpace());
  strip()->ActivateTabAt(1);
  strip()->ActivateTabAt(0);  // b is now the previously active tab.

  controller().ToggleSplitWithPrevious();
  EXPECT_TRUE(controller().ActiveIsSplit());

  controller().ToggleSplitWithPrevious();
  EXPECT_FALSE(controller().ActiveIsSplit());
  EXPECT_EQ(2, strip()->count());
}
```

- [ ] **Step 2: Run them and watch them fail**

```bash
scripts/build dev arcium_unittests && out/dev/arcium_unittests --gtest_filter='RowContextMenuTest.*Split*:SplitControllerTest.TheShortcut*'
```

- [ ] **Step 3: Add `ToggleSplitWithPrevious`**

```cpp
void SplitController::ToggleSplitWithPrevious() {
  if (ActiveIsSplit()) {
    Unsplit();
    return;
  }
  // The tab the reader was on before this one, in this space. The strip
  // records activations; ask it rather than keeping a second history.
  const int previous = IndexOfPreviouslyActiveTab();
  if (previous >= 0) {
    SplitWithActive(previous);
  }
}
```

`IndexOfPreviouslyActiveTab` remembers the last active index in
`OnTabStripModelChanged`'s selection change, filtered to the active space --
add `TabStripModelObserver` to `SplitController` for this, and only this.

- [ ] **Step 4: Add the menu item**

In `row_context_menu.cc`, add `kSplitWithCurrentPage` to the command enum and,
for every section, after the existing items:

```cpp
  if (model_->CanSplitRow(row)) {
    menu_->AddItem(kSplitWithCurrentPage, u"Split with current page");
  }
```

`CanSplitRow` on `SidebarModel` answers from the controller for a warm row and
from `CanSplit`-against-the-active-tab for a cold entry (a cold entry can
always be opened, so the only question is whether its space matches).
`ExecuteCommand` calls `SplitRowWithCurrentPage` or
`SplitEntryWithCurrentPage` depending on `row.entry_id.is_valid()`. Implement
both on `SidebarTabModel` as one-line forwards to the controller, and on the
playground's fake model as no-ops that record the call.

- [ ] **Step 5: Bind the shortcut**

Check first that nothing claims it:

```bash
grep -n "VKEY_S" /Volumes/Texternal/chromium/src/chrome/browser/global_keyboard_shortcuts_mac.mm
```

If Cmd+Option+S is free, register it where the sidebar's existing accelerator
is registered (`sidebar_view.cc:181` shows the pattern) as
`ui::Accelerator(ui::VKEY_S, ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN)` and route
it to `ToggleSplitWithPrevious`. If it is taken, stop and report which command
has it rather than choosing another key.

- [ ] **Step 6: Run the tests and commit**

```bash
scripts/format && scripts/build dev arcium_unittests && out/dev/arcium_unittests --gtest_filter='RowContextMenuTest.*:SplitControllerTest.*:SidebarViewsTest.*'
```

```bash
git add arcium/ui/sidebar/row_context_menu.h arcium/ui/sidebar/row_context_menu.cc arcium/ui/sidebar/sidebar_model.h arcium/ui/browser/sidebar_tab_model.h arcium/ui/browser/sidebar_tab_model.cc arcium/ui/browser/split_controller.h arcium/ui/browser/split_controller.cc arcium/ui/browser/browser_sidebar_controller.cc arcium/test/sidebar_views_unittest.cc arcium/test/split_controller_unittest.cc
git commit
```

Message: why a shortcut as well as a menu item -- it is the only way into a
split that needs no pointer, and it is also the fastest way out of one.

---

### Task 5: The box learns to ask which tab

**Files:**
- Modify: `arcium/ui/browser/box_commands.h`, `.cc`
- Modify: `arcium/ui/browser/command_box.h`, `.cc`
- Modify: `arcium/ui/browser/browser_sidebar_controller_box.cc`
- Modify: `arcium/test/box_commands_unittest.cc`
- Modify: `arcium/test/browser/command_box_browsertest.cc`

**Interfaces:**
- Consumes: `SuggestionRow`, `TabSearchService`'s live-tab results, Task 1's
  `CanSplit`.
- Produces: nothing other tasks use.

- [ ] **Step 1: Write the failing test**

`arcium/test/box_commands_unittest.cc`:

```cpp
TEST(BoxCommandsTest, SplitIsOffered) {
  EXPECT_EQ(std::vector<int>{kBoxCommandSplit}, IdsFor(u"split"));
}
```

`arcium/test/browser/command_box_browsertest.cc`:

```cpp
IN_PROC_BROWSER_TEST_F(CommandBoxTest, TakingSplitAsksWhichTabRatherThanClosing) {
  OpenSecondTabInActiveSpace(GURL("https://b.test/"));
  ShowBox();
  TypeInBox(u"split");
  TakeSelectedRow();

  EXPECT_TRUE(BoxIsOpen());
  EXPECT_EQ(u"", BoxText());
  EXPECT_GE(BoxRowCount(), 1u);
  EXPECT_THAT(BoxRowTitle(0), testing::StartsWith(u"Split with "));
}

IN_PROC_BROWSER_TEST_F(CommandBoxTest, TakingATabInSplitModeFormsTheSplit) {
  OpenSecondTabInActiveSpace(GURL("https://b.test/"));
  ShowBox();
  TypeInBox(u"split");
  TakeSelectedRow();
  TakeSelectedRow();

  EXPECT_FALSE(BoxIsOpen());
  EXPECT_EQ(2u, browser()->tab_strip_model()->GetForegroundTabs().size());
}

IN_PROC_BROWSER_TEST_F(CommandBoxTest, EscapeLeavesSplitModeBeforeItCloses) {
  ShowBox();
  TypeInBox(u"split");
  TakeSelectedRow();
  PressEscape();

  EXPECT_TRUE(BoxIsOpen());
  PressEscape();
  EXPECT_FALSE(BoxIsOpen());
}
```

- [ ] **Step 2: Run them and watch them fail**

```bash
scripts/build dev arcium_unittests arcium_browsertests && out/dev/arcium_unittests --gtest_filter='BoxCommandsTest.SplitIsOffered' && out/dev/arcium_browsertests --gtest_filter='CommandBoxTest.*Split*:CommandBoxTest.Escape*'
```

Three browser tests, so this is not the whole suite -- no need to ask about the
machine. Say so in the report.

- [ ] **Step 3: Add the command**

`box_commands.h`:

```cpp
inline constexpr int kBoxCommandSplit = -4;
```

`box_commands.cc`, in `AllBoxCommands()`:

```cpp
      {kBoxCommandSplit, u"Split the screen", u"side pane"},
```

- [ ] **Step 4: Give the box a second stage**

`command_box.h` gains:

```cpp
  // What the box is answering. Normally anything; in kSplitPartner it offers
  // only the open tabs of the active space that may share the screen with the
  // page on it, and taking one forms the split.
  enum class Mode { kAnything, kSplitPartner };
  void EnterSplitPartnerMode(std::vector<SuggestionRow> tabs);
  Mode mode_for_testing() const { return mode_; }
```

with `Mode mode_ = Mode::kAnything;` and
`std::vector<SuggestionRow> partner_rows_;`.

- `EnterSplitPartnerMode` sets the mode, clears the field without closing,
  replaces `rows_` with `tabs`, and rebuilds the row views.
- `ContentsChanged` filters `partner_rows_` by the typed text while in the
  mode instead of asking the suggestion source.
- `HandleKeyEvent`'s `VKEY_ESCAPE` case becomes:

```cpp
    case ui::VKEY_ESCAPE:
      if (mode_ == Mode::kSplitPartner) {
        LeaveSplitPartnerMode();  // Back to what it was showing.
        return true;
      }
      GetWidget()->Close();
      return true;
```

- `Take` in the mode calls `on_open_` with the chosen row, which carries the
  partner's tab handle. Add `std::optional<int> split_with_tab_index;` to
  `SuggestionRow` -- a handle would be better, but the box's callback path
  already resolves handles to indices at the moment it acts (see
  `SearchResult::tab_handle`'s comment); follow that and resolve at the call
  site, not here.

- [ ] **Step 5: Wire the command**

In `browser_sidebar_controller_box.cc`, `RunBoxCommand`:

```cpp
    case kBoxCommandSplit:
      // Does not close the box: the command names no partner, so the box asks.
      if (command_box_) {
        command_box_->EnterSplitPartnerMode(SplittablePartnerRows());
      }
      return;
```

`SplittablePartnerRows()` asks `TabSearchService` for the window's live tabs,
keeps those in the active space that `split_->CanSplit(active, index)`
accepts, and titles each "Split with <title>".

Because `RunBoxCommand` currently runs on a posted task after the box closes,
this case must run **before** that close. Read `OnCommandBoxAccepted` and give
the split command an early return that runs synchronously, leaving every other
command's posted path exactly as it is.

- [ ] **Step 6: Run the tests and commit**

```bash
scripts/format && scripts/build dev arcium_unittests arcium_browsertests && out/dev/arcium_unittests --gtest_filter='BoxCommandsTest.*:SuggestionSourceTest.*' && out/dev/arcium_browsertests --gtest_filter='CommandBoxTest.*:BoxCommandsBrowserTest.*'
```

```bash
git add arcium/ui/browser/box_commands.h arcium/ui/browser/box_commands.cc arcium/ui/browser/command_box.h arcium/ui/browser/command_box.cc arcium/ui/browser/browser_sidebar_controller_box.cc arcium/ui/browser/suggestion_source.h arcium/test/box_commands_unittest.cc arcium/test/browser/command_box_browsertest.cc
git commit
```

Message: why the box gained a mode -- every command until now answered with
itself, and splitting has to name a second tab, which a one-shot row cannot.

---

### Task 6: A tab leaving its space ends the split first

**Files:**
- Modify: `arcium/ui/browser/space_switcher.h`, `space_switcher_spaces.cc`
- Modify: `arcium/ui/browser/browser_sidebar_controller.cc` (hand the
  switcher the controller)
- Create: `arcium/test/browser/split_view_browsertest.cc`
- Modify: `arcium/test/BUILD.gn`

**Interfaces:**
- Consumes: Task 1's `EndSplitFor`.
- Produces: nothing.

- [ ] **Step 1: Write the failing test**

```cpp
// arcium/test/browser/split_view_browsertest.cc
IN_PROC_BROWSER_TEST_F(SplitViewTest, MovingOneHalfToAnotherSpaceEndsTheSplit) {
  const SpaceId work = AddSpace(u"Work");
  OpenTab(GURL("https://a.test/"));
  OpenTab(GURL("https://b.test/"));
  SplitTheTwoOpenTabs();
  ASSERT_EQ(2u, strip()->GetForegroundTabs().size());

  MoveTabToSpace(/*index=*/1, work);

  EXPECT_EQ(2, strip()->count());
  EXPECT_FALSE(strip()->GetSplitForTab(0).has_value());
  EXPECT_FALSE(strip()->GetSplitForTab(1).has_value());
  EXPECT_NE(SpaceOfTabAt(0), SpaceOfTabAt(1));
}
```

A browser test rather than a unit test because the move between profiles
reopens the tab, and the reopen needs real storage.

- [ ] **Step 2: Run it and watch it fail**

```bash
scripts/build dev arcium_browsertests && out/dev/arcium_browsertests --gtest_filter='SplitViewTest.MovingOneHalf*'
```

Expected: fails with a split still in place, or with three tabs.

- [ ] **Step 3: End the split before the move**

In `space_switcher_spaces.cc`'s `MoveTabToSpace`, before the `SetSpaceTag`
call:

```cpp
  // Before the tag and before any reopen. A split whose halves are in two
  // spaces must not exist even for one turn of the loop: the partition guard
  // reads tags, and a reopen inside a split would leave a pane holding a tab
  // that has moved out from under it.
  if (split_) {
    split_->EndSplitFor(index);
  }
```

Give `SpaceSwitcher` a `SetSplitController(SplitController*)` setter -- the
controller needs the switcher to construct, so this is the same
construct-then-connect shape `SetArchiveService` already uses on
`SidebarTabModel`. Null in the playground and in every fixture built before
this stage, which keeps them working unchanged.

Do the same in `MoveEntryToSpace`, for the entry's tab if it has one.

- [ ] **Step 4: Run the test and prove it can fail**

```bash
scripts/format && scripts/build dev arcium_browsertests && out/dev/arcium_browsertests --gtest_filter='SplitViewTest.*'
```

Then move the `EndSplitFor` call to *after* the reopen and confirm the test
fails -- this is the ordering Stage 3b's tab-move defect turned on, and a test
that passes either way is not testing it. Put it back.

- [ ] **Step 5: Commit**

```bash
git add arcium/ui/browser/space_switcher.h arcium/ui/browser/space_switcher_spaces.cc arcium/ui/browser/browser_sidebar_controller.cc arcium/test/browser/split_view_browsertest.cc arcium/test/BUILD.gn
git commit
```

Message: why the split ends before the move rather than after -- the same
ordering that made a tab moved between profiles come back twice.

---

### Task 7: A split is still there after a relaunch

**Files:**
- Modify: `arcium/browser/model/space.h` (the record)
- Modify: `arcium/browser/model/arcium_model.h`, `.cc` (the setter)
- Modify: `arcium/browser/model/model_serializer.h` (version 5 -> 6), `.cc`
- Modify: `arcium/browser/model/model_migration.cc` (the step)
- Modify: `arcium/ui/browser/split_controller.h`, `.cc` (record, re-form)
- Modify: `arcium/test/model_serializer_unittest.cc`,
  `arcium/test/model_migration_unittest.cc`
- Modify: `arcium/test/browser/split_view_browsertest.cc`

**Interfaces:**
- Consumes: `TabKey`, `KeyOf`, `ExistingKeyOf` from `arcium/browser/tab_space.h`.
- Produces: `Space::split`, which nothing else reads.

- [ ] **Step 1: Write the failing tests**

`model_serializer_unittest.cc`:

```cpp
TEST(ModelSerializerTest, ASpaceRemembersItsSplit) {
  ArciumModel model;
  const SpaceId id = model.spaces().front().id;
  const TabKey first = TabKey::Generate();
  const TabKey second = TabKey::Generate();
  model.SetSpaceSplit(id, SpaceSplit{first, second, /*stacked=*/false, 0.4});

  ArciumModel read;
  ASSERT_TRUE(ApplyDict(SerializeModel(model), read));

  const std::optional<SpaceSplit>& split = read.GetSpace(id)->split;
  ASSERT_TRUE(split.has_value());
  EXPECT_EQ(first, split->first);
  EXPECT_EQ(second, split->second);
  EXPECT_FALSE(split->stacked);
  EXPECT_DOUBLE_EQ(0.4, split->ratio);
}

TEST(ModelSerializerTest, ASplitNamingOneTabIsDropped) {
  // Half a record is no record: it would re-form a split with itself.
  base::DictValue dict = MinimalModelDict();
  SetFirstSpaceSplitJson(dict, R"({"first": "abc", "ratio": 0.5})");

  ArciumModel read;
  ASSERT_TRUE(ApplyDict(std::move(dict), read));
  EXPECT_FALSE(read.spaces().front().split.has_value());
}
```

`model_migration_unittest.cc`:

```cpp
TEST(ModelMigrationTest, AVersionFiveFileBecomesOneWithNoSplits) {
  base::DictValue dict = VersionFiveDict();

  const std::optional<base::DictValue> migrated =
      MigrateModelDict(std::move(dict));

  ASSERT_TRUE(migrated.has_value());
  EXPECT_EQ(kModelSchemaVersion, migrated->FindInt("version"));
  const base::ListValue* spaces = migrated->FindList("spaces");
  ASSERT_TRUE(spaces);
  for (const base::Value& space : *spaces) {
    EXPECT_FALSE(space.GetDict().contains("split"));
  }
}
```

`split_view_browsertest.cc`, in two parts as the profile restore tests are
written (`PRE_` and the run after it):

```cpp
IN_PROC_BROWSER_TEST_F(SplitViewTest, PRE_ASplitSurvivesAQuit) {
  OpenTab(GURL("https://a.test/"));
  OpenTab(GURL("https://b.test/"));
  SplitTheTwoOpenTabs();
}

IN_PROC_BROWSER_TEST_F(SplitViewTest, ASplitSurvivesAQuit) {
  WaitForSplitToReform();
  EXPECT_EQ(2u, strip()->GetForegroundTabs().size());
}

IN_PROC_BROWSER_TEST_F(SplitViewTest,
                       PRE_ASplitInAnotherSpaceLoadsNothingUntilItIsEntered) {
  const SpaceId work = AddSpace(u"Work");
  SwitchToSpace(work);
  OpenTab(GURL("https://a.test/"));
  OpenTab(GURL("https://b.test/"));
  SplitTheTwoOpenTabs();
  SwitchToSpace(FirstSpace());
}

IN_PROC_BROWSER_TEST_F(SplitViewTest,
                       ASplitInAnotherSpaceLoadsNothingUntilItIsEntered) {
  WaitForSplitToReform();
  EXPECT_EQ(0, RenderProcessCountForSpace(WorkSpace()));

  // The positive control: everything above asserts that nothing happened,
  // which a counter wired to nothing would also report.
  SwitchToSpace(WorkSpace());
  WaitForBothPanesToLoad();
  EXPECT_EQ(2u, strip()->GetForegroundTabs().size());
  EXPECT_GT(RenderProcessCountForSpace(WorkSpace()), 0);
}
```

- [ ] **Step 2: Run them and watch them fail**

```bash
scripts/build dev arcium_unittests arcium_browsertests && out/dev/arcium_unittests --gtest_filter='ModelSerializerTest.*Split*:ModelMigrationTest.AVersionFive*' && out/dev/arcium_browsertests --gtest_filter='SplitViewTest.*Survives*:SplitViewTest.*AnotherSpace*'
```

- [ ] **Step 3: The record**

`arcium/browser/model/space.h`, above `struct Space`:

```cpp
// Two tabs of one space sharing the screen, named the way a tab is named
// across a restart. Chromium's own ids do not survive one (Stage 2 finding 1),
// which is why `last_active_tab` below is a TabKey too.
struct SpaceSplit {
  TabKey first;
  TabKey second;
  // Side by side unless this says otherwise. Two values, because Chromium's
  // SplitTabLayout has two.
  bool stacked = false;
  // The first pane's share of the width, as SplitTabVisualData keeps it.
  double ratio = 0.5;
};
```

and in `Space`, after `last_active_tab`:

```cpp
  // Absent when this space's tabs each have the screen to themselves.
  std::optional<SpaceSplit> split;
```

Add `ArciumModel::SetSpaceSplit(SpaceId, std::optional<SpaceSplit>)` beside
`SetLastActiveTab`, following it exactly: no notification and no write when
the value is unchanged.

- [ ] **Step 4: The file**

`model_serializer.h`: `kModelSchemaVersion` 5 -> 6.

`model_serializer.cc`, in the space loop after `last_active_tab`:

```cpp
    if (space.split) {
      base::DictValue split;
      split.Set("first", space.split->first.value());
      split.Set("second", space.split->second.value());
      split.Set("layout", space.split->stacked ? "stacked" : "side");
      split.Set("ratio", space.split->ratio);
      value.Set("split", std::move(split));
    }
```

and on the read side after `last_active_tab`:

```cpp
    if (const base::DictValue* split = value->FindDict("split")) {
      const std::string* first = split->FindString("first");
      const std::string* second = split->FindString("second");
      const TabKey a = first ? TabKey::FromString(*first) : TabKey();
      const TabKey b = second ? TabKey::FromString(*second) : TabKey();
      // Half a record is no record: it would ask for a split with itself.
      // A split is a convenience and never a reason to refuse a model file.
      if (a.is_valid() && b.is_valid() && a != b) {
        const std::string* layout = split->FindString("layout");
        space.split = SpaceSplit{
            a, b, layout && *layout == "stacked",
            split->FindDouble("ratio").value_or(0.5)};
      }
    }
```

`model_migration.cc`:

```cpp
// Version 5 -> 6: a space may record a split. A version 5 file has none, and
// an absent key is exactly what that means, so there is nothing to write --
// unlike MigrateV2ToV3, whose key had to exist to be read.
bool MigrateV5ToV6(base::DictValue&) {
  return true;
}
```

added to `kSteps`. The `static_assert` below it catches a forgotten step.

- [ ] **Step 5: Record and re-form**

In `SplitController`:

- `OnSplitTabChanged` writes the active space's split to the model, or clears
  it when the split went away. Read the change's `visual_data()` for the
  layout and ratio, and the strip for the two `TabKey`s via `KeyOf`.
- On construction, read every space's recorded split into a pending list.
  `OnTabStripModelChanged`'s insert case asks, for each pending record,
  whether both keys are now in the strip (`ExistingKeyOf` over the strip, no
  `KeyOf`: asking would mint a key for a tab that has none). When both are,
  form the split with `RestoreSplit` if a recorded id is wanted, otherwise
  `AddToNewSplit`, apply the layout and ratio with `UpdateSplitLayout` and
  `UpdateSplitRatio`, and drop the record from the pending list.
- **When the pending list empties, stop observing.** This is the perf answer:
  after launch the controller watches nothing.
- A record whose second tab never arrives simply never fires. Do not add a
  timeout.

- [ ] **Step 6: Run the tests, prove they can fail, commit**

```bash
scripts/format && scripts/build dev arcium_unittests arcium_browsertests && out/dev/arcium_unittests --gtest_filter='ModelSerializerTest.*:ModelMigrationTest.*:ModelStoreTest.*' && out/dev/arcium_browsertests --gtest_filter='SplitViewTest.*'
```

Mutate the re-forming so it fires on the first tab rather than the second, and
confirm `ASplitSurvivesAQuit` fails. Mutate the positive control's second half
away and confirm the inactive-space test still passes -- which is the point of
having it, and why it must go back.

```bash
git add arcium/browser/model/space.h arcium/browser/model/arcium_model.h arcium/browser/model/arcium_model.cc arcium/browser/model/model_serializer.h arcium/browser/model/model_serializer.cc arcium/browser/model/model_migration.cc arcium/ui/browser/split_controller.h arcium/ui/browser/split_controller.cc arcium/test/model_serializer_unittest.cc arcium/test/model_migration_unittest.cc arcium/test/browser/split_view_browsertest.cc
git commit
```

Message: why a split is written down by tab key rather than by Chromium's
split id -- the id does not survive a restart and the key already does, for
the same reason a space's last active tab is one.

---

### Task 8: A link dropped at the page's edge lands in the space on screen

**Files:**
- Modify: `arcium/test/browser/split_view_browsertest.cc`

Nothing to build: Chromium's own drop target already does this. The test
exists because nothing else in the stage would notice if it stopped working,
or if the tab it creates skipped Arcium's new-tab storage hooks.

- [ ] **Step 1: Write the test**

```cpp
IN_PROC_BROWSER_TEST_F(SplitViewTest, ALinkDroppedAtTheEdgeJoinsTheSpaceOnScreen) {
  const SpaceId work = AddSpace(u"Work");
  SwitchToSpace(work);
  OpenTab(GURL("https://a.test/"));

  DropLinkOnPageEdge(GURL("https://b.test/"), /*right=*/true);

  ASSERT_EQ(2u, strip()->GetForegroundTabs().size());
  EXPECT_EQ(work, SpaceOfTabAt(strip()->active_index()));
  EXPECT_EQ(StoragePartitionOfSpace(work),
            StoragePartitionOfTabAt(strip()->active_index()));
}
```

`DropLinkOnPageEdge` drives
`MultiContentsViewDropTargetController::HandleLinkDrop` through the view, as
`multi_contents_view_drop_target_controller_browsertest.cc` does -- read that
file for the shape rather than inventing one.

- [ ] **Step 2: Run it**

```bash
scripts/build dev arcium_browsertests && out/dev/arcium_browsertests --gtest_filter='SplitViewTest.ALinkDropped*'
```

If it fails, the failure is real and belongs in the findings: it means a tab
created by that path is not getting its space's storage, which is a Stage 3b
promise rather than a Stage 5 one. Stop and report before fixing.

- [ ] **Step 3: Commit**

```bash
git add arcium/test/browser/split_view_browsertest.cc
git commit
```

Message: why a test for code we did not write -- it is the one way into a
split that Arcium does not own, so nothing else here would notice it breaking.

---

### Task 9: Close the stage

**Files:**
- Create: `docs/stage5a-findings.md`
- Create: `docs/stage5a-hand-checks.md`
- Modify: `docs/superpowers/specs/2026-09-05-arcium-browser-design.md`
  (section 7, Deviations)
- Modify: `CLAUDE.md` (the stage table)
- Create: `docs/perf/2026-09-<dd>-stage5a.md`

- [ ] **Step 1: Write the hand checks**

`docs/stage5a-hand-checks.md`: the ten rows of the design's A5.1 list, one per
line, each with what to do and what to look for. This is a list the owner
walks, so it names gestures, not functions.

- [ ] **Step 2: Ask about the machine, then run everything**

The whole browser suite takes over the screen for several minutes. Ask whether
the machine is free. If it is:

```bash
out/dev/arcium_unittests && out/dev/arcium_browsertests
```

If it is not, run only `arcium_unittests` plus the split browser tests, and say
in the findings exactly which ones did not run.

- [ ] **Step 3: Measure**

```bash
scripts/perf --label stage5a
```

Record the result in `docs/perf/`. Answer the four questions against the
measurement, not against the design's predictions, and say where they
disagree. The machine is shared: if it is loaded, say so and measure later
rather than blocking on it.

- [ ] **Step 4: Record the deviation**

In section 7 of the master spec, add: R5.1 ships with two panes rather than
two to four, with the reason (Chromium's split is exactly two, and replacing
`MultiContentsView` would be the largest upstream surface in the project) and
the note that three and four stay open behind a feature flag if they are ever
built.

- [ ] **Step 5: Write the findings and update the stage table**

`docs/stage5a-findings.md`: what was built, what every defect found during the
build was and how it was proved fixed, what the hand rows are and which have
been walked, and what is still owed. Follow `docs/stage4b-findings.md`'s shape.

In `CLAUDE.md`, replace the `5 Layout` row with one naming 5a as built, the
spec and plan paths, the test counts, what was skipped, and that R5.2 to R5.4
are not started.

- [ ] **Step 6: Commit**

```bash
git add docs/stage5a-findings.md docs/stage5a-hand-checks.md docs/superpowers/specs/2026-09-05-arcium-browser-design.md docs/perf CLAUDE.md
git commit
```

Message: why the record is part of the work -- a stage whose state lives only
in a session is a stage nobody can pick up.

---

## Self-review

**Spec coverage.** Every section of the design maps to a task: the rule and
controller to Task 1, the sidebar to Task 2, the four ways in to Tasks 3, 4, 5
and 8, breaking a split to Tasks 1 and 6, spaces to Task 6, after a quit to
Task 7, the mini strip to nothing (it is Chromium's and stays), the four
questions and A5.1 to Task 9.

**Two things the plan assumes and the implementer must check first.** The name
and signature of the loose-page predicate in `arcium/browser/loose_page.h`
(Task 1), and that Cmd+Option+S is unclaimed in
`global_keyboard_shortcuts_mac.mm` (Task 4). Both are named in the step that
needs them, and Task 4 says to stop rather than pick another key.

**One risk with no test.** `AddToNewSplit` reorders the strip to make the two
tabs contiguous, which moves a Today row in the sidebar list. The design
accepts this; no test asserts the order afterwards, because the order it
produces is Chromium's to decide and freezing it here would be a test of
upstream. If the hand pass finds the movement confusing, that is a finding,
not a regression.
