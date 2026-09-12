// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/folder.h"
#include "arcium/browser/model/space.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/ui/browser/sidebar_tab_model.h"
#include "arcium/ui/playground/fake_sidebar_model.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

// The folder half of the sidebar interface. Kept apart from
// sidebar_tab_model_unittest.cc so neither file grows past reading size.
class SidebarFoldersTest : public BrowserWithTestWindowTest {
 protected:
  TabStripModel* strip() { return browser()->tab_strip_model(); }

  // Browser batches a navigation's UI updates and delivers them from a task
  // it posts 200 ms out (kUIUpdateCoalescingTime,
  // chrome/browser/ui/browser.cc), ending in
  // TabStripModel::TabChangedAt(kAll) — one more notification for the
  // sidebar. RunUntilIdle does not run a task that is not due yet, so a test
  // that counts notifications and does not turn this off is counting
  // whatever the wall clock happened to deliver inside its window: the
  // update lands in the window if the tests before it ran slowly and outside
  // it if they ran fast. That was the whole of this file's flake. This is
  // upstream's own seam for it — with the delay at zero the same
  // RunUntilIdle that drains everything else drains this too. Only the test
  // that counts needs it; the rest stay on stock timing.
  void MakeBrowserUiUpdatesImmediate() {
    browser()->set_update_ui_immediately_for_testing();
  }

  std::unique_ptr<SidebarTabModel> MakeModel() {
    return std::make_unique<SidebarTabModel>(strip(), &arcium_model_,
                                             &binding_);
  }

  ArciumModel arcium_model_;
  TabBinding binding_;
};

TEST_F(SidebarFoldersTest, ThereAreNoFoldersUntilOneIsMade) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  EXPECT_TRUE(model->folders().empty());
}

TEST_F(SidebarFoldersTest, CreatingAFolderWithAnEntryPutsTheEntryInside) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  const EntryId id = model->rows()[0].entry_id;

  const FolderId folder = model->CreateFolderWithEntry(id, u"Work");
  ASSERT_TRUE(folder.is_valid());

  std::vector<SidebarFolder> folders = model->folders();
  ASSERT_EQ(1u, folders.size());
  EXPECT_EQ(folder, folders[0].id);
  EXPECT_EQ(u"Work", folders[0].name);
  EXPECT_FALSE(folders[0].collapsed);
  // Precomputed, so a header never has to scan the rows to draw its count.
  EXPECT_EQ(1, folders[0].entry_count);

  ASSERT_EQ(1u, model->rows().size());
  EXPECT_EQ(folder, model->rows()[0].folder_id);
}

// Folders hold pinned entries. A favourite has no folder to be in, and a
// Today row has no entry at all.
TEST_F(SidebarFoldersTest, AFavouriteCannotBeFoldered) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->AddToFavorites(0);
  const EntryId id = model->rows()[0].entry_id;
  // Without this the test is vacuous the day AddToFavorites stops working: an
  // invalid id makes CreateFolderWithEntry refuse through its unknown-entry
  // branch, and the refusal this test names — a favourite cannot be foldered
  // — would never be reached again.
  ASSERT_TRUE(id.is_valid());

  EXPECT_FALSE(model->CreateFolderWithEntry(id, u"Work").is_valid());
  EXPECT_TRUE(model->folders().empty());
}

TEST_F(SidebarFoldersTest, AnUnknownEntryMakesNoFolder) {
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  EXPECT_FALSE(
      model->CreateFolderWithEntry(EntryId::Generate(), u"Work").is_valid());
  EXPECT_TRUE(model->folders().empty());
}

