# Stage 4a: the address pill, the command box and extensions — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Arcium usable as a daily browser: extensions you pin appear as buttons in the sidebar and open their popups, the address pill shows the domain and two hover buttons instead of Chromium's icon row, and one floating box — opened by Cmd+T, Cmd+L or a click on the pill — answers with suggestions as you type.

**Architecture:** Chromium's `LocationBarView` stays hosted inside the pill and merely stops drawing, because Chrome's password bubbles, page information and permission chips all reach for it; the pill paints its own domain text and buttons over it. Chromium's `ExtensionsToolbarDesktop` is reparented out of the hidden toolbar into a new sidebar row, the same move the location bar already makes. The command box owns an `AutocompleteController` of its own, built on `ChromeAutocompleteProviderClient`, created when the box opens and destroyed when it closes.

**Tech Stack:** Chromium Views (C++), `//components/omnibox/browser`, `//components/security_state/content`, `ExtensionsToolbarDesktop`, `ShowPageInfoDialog`, GN, gtest.

**Spec:** `docs/superpowers/specs/2026-09-13-stage-4a-pill-and-extensions-design.md`

## Global Constraints

- All Arcium code lives in `arcium/`. Upstream Chromium files change only through numbered patches in `patches/`, and a patch is a hook that calls into `arcium/` — logic never lives in a patch.
- Every patch starts with a plain-text header naming the seam, the reason, and the `arcium/` function it delegates to, before the first `diff --git` line.
- Patches are generated, never hand-written: `git -C /Volumes/Texternal/chromium/src diff -- <file> > patches/NNNN-name.patch`, then prepend the header. Afterwards run `scripts/sync` twice; the second run must report every patch applied or skipped cleanly.
- `arcium/ui/sidebar` must not depend on `//chrome`. Anything needing Chrome lives in `arcium/ui/browser` and reaches the sidebar through a delegate callback. The sidebar's model interface stays free of `//chrome` too.
- Views in C++ only. Never WebUI, no Swift, AppKit or Cocoa.
- No UI-thread writes and no sync I/O, ever.
- Use `base::DictValue` and `base::ListValue`. `base::Value::Dict` does not exist in this tree.
- A file over ~500 lines is a smell; split it.
- Format with `scripts/format <files>`. `git cl format` does not work in this checkout.
- Test-driven: write the failing test, run it, watch it fail for the right reason, then implement.
- Commit by explicit path. Never `git add -A`. The message says why, carries no task number, and ends with `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`. **Do not push.**
- Never commit Chromium sources or build output. Do not modify or commit the untracked `AGENTS.md`.
- Run every git command from `/Volumes/Texternal/repositories/arcium`, never inside the Chromium checkout.
- Before building, check `pgrep -f siso` and `pgrep -f "Arcium.app/Contents/MacOS/Arcium"`. Never build `chrome` or `arcium_browsertests` while the owner's Arcium is running from `out/dev`. `arcium_unittests` is always safe. Use `ARCIUM_JOBS=4` and a 600000 ms timeout.
- Browser tests open real windows and take over the screen on macOS. Run them only when the machine is free, and never as a background sweep while the owner is working.
- One checkout, one owner: `chromium/src/arcium` is a symlink pointing at whichever working tree last ran `scripts/sync`. Check it with `ls -l /Volumes/Texternal/chromium/src/arcium` before trusting a build, and `touch` a source file after a symlink swap or siso will serve a cached object.

## File Structure

Created:

| File | Responsibility |
|---|---|
| `arcium/ui/sidebar/pill_domain.h/.cc` | One pure function: what the pill shows for a URL. No dependencies beyond `//url` and `//base`. |
| `arcium/ui/sidebar/extensions_row_view.h/.cc` | A host view above the favourites that wraps whatever child it is given into lines. Knows nothing about extensions. |
| `arcium/ui/browser/suggestion_source.h/.cc` | Owns an `AutocompleteController`, turns its results into rows the box can draw. |
| `arcium/ui/browser/command_box.h/.cc` | The floating box: field, result list, keyboard. Replaces `QuickEntryBubble`. |
| `arcium/ui/browser/command_box_row.h/.cc` | One result row view: icon, title, URL, open-tab marker. |
| `arcium/ui/browser/hosted_location_bar.h/.cc` | The decision a patch asks: what a hosted location bar may draw. |
| `arcium/test/pill_domain_unittest.cc` | Table test for the domain function. |
| `arcium/test/url_pill_unittest.cc` | The pill's resting look, its buttons, its reveal. |
| `arcium/test/suggestion_source_unittest.cc` | Rows out of results, and that nothing starts unasked. |
| `arcium/test/browser/sidebar_ui_browsertest_base.h/.cc` | The fixture and helpers every browser test in this stage shares. |
| `arcium/test/browser/pill_browsertest.cc` | The pill against a real browser: hosted bar silent, site info, copy, insecure mark. |
| `arcium/test/browser/extensions_row_browsertest.cc` | A real extension pinned, drawn, clicked, unpinned. |
| `arcium/test/browser/command_box_browsertest.cc` | Typing, suggestions, Enter, Cmd+L. |
| `patches/0200-location-bar-hosted.patch` | Hook: a hosted location bar draws nothing of its own. |
| `patches/0210-extensions-container-autohide.patch` | Hook: the extensions container is built in auto-hide mode. |

Modified:

| File | Change |
|---|---|
| `arcium/ui/sidebar/url_pill_view.h/.cc` | Domain text, three buttons, hover and focus reveal, click opens the box. |
| `arcium/ui/sidebar/sidebar_view.h/.cc` | The extensions row between the pill and the favourites; pill delegate plumbing. |
| `arcium/ui/sidebar/sidebar_metrics.h` | Metrics for the extension buttons. |
| `arcium/ui/browser/browser_sidebar_controller.h/.cc` | Wires the pill's four actions, reparents the extensions container, owns the box. |
| `patches/0090-new-tab-quick-entry.patch` | Extended to take `IDC_FOCUS_LOCATION` as well as `IDC_NEW_TAB`. |
| `arcium/ui/sidebar/BUILD.gn`, `arcium/ui/browser/BUILD.gn`, `arcium/test/BUILD.gn` | New sources and deps. |
| `arcium/ui/browser/quick_entry_bubble.h/.cc` | Deleted; `command_box` replaces it. |
| `docs/stage4a-findings.md`, `CLAUDE.md`, `scripts/acceptance-4a` | Written in the last task. |

## Shared browser-test fixtures

Three browser tests here need the same handful of helpers. Write them once, in
`arcium/test/browser/sidebar_ui_browsertest_base.h/.cc`, created as part of
Task 3 (the first task that needs them) and used by Tasks 4, 5, 7, 8 and 9.

```cpp
// A browser with the sidebar up, and short ways to reach the pieces of it
// these tests talk about.
class SidebarUiTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    no_reveal_animation_ = UrlPillView::DisableRevealAnimationForTesting();
  }

  BrowserSidebarController* Controller() {
    return BrowserView::GetBrowserViewForBrowser(browser())->arcium_sidebar();
  }
  UrlPillView* Pill() { return Controller()->view()->url_pill(); }
  ExtensionsRowView* Row() { return Controller()->view()->extensions_row(); }
  ExtensionsToolbarDesktop* Container() {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->toolbar()
        ->extensions_container();
  }
  CommandBox* Box() { return Controller()->command_box_for_testing(); }

  void OpenBox() {
    Controller()->ShowCommandBox(std::nullopt);
    ASSERT_TRUE(Box());
  }
  void Type(const std::u16string& text) {
    Box()->SetText(text, /*select_all=*/false);
  }
  void PressEnter() { SendKeyToBox(ui::VKEY_RETURN); }
  void PressEscape() { SendKeyToBox(ui::VKEY_ESCAPE); }
  void ClickPillBackground() {
    // The middle of the pill, which is its text and not one of its buttons.
    ui::test::EventGenerator generator(
        Pill()->GetWidget()->GetNativeWindow());
    generator.MoveMouseTo(Pill()->GetBoundsInScreen().CenterPoint());
    generator.ClickLeftButton();
  }
  // Suggestions arrive asynchronously from providers that do real work, so
  // every assertion about rows waits for them rather than sleeping.
  void WaitForRows() {
    base::RunLoop loop;
    Box()->SetRowsChangedClosureForTesting(loop.QuitClosure());
    loop.Run();
  }
  void WaitForRowThatIsAnOpenTab() {
    while (!AnyRowIsAnOpenTab()) {
      WaitForRows();
    }
  }
  void WaitForHistory(const GURL& url);   // blocks on HistoryService
  void PinEntryWithUrl(const GURL& url, const std::u16string& title);
  extensions::ExtensionId LoadTestExtension();  // ChromeTestExtensionLoader
  void PinExtension(const extensions::ExtensionId& id);    // ToolbarActionsModel
  void UnpinExtension(const extensions::ExtensionId& id);  // SetActionVisibility

 private:
  void SendKeyToBox(ui::KeyboardCode key);
  bool AnyRowIsAnOpenTab();
  base::AutoReset<bool> no_reveal_animation_{nullptr, false};
};
```

`LoadTestExtension` writes a minimal manifest V3 extension with a browser
action into a scoped temporary directory and loads it unpacked, so the tests
need no store account and no network. Called more than once it writes a
differently-named extension each time, which is what the nine-extension
wrapping test needs.

---

### Task 1: The domain the pill shows

**Files:**
- Create: `arcium/ui/sidebar/pill_domain.h`, `arcium/ui/sidebar/pill_domain.cc`
- Create: `arcium/test/pill_domain_unittest.cc`
- Modify: `arcium/ui/sidebar/url_pill_view.h`, `arcium/ui/sidebar/url_pill_view.cc`
- Modify: `arcium/ui/sidebar/BUILD.gn`, `arcium/test/BUILD.gn`
- Modify: `arcium/ui/sidebar/sidebar_view.cc` (the one call site that sets pill text)

**Interfaces:**
- Consumes: nothing.
- Produces: `std::u16string arcium::PillDomain(const GURL& url)`; `void UrlPillView::SetUrl(const GURL& url)`; `const std::u16string& UrlPillView::domain_for_testing() const`.

- [ ] **Step 1: Write the failing test**

Create `arcium/test/pill_domain_unittest.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/pill_domain.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

TEST(PillDomainTest, DropsAWwwThatIsAPrefixAndNotAName) {
  EXPECT_EQ(u"google.com", PillDomain(GURL("https://www.google.com/")));
  // "www.com" is a name in its own right: dropping the prefix would leave
  // "com", which is not where the user is.
  EXPECT_EQ(u"www.com", PillDomain(GURL("https://www.com/")));
}

TEST(PillDomainTest, KeepsASubdomain) {
  // The registrable domain would say google.com here, which would be the bar
  // telling the user they are somewhere they are not.
  EXPECT_EQ(u"mail.google.com",
            PillDomain(GURL("https://mail.google.com/mail/u/0")));
}

TEST(PillDomainTest, KeepsAPortAndAnAddress) {
  EXPECT_EQ(u"localhost:8899", PillDomain(GURL("http://localhost:8899/x")));
  EXPECT_EQ(u"127.0.0.1", PillDomain(GURL("https://127.0.0.1/")));
}

TEST(PillDomainTest, LeavesPunycodeAsPunycode) {
  // Unicode here is how a spoofed host hides. Punycode is ugly and honest.
  EXPECT_EQ(u"xn--80ak6aa92e.com",
            PillDomain(GURL("https://xn--80ak6aa92e.com/")));
}

TEST(PillDomainTest, HasNothingToShowForAPageThatIsNotAWebsite) {
  EXPECT_EQ(u"", PillDomain(GURL("chrome://settings")));
  EXPECT_EQ(u"", PillDomain(GURL("file:///tmp/x.html")));
  EXPECT_EQ(u"", PillDomain(GURL()));
  EXPECT_EQ(u"", PillDomain(GURL("about:blank")));
}

}  // namespace
}  // namespace arcium
```

- [ ] **Step 2: Run it and watch it fail**

```bash
ARCIUM_JOBS=4 scripts/build dev arcium_unittests
```

Expected: the build fails with `'arcium/ui/sidebar/pill_domain.h' file not found`. That is the failure; do not proceed until you have seen it.

- [ ] **Step 3: Write the header**

