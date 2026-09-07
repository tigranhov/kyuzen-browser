// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/archive_list_view.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/l10n/time_format.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace arcium {

namespace {

// Wider than the sidebar: a title elided at 250px next to a timestamp is
// mostly ellipsis.
constexpr int kBubbleWidth = 320;
// Ten rows. Past that the list scrolls rather than growing a bubble taller
// than the window.
constexpr int kMaxListHeight = 10 * metrics::kRowHeight;
// What the list asks the archive for. The archive grows without bound and
// this view is a lid on a bin, not a history browser — Stage 6's library is
// the thing that pages.
constexpr int kMaxRows = 50;

// The two things an empty list can mean, which must never be confused: an
// archive with nothing in it, and an archive that could not be read. Telling
// a user whose file will not open that nothing was archived is telling them
// their tabs were thrown away.
constexpr char16_t kNothingArchived[] = u"Nothing archived yet";
constexpr char16_t kArchiveUnreadable[] = u"The archive could not be opened";

// "2 h ago". A clock that has moved backwards since the row was written — a
// timezone correction, an NTP step — must not print a negative elapsed time,
// so it reads as "just now" instead.
std::u16string RelativeTime(base::Time archived_at, base::Time now) {
  const base::TimeDelta elapsed = now - archived_at;
  return ui::TimeFormat::Simple(
      ui::TimeFormat::FORMAT_ELAPSED, ui::TimeFormat::LENGTH_SHORT,
      elapsed.is_negative() ? base::TimeDelta() : elapsed);
}

// One 32px row: a globe, the title, and how long ago it was archived.
//
// A globe rather than a favicon: the archive stores a URL and a title and
// nothing else, and reading the real favicon means a FaviconService lookup —
// per row, on open — which is Stage 6's problem along with the rest of this
// view. Cold sidebar entries stand in the same globe for the same reason.
class ArchiveRowView : public views::Button {
  METADATA_HEADER(ArchiveRowView, views::Button)

 public:
  ArchiveRowView(PressedCallback callback,
                 const ArchivedRow& row,
                 base::Time now)
      : views::Button(std::move(callback)) {
    SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
    auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
    layout->SetOrientation(views::LayoutOrientation::kHorizontal)
        .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
        .SetInteriorMargin(gfx::Insets::VH(0, metrics::kRowHorizontalPadding))
        .SetDefault(views::kMarginsKey,
                    gfx::Insets::VH(0, metrics::kRowIconTextGap / 2));

    auto* icon = AddChildView(std::make_unique<views::ImageView>());
    icon->SetImageSize(gfx::Size(metrics::kFaviconSize, metrics::kFaviconSize));
    icon->SetImage(ui::ImageModel::FromVectorIcon(
        vector_icons::kGlobeIcon, kColorArciumRowText, metrics::kFaviconSize));

    auto* title = AddChildView(std::make_unique<views::Label>(row.title));
    title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title->SetElideBehavior(gfx::ELIDE_TAIL);
    title->SetEnabledColor(kColorArciumRowText);
    // The list is inside a ScrollView, as the Today list is; see the note
    // there and in TabRowView.
    title->SetSubpixelRenderingEnabled(false);
    // The title yields the width, never the timestamp: "2 h ago" elided is
    // nothing at all, and it is the column that makes the list readable.
    title->SetProperty(
        views::kFlexBehaviorKey,
        views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                                 views::MinimumFlexSizeRule::kScaleToZero,
                                 views::MaximumFlexSizeRule::kUnbounded));

    const std::u16string when = RelativeTime(row.archived_at, now);
    auto* age = AddChildView(std::make_unique<views::Label>(when));
    age->SetEnabledColor(kColorArciumRowTextSecondary);
    age->SetSubpixelRenderingEnabled(false);

    GetViewAccessibility().SetName(row.title + u", " + when);
    SetTooltipText(base::UTF8ToUTF16(row.url.spec()));
  }
  ArchiveRowView(const ArchiveRowView&) = delete;
  ArchiveRowView& operator=(const ArchiveRowView&) = delete;
  ~ArchiveRowView() override = default;

  // views::Button / View:
  void OnMouseEntered(const ui::MouseEvent& event) override {
    hovered_ = true;
    OnThemeChanged();
  }
  void OnMouseExited(const ui::MouseEvent& event) override {
    hovered_ = false;
    OnThemeChanged();
  }
  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    SetBackground(hovered_ ? views::CreateRoundedRectBackground(
                                 kColorArciumRowHoverBackground,
                                 metrics::kRowCornerRadius)
                           : nullptr);
  }
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(available_size.width().value_or(kBubbleWidth),
                     metrics::kRowHeight);
  }

 private:
  bool hovered_ = false;
};

BEGIN_METADATA(ArchiveRowView)
END_METADATA

}  // namespace