TEST_F(SidebarFoldersTest, MovingAnEntryBetweenFoldersAndBackToTopLevel) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  model->PinTab(0);
  const EntryId first = model->rows()[0].entry_id;

  const FolderId work = model->CreateFolderWithEntry(first, u"Work");
  const FolderId play =
      model->CreateFolderWithEntry(model->rows()[1].entry_id, u"Play");
  ASSERT_TRUE(work.is_valid());
  ASSERT_TRUE(play.is_valid());

  model->MoveEntryToFolder(first, play);
  EXPECT_EQ(play, model->rows()[0].folder_id);

  model->MoveEntryToFolder(first, std::nullopt);
  EXPECT_FALSE(model->rows()[0].folder_id.has_value());
}

// Ids arrive from menus built off a model that may have changed underneath.
TEST_F(SidebarFoldersTest, MovingIntoAFolderThatDoesNotExistIsANoOp) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  const EntryId id = model->rows()[0].entry_id;
  const FolderId folder = model->CreateFolderWithEntry(id, u"Work");

  model->MoveEntryToFolder(id, FolderId::Generate());
  EXPECT_EQ(folder, model->rows()[0].folder_id);
}

TEST_F(SidebarFoldersTest, CollapsingAndRenamingShowInFolders) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  const FolderId folder =
      model->CreateFolderWithEntry(model->rows()[0].entry_id, u"Work");

  model->SetFolderCollapsed(folder, true);
  ASSERT_EQ(1u, model->folders().size());
  EXPECT_TRUE(model->folders()[0].collapsed);

  model->SetFolderName(folder, u"Renamed");
  EXPECT_EQ(u"Renamed", model->folders()[0].name);

  model->SetFolderCollapsed(folder, false);
  EXPECT_FALSE(model->folders()[0].collapsed);
}

// The menu item says the tabs stay, and this is why it can.
TEST_F(SidebarFoldersTest, DeletingAFolderReturnsItsEntriesToTheTopLevel) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  const EntryId id = model->rows()[0].entry_id;
  const FolderId folder = model->CreateFolderWithEntry(id, u"Work");

  model->DeleteFolder(folder);
  EXPECT_TRUE(model->folders().empty());
  ASSERT_EQ(1u, model->rows().size());
  EXPECT_EQ(id, model->rows()[0].entry_id);
  EXPECT_EQ(SidebarSection::kPinned, model->rows()[0].section);
  EXPECT_FALSE(model->rows()[0].folder_id.has_value());
}

// `position` decides, not the order the folders sit in the vector. Making
// them through AddFolder cannot show that — it hands out positions in
// insertion order, so the sort would be a no-op and the test would pass with
// it deleted. Seeded the way a restore from disk seeds it instead, with the
// two disagreeing.
TEST_F(SidebarFoldersTest, FoldersComeBackInPositionOrder) {
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  std::vector<Space> spaces = arcium_model_.spaces();
  ASSERT_FALSE(spaces.empty());

  Folder second;
  second.id = FolderId::Generate();
  second.space_id = spaces[0].id;
  second.name = u"Second";
  second.position = 1;
  Folder first;
  first.id = FolderId::Generate();
  first.space_id = spaces[0].id;
  first.name = u"First";
  first.position = 0;
  // Vector order is Second, First; position order is First, Second.
  arcium_model_.ReplaceAll({}, std::move(spaces), {second, first}, {});

  std::vector<SidebarFolder> folders = model->folders();
  ASSERT_EQ(2u, folders.size());
  EXPECT_EQ(u"First", folders[0].name);
  EXPECT_EQ(u"Second", folders[1].name);
}

// Every folder command is built to be a no-op on an id the model does not
// have, because a menu is a snapshot and the model can move under it.
TEST_F(SidebarFoldersTest, CommandsOnARemovedFolderDoNothing) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  const EntryId entry = model->rows()[0].entry_id;
  const FolderId folder = model->CreateFolderWithEntry(entry, u"Work");
  ASSERT_TRUE(folder.is_valid());
  model->DeleteFolder(folder);
  ASSERT_TRUE(model->folders().empty());

  model->SetFolderName(folder, u"Renamed");
  model->SetFolderCollapsed(folder, true);
  model->DeleteFolder(folder);
  model->MoveEntryToFolder(entry, folder);

  EXPECT_TRUE(model->folders().empty());
  ASSERT_EQ(1u, model->rows().size());
  // The entry came back to the top level when the folder went, and none of
  // the commands above put it anywhere else.
  EXPECT_FALSE(model->rows()[0].folder_id.has_value());
}

