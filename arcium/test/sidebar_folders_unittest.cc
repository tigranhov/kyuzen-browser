// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <vector>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/ui/browser/sidebar_tab_model.h"
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

TEST_F(SidebarFoldersTest, FoldersComeBackInPositionOrder) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  model->PinTab(0);
  model->CreateFolderWithEntry(model->rows()[0].entry_id, u"First");
  model->CreateFolderWithEntry(model->rows()[1].entry_id, u"Second");

  std::vector<SidebarFolder> folders = model->folders();
  ASSERT_EQ(2u, folders.size());
  EXPECT_EQ(u"First", folders[0].name);
  EXPECT_EQ(u"Second", folders[1].name);
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

}  // namespace
}  // namespace arcium