Create `arcium/ui/sidebar/pill_domain.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_PILL_DOMAIN_H_
#define ARCIUM_UI_SIDEBAR_PILL_DOMAIN_H_

#include <string>

class GURL;

namespace arcium {

// What the address pill shows at rest: the host of `url`, with a leading
// "www." dropped, and the port kept when there is one.
//
// Deliberately not the registrable domain. That would put "google.com" in the
// bar while the user is on mail.google.com, and the one job this text has is
// saying where you are. Punycode is left as punycode for the same reason: a
// host that renders as a Latin lookalike is exactly the one worth seeing
// spelled out.
//
// A URL that is not a website -- a new tab, a settings page, a file -- has no
// domain to show and comes back empty. The pill draws its own label then.
std::u16string PillDomain(const GURL& url);

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_PILL_DOMAIN_H_
```

- [ ] **Step 4: Write the implementation**

Create `arcium/ui/sidebar/pill_domain.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/pill_domain.h"

#include <string_view>

#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "url/gurl.h"

namespace arcium {

namespace {
constexpr std::string_view kWww = "www.";
}  // namespace

std::u16string PillDomain(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || !url.has_host()) {
    return std::u16string();
  }
  std::string_view host = url.host_piece();
  // Only when what is left is still a name with a dot in it. "www.com" keeps
  // its prefix, because "com" is not a place.
  if (host.starts_with(kWww) &&
      host.find('.', kWww.size()) != std::string_view::npos) {
    host.remove_prefix(kWww.size());
  }
  if (url.has_port()) {
    return base::UTF8ToUTF16(base::StrCat({host, ":", url.port_piece()}));
  }
  return base::UTF8ToUTF16(host);
}

}  // namespace arcium
```

- [ ] **Step 5: Wire the new sources into GN**

In `arcium/ui/sidebar/BUILD.gn`, add to the `sidebar` target's `sources`, in alphabetical order among the existing entries:

```
    "pill_domain.cc",
    "pill_domain.h",
```

In `arcium/test/BUILD.gn`, add to `arcium_unittests`'s `sources`, in alphabetical order:

```
    "pill_domain_unittest.cc",
```

- [ ] **Step 6: Run the test and watch it pass**

```bash
ARCIUM_JOBS=4 scripts/build dev arcium_unittests && out/dev/arcium_unittests --gtest_filter='PillDomainTest.*'
```

Expected: 5 tests, all PASSED.

- [ ] **Step 7: Make the pill take a URL**

In `arcium/ui/sidebar/url_pill_view.h`, replace `SetPlaceholderText` with:

```cpp
  // The address of the tab on screen. An empty or non-website URL leaves the
  // pill showing its own label.
  void SetUrl(const GURL& url);
  const std::u16string& domain_for_testing() const { return domain_; }
```

and add `#include "url/gurl.h"` plus a `std::u16string domain_;` member beside `placeholder_`.

In `arcium/ui/sidebar/url_pill_view.cc`, replace `SetPlaceholderText` with:

```cpp
void UrlPillView::SetUrl(const GURL& url) {
  domain_ = PillDomain(url);
  placeholder_->SetText(domain_.empty() ? u"Search or enter address" : domain_);
  placeholder_->SetEnabledColor(domain_.empty()
                                    ? kColorArciumRowTextSecondary
                                    : kColorArciumRowText);
}
```

adding `#include "arcium/ui/sidebar/pill_domain.h"`.

- [ ] **Step 8: Update the one caller**

In `arcium/ui/sidebar/sidebar_view.cc`, the block near the end of `OnSidebarModelChanged` that reads `url_pill_->SetPlaceholderText(base::UTF8ToUTF16(row.url.host()))` becomes `url_pill_->SetUrl(row.url)`. Remove the now-unused `base::UTF8ToUTF16` include if nothing else in the file uses it.

- [ ] **Step 9: Run the whole unit suite**

```bash
out/dev/arcium_unittests
```

Expected: every test passes, including the existing sidebar view tests that drive the pill.

- [ ] **Step 10: Format and commit**

```bash
scripts/format arcium/ui/sidebar/pill_domain.h arcium/ui/sidebar/pill_domain.cc arcium/ui/sidebar/url_pill_view.h arcium/ui/sidebar/url_pill_view.cc arcium/ui/sidebar/sidebar_view.cc arcium/test/pill_domain_unittest.cc
git add arcium/ui/sidebar/pill_domain.h arcium/ui/sidebar/pill_domain.cc arcium/ui/sidebar/url_pill_view.h arcium/ui/sidebar/url_pill_view.cc arcium/ui/sidebar/sidebar_view.cc arcium/ui/sidebar/BUILD.gn arcium/test/pill_domain_unittest.cc arcium/test/BUILD.gn
git commit -F - <<'MSG'
Show where you are in the pill, not the whole address

The pill carried whatever text it was handed. It now carries the host with a
leading www. dropped, and keeps a subdomain: showing the registrable domain
would say google.com while the reader is on mail.google.com, which is the one
thing this line must never do. Punycode stays punycode for the same reason.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

### Task 2: The pill's three buttons, and when they show

**Files:**
- Modify: `arcium/ui/sidebar/url_pill_view.h`, `arcium/ui/sidebar/url_pill_view.cc`
- Modify: `arcium/ui/sidebar/sidebar_metrics.h`
- Modify: `arcium/ui/sidebar/sidebar_view.h`, `arcium/ui/sidebar/sidebar_view.cc`
- Create: `arcium/test/url_pill_unittest.cc`
- Modify: `arcium/test/BUILD.gn`

**Interfaces:**
- Consumes: `arcium::PillDomain`, `UrlPillView::SetUrl` (Task 1).
- Produces:
  - `struct UrlPillView::Actions { base::RepeatingClosure open_box, open_extensions, copy_link, open_site_info; }`
  - `explicit UrlPillView(Actions actions)` — replaces the old `UrlPillView(base::RepeatingClosure)`
  - `void UrlPillView::SetConnectionSecure(bool secure)`
  - `void UrlPillView::SetHostedBarSpeaking(bool speaking)`
  - `views::ImageButton* UrlPillView::site_button_for_testing()`, `extensions_button_for_testing()`, `copy_button_for_testing()`
  - `static base::AutoReset<bool> UrlPillView::DisableRevealAnimationForTesting()`
  - `SidebarView::Delegate` gains `open_extensions`, `copy_link`, `open_site_info`, and `edit_url` keeps its meaning: open the box.

**Context an implementer needs.** The pill hosts Chromium's real location bar behind its own text (Task 3 makes that bar draw nothing). The bar is still a live view with a permission chip in it, so the pill must get out of the way when the bar has something to say — that is what `SetHostedBarSpeaking` is for, and it is why the reveal rules below have a fourth state. Icons come from `//components/vector_icons`, already a dependency of this target: `vector_icons::kExtensionFilledIcon`, `kContentCopyIcon`, `kLockIcon`, `kWarningIcon`.

- [ ] **Step 1: Write the failing test**

Create `arcium/test/url_pill_unittest.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/url_pill_view.h"

#include <memory>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class UrlPillTest : public views::ViewsTestBase {
 public:
  void SetUp() override {
    views::ViewsTestBase::SetUp();
    widget_ = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    UrlPillView::Actions actions;
    actions.open_box = base::BindRepeating(&UrlPillTest::Count,
                                           base::Unretained(this), &boxes_);
    actions.open_extensions = base::BindRepeating(
        &UrlPillTest::Count, base::Unretained(this), &extensions_);
    actions.copy_link = base::BindRepeating(&UrlPillTest::Count,
                                            base::Unretained(this), &copies_);
    actions.open_site_info = base::BindRepeating(
        &UrlPillTest::Count, base::Unretained(this), &site_infos_);
    pill_ = widget_->SetContentsView(
        std::make_unique<UrlPillView>(std::move(actions)));
    widget_->Show();
  }

  void TearDown() override {
    pill_ = nullptr;
    widget_.reset();
    views::ViewsTestBase::TearDown();
  }

 protected:
  void Count(int* counter) { ++*counter; }

  std::unique_ptr<views::Widget> widget_;
  raw_ptr<UrlPillView> pill_ = nullptr;
  int boxes_ = 0;
  int extensions_ = 0;
  int copies_ = 0;
  int site_infos_ = 0;
  base::AutoReset<bool> no_animation_ =
      UrlPillView::DisableRevealAnimationForTesting();
};

TEST_F(UrlPillTest, AtRestItIsTextAndNothingElse) {
  pill_->SetUrl(GURL("https://www.google.com/"));
  pill_->SetConnectionSecure(true);
  EXPECT_EQ(u"google.com", pill_->domain_for_testing());
  EXPECT_FALSE(pill_->site_button_for_testing()->GetVisible());
  EXPECT_FALSE(pill_->extensions_button_for_testing()->GetVisible());
  EXPECT_FALSE(pill_->copy_button_for_testing()->GetVisible());
}

TEST_F(UrlPillTest, HoveringRevealsAllThree) {
  pill_->SetUrl(GURL("https://www.google.com/"));
  pill_->SetConnectionSecure(true);
  pill_->SetRevealedForTesting(true);
  EXPECT_TRUE(pill_->site_button_for_testing()->GetVisible());
  EXPECT_TRUE(pill_->extensions_button_for_testing()->GetVisible());
  EXPECT_TRUE(pill_->copy_button_for_testing()->GetVisible());

  // And they go again. Without this the test would pass against a pill that
  // reveals once and never hides.
  pill_->SetRevealedForTesting(false);
  EXPECT_FALSE(pill_->extensions_button_for_testing()->GetVisible());
}

TEST_F(UrlPillTest, KeyboardFocusRevealsThemToo) {
  pill_->SetUrl(GURL("https://www.google.com/"));
  ASSERT_FALSE(pill_->extensions_button_for_testing()->GetVisible());
  // A control a pointer is the only way to reach is a control some people
  // cannot reach at all.
  pill_->RequestFocus();
  EXPECT_TRUE(pill_->extensions_button_for_testing()->GetVisible());
}

TEST_F(UrlPillTest, AnInsecureConnectionSaysSoWithoutBeingAskedTo) {
  pill_->SetUrl(GURL("http://example.com/"));
  pill_->SetConnectionSecure(false);
  EXPECT_TRUE(pill_->site_button_for_testing()->GetVisible());
  // Only that one: the other two still wait to be hovered.
  EXPECT_FALSE(pill_->extensions_button_for_testing()->GetVisible());
}

TEST_F(UrlPillTest, WhenTheBarHasSomethingToSayThePillGetsOutOfTheWay) {
  pill_->SetUrl(GURL("http://example.com/"));
  pill_->SetConnectionSecure(false);
  pill_->SetRevealedForTesting(true);
  pill_->SetHostedBarSpeaking(true);
  EXPECT_FALSE(pill_->site_button_for_testing()->GetVisible());
  EXPECT_FALSE(pill_->extensions_button_for_testing()->GetVisible());
  EXPECT_FALSE(pill_->copy_button_for_testing()->GetVisible());

  pill_->SetHostedBarSpeaking(false);
  EXPECT_TRUE(pill_->extensions_button_for_testing()->GetVisible());
}

TEST_F(UrlPillTest, EachButtonDoesItsOwnJob) {
  pill_->SetUrl(GURL("https://www.google.com/"));
  pill_->SetRevealedForTesting(true);
  views::test::ButtonTestApi(pill_->site_button_for_testing())
      .NotifyClick(ui::test::TestEvent());
  views::test::ButtonTestApi(pill_->extensions_button_for_testing())
      .NotifyClick(ui::test::TestEvent());
  views::test::ButtonTestApi(pill_->copy_button_for_testing())
      .NotifyClick(ui::test::TestEvent());
  EXPECT_EQ(1, site_infos_);
  EXPECT_EQ(1, extensions_);
  EXPECT_EQ(1, copies_);
  EXPECT_EQ(0, boxes_);
}

}  // namespace
}  // namespace arcium
```

Add `#include "ui/views/test/button_test_api.h"` and `#include "ui/events/test/test_event.h"` to the includes above.

- [ ] **Step 2: Run it and watch it fail**

```bash
ARCIUM_JOBS=4 scripts/build dev arcium_unittests
```

Expected: a compile failure on `UrlPillView::Actions`. That is the failure to see.

- [ ] **Step 3: Add the metrics**