TEST_F(SidebarFoldersTest, CommandsOnAFolderThatNeverExistedDoNothing) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  const FolderId unknown = FolderId::Generate();

  model->SetFolderName(unknown, u"Renamed");
  model->SetFolderCollapsed(unknown, true);
  model->DeleteFolder(unknown);

  EXPECT_TRUE(model->folders().empty());
  ASSERT_EQ(1u, model->rows().size());
  EXPECT_FALSE(model->rows()[0].folder_id.has_value());
}

TEST_F(SidebarFoldersTest, EntryCountsAreCountedPerFolder) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  AddTab(browser(), GURL("https://c.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  model->PinTab(0);
  model->PinTab(0);
  const FolderId work =
      model->CreateFolderWithEntry(model->rows()[0].entry_id, u"Work");
  model->MoveEntryToFolder(model->rows()[1].entry_id, work);

  std::vector<SidebarFolder> folders = model->folders();
  ASSERT_EQ(1u, folders.size());
  EXPECT_EQ(2, folders[0].entry_count);
}

// A folder change is a model mutation like any other, so it coalesces into
// the same one notification per burst the rest of the model uses.
TEST_F(SidebarFoldersTest, AFolderChangeNotifiesOnce) {
  MakeBrowserUiUpdatesImmediate();
  AddTab(browser(), GURL("https://a.example/"));
  task_environment()->RunUntilIdle();
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  task_environment()->RunUntilIdle();

  class CountingObserver : public SidebarModel::Observer {
   public:
    void OnSidebarModelChanged() override { ++count; }
    int count = 0;
  } observer;
  model->AddObserver(&observer);

  model->CreateFolderWithEntry(model->rows()[0].entry_id, u"Work");
  EXPECT_EQ(0, observer.count);
  task_environment()->RunUntilIdle();
  EXPECT_EQ(1, observer.count);
  model->RemoveObserver(&observer);
}

TEST_F(SidebarFoldersTest, AFolderCanBeNestedInsideAnother) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  const EntryId entry = model->rows()[0].entry_id;
  const FolderId outer = model->CreateFolderWithEntry(entry, u"Outer");
  const FolderId inner = arcium_model_.AddFolderForTesting(u"Inner");

  model->SetFolderParent(inner, outer);

  std::vector<SidebarFolder> folders = model->folders();
  ASSERT_EQ(2u, folders.size());
  // Pre-order: the parent first, then what is inside it.
  EXPECT_EQ(outer, folders[0].id);
  EXPECT_EQ(0, folders[0].depth);
  EXPECT_FALSE(folders[0].parent_id.has_value());
  EXPECT_EQ(inner, folders[1].id);
  EXPECT_EQ(1, folders[1].depth);
  EXPECT_EQ(outer, folders[1].parent_id);
}

TEST_F(SidebarFoldersTest, AFolderCountsTheEntriesBelowIt) {
  AddTab(browser(), GURL("https://b.example/"));
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  model->PinTab(1);
  const std::vector<SidebarRow> rows = model->rows();
  const EntryId first = rows[0].entry_id;
  const EntryId second = rows[1].entry_id;

  const FolderId outer = model->CreateFolderWithEntry(first, u"Outer");
  const FolderId inner = arcium_model_.AddFolderForTesting(u"Inner");
  model->SetFolderParent(inner, outer);
  model->MoveEntryToFolder(second, inner);

  std::vector<SidebarFolder> folders = model->folders();
  ASSERT_EQ(2u, folders.size());
  // One of its own plus one below it: what a collapsed header has to say to
  // be honest about what it is hiding.
  EXPECT_EQ(2, folders[0].entry_count);
  EXPECT_EQ(1, folders[1].entry_count);
}