ArchiveListView::ArchiveListView(views::View* anchor, SidebarModel* model)
    : views::BubbleDialogDelegate(anchor, views::BubbleBorder::LEFT_TOP),
      model_(model) {
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  SetShowCloseButton(false);
  SetTitle(u"Archived");
  set_fixed_width(kBubbleWidth);
  set_margins(gfx::Insets::VH(8, 0));
  if (views::Widget* widget = anchor->GetWidget()) {
    set_parent_window(widget->GetNativeView());
  }

  auto contents = std::make_unique<views::View>();
  contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));

  status_ = contents->AddChildView(std::make_unique<views::Label>());
  status_->SetEnabledColor(kColorArciumRowTextSecondary);
  status_->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::VH(4, metrics::kRowHorizontalPadding)));
  status_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  status_->SetMultiLine(true);

  // ScrollWithLayers is the macOS default, and the Today list documents why
  // it is turned off there: a layer-backed viewport is not opaque over the
  // sidebar's gradient, which trips a views::Label DCHECK and hides the rows
  // from the offscreen paint --snapshot uses. A bubble paints its own opaque
  // background, so that particular failure is not reachable here — but there
  // is no reason to pay for a compositor layer either, and the two scrolling
  // lists in this stage behaving identically is worth more than the layer.
  scroll_ = contents->AddChildView(std::make_unique<views::ScrollView>(
      views::ScrollView::ScrollWithLayers::kDisabled));
  contents_ = scroll_->SetContents(std::make_unique<views::View>());
  contents_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  // Without a height clamp the viewport never sizes its contents and the rows
  // stay at zero height, exactly as in SidebarView.
  scroll_->ClipHeightTo(0, kMaxListHeight);
  // The bubble paints the background; the viewport must not paint a second
  // opaque rectangle over it.
  scroll_->SetBackgroundColor(std::nullopt);
  scroll_->SetDrawOverflowIndicator(false);
  scroll_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  SetContentsView(std::move(contents));

  // Asked for once, on open. Nothing in the sidebar reads the archive on a
  // model change, so an archive of a hundred thousand rows costs a browser
  // that never opens this list exactly nothing.
  model_->RequestArchivedRows(
      kMaxRows,
      base::BindOnce(&ArchiveListView::OnRowsRead, weak_factory_.GetWeakPtr()));
  // Lays the empty contents out; `loaded_` is false, so it shows neither rows
  // nor a message. See Rebuild().
  Rebuild();
}

ArchiveListView::~ArchiveListView() = default;

std::u16string ArchiveListView::status_message_for_testing() const {
  return status_->GetVisible() ? std::u16string(status_->GetText())
                               : std::u16string();
}

void ArchiveListView::OnRowsRead(std::vector<ArchivedRow> rows,
                                 bool archive_readable) {
  archived_ = std::move(rows);
  archive_readable_ = archive_readable;
  loaded_ = true;
  Rebuild();
}

void ArchiveListView::Rebuild() {
  contents_->RemoveAllChildViews();
  rows_.clear();
  // One `now` for the whole list, so two rows archived in the same second
  // cannot print different ages because the clock ticked mid-loop.
  const base::Time now = base::Time::Now();
  for (const ArchivedRow& row : archived_) {
    rows_.push_back(contents_->AddChildView(std::make_unique<ArchiveRowView>(
        // The row travels by value. Clicking rebuilds the list and destroys
        // the view that was clicked, so the callback must not reach back into
        // it, and an index into `archived_` would name a different row by the
        // time the next click lands.
        base::BindRepeating(&ArchiveListView::OnRowClicked,
                            weak_factory_.GetWeakPtr(), row),
        row, now)));
  }
  // Three states, and the first one is silence. Before the reply lands there
  // is nothing true to say — the read is posted, so the list is empty for a
  // turn of the run loop whatever the archive holds — and the bubble is a
  // title over a blank strip until it can say something that is not a guess.
  if (!loaded_) {
    status_->SetVisible(false);
  } else if (!archive_readable_) {
    status_->SetText(kArchiveUnreadable);
    status_->SetVisible(true);
  } else {
    status_->SetText(kNothingArchived);
    status_->SetVisible(archived_.empty());
  }
  scroll_->SetVisible(!archived_.empty());
  if (GetWidget()) {
    SizeToContents();
  }
}

void ArchiveListView::OnRowClicked(ArchivedRow row) {
  // Rebuild() below destroys the row view this call came from. That is a
  // thing Views supports — ButtonController::OnMouseReleased returns straight
  // after NotifyClick for exactly this reason, and Button::NotifyClick touches
  // nothing after running the callback — but only because `row` is a copy
  // taken as the call was made, not a reference into the view or into
  // `archived_`. TabRowView takes its copies for the same reason.
  //
  // Order matters only in that the model is told before the list forgets:
  // ReopenArchived needs both halves of the archive's key, and `row` is the
  // only place this view still has them.
  model_->ReopenArchived(row.url, row.archived_at);
  std::erase_if(archived_, [&row](const ArchivedRow& other) {
    return other.url == row.url && other.archived_at == row.archived_at;
  });
  Rebuild();
}

}  // namespace arcium