In `arcium/ui/sidebar/sidebar_metrics.h`, beside `kUrlPillHeight`:

```cpp
// The buttons inside the URL pill: small enough that three of them plus the
// domain fit a 250px sidebar without the text eliding on a normal host.
inline constexpr int kPillButtonSize = 20;
inline constexpr int kPillButtonGap = 2;
inline constexpr int kPillIconSize = 14;
// Zen's own stylesheet fades these over 150ms. Copied rather than guessed.
inline constexpr int kPillRevealMs = 150;
```

- [ ] **Step 4: Rewrite the pill's header**

`arcium/ui/sidebar/url_pill_view.h` becomes:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_URL_PILL_VIEW_H_
#define ARCIUM_UI_SIDEBAR_URL_PILL_VIEW_H_

#include <memory>
#include <string>

#include "base/auto_reset.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "url/gurl.h"

namespace views {
class ImageButton;
class Label;
}  // namespace views

namespace arcium {

// Row 2: the address pill. It shows where you are and nothing else until you
// reach for it, and it hosts Chromium's real location bar behind its own text
// -- not to draw it, but because Chrome's password bubbles, page information
// and permission chips all reach for that view and there is nowhere else in
// this window for them to point.
class UrlPillView : public views::View, public views::FocusChangeListener {
  METADATA_HEADER(UrlPillView, views::View)

 public:
  // What the pill cannot do itself. All four are window-level.
  struct Actions {
    base::RepeatingClosure open_box;
    base::RepeatingClosure open_extensions;
    base::RepeatingClosure copy_link;
    base::RepeatingClosure open_site_info;
  };

  explicit UrlPillView(Actions actions);
  UrlPillView(const UrlPillView&) = delete;
  UrlPillView& operator=(const UrlPillView&) = delete;
  ~UrlPillView() override;

  // The address of the tab on screen.
  void SetUrl(const GURL& url);
  // False draws the site button as a warning and shows it without hovering.
  void SetConnectionSecure(bool secure);
  // True while the hosted bar has something of its own to show -- a permission
  // request. The pill then shows nothing at all, so what the bar is asking is
  // both visible and clickable.
  void SetHostedBarSpeaking(bool speaking);

  // Replaces the placeholder with `view`, which fills the pill behind the
  // pill's own text.
  views::View* SetHostedView(std::unique_ptr<views::View> view);
  bool has_hosted_view() const { return hosted_ != nullptr; }

  const std::u16string& domain_for_testing() const { return domain_; }
  views::ImageButton* site_button_for_testing() { return site_; }
  views::ImageButton* extensions_button_for_testing() { return extensions_; }
  views::ImageButton* copy_button_for_testing() { return copy_; }
  void SetRevealedForTesting(bool revealed);
  static base::AutoReset<bool> DisableRevealAnimationForTesting();

  // views::View:
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void OnThemeChanged() override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

  // views::FocusChangeListener. Keyboard focus reveals the buttons on the same
  // terms as hover, because a control only a pointer can reach is not a
  // control everyone can reach.
  void OnDidChangeFocus(views::View* before, views::View* now) override;

 private:
  views::ImageButton* AddButton(base::RepeatingClosure action,
                                const std::u16string& tooltip);
  // Recomputes all three buttons from `revealed_`, `secure_` and `speaking_`.
  void UpdateButtons();
  void SetButtonShown(views::ImageButton* button, bool shown);
  void SetRevealed(bool revealed);
  void RefreshIcons();

  Actions actions_;
  raw_ptr<views::Label> text_ = nullptr;
  raw_ptr<views::ImageButton> site_ = nullptr;
  raw_ptr<views::ImageButton> extensions_ = nullptr;
  raw_ptr<views::ImageButton> copy_ = nullptr;
  raw_ptr<views::View> hosted_ = nullptr;
  std::u16string domain_;
  bool secure_ = true;
  bool revealed_ = false;
  bool speaking_ = false;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_URL_PILL_VIEW_H_
```

- [ ] **Step 5: Write the implementation**

Replace `arcium/ui/sidebar/url_pill_view.cc`. The parts that carry the decisions:

```cpp
namespace {
bool g_animate_reveal = true;
}  // namespace

UrlPillView::UrlPillView(Actions actions) : actions_(std::move(actions)) {
  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
      .SetInteriorMargin(gfx::Insets::VH(0, metrics::kPillButtonGap))
      .SetDefault(views::kMarginsKey,
                  gfx::Insets::VH(0, metrics::kPillButtonGap));

  site_ = AddButton(actions_.open_site_info, u"Site information");

  auto text = std::make_unique<views::Label>();
  text->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  text->SetElideBehavior(gfx::ELIDE_TAIL);
  text->SetProperty(views::kFlexBehaviorKey,
                    views::FlexSpecification(
                        views::MinimumFlexSizeRule::kScaleToZero,
                        views::MaximumFlexSizeRule::kUnbounded));
  text_ = AddChildView(std::move(text));

  extensions_ = AddButton(actions_.open_extensions, u"Extensions");
  copy_ = AddButton(actions_.copy_link, u"Copy link");
  UpdateButtons();
}

void UrlPillView::SetConnectionSecure(bool secure) {
  if (secure == secure_) {
    return;
  }
  secure_ = secure;
  RefreshIcons();
  UpdateButtons();
}

void UrlPillView::SetHostedBarSpeaking(bool speaking) {
  if (speaking == speaking_) {
    return;
  }
  speaking_ = speaking;
  // The pill's own text goes too, not only its buttons: the bar draws its
  // question where this text sits, and two lines of writing in one pill is
  // nobody's idea of a question.
  text_->SetVisible(!speaking_);
  UpdateButtons();
}

void UrlPillView::UpdateButtons() {
  // A bar with something to say owns the whole pill.
  if (speaking_) {
    SetButtonShown(site_, false);
    SetButtonShown(extensions_, false);
    SetButtonShown(copy_, false);
    return;
  }
  // The warning is the one thing here whose whole value is being seen
  // unasked, so it does not wait for a pointer.
  SetButtonShown(site_, revealed_ || !secure_);
  SetButtonShown(extensions_, revealed_);
  SetButtonShown(copy_, revealed_);
}

void UrlPillView::SetButtonShown(views::ImageButton* button, bool shown) {
  if (button->GetVisible() == shown) {
    return;
  }
  if (!g_animate_reveal) {
    button->SetVisible(shown);
    return;
  }
  if (shown) {
    button->SetVisible(true);
    button->layer()->SetOpacity(0.f);
  }
  views::AnimationBuilder()
      .SetPreemptionStrategy(
          ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET)
      .OnEnded(base::BindOnce(
          [](base::WeakPtr<views::ImageButton> b, bool shown) {
            if (b && !shown) {
              b->SetVisible(false);
            }
          },
          button->GetWeakPtr(), shown))
      .Once()
      .SetDuration(base::Milliseconds(metrics::kPillRevealMs))
      .SetOpacity(button, shown ? 1.f : 0.f, gfx::Tween::LINEAR);
}

bool UrlPillView::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsOnlyLeftMouseButton()) {
    actions_.open_box.Run();
    return true;
  }
  return views::View::OnMousePressed(event);
}

void UrlPillView::OnDidChangeFocus(views::View* before, views::View* now) {
  SetRevealed(IsMouseHovered() || (now && Contains(now)));
}

views::View* UrlPillView::SetHostedView(std::unique_ptr<views::View> view) {
  // Added at the front so the pill's own text and buttons hit-test first: the
  // bar behind it is there to be pointed at by bubbles, not clicked.
  hosted_ = AddChildViewAt(std::move(view), 0);
  hosted_->SetProperty(views::kViewIgnoredByLayoutKey, true);
  return hosted_;
}
```

`AddButton` creates a `views::ImageButton` sized `kPillButtonSize`, calls `SetPaintToLayer()` and `layer()->SetFillsBoundsOpaquely(false)` so the fade has a layer to animate, sets the tooltip, and starts hidden. `RefreshIcons` sets the three images: `vector_icons::kLockIcon` or `kWarningIcon` for `site_` depending on `secure_`, `kExtensionFilledIcon` for `extensions_`, `kContentCopyIcon` for `copy_`, each at `metrics::kPillIconSize` in `kColorArciumRowText`. `OnMouseEntered`/`OnMouseExited` call `SetRevealed(IsMouseHovered())`. `AddedToWidget`/`RemovedFromWidget` add and remove `this` as a focus change listener on `GetFocusManager()`. `SetHostedView` no longer hides the text, because the hosted bar draws nothing.

Note the hosted view is marked ignored by layout and must be given the pill's full local bounds in `Layout`; add a `Layout(PassKey)` override that calls the base and then `hosted_->SetBoundsRect(GetLocalBounds())`.

- [ ] **Step 6: Widen the sidebar's delegate**

In `arcium/ui/sidebar/sidebar_view.h`, `SidebarView::Delegate` gains three closures beside `edit_url`:

```cpp
    // Clicking the URL pill: open the command box.
    base::RepeatingClosure edit_url;
    // The pill's own buttons.
    base::RepeatingClosure open_extensions;
    base::RepeatingClosure copy_link;
    base::RepeatingClosure open_site_info;
```

In `arcium/ui/sidebar/sidebar_view.cc`, build the pill from them:

```cpp
  UrlPillView::Actions pill_actions;
  pill_actions.open_box = delegate_.edit_url;
  pill_actions.open_extensions = delegate_.open_extensions;
  pill_actions.copy_link = delegate_.copy_link;
  pill_actions.open_site_info = delegate_.open_site_info;
  url_pill_ = AddChildView(std::make_unique<UrlPillView>(std::move(pill_actions)));
```

Every existing construction site of `SidebarView::Delegate` — the playground and the browser controller — must fill the three new fields. In the playground, pass `base::DoNothing()` for all three.

- [ ] **Step 7: Run the tests and watch them pass**

```bash
ARCIUM_JOBS=4 scripts/build dev arcium_unittests && out/dev/arcium_unittests --gtest_filter='UrlPillTest.*:PillDomainTest.*'
```

Expected: all pass. Then run the whole binary — the existing sidebar view tests construct a `Delegate` and must still build and pass:

```bash
out/dev/arcium_unittests
```

- [ ] **Step 8: Format and commit**

```bash
scripts/format arcium/ui/sidebar/url_pill_view.h arcium/ui/sidebar/url_pill_view.cc arcium/ui/sidebar/sidebar_view.h arcium/ui/sidebar/sidebar_view.cc arcium/ui/sidebar/sidebar_metrics.h arcium/test/url_pill_unittest.cc
git add arcium/ui/sidebar/url_pill_view.h arcium/ui/sidebar/url_pill_view.cc arcium/ui/sidebar/sidebar_view.h arcium/ui/sidebar/sidebar_view.cc arcium/ui/sidebar/sidebar_metrics.h arcium/test/url_pill_unittest.cc arcium/test/BUILD.gn
git commit -F - <<'MSG'
Give the pill its own three buttons, and hide them until they are wanted

Site information, extensions and copy, revealed by hover or by keyboard focus
so a pointer is not the only way in. The warning for a connection that is not
secure is the exception and shows unasked, because an affordance nobody sees
is a click saved and a warning nobody sees is the warning gone.

A permission request still comes from the bar behind the pill, and it needs
both the room and the clicks, so the pill shows nothing at all while that bar
has something to say.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

### Task 3: The hosted address bar stops drawing

**Files:**
- Create: `arcium/ui/browser/hosted_location_bar.h`, `arcium/ui/browser/hosted_location_bar.cc`
- Create: `patches/0200-location-bar-hosted.patch`
- Create: `arcium/test/browser/sidebar_ui_browsertest_base.h`, `arcium/test/browser/sidebar_ui_browsertest_base.cc` (the shared fixture above)
- Create: `arcium/test/browser/pill_browsertest.cc`
- Modify: `arcium/ui/browser/BUILD.gn`, `arcium/test/BUILD.gn`

**Interfaces:**
- Consumes: `UrlPillView::SetHostedBarSpeaking` (Task 2).
- Produces:
  - `bool arcium::IsLocationBarHosted(const views::View* bar)` — true when the bar sits inside a `UrlPillView`.
  - `void arcium::OnHostedLocationBarLaidOut(views::View* bar)` — hides what the pill draws itself, keeps the permission chip, and tells the pill whether the bar is speaking.