TEST_F(SidebarFoldersTest, TheModelRefusesAFolderMoveThatWouldMakeACycle) {
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  const FolderId outer = arcium_model_.AddFolderForTesting(u"Outer");
  const FolderId inner = arcium_model_.AddFolderForTesting(u"Inner");
  model->SetFolderParent(inner, outer);

  EXPECT_FALSE(model->CanMoveFolderTo(outer, inner));
  model->SetFolderParent(outer, inner);

  std::vector<SidebarFolder> folders = model->folders();
  ASSERT_EQ(2u, folders.size());
  EXPECT_EQ(outer, folders[0].id);
  EXPECT_EQ(0, folders[0].depth);
  EXPECT_EQ(inner, folders[1].id);
  EXPECT_EQ(1, folders[1].depth);
}

TEST_F(SidebarFoldersTest, DeletingAFolderLeavesItsSubfolderOneLevelUp) {
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  const FolderId outer = arcium_model_.AddFolderForTesting(u"Outer");
  const FolderId middle = arcium_model_.AddFolderForTesting(u"Middle");
  const FolderId inner = arcium_model_.AddFolderForTesting(u"Inner");
  model->SetFolderParent(middle, outer);
  model->SetFolderParent(inner, middle);

  model->DeleteFolder(middle);

  std::vector<SidebarFolder> folders = model->folders();
  ASSERT_EQ(2u, folders.size());
  EXPECT_EQ(outer, folders[0].id);
  EXPECT_EQ(inner, folders[1].id);
  EXPECT_EQ(1, folders[1].depth);
  EXPECT_EQ(outer, folders[1].parent_id);
}

// The same rule again, on the playground's fake. The fake exists to mirror the
// real model's rules -- that mirroring is the only reason a view test written
// against it catches a real bug -- so a rule it copies and nobody checks is a
// rule that can drift. This one is deliberately the twin of
// DeletingAFolderLeavesItsSubfolderOneLevelUp above; if the two ever disagree,
// one of them is wrong and this says which.
TEST(FakeSidebarModelTest, DeletingAFolderLeavesItsSubfolderOneLevelUp) {
  FakeSidebarModel model;
  model.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  model.AddTab(u"Three", "https://three.example/", SidebarSection::kPinned,
               false);
  const FolderId outer = model.AddFolderWith(u"Outer", {u"One"});
  const FolderId middle = model.AddFolderWith(u"Middle", {u"Two"});
  const FolderId inner = model.AddFolderWith(u"Inner", {u"Three"});
  model.SetFolderParent(middle, outer);
  model.SetFolderParent(inner, middle);

  model.DeleteFolder(middle);

  std::vector<SidebarFolder> folders = model.folders();
  ASSERT_EQ(2u, folders.size());
  EXPECT_EQ(outer, folders[0].id);
  EXPECT_EQ(inner, folders[1].id);
  // One level up, to the deleted folder's own parent -- not to the top level.
  EXPECT_EQ(1, folders[1].depth);
  EXPECT_EQ(outer, folders[1].parent_id);
  // And the entry that was in the deleted folder came up with it.
  int in_outer = 0;
  for (const SidebarRow& row : model.rows()) {
    if (row.folder_id == outer) {
      ++in_outer;
    }
  }
  EXPECT_EQ(2, in_outer);
}