**Context an implementer needs.** Arcium moves Chromium's `LocationBarView` into the sidebar's pill (`BrowserSidebarController::HostLocationBar`). Everything it draws — background, border, location icon, omnibox text, page action icons — now sits under the pill's own text and must stop. Two things must not stop: the view must stay drawn, because `ToolbarView::GetBubbleAnchor` returns it and falls back to a container at the wrong end of the window when it is not; and its permission chip must still appear, because `PermissionPromptChip` does not merely anchor to the bar, it *is* a chip the bar owns. The chip is whichever child is a `PermissionDashboardView` or a `PermissionChipView`.

- [ ] **Step 1: Write the failing browser test**

Create `arcium/test/browser/pill_browsertest.cc` with a fixture following `arcium/test/browser/profile_browsertest_base.h`, and this first case:

```cpp
IN_PROC_BROWSER_TEST_F(PillTest, TheBarBehindThePillDrawsNothingOfItsOwn) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));

  LocationBarView* bar = BrowserView::GetBrowserViewForBrowser(browser())
                             ->GetLocationBarView();
  ASSERT_TRUE(bar);
  // Still drawn: Chrome's password and page information bubbles anchor here,
  // and a bar that is not drawn sends them to the top of the window.
  EXPECT_TRUE(bar->IsDrawn());
  EXPECT_EQ(nullptr, bar->GetBackground());
  for (views::View* child : bar->children()) {
    EXPECT_FALSE(child->GetVisible())
        << "a hosted bar shows nothing at rest: " << child->GetClassName();
  }
}
```

- [ ] **Step 2: Run it and watch it fail**

Check the machine is free first — browser tests open real windows:

```bash
pgrep -f siso; pgrep -f "Arcium.app/Contents/MacOS/Arcium"
ARCIUM_JOBS=4 scripts/build dev arcium_browsertests
out/dev/arcium_browsertests --gtest_filter='PillTest.TheBarBehindThePillDrawsNothingOfItsOwn' --test-launcher-timeout=300000 --ui-test-action-timeout=120000
```

Expected: FAIL, with a non-null background and several visible children.

- [ ] **Step 3: Write the hook**

Create `arcium/ui/browser/hosted_location_bar.h`:

```cpp
#ifndef ARCIUM_UI_BROWSER_HOSTED_LOCATION_BAR_H_
#define ARCIUM_UI_BROWSER_HOSTED_LOCATION_BAR_H_

namespace views {
class View;
}

namespace arcium {

// True while `bar` sits inside the sidebar's address pill.
//
// The pill draws the address itself, so the bar behind it draws nothing. It
// is still there, and still drawn, for two reasons it cannot delegate:
// Chrome's password and page information bubbles find their anchor through
// it, and a permission request is a chip it owns outright.
bool IsLocationBarHosted(const views::View* bar);

// Called when a hosted bar has just laid out. Hides every child the pill
// draws itself, leaves the permission chip alone, and tells the pill whether
// that chip has something to say.
void OnHostedLocationBarLaidOut(views::View* bar);

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_HOSTED_LOCATION_BAR_H_
```

`hosted_location_bar.cc` walks up from `bar` with `views::AsViewClass<UrlPillView>` to answer `IsLocationBarHosted`, and in `OnHostedLocationBarLaidOut`:

```cpp
void OnHostedLocationBarLaidOut(views::View* bar) {
  UrlPillView* pill = PillFor(bar);
  if (!pill) {
    return;
  }
  bool speaking = false;
  for (views::View* child : bar->children()) {
    const bool is_chip = views::IsViewClass<PermissionDashboardView>(child) ||
                         views::IsViewClass<PermissionChipView>(child);
    if (!is_chip) {
      // Hidden rather than removed: this runs on every layout, and the bar
      // rebuilds and re-shows its own children whenever the page changes.
      child->SetVisible(false);
      continue;
    }
    speaking = speaking || child->GetVisible();
  }
  pill->SetHostedBarSpeaking(speaking);
}
```

- [ ] **Step 4: Make the patch**

Edit three places in `/Volumes/Texternal/chromium/src/chrome/browser/ui/views/location_bar/location_bar_view.cc`:

- at the top of `LocationBarView::OnPaintBorder`, `if (arcium::IsLocationBarHosted(this)) { return; }`
- in `RefreshBackground`, where it calls `SetBackground(...)`, wrap both calls in the same guard and call `SetBackground(nullptr)` instead when hosted
- at the end of `LocationBarView::Layout`, `arcium::OnHostedLocationBarLaidOut(this);`

plus the include. Then generate the patch — never write it by hand:

```bash
git -C /Volumes/Texternal/chromium/src diff -- chrome/browser/ui/views/location_bar/location_bar_view.cc > /tmp/lb.diff
```

Create `patches/0200-location-bar-hosted.patch` with this header followed by the contents of that diff:

```
Seam: LocationBarView::OnPaintBorder, RefreshBackground, Layout.
Why: the sidebar's pill draws the address itself, so the bar hosted behind it
     draws nothing -- while staying present and drawn, because Chrome's
     password and page information bubbles anchor to it and a permission
     request is a chip it owns.
Delegates to: arcium::IsLocationBarHosted, arcium::OnHostedLocationBarLaidOut
```

- [ ] **Step 5: Sync and rebuild**

```bash
ls -l /Volumes/Texternal/chromium/src/arcium   # must point at this worktree
scripts/sync && scripts/sync
ARCIUM_JOBS=4 scripts/build dev arcium_browsertests
```

The second `scripts/sync` must report every patch applied or skipped, with no conflict.

- [ ] **Step 6: Add the second test, the one that proves the chip survives**

Everything above asserts that things are hidden, which is also what a hook that hid the whole bar would report. This is the control:

```cpp
IN_PROC_BROWSER_TEST_F(PillTest, APermissionRequestStillHasSomewhereToAppear) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  permissions::PermissionRequestManager* manager =
      permissions::PermissionRequestManager::FromWebContents(
          browser()->tab_strip_model()->GetActiveWebContents());
  permissions::MockPermissionRequest request(
      permissions::RequestType::kGeolocation);
  manager->AddRequest(
      browser()->tab_strip_model()->GetActiveWebContents()->GetPrimaryMainFrame(),
      &request);
  base::RunLoop().RunUntilIdle();

  LocationBarView* bar = BrowserView::GetBrowserViewForBrowser(browser())
                             ->GetLocationBarView();
  bool chip_visible = false;
  for (views::View* child : bar->children()) {
    chip_visible = chip_visible || child->GetVisible();
  }
  EXPECT_TRUE(chip_visible) << "the request has nowhere to appear";
  // And the pill stands aside for it.
  EXPECT_FALSE(Pill()->extensions_button_for_testing()->GetVisible());
}
```

- [ ] **Step 7: Run both and watch them pass**

```bash
out/dev/arcium_browsertests --gtest_filter='PillTest.*' --test-launcher-timeout=300000 --ui-test-action-timeout=120000
```

- [ ] **Step 8: Format and commit**

```bash
scripts/format arcium/ui/browser/hosted_location_bar.h arcium/ui/browser/hosted_location_bar.cc arcium/test/browser/pill_browsertest.cc
git add arcium/ui/browser/hosted_location_bar.h arcium/ui/browser/hosted_location_bar.cc arcium/ui/browser/BUILD.gn arcium/test/browser/sidebar_ui_browsertest_base.h arcium/test/browser/sidebar_ui_browsertest_base.cc arcium/test/browser/pill_browsertest.cc arcium/test/BUILD.gn patches/0200-location-bar-hosted.patch
git commit -F - <<'MSG'
Quieten the address bar the pill is drawn over, without taking it away

The pill draws the address itself, so everything the bar behind it drew was a
second copy under the first. Taking the bar out instead was the tempting fix
and the wrong one: Chrome's password and page information bubbles find their
anchor through that view, and a permission request is not merely anchored to
it but is a chip it owns, which refuses to appear when the bar is not there.

So it stays, drawn and silent, and the pill stands aside whenever the chip has
something to ask.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

### Task 4: The pill's four actions, wired to the window

**Files:**
- Modify: `arcium/ui/browser/browser_sidebar_controller.h`, `arcium/ui/browser/browser_sidebar_controller.cc`
- Modify: `arcium/ui/browser/BUILD.gn` (add `//components/security_state/content`, `//chrome/browser/ui/page_info` is reached through the circular-includes allowance)
- Modify: `arcium/test/browser/pill_browsertest.cc`

**Interfaces:**
- Consumes: `UrlPillView::Actions`, `SetUrl`, `SetConnectionSecure` (Task 2); `SidebarView::Delegate`'s three new closures.
- Produces: nothing new for later tasks.

**A deviation from the spec's test list, on purpose.** The spec asks for a unit
test of "whether a URL counts as not secure". There is no URL-shaped answer to
that: the judgement belongs to `SecurityStateTabHelper`, which needs a real
navigation and a real certificate state to have an opinion. The browser tests
below cover it instead, at both ends — a plain `http` page and an internal
page — and the spec's test list should be read as satisfied by them.

**Context an implementer needs.** The controller already builds `SidebarView::Delegate`. It gains four fillings. Security comes from `SecurityStateTabHelper::FromWebContents(contents)->GetSecurityLevel()` in `//components/security_state/content`; treat `security_state::WARNING` and `DANGEROUS` as not secure and everything else as secure, so an internal page does not wear a warning. The controller becomes a `content::WebContentsObserver` on the active tab so the mark follows a page that turns insecure mid-life, not only a navigation.

- [ ] **Step 1: Write the failing tests**

Add to `arcium/test/browser/pill_browsertest.cc`:

```cpp
IN_PROC_BROWSER_TEST_F(PillTest, ItSaysWhereYouAre) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("a.test", "/title1.html")));
  EXPECT_EQ(u"a.test", Pill()->domain_for_testing());
}

IN_PROC_BROWSER_TEST_F(PillTest, AnInsecurePageWearsItsWarningUnasked) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("a.test", "/title1.html")));
  // The embedded server is plain http, which is what a warning is for.
  EXPECT_TRUE(Pill()->site_button_for_testing()->GetVisible());

  // The control: an internal page is not a website and must not be accused.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("chrome://version")));
  EXPECT_FALSE(Pill()->site_button_for_testing()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(PillTest, CopyPutsTheWholeAddressOnTheClipboard) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL url = embedded_test_server()->GetURL("a.test", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  Pill()->SetRevealedForTesting(true);
  views::test::ButtonTestApi(Pill()->copy_button_for_testing())
      .NotifyClick(ui::test::TestEvent());
  std::u16string clipboard;
  ui::Clipboard::GetForCurrentThread()->ReadText(
      ui::ClipboardBuffer::kCopyPaste, /*data_dst=*/nullptr, &clipboard);
  // The pill shows a domain; copy gives the address, which is the point of
  // having both.
  EXPECT_EQ(base::UTF8ToUTF16(url.spec()), clipboard);
}

IN_PROC_BROWSER_TEST_F(PillTest, TheSiteButtonOpensPageInformation) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("a.test", "/title1.html")));
  Pill()->SetRevealedForTesting(true);
  views::test::ButtonTestApi(Pill()->site_button_for_testing())
      .NotifyClick(ui::test::TestEvent());
  EXPECT_TRUE(PageInfoBubbleView::GetPageInfoBubbleForTesting());
}
```

- [ ] **Step 2: Run them and watch them fail**

```bash
ARCIUM_JOBS=4 scripts/build dev arcium_browsertests
out/dev/arcium_browsertests --gtest_filter='PillTest.*' --test-launcher-timeout=300000 --ui-test-action-timeout=120000
```

Expected: the four new cases fail; the two from Task 3 still pass.

- [ ] **Step 3: Fill the delegate**

In `BrowserSidebarController`'s constructor, where `SidebarView::Delegate` is built:

```cpp
  sidebar_delegate.edit_url = base::BindRepeating(
      &BrowserSidebarController::ShowCommandBox, weak_factory_.GetWeakPtr());
  sidebar_delegate.open_extensions = base::BindRepeating(
      &BrowserSidebarController::OpenExtensionsMenu, weak_factory_.GetWeakPtr());
  sidebar_delegate.copy_link = base::BindRepeating(
      &BrowserSidebarController::CopyCurrentUrl, weak_factory_.GetWeakPtr());
  sidebar_delegate.open_site_info = base::BindRepeating(
      &BrowserSidebarController::ShowSiteInfo, weak_factory_.GetWeakPtr());
```