// "New folder" on a row inside a folder used to make a top-level folder and
// pull the entry out to it, which moved the entry somewhere the user was not
// looking. The new folder belongs where the row was drawn: inside the folder
// the entry is already in. Zen's analogous item is "New Subfolder", which is
// likewise relative to its context rather than absolute.
TEST_F(SidebarFoldersTest, AFolderMadeFromAnEntryInsideAFolderIsMadeThere) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  const EntryId id = model->rows()[0].entry_id;
  const FolderId outer = model->CreateFolderWithEntry(id, u"Work");
  ASSERT_TRUE(outer.is_valid());
  ASSERT_EQ(outer, model->rows()[0].folder_id);

  const FolderId inner = model->CreateFolderWithEntry(id, u"Reading");

  ASSERT_TRUE(inner.is_valid());
  ASSERT_NE(outer, inner);
  // The new folder sits inside the old one, and the entry went with it.
  std::vector<SidebarFolder> folders = model->folders();
  ASSERT_EQ(2u, folders.size());
  const SidebarFolder* nested = nullptr;
  for (const SidebarFolder& folder : folders) {
    if (folder.id == inner) {
      nested = &folder;
    }
  }
  ASSERT_TRUE(nested);
  EXPECT_EQ(outer, nested->parent_id);
  EXPECT_EQ(1, nested->depth);
  EXPECT_EQ(inner, model->rows()[0].folder_id);
}

// The other half of the same rule: an entry that is in no folder still makes
// a top-level one. Without this the fix could have hard-coded a parent.
TEST_F(SidebarFoldersTest, AFolderMadeFromATopLevelEntryStaysAtTheTopLevel) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  const EntryId id = model->rows()[0].entry_id;

  const FolderId folder = model->CreateFolderWithEntry(id, u"Work");

  ASSERT_TRUE(folder.is_valid());
  ASSERT_EQ(1u, model->folders().size());
  EXPECT_FALSE(model->folders()[0].parent_id.has_value());
  EXPECT_EQ(0, model->folders()[0].depth);
}

// Nesting on create is a depth increase like any other, so it answers to the
// same cap. Written against kMaxFolderDepth rather than a literal so the cap
// can move without this test quietly becoming a test of nothing.
TEST_F(SidebarFoldersTest, AFolderIsNotMadeWhenItWouldPassTheDepthCap) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  const EntryId id = model->rows()[0].entry_id;

  // Build the chain by creating from the same entry over and over: each call
  // makes a folder inside the last, which is the behaviour under test.
  FolderId deepest;
  while (static_cast<int>(model->folders().size()) < kMaxFolderDepth) {
    const FolderId made = model->CreateFolderWithEntry(id, u"Deeper");
    ASSERT_TRUE(made.is_valid()) << "ran out of depth before the cap";
    deepest = made;
  }
  // The entry is now in a folder at the deepest legal level.
  const std::vector<SidebarFolder> full = model->folders();
  ASSERT_EQ(static_cast<size_t>(kMaxFolderDepth), full.size());
  ASSERT_EQ(kMaxFolderDepth - 1, full.back().depth);
  ASSERT_EQ(deepest, model->rows()[0].folder_id);

  EXPECT_FALSE(model->CanCreateFolderWithEntry(id));
  EXPECT_FALSE(model->CreateFolderWithEntry(id, u"One too many").is_valid());

  // Nothing was made, and the entry did not move.
  EXPECT_EQ(static_cast<size_t>(kMaxFolderDepth), model->folders().size());
  EXPECT_EQ(deepest, model->rows()[0].folder_id);
}

// The affordance and the rule agree. A menu that offers a folder the model
// will refuse is the mistake CanMoveFolderTo already exists to prevent.
TEST_F(SidebarFoldersTest, RoomForAFolderIsOfferedUntilTheCapIsReached) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  const EntryId id = model->rows()[0].entry_id;

  for (int made = 0; made < kMaxFolderDepth; ++made) {
    EXPECT_TRUE(model->CanCreateFolderWithEntry(id))
        << "refused at depth " << made;
    ASSERT_TRUE(model->CreateFolderWithEntry(id, u"Deeper").is_valid());
  }
  EXPECT_FALSE(model->CanCreateFolderWithEntry(id));
}

}  // namespace
}  // namespace arcium