(`ShowCommandBox` is `ShowQuickEntry` renamed in Task 7; until then bind the existing method.)

- [ ] **Step 4: Implement the three new actions**

```cpp
void BrowserSidebarController::OpenExtensionsMenu() {
  ToolbarView* toolbar = browser_view_->toolbar();
  if (!toolbar || !toolbar->extensions_container()) {
    return;
  }
  toolbar->extensions_container()->GetExtensionsButton()->ToggleExtensionsMenu();
}

void BrowserSidebarController::CopyCurrentUrl() {
  content::WebContents* contents =
      browser_view_->browser()->tab_strip_model()->GetActiveWebContents();
  if (!contents) {
    return;
  }
  ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste)
      .WriteText(base::UTF8ToUTF16(contents->GetLastCommittedURL().spec()));
}

void BrowserSidebarController::ShowSiteInfo() {
  content::WebContents* contents =
      browser_view_->browser()->tab_strip_model()->GetActiveWebContents();
  if (!contents) {
    return;
  }
  ShowPageInfoDialog(contents, base::DoNothing(),
                     bubble_anchor_util::Anchor::kLocationBar);
}
```

- [ ] **Step 5: Follow the page's security**

Make `BrowserSidebarController` a `content::WebContentsObserver`. Re-`Observe` the active contents whenever the model changes, and override:

```cpp
void BrowserSidebarController::DidChangeVisibleSecurityState() {
  UpdatePillSecurity();
}

void BrowserSidebarController::PrimaryPageChanged(content::Page& page) {
  UpdatePillSecurity();
}

void BrowserSidebarController::UpdatePillSecurity() {
  content::WebContents* contents = web_contents();
  auto* helper =
      contents ? SecurityStateTabHelper::FromWebContents(contents) : nullptr;
  const security_state::SecurityLevel level =
      helper ? helper->GetSecurityLevel() : security_state::NONE;
  // NONE is an internal page or a data URL: neither secure nor an accusation.
  // Only the two levels that exist to be warned about count as insecure.
  const bool insecure = level == security_state::WARNING ||
                        level == security_state::DANGEROUS;
  view_->url_pill()->SetConnectionSecure(!insecure);
}
```

Call `UpdatePillSecurity()` from `OnSidebarModelChanged` too, after the active tab may have changed, and re-`Observe` there.

- [ ] **Step 6: Run and watch them pass**

```bash
out/dev/arcium_browsertests --gtest_filter='PillTest.*' --test-launcher-timeout=300000 --ui-test-action-timeout=120000
```

Expected: all six pass.

- [ ] **Step 7: Format and commit**

```bash
scripts/format arcium/ui/browser/browser_sidebar_controller.h arcium/ui/browser/browser_sidebar_controller.cc arcium/test/browser/pill_browsertest.cc
git add arcium/ui/browser/browser_sidebar_controller.h arcium/ui/browser/browser_sidebar_controller.cc arcium/ui/browser/BUILD.gn arcium/test/browser/pill_browsertest.cc
git commit -F - <<'MSG'
Make the pill's buttons do something

Site information opens Chrome's own page information rather than a panel of
ours: it is a security surface Chromium already writes, maintains and
translates, and rebuilding it would buy nothing a reader could see.

The warning follows the page rather than the navigation, because a page can
turn insecure while it sits there, and only the two security levels that exist
to be warned about count -- an internal page is not a website and must not be
accused of anything.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

### Task 5: A row of extension buttons above the favourites

**Files:**
- Create: `arcium/ui/sidebar/extensions_row_view.h`, `arcium/ui/sidebar/extensions_row_view.cc`
- Create: `patches/0210-extensions-container-autohide.patch`
- Create: `arcium/test/browser/extensions_row_browsertest.cc`
- Modify: `arcium/ui/sidebar/sidebar_view.h`, `arcium/ui/sidebar/sidebar_view.cc`, `arcium/ui/sidebar/sidebar_metrics.h`
- Modify: `arcium/ui/browser/browser_sidebar_controller.h`, `arcium/ui/browser/browser_sidebar_controller.cc`
- Modify: `arcium/ui/sidebar/BUILD.gn`, `arcium/test/BUILD.gn`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:
  - `class arcium::ExtensionsRowView : public views::View` with `views::View* SetHostedView(std::unique_ptr<views::View>)`, `bool has_hosted_view() const`, and a `Layout` that wraps the hosted view's children into lines.
  - `ExtensionsRowView* SidebarView::extensions_row()`.
  - `void BrowserSidebarController::HostExtensionsContainer()`, called once from the same place as `HostLocationBar` (patch 0050).

**Context an implementer needs.** Chromium builds the whole machine already: `ExtensionsToolbarDesktop` holds one `ToolbarActionView` per pinned extension plus a menu button, and `ExtensionsMenuCoordinator` opens the list with its pin and unpin switches — all Views, so nothing here adds a process. The work is placement. The container is reparented out of the hidden toolbar exactly as the location bar is, which keeps `ToolbarView::extensions_container()` valid so extension popups still find their owner. The menu button must not appear in the row: `DisplayMode::kAutoHide` exists for windows that want as few visible icons as possible and keeps that button out while leaving `ToggleExtensionsMenu()` callable, which is what the pill's extensions button uses. `ToolbarView` constructs the container with the default `kNormal`, so one line of it is hooked.

**Why the row wraps itself rather than using the container's own layout.** `ExtensionsToolbarDesktop` lays its icons out in one horizontal line and drops what does not fit. A 250px sidebar holds about seven, and the owner will pin more. `ExtensionsRowView` therefore positions the hosted container's action views itself, in lines.

- [ ] **Step 1: Write the failing browser test**

Create `arcium/test/browser/extensions_row_browsertest.cc`. Load an unpacked test extension with `extensions::ChromeTestExtensionLoader`, pin it through `ToolbarActionsModel::SetActionVisibility`, and assert:

```cpp
IN_PROC_BROWSER_TEST_F(ExtensionsRowTest, APinnedExtensionIsAButtonInTheSidebar) {
  ASSERT_TRUE(Row()->has_hosted_view());
  // Nothing pinned: the row takes no height, so an empty row is not a gap.
  EXPECT_EQ(0, Row()->GetPreferredSize(views::SizeBounds()).height());

  const extensions::ExtensionId id = LoadTestExtension();
  PinExtension(id);
  RunLoopUntilIdle();

  EXPECT_GT(Row()->GetPreferredSize(views::SizeBounds()).height(), 0);
  views::View* button = Container()->GetViewForId(id);
  ASSERT_TRUE(button);
  EXPECT_TRUE(button->GetVisible());
  EXPECT_TRUE(Row()->Contains(button));

  // The menu button is not in the row; the pill's button opens the menu.
  EXPECT_FALSE(Container()->GetExtensionsButton()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(ExtensionsRowTest, ClickingOneOpensItsPopup) {
  const extensions::ExtensionId id = LoadTestExtension();
  PinExtension(id);
  RunLoopUntilIdle();

  ExtensionsToolbarDesktop* container = Container();
  views::View* button = container->GetViewForId(id);
  ASSERT_TRUE(button);
  ExtensionActionTestHelper::Create(browser())->Press(id);
  EXPECT_TRUE(ExtensionActionTestHelper::Create(browser())->HasPopup())
      << "the button draws but does nothing, which is worse than no button";
}

IN_PROC_BROWSER_TEST_F(ExtensionsRowTest, UnpinningTakesItBackOut) {
  const extensions::ExtensionId id = LoadTestExtension();
  PinExtension(id);
  RunLoopUntilIdle();
  ASSERT_GT(Row()->GetPreferredSize(views::SizeBounds()).height(), 0);

  UnpinExtension(id);
  RunLoopUntilIdle();
  EXPECT_EQ(0, Row()->GetPreferredSize(views::SizeBounds()).height());
}

IN_PROC_BROWSER_TEST_F(ExtensionsRowTest, NineOfThemWrapRatherThanBeingClipped) {
  std::vector<extensions::ExtensionId> ids;
  for (int i = 0; i < 9; ++i) {
    ids.push_back(LoadTestExtension());
    PinExtension(ids.back());
  }
  RunLoopUntilIdle();
  Row()->SetSize(gfx::Size(metrics::kSidebarWidth, 200));
  views::test::RunScheduledLayout(Row());

  const int one_line = metrics::kExtensionButtonSize;
  EXPECT_GT(Row()->GetPreferredSize(views::SizeBounds()).height(), one_line)
      << "nine buttons must take more than one line";
  for (const extensions::ExtensionId& id : ids) {
    views::View* button = Container()->GetViewForId(id);
    ASSERT_TRUE(button);
    EXPECT_TRUE(Row()->GetLocalBounds().Contains(button->bounds()))
        << "a pinned extension was clipped out of the row";
  }
}
```

- [ ] **Step 2: Run it and watch it fail**

```bash
pgrep -f siso; pgrep -f "Arcium.app/Contents/MacOS/Arcium"
ARCIUM_JOBS=4 scripts/build dev arcium_browsertests
out/dev/arcium_browsertests --gtest_filter='ExtensionsRowTest.*' --test-launcher-timeout=300000 --ui-test-action-timeout=120000
```

Expected: a compile failure on `ExtensionsRowView`.

- [ ] **Step 3: Add the metrics**

In `arcium/ui/sidebar/sidebar_metrics.h`:

```cpp
// The pinned-extension buttons above the favourites. Smaller than a favourite
// tile on purpose: an extension is a control, not a destination.
inline constexpr int kExtensionButtonSize = 26;
inline constexpr int kExtensionButtonGap = 6;
```

- [ ] **Step 4: Write the row**

`ExtensionsRowView` is a plain host, like `UrlPillView`: it takes any child through `SetHostedView`, and lays that child's own children out in lines. It must not name a single Chromium extension type, because this target may not depend on `//chrome`.

```cpp
gfx::Size ExtensionsRowView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int count = VisibleButtonCount();
  if (count == 0) {
    // An empty row is not a thin gap above the favourites: it is nothing.
    return gfx::Size(0, 0);
  }
  const int width = available_size.width().value_or(metrics::kSidebarWidth);
  const int per_line = ButtonsPerLine(width);
  const int lines = (count + per_line - 1) / per_line;
  return gfx::Size(width,
                   lines * metrics::kExtensionButtonSize +
                       (lines - 1) * metrics::kExtensionButtonGap);
}

void ExtensionsRowView::Layout(PassKey) {
  if (!hosted_) {
    return;
  }
  hosted_->SetBoundsRect(GetLocalBounds());
  const int per_line = ButtonsPerLine(width());
  int index = 0;
  for (views::View* button : hosted_->children()) {
    if (!button->GetVisible()) {
      continue;
    }
    const int line = index / per_line;
    const int column = index % per_line;
    button->SetBounds(
        column * (metrics::kExtensionButtonSize + metrics::kExtensionButtonGap),
        line * (metrics::kExtensionButtonSize + metrics::kExtensionButtonGap),
        metrics::kExtensionButtonSize, metrics::kExtensionButtonSize);
    ++index;
  }
}
```

`ButtonsPerLine(width)` is `std::max(1, (width + gap) / (size + gap))`. The row observes the hosted view's child list with `views::ViewObserver` (`OnChildViewAdded`, `OnChildViewRemoved`, `OnViewVisibilityChanged`) and calls `PreferredSizeChanged()`, so pinning and unpinning resize it.

Add it to `SidebarView` between the pill and the favourites:

```cpp
  url_pill_ = AddChildView(...);
  extensions_row_ = AddChildView(std::make_unique<ExtensionsRowView>());
  favorites_ = AddChildView(std::make_unique<FavoritesGridView>(model_));
```

- [ ] **Step 5: Reparent the container**

In `BrowserSidebarController`, beside `HostLocationBar`:

```cpp
void BrowserSidebarController::HostExtensionsContainer() {
  ToolbarView* toolbar = browser_view_->toolbar();
  if (!toolbar || !toolbar->extensions_container() ||
      view_->extensions_row()->has_hosted_view()) {
    return;
  }
  // The toolbar keeps its pointer and its accessor, which is how extension
  // popups still find their owner; only the view moves. The same trade the
  // location bar makes one row up.
  ExtensionsToolbarDesktop* container = toolbar->extensions_container();
  std::unique_ptr<views::View> owned =
      container->parent()->RemoveChildViewT(static_cast<views::View*>(container));
  view_->extensions_row()->SetHostedView(std::move(owned));
}
```

Call it from the same place patch 0050 calls `HostLocationBar`.

- [ ] **Step 6: Make the patch for auto-hide mode**

In `/Volumes/Texternal/chromium/src/chrome/browser/ui/views/toolbar/toolbar_view.cc`, at the construction of the extensions container, pass the mode Arcium asks for:

```cpp
    extensions_container = std::make_unique<ExtensionsToolbarDesktop>(
        browser_, arcium::ExtensionsDisplayMode(browser_));
```

`arcium::ExtensionsDisplayMode` returns `ExtensionsToolbarDesktop::DisplayMode::kAutoHide` when the window has an Arcium sidebar and `kNormal` otherwise. Put it in `arcium/ui/browser/hosted_location_bar.cc`'s neighbour — a new small file is not warranted; add it to `arcium/ui/browser/browser_sidebar_controller.cc` next to `HandleNewTabCommand`, which is the file's existing home for hook targets, and declare it in the header beside that one.

Generate and head the patch:

```bash
git -C /Volumes/Texternal/chromium/src diff -- chrome/browser/ui/views/toolbar/toolbar_view.cc > /tmp/tv.diff
```

Header:

```
Seam: ToolbarView::Init, the extensions container's construction.
Why: Arcium's sidebar shows pinned extension buttons in a row of their own,
     with no menu button beside them -- that menu opens from the address pill.
     Auto-hide mode is the upstream way to ask for exactly that.
Delegates to: arcium::ExtensionsDisplayMode
```

- [ ] **Step 7: Sync, rebuild, run**

```bash
ls -l /Volumes/Texternal/chromium/src/arcium
scripts/sync && scripts/sync
ARCIUM_JOBS=4 scripts/build dev arcium_browsertests
out/dev/arcium_browsertests --gtest_filter='ExtensionsRowTest.*:PillTest.*' --test-launcher-timeout=300000 --ui-test-action-timeout=120000
```

Expected: all pass.

- [ ] **Step 8: Format and commit**

```bash
scripts/format arcium/ui/sidebar/extensions_row_view.h arcium/ui/sidebar/extensions_row_view.cc arcium/ui/sidebar/sidebar_view.h arcium/ui/sidebar/sidebar_view.cc arcium/ui/sidebar/sidebar_metrics.h arcium/ui/browser/browser_sidebar_controller.h arcium/ui/browser/browser_sidebar_controller.cc arcium/test/browser/extensions_row_browsertest.cc
git add arcium/ui/sidebar/extensions_row_view.h arcium/ui/sidebar/extensions_row_view.cc arcium/ui/sidebar/sidebar_view.h arcium/ui/sidebar/sidebar_view.cc arcium/ui/sidebar/sidebar_metrics.h arcium/ui/sidebar/BUILD.gn arcium/ui/browser/browser_sidebar_controller.h arcium/ui/browser/browser_sidebar_controller.cc arcium/test/browser/extensions_row_browsertest.cc arcium/test/BUILD.gn patches/0210-extensions-container-autohide.patch
git commit -F - <<'MSG'
Give extensions somewhere to be

They installed and ran, and then had nowhere to appear, because the strip that
draws their buttons lives in the toolbar this browser never lays out. The
strip moves into a row of its own above the favourites, the same move the
address bar already makes one row up, which keeps the toolbar's pointer to it
valid so extension popups still find their owner.

The row wraps rather than clipping, because the strip's own layout drops what
does not fit on one line and a 250px sidebar holds about seven.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

### Task 6: Turning autocomplete results into rows

**Files:**
- Create: `arcium/ui/browser/suggestion_source.h`, `arcium/ui/browser/suggestion_source.cc`
- Create: `arcium/test/suggestion_source_unittest.cc`
- Modify: `arcium/ui/browser/BUILD.gn`, `arcium/test/BUILD.gn`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:
  - `struct arcium::SuggestionRow { std::u16string title; std::u16string subtitle; GURL destination; bool is_open_tab = false; }`
  - `std::vector<SuggestionRow> arcium::RowsForResult(const AutocompleteResult& result)`
  - `class arcium::SuggestionSource` with `explicit SuggestionSource(Profile* profile)`, `void Start(const std::u16string& text, base::RepeatingCallback<void(std::vector<SuggestionRow>)> on_rows)`, `void Stop()`.

**Context an implementer needs.** `AutocompleteController` is constructible on its own — `chrome/browser/ui/webui/cr_components/searchbox/searchbox_handler.cc` does exactly this for the new tab page's search box — taking a `ChromeAutocompleteProviderClient` and an `AutocompleteControllerConfig`. Set `unscoped_open_tab_suggestions = true` so tabs open in other spaces are offered without the user having to ask for them by keyword. `//components/omnibox/browser` is already a dependency of `arcium/ui/browser`. A match is an open tab when `match.has_tab_match.value_or(false)`.

Keep the pure part pure: `RowsForResult` takes a result and returns rows, which is the half worth unit-testing. The controller half is proved by the browser test in Task 7.

- [ ] **Step 1: Write the failing test**

Create `arcium/test/suggestion_source_unittest.cc`:

```cpp
#include "arcium/ui/browser/suggestion_source.h"

#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

AutocompleteMatch MakeMatch(const std::string& url,
                            const std::u16string& description,
                            bool open_tab) {
  AutocompleteMatch match(nullptr, 1000, false,
                          AutocompleteMatchType::HISTORY_URL);
  match.destination_url = GURL(url);
  match.contents = base::UTF8ToUTF16(url);
  match.description = description;
  match.has_tab_match = open_tab;
  return match;
}

TEST(SuggestionSourceTest, KeepsTheControllersOrder) {
  AutocompleteResult result;
  ACMatches matches;
  matches.push_back(MakeMatch("https://a.test/", u"A", false));
  matches.push_back(MakeMatch("https://b.test/", u"B", false));
  result.AppendMatches(matches);

  const std::vector<SuggestionRow> rows = RowsForResult(result);
  ASSERT_EQ(2u, rows.size());
  EXPECT_EQ(u"A", rows[0].title);
  EXPECT_EQ(GURL("https://b.test/"), rows[1].destination);
}

TEST(SuggestionSourceTest, SaysWhichRowIsATabYouAlreadyHave) {
  AutocompleteResult result;
  ACMatches matches;
  matches.push_back(MakeMatch("https://a.test/", u"A", true));
  matches.push_back(MakeMatch("https://b.test/", u"B", false));
  result.AppendMatches(matches);

  const std::vector<SuggestionRow> rows = RowsForResult(result);
  ASSERT_EQ(2u, rows.size());
  EXPECT_TRUE(rows[0].is_open_tab);
  EXPECT_FALSE(rows[1].is_open_tab);
}

TEST(SuggestionSourceTest, ARowWithNowhereToGoIsNotARow) {
  AutocompleteResult result;
  ACMatches matches;
  matches.push_back(MakeMatch("", u"nowhere", false));
  result.AppendMatches(matches);
  EXPECT_TRUE(RowsForResult(result).empty());
}

TEST(SuggestionSourceTest, FallsBackToTheAddressWhenThereIsNoTitle) {
  AutocompleteResult result;
  ACMatches matches;
  matches.push_back(MakeMatch("https://a.test/", u"", false));
  result.AppendMatches(matches);
  ASSERT_EQ(1u, RowsForResult(result).size());
  EXPECT_EQ(u"https://a.test/", RowsForResult(result)[0].title);
}

}  // namespace
}  // namespace arcium
```

- [ ] **Step 2: Run it and watch it fail**

```bash
ARCIUM_JOBS=4 scripts/build dev arcium_unittests
```

Expected: `'arcium/ui/browser/suggestion_source.h' file not found`.

- [ ] **Step 3: Write the header and the pure half**

```cpp
// A row the command box draws. Everything it needs and nothing it does not:
// the box never sees an AutocompleteMatch.
struct SuggestionRow {
  std::u16string title;
  std::u16string subtitle;
  GURL destination;
  // True when this is a tab the window already has open, possibly in another
  // space. The box switches to it rather than loading a second copy.
  bool is_open_tab = false;
};

std::vector<SuggestionRow> RowsForResult(const AutocompleteResult& result);
```

```cpp
std::vector<SuggestionRow> RowsForResult(const AutocompleteResult& result) {
  std::vector<SuggestionRow> rows;
  for (const AutocompleteMatch& match : result) {
    if (!match.destination_url.is_valid()) {
      // A row that goes nowhere is a row that lies about being clickable.
      continue;
    }
    SuggestionRow row;
    row.destination = match.destination_url;
    row.is_open_tab = match.has_tab_match.value_or(false);
    row.title = match.description.empty() ? match.contents : match.description;
    row.subtitle = match.description.empty() ? std::u16string() : match.contents;
    rows.push_back(std::move(row));
  }
  return rows;
}
```

- [ ] **Step 4: Write the controller half**

```cpp
SuggestionSource::SuggestionSource(Profile* profile) {
  AutocompleteControllerConfig config;
  config.provider_types = AutocompleteClassifier::DefaultOmniboxProviders();
  // Tabs open in other spaces are offered without having to be asked for by
  // keyword: a space you are not looking at is still your browser.
  config.unscoped_open_tab_suggestions = true;
  controller_ = std::make_unique<AutocompleteController>(
      std::make_unique<ChromeAutocompleteProviderClient>(profile), config);
  controller_->AddObserver(this);
}

void SuggestionSource::Start(const std::u16string& text,
                             base::RepeatingCallback<void(std::vector<SuggestionRow>)> on_rows) {
  on_rows_ = std::move(on_rows);
  if (text.empty()) {
    controller_->Stop(AutocompleteStopReason::kClobbered);
    on_rows_.Run({});
    return;
  }
  AutocompleteInput input(text, metrics::OmniboxEventProto::OTHER,
                          ChromeAutocompleteSchemeClassifier(profile_));
  controller_->Start(input);
}

void SuggestionSource::OnResultChanged(AutocompleteController* controller,
                                       bool default_match_changed) {
  on_rows_.Run(RowsForResult(controller->result()));
}
```

Nothing is constructed until a `SuggestionSource` is, and Task 7 constructs one only when the box opens.

- [ ] **Step 5: Run and watch pass**

```bash
ARCIUM_JOBS=4 scripts/build dev arcium_unittests && out/dev/arcium_unittests --gtest_filter='SuggestionSourceTest.*'
```

Expected: 4 tests pass.

- [ ] **Step 6: Format and commit**

```bash
scripts/format arcium/ui/browser/suggestion_source.h arcium/ui/browser/suggestion_source.cc arcium/test/suggestion_source_unittest.cc
git add arcium/ui/browser/suggestion_source.h arcium/ui/browser/suggestion_source.cc arcium/ui/browser/BUILD.gn arcium/test/suggestion_source_unittest.cc arcium/test/BUILD.gn
git commit -F - <<'MSG'
Ask Chrome's own answers, and hand back rows instead of matches

The box needs a title, an address, somewhere to go and whether we already have
that page open. Everything else an autocomplete match carries is the search
system's business, so it stops here, which also leaves the half worth testing
as a pure function over a result.

Tabs in spaces you are not looking at are offered without being asked for by
keyword, because a space you cannot see is still your browser.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

### Task 7: The command box

**Files:**
- Create: `arcium/ui/browser/command_box.h`, `arcium/ui/browser/command_box.cc`
- Create: `arcium/ui/browser/command_box_row.h`, `arcium/ui/browser/command_box_row.cc`
- Delete: `arcium/ui/browser/quick_entry_bubble.h`, `arcium/ui/browser/quick_entry_bubble.cc`
- Create: `arcium/test/browser/command_box_browsertest.cc`
- Modify: `arcium/ui/browser/browser_sidebar_controller.h`, `arcium/ui/browser/browser_sidebar_controller.cc`
- Modify: `arcium/ui/browser/BUILD.gn`, `arcium/test/BUILD.gn`

**Interfaces:**
- Consumes: `arcium::SuggestionRow`, `arcium::SuggestionSource` (Task 6).
- Produces:
  - `class arcium::CommandBox : public views::BubbleDialogDelegate, public views::TextfieldController` with `CommandBox(BrowserView* browser_view, SuggestionSource* source, OpenCallback on_open)`, `void FocusField()`, `void SetText(const std::u16string& text, bool select_all)`.
  - For tests: `size_t row_count_for_testing()`, `const SuggestionRow& row_for_testing(size_t)`, `size_t selected_row_for_testing()`, `const std::u16string& text_for_testing()`, `size_t selected_length_for_testing()`, `void SetRowsChangedClosureForTesting(base::RepeatingClosure)`.
  - `CommandBox* BrowserSidebarController::command_box_for_testing()` and `SuggestionSource* BrowserSidebarController::suggestion_source_for_testing()`.
  - `using CommandBox::OpenCallback = base::OnceCallback<void(SuggestionRow)>` — what the window does with the chosen row.
  - `void BrowserSidebarController::ShowCommandBox(std::optional<std::u16string> initial_text)` replacing `ShowQuickEntry()`.

**Context an implementer needs.** `QuickEntryBubble` is today's Cmd+T box: a `views::BubbleDialogDelegate` with one `views::Textfield`, anchored over the contents area, that classifies the typed text on Enter and opens a tab. `CommandBox` is that plus a result list, and it replaces the file rather than growing beside it. The `SuggestionSource` is created with the box and destroyed with it, so an idle browser carries no autocomplete providers and no timers.

- [ ] **Step 1: Write the failing browser test**

Create `arcium/test/browser/command_box_browsertest.cc`:

```cpp
IN_PROC_BROWSER_TEST_F(CommandBoxTest, TypingGetsAnswers) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL url = embedded_test_server()->GetURL("a.test", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  WaitForHistory(url);

  OpenBox();
  Type(u"a.test");
  WaitForRows();
  EXPECT_GT(Box()->row_count_for_testing(), 0u);
}

IN_PROC_BROWSER_TEST_F(CommandBoxTest, EnterOnATabYouHaveSwitchesToIt) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL url = embedded_test_server()->GetURL("a.test", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("b.test", "/title2.html")));
  // Two tabs: a.test first, b.test active.
  const int tabs_before = browser()->tab_strip_model()->count();

  OpenBox();
  Type(u"a.test");
  WaitForRowThatIsAnOpenTab();
  PressEnter();

  EXPECT_EQ(tabs_before, browser()->tab_strip_model()->count())
      << "a tab we already had was opened a second time";
  EXPECT_EQ(url, browser()
                     ->tab_strip_model()
                     ->GetActiveWebContents()
                     ->GetLastCommittedURL());

  // The control. Everything above says a tab was not added, which is also
  // what a box wired to nothing would report. A row that is not an open tab
  // must add one.
  OpenBox();
  Type(u"c.test/title3.html");
  WaitForRows();
  PressEnter();
  EXPECT_EQ(tabs_before + 1, browser()->tab_strip_model()->count());
}

IN_PROC_BROWSER_TEST_F(CommandBoxTest, EscapeLeavesThePageAlone) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL url = embedded_test_server()->GetURL("a.test", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  OpenBox();
  Type(u"b.test");
  PressEscape();
  EXPECT_FALSE(Box());
  EXPECT_EQ(url, browser()
                     ->tab_strip_model()
                     ->GetActiveWebContents()
                     ->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_F(CommandBoxTest, ClickingThePillOpensIt) {
  OpenNothing();
  ASSERT_FALSE(Box());
  ClickPillBackground();
  EXPECT_TRUE(Box());
  // And the address bar behind the pill did not take focus: one box, one
  // place suggestions come from.
  EXPECT_FALSE(BrowserView::GetBrowserViewForBrowser(browser())
                   ->GetLocationBarView()
                   ->HasFocus());
}

IN_PROC_BROWSER_TEST_F(CommandBoxTest, NothingIsRunningWhileItIsClosed) {
  // The suggestion machinery is built with the box and dies with it.
  EXPECT_FALSE(Controller()->suggestion_source_for_testing());
  OpenBox();
  EXPECT_TRUE(Controller()->suggestion_source_for_testing());
  PressEscape();
  EXPECT_FALSE(Controller()->suggestion_source_for_testing());
}
```

- [ ] **Step 2: Run it and watch it fail**

```bash
pgrep -f siso; pgrep -f "Arcium.app/Contents/MacOS/Arcium"
ARCIUM_JOBS=4 scripts/build dev arcium_browsertests
out/dev/arcium_browsertests --gtest_filter='CommandBoxTest.*' --test-launcher-timeout=300000 --ui-test-action-timeout=120000
```

Expected: a compile failure on `CommandBox`.

- [ ] **Step 3: Write the row view**

`CommandBoxRow` is a `views::View` holding an icon, a title label, a subtitle label and, when `is_open_tab`, a trailing pill reading "Open tab". It has `void SetSelected(bool)`, which paints the selected background. Nothing else.

- [ ] **Step 4: Write the box**

Copy `quick_entry_bubble.cc`'s widget setup — the anchoring over the contents area, `set_close_on_deactivate(true)`, the 560px width — and add beneath the field a `views::View` of rows, plus:

```cpp
void CommandBox::ContentsChanged(views::Textfield* sender,
                                 const std::u16string& text) {
  source_->Start(text, base::BindRepeating(&CommandBox::OnRows,
                                           weak_factory_.GetWeakPtr()));
}

void CommandBox::OnRows(std::vector<SuggestionRow> rows) {
  rows_ = std::move(rows);
  // The first row is the one Enter takes, so a new set of answers resets the
  // selection: keeping an index into a list that just changed underneath it
  // would send Enter somewhere the reader never looked at.
  selected_ = 0;
  RebuildRowViews();
  SizeToContents();
}

bool CommandBox::HandleKeyEvent(views::Textfield* sender,
                                const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  switch (key_event.key_code()) {
    case ui::VKEY_DOWN:
      Move(1);
      return true;
    case ui::VKEY_UP:
      Move(-1);
      return true;
    case ui::VKEY_RETURN:
      Accept();
      return true;
    default:
      return false;
  }
}
```

`Accept()` takes `rows_[selected_]` when there is one, and otherwise classifies the raw text exactly as `QuickEntryBubble::OnQuickEntrySubmitted` does today, so typing a full address with no answers back yet still works. Either way it runs the `OpenCallback` and closes. Escape is `BubbleDialogDelegate`'s own.

- [ ] **Step 5: Wire it into the controller**

`ShowQuickEntry` becomes:

```cpp
void BrowserSidebarController::ShowCommandBox(
    std::optional<std::u16string> initial_text) {
  if (command_box_widget_) {
    command_box_->FocusField();
    return;
  }
  suggestion_source_ =
      std::make_unique<SuggestionSource>(browser_view_->GetProfile());
  command_box_ = std::make_unique<CommandBox>(
      browser_view_, suggestion_source_.get(),
      base::BindOnce(&BrowserSidebarController::OnCommandBoxAccepted,
                     weak_factory_.GetWeakPtr()));
  command_box_widget_ = views::BubbleDialogDelegate::CreateBubble(
      command_box_.get(),
      base::BindOnce(&BrowserSidebarController::OnCommandBoxClosed,
                     weak_factory_.GetWeakPtr()));
  command_box_widget_->Show();
  if (initial_text) {
    command_box_->SetText(*initial_text, /*select_all=*/true);
  }
  command_box_->FocusField();
}

void BrowserSidebarController::OnCommandBoxAccepted(SuggestionRow row) {
  if (row.is_open_tab && ActivateTabWithUrl(row.destination)) {
    return;
  }
  chrome::AddSelectedTabWithURL(browser_view_->browser(), row.destination,
                                ui::PAGE_TRANSITION_TYPED);
}
```

`ActivateTabWithUrl` walks the strip for a tab whose last committed URL matches, switches to that tab's space through `space_switcher_` when it is not the active one, activates it, and returns whether it found one — returning false is what makes the fallback above honest rather than silent.

`OnCommandBoxClosed` destroys the box, its widget and the suggestion source together.

Delete `quick_entry_bubble.h/.cc`, drop them from `BUILD.gn`, and point `HandleNewTabCommand` at `ShowCommandBox(std::nullopt)`.

- [ ] **Step 6: Run and watch pass**

```bash
out/dev/arcium_browsertests --gtest_filter='CommandBoxTest.*' --test-launcher-timeout=300000 --ui-test-action-timeout=120000
```

- [ ] **Step 7: Format and commit**

```bash
scripts/format arcium/ui/browser/command_box.h arcium/ui/browser/command_box.cc arcium/ui/browser/command_box_row.h arcium/ui/browser/command_box_row.cc arcium/ui/browser/browser_sidebar_controller.h arcium/ui/browser/browser_sidebar_controller.cc arcium/test/browser/command_box_browsertest.cc
git add arcium/ui/browser/command_box.h arcium/ui/browser/command_box.cc arcium/ui/browser/command_box_row.h arcium/ui/browser/command_box_row.cc arcium/ui/browser/browser_sidebar_controller.h arcium/ui/browser/browser_sidebar_controller.cc arcium/ui/browser/BUILD.gn arcium/test/browser/command_box_browsertest.cc arcium/test/BUILD.gn
git rm arcium/ui/browser/quick_entry_bubble.h arcium/ui/browser/quick_entry_bubble.cc
git commit -F - <<'MSG'
Answer while the reader types, instead of taking dictation

The box was a text field that did nothing until Enter. It now offers what the
reader is reaching for, and a row that is a tab they already have switches to
it rather than opening the page twice.

The machinery behind it is built when the box opens and dies when it closes,
so a browser sitting idle carries no providers and no timers for a box nobody
opened.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

### Task 8: Offer the tabs, pins and archived pages this browser already knows about

**Files:**
- Modify: `arcium/ui/browser/suggestion_source.h`, `arcium/ui/browser/suggestion_source.cc`
- Modify: `arcium/ui/browser/command_box.cc`
- Modify: `arcium/test/suggestion_source_unittest.cc`
- Modify: `arcium/test/browser/command_box_browsertest.cc`

**Interfaces:**
- Consumes: `SuggestionRow`, `RowsForResult` (Task 6); `arcium::TabSearchService` and `arcium::TabSearchService::SearchResult` (existing, `arcium/ui/browser/tab_search_service.h`).
- Produces: `std::vector<SuggestionRow> arcium::MergeSuggestions(std::vector<SuggestionRow> mine, std::vector<SuggestionRow> web)`.

**Context an implementer needs.** `TabSearchService` already exists, is unit-tested (`arcium/test/tab_search_service_unittest.cc`, `space_scoping_unittest.cc`) and has no caller in the browser: it was built for this box and left waiting. It searches three things Chrome's providers know nothing about — live tabs across every space, pinned and favourite entries, and the archive — and it answers asynchronously through `Search(query, limit, callback)`. Chrome's providers bring history, the search engine and the raw address. The box wants both.

The merge rule: **what you already have comes first.** A tab, a pin or an archived page is something the reader put there; a history hit is something they passed through. Within each half the source's own order stands. A destination that appears in both halves appears once, in the first half.

- [ ] **Step 1: Write the failing test**

Add to `arcium/test/suggestion_source_unittest.cc`:

```cpp
TEST(MergeSuggestionsTest, WhatYouAlreadyHaveComesFirst) {
  std::vector<SuggestionRow> mine = {Row("https://tab.test/", true)};
  std::vector<SuggestionRow> web = {Row("https://history.test/", false)};
  const std::vector<SuggestionRow> merged =
      MergeSuggestions(std::move(mine), std::move(web));
  ASSERT_EQ(2u, merged.size());
  EXPECT_EQ(GURL("https://tab.test/"), merged[0].destination);
  EXPECT_EQ(GURL("https://history.test/"), merged[1].destination);
}

TEST(MergeSuggestionsTest, TheSamePageIsOfferedOnce) {
  std::vector<SuggestionRow> mine = {Row("https://a.test/", true)};
  std::vector<SuggestionRow> web = {Row("https://a.test/", false),
                                    Row("https://b.test/", false)};
  const std::vector<SuggestionRow> merged =
      MergeSuggestions(std::move(mine), std::move(web));
  ASSERT_EQ(2u, merged.size());
  // And it keeps the half that knows it is open, not the half that does not.
  EXPECT_TRUE(merged[0].is_open_tab);
  EXPECT_EQ(GURL("https://b.test/"), merged[1].destination);
}

TEST(MergeSuggestionsTest, EitherHalfMayBeEmpty) {
  EXPECT_EQ(1u, MergeSuggestions({Row("https://a.test/", true)}, {}).size());
  EXPECT_EQ(1u, MergeSuggestions({}, {Row("https://a.test/", false)}).size());
  EXPECT_TRUE(MergeSuggestions({}, {}).empty());
}
```

and to the browser test:

```cpp
IN_PROC_BROWSER_TEST_F(CommandBoxTest, APinnedPageIsOfferedBeforeAHistoryHit) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL pinned = embedded_test_server()->GetURL("pin.test", "/title1.html");
  PinEntryWithUrl(pinned, u"A pinned page");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("pin.test", "/title2.html")));
  WaitForHistory(embedded_test_server()->GetURL("pin.test", "/title2.html"));

  OpenBox();
  Type(u"pin.test");
  WaitForRows();
  EXPECT_EQ(pinned, Box()->row_for_testing(0).destination);
}
```

- [ ] **Step 2: Run both and watch them fail**

```bash
ARCIUM_JOBS=4 scripts/build dev arcium_unittests
```

Expected: `MergeSuggestions` not declared.

- [ ] **Step 3: Write the merge**

```cpp
std::vector<SuggestionRow> MergeSuggestions(std::vector<SuggestionRow> mine,
                                            std::vector<SuggestionRow> web) {
  std::set<GURL> seen;
  std::vector<SuggestionRow> merged;
  // What the reader already put somewhere outranks what they once passed
  // through, however well the second half scored it.
  for (std::vector<SuggestionRow>* half : {&mine, &web}) {
    for (SuggestionRow& row : *half) {
      if (!seen.insert(row.destination).second) {
        continue;
      }
      merged.push_back(std::move(row));
    }
  }
  return merged;
}
```

- [ ] **Step 4: Ask both sources**

`SuggestionSource` gains a `TabSearchService*`, given by the controller, and `Start` fires both searches, keeping the latest answer from each and calling back with the merge whenever either arrives. Convert a `TabSearchService::SearchResult` into a `SuggestionRow` with `is_open_tab` true only for `Source::kLiveTab` — a pin and an archived page are pages to open, not tabs to switch to.

- [ ] **Step 5: Run everything and watch it pass**

```bash
ARCIUM_JOBS=4 scripts/build dev arcium_unittests && out/dev/arcium_unittests --gtest_filter='SuggestionSourceTest.*:MergeSuggestionsTest.*'
ARCIUM_JOBS=4 scripts/build dev arcium_browsertests
out/dev/arcium_browsertests --gtest_filter='CommandBoxTest.*' --test-launcher-timeout=300000 --ui-test-action-timeout=120000
```

- [ ] **Step 6: Format and commit**

```bash
scripts/format arcium/ui/browser/suggestion_source.h arcium/ui/browser/suggestion_source.cc arcium/ui/browser/command_box.cc arcium/test/suggestion_source_unittest.cc arcium/test/browser/command_box_browsertest.cc
git add arcium/ui/browser/suggestion_source.h arcium/ui/browser/suggestion_source.cc arcium/ui/browser/command_box.cc arcium/test/suggestion_source_unittest.cc arcium/test/browser/command_box_browsertest.cc
git commit -F - <<'MSG'
Offer what this browser already holds before what the web remembers

The service that searches live tabs, pinned entries and the archive was built
and tested a stage ago and has been sitting without a caller ever since. This
is the caller it was for.

Its answers come first. A tab, a pin or an archived page is somewhere the
reader put something; a history hit is somewhere they once passed through, and
ranking the second above the first would bury the thing they were reaching for
under the thing they forgot.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

### Task 9: Cmd+L opens the same box, holding the address

**Files:**
- Modify: `patches/0090-new-tab-quick-entry.patch`
- Modify: `arcium/ui/browser/browser_sidebar_controller.h`, `arcium/ui/browser/browser_sidebar_controller.cc`
- Modify: `arcium/test/browser/command_box_browsertest.cc`

**Interfaces:**
- Consumes: `BrowserSidebarController::ShowCommandBox(std::optional<std::u16string>)` (Task 7).
- Produces: `bool arcium::HandleFocusLocationCommand(Browser* browser)`, beside the existing `HandleNewTabCommand`.

**Context an implementer needs.** Patch 0090 already hooks `BrowserCommandController::ExecuteCommandWithDisposition` for `IDC_NEW_TAB` and delegates to `arcium::HandleNewTabCommand`. `IDC_FOCUS_LOCATION` is Cmd+L, and it must reach the same box rather than focusing the bar behind the pill, which draws nothing and would look like nothing happening. Both hooks live in one file, so they live in one patch: extend 0090 rather than adding a second patch touching the same lines, and update its header to name both commands.

- [ ] **Step 1: Write the failing test**

```cpp
IN_PROC_BROWSER_TEST_F(CommandBoxTest, FocusLocationOpensTheBoxHoldingTheAddress) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL url = embedded_test_server()->GetURL("a.test", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));

  chrome::ExecuteCommand(browser(), IDC_FOCUS_LOCATION);
  ASSERT_TRUE(Box());
  EXPECT_EQ(base::UTF8ToUTF16(url.spec()), Box()->text_for_testing());
  // Selected end to end, so typing replaces it, which is what this key is for.
  EXPECT_EQ(url.spec().size(), Box()->selected_length_for_testing());

  // The bar behind the pill did not take the focus instead.
  EXPECT_FALSE(BrowserView::GetBrowserViewForBrowser(browser())
                   ->GetLocationBarView()
                   ->HasFocus());
}
```

- [ ] **Step 2: Run it and watch it fail**

```bash
out/dev/arcium_browsertests --gtest_filter='CommandBoxTest.FocusLocationOpensTheBoxHoldingTheAddress' --test-launcher-timeout=300000 --ui-test-action-timeout=120000
```

Expected: FAIL, no box.

- [ ] **Step 3: Add the hook target**

```cpp
bool HandleFocusLocationCommand(Browser* browser) {
  BrowserSidebarController* sidebar = SidebarFor(browser);
  if (!sidebar) {
    return false;
  }
  content::WebContents* contents =
      browser->tab_strip_model()->GetActiveWebContents();
  // The whole address, not the domain the pill shows: this key exists to
  // replace or edit what is there, and half an address is neither.
  std::u16string text;
  if (contents && contents->GetLastCommittedURL().is_valid()) {
    text = base::UTF8ToUTF16(contents->GetLastCommittedURL().spec());
  }
  sidebar->ShowCommandBox(std::move(text));
  return true;
}
```

`CommandBox::SetText(text, select_all=true)` selects it end to end.

- [ ] **Step 4: Extend the patch**

Add the `IDC_FOCUS_LOCATION` case beside the existing `IDC_NEW_TAB` one in `chrome/browser/ui/browser_command_controller.cc`, regenerate, and replace the body of `patches/0090-new-tab-quick-entry.patch` below its header:

```bash
git -C /Volumes/Texternal/chromium/src diff -- chrome/browser/ui/browser_command_controller.cc > /tmp/bcc.diff
```

New header:

```
Seam: BrowserCommandController::ExecuteCommandWithDisposition, IDC_NEW_TAB and
      IDC_FOCUS_LOCATION.
Why: both keys open the sidebar's command box -- Cmd+T empty, Cmd+L holding the
     current address -- rather than opening a bare tab or focusing an address
     bar that draws nothing.
Delegates to: arcium::HandleNewTabCommand, arcium::HandleFocusLocationCommand
```

- [ ] **Step 5: Sync, rebuild, run the whole browser suite**

```bash
ls -l /Volumes/Texternal/chromium/src/arcium
scripts/sync && scripts/sync
ARCIUM_JOBS=4 scripts/build dev arcium_browsertests
out/dev/arcium_browsertests --test-launcher-timeout=300000 --ui-test-action-timeout=120000
```

Expected: everything passes, this stage's tests and the profile tests from Stage 3b alike.

- [ ] **Step 6: Format and commit**

```bash
scripts/format arcium/ui/browser/browser_sidebar_controller.h arcium/ui/browser/browser_sidebar_controller.cc arcium/test/browser/command_box_browsertest.cc
git add arcium/ui/browser/browser_sidebar_controller.h arcium/ui/browser/browser_sidebar_controller.cc arcium/test/browser/command_box_browsertest.cc patches/0090-new-tab-quick-entry.patch
git commit -F - <<'MSG'
Send Cmd+L to the box as well, holding the whole address

It focused an address bar that now draws nothing, which on screen is a key
that does nothing at all. It opens the same box Cmd+T does, carrying the full
address rather than the domain the pill shows, selected end to end: this key
exists to replace or edit what is there, and half an address is neither.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

### Task 10: Write down what was built, and set up the pass that checks it

**Files:**
- Create: `docs/stage4a-findings.md`
- Create: `scripts/acceptance-4a`
- Create: `docs/stage4a-acceptance-harness.md`
- Modify: `CLAUDE.md` (the stage table)
- Create: `docs/perf/2026-XX-XX-stage4a.md` (dated on the day it is measured)

**Interfaces:**
- Consumes: everything above.
- Produces: nothing code depends on.

- [ ] **Step 1: Run perf and record it**

Only when the machine is quiet:

```bash
pgrep -f siso; pgrep -f "Arcium.app/Contents/MacOS/Arcium"
scripts/perf --runs 5 --label stage4a
```

Write the numbers into `docs/perf/<date>-stage4a.md` beside the four questions from the spec's section 10, and say plainly where a number contradicts an expectation rather than rounding it away.

- [ ] **Step 2: Write the acceptance harness**

`scripts/acceptance-4a` follows `scripts/acceptance-3b`: it launches `out/dev` against a throwaway user-data-dir, serves a loopback page over plain `http` so A4a.5's warning has something to warn about, and prints the seven acceptance rows from the spec's section 9 as a checklist. It must **not** install iCloud Passwords or the Claude extension itself — those are the owner's accounts and the owner's clicks — but it prints the two extension pages to open. Validate it with `bash -n scripts/acceptance-4a`.

`docs/stage4a-acceptance-harness.md` says what the harness sets up and, as the Stage 3b one does, states plainly the one thing it substitutes: a local `http` page stands in for a real insecure site.

- [ ] **Step 3: Write the findings**

`docs/stage4a-findings.md` records, in the house style of `docs/stage3b-findings.md`: what shipped, every defect found during the pass and whether it was fixed, what is covered by a test and what by a person, and — in its own paragraph — what the pass does **not** cover. Say which acceptance rows were run by hand, on what date, and by whom.

- [ ] **Step 4: Update the stage table**

Add a Stage 4a row to `CLAUDE.md`'s stage table pointing at the findings, the spec and the perf file, and note in the Stage 4 row which of its requirements this stage has already taken.

- [ ] **Step 5: Commit**

```bash
git add docs/stage4a-findings.md docs/stage4a-acceptance-harness.md scripts/acceptance-4a CLAUDE.md docs/perf/
git commit -F - <<'MSG'
Write down what this stage covers, and what it does not

A pass nobody can repeat is a pass nobody can trust, so the harness builds the
world the rows assume and the findings say which rows a person ran, on what
day, and which are covered by a test instead.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

## Notes for whoever executes this

- **Do not push.** The owner has kept this branch local for every stage so far.
- **The screen is not yours.** Browser tests open real windows on macOS. Check `pgrep -f "Arcium.app/Contents/MacOS/Arcium"` before every browser-test run and stop if the owner's browser is up.
- **The checkout is shared.** `ls -l /Volumes/Texternal/chromium/src/arcium` before trusting any build. A build against another worktree's sources succeeds, links and runs, and is wrong.
- **A flat measurement is not a pass.** Two tests here assert that something did not happen — no second tab, no extra process. Each carries a positive control, and neither is finished without one.
- **One open defect is inherited, not caused here.** Both delete-a-profile browser tests crashed intermittently at shutdown before this stage; a fix landed in `db3b315` and has not yet had the twenty consecutive runs that would tell it from luck. If one of them crashes during this stage's suite runs, that is the known one — record it, do not chase it into this stage's work.
