// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Step 1, Bring your setup: what was found, what comes with it, and the
// spaces that will arrive.

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/vector_icons.h"
#include "arcium/ui/welcome/welcome_model.h"
#include "arcium/ui/welcome/welcome_steps.h"
#include "arcium/ui/welcome/welcome_style.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ref.h"
#include "cc/paint/paint_flags.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/radio_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/link.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/layout/table_layout.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view_class_properties.h"

namespace arcium::welcome {
namespace {

constexpr int kSourceGroup = 1;
constexpr size_t kPreviewSpaces = 4;

// Another browser, drawn as a plain ring around its initial: never its logo.
class SourceMark : public views::View {
  METADATA_HEADER(SourceMark, views::View)

 public:
  explicit SourceMark(std::u16string initial) {
    SetLayoutManager(std::make_unique<views::FillLayout>());
    SetPreferredSize(gfx::Size(44, 44));
    auto label = MakeLabel(initial, TextStyle::kBodyStrong);
    label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    AddChildView(std::move(label));
  }

  void OnPaint(gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1.5f);
    flags.setColor(GetColorProvider()->GetColor(kColorArciumWelcomeText));
    const gfx::RectF bounds(GetLocalBounds());
    canvas->DrawCircle(bounds.CenterPoint(), bounds.width() / 2 - 1, flags);
  }
};

BEGIN_METADATA(SourceMark)
END_METADATA

// One browser that was found, as a tile that chooses it.
class SourceTile : public views::Button {
  METADATA_HEADER(SourceTile, views::Button)

 public:
  SourceTile(WelcomeModel& model, size_t index)
      : views::Button(
            base::BindRepeating(&SourceTile::Choose, base::Unretained(this))),
        model_(model),
        index_(index) {
    const WelcomeSource& source = model.sources()[index];
    const bool chosen = model.chosen_source() == index;
    auto tile = MakeTile(chosen && model.sources().size() > 1);
    SetLayoutManager(std::make_unique<views::FillLayout>());
    auto* row = tile->SetLayoutManager(std::make_unique<views::BoxLayout>());
    row->set_inside_border_insets(gfx::Insets::VH(10, 14));
    row->set_between_child_spacing(14);
    row->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    tile->AddChildView(std::make_unique<SourceMark>(source.name.substr(0, 1)));
    auto* words = tile->AddChildView(std::make_unique<views::BoxLayoutView>());
    words->SetOrientation(views::BoxLayout::Orientation::kVertical);
    words->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStart);
    words->SetBetweenChildSpacing(2);
    words->AddChildView(MakeLabel(source.name, TextStyle::kBodyStrong));
    words->AddChildView(MakeLabel(source.detail, TextStyle::kCaption));
    row->SetFlexForView(words, 1);
    // A browser with several profiles holding spaces asks which one.
    if (chosen && source.profiles.size() > 1) {
      std::vector<ui::SimpleComboboxModel::Item> items;
      for (const std::u16string& profile : source.profiles) {
        items.emplace_back(profile);
      }
      auto* profiles = tile->AddChildView(std::make_unique<views::Combobox>(
          std::make_unique<ui::SimpleComboboxModel>(std::move(items))));
      profiles->SetSelectedIndex(source.profile);
      profiles->GetViewAccessibility().SetName(u"Profile");
      profiles->SetCallback(base::BindRepeating(
          [](WelcomeModel* model, views::Combobox* box) {
            model->ChooseProfile(box->GetSelectedIndex().value_or(0));
          },
          &model, base::Unretained(profiles)));
    }
    AddChildView(std::move(tile));
    GetViewAccessibility().SetName(source.name + u", " + source.detail);
  }

 private:
  void Choose() { model_->ChooseSource(index_); }

  const raw_ref<WelcomeModel> model_;
  const size_t index_;
};

BEGIN_METADATA(SourceTile)
END_METADATA

// One kind of thing the source holds, with a tick to leave it behind.
std::unique_ptr<views::View> KindTile(WelcomeModel& model,
                                      WelcomeKind kind,
                                      const gfx::VectorIcon& icon,
                                      const std::u16string& text,
                                      bool enabled) {
  auto tile = MakeTile();
  auto* row = tile->SetLayoutManager(std::make_unique<views::BoxLayout>());
  row->set_inside_border_insets(gfx::Insets::VH(16, 12));
  row->set_between_child_spacing(10);
  row->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  auto* tick = tile->AddChildView(std::make_unique<AccentCheckbox>());
  tick->SetChecked(enabled && model.kind_chosen(kind));
  tick->SetEnabled(enabled);
  tick->GetViewAccessibility().SetName(text);
  tick->SetCallback(base::BindRepeating(
      [](WelcomeModel* model, WelcomeKind kind, views::Checkbox* tick) {
        model->SetKindChosen(kind, tick->GetChecked());
      },
      &model, kind, base::Unretained(tick)));
  tile->AddChildView(MakeIcon(icon, 18, kColorArciumWelcomeText));
  tile->AddChildView(MakeLabel(text, TextStyle::kBody));
  return tile;
}

std::unique_ptr<views::View> Kinds(WelcomeModel& model,
                                   const WelcomeSource& source) {
  // Two columns of equal width, whatever each tile's words would ask for: a
  // column sized from nothing and given half the room each.
  auto grid = std::make_unique<views::View>();
  const auto column = [](views::TableLayout& table) -> views::TableLayout& {
    return table.AddColumn(views::LayoutAlignment::kStretch,
                           views::LayoutAlignment::kStretch, 1.0f,
                           views::TableLayout::ColumnSize::kFixed, 0, 0);
  };
  views::TableLayout& table =
      *grid->SetLayoutManager(std::make_unique<views::TableLayout>());
  column(table).AddPaddingColumn(views::TableLayout::kFixedSize, 10);
  column(table)
      .AddRows(1, views::TableLayout::kFixedSize)
      .AddPaddingRow(views::TableLayout::kFixedSize, 10)
      .AddRows(1, views::TableLayout::kFixedSize);
  const auto add_row = [&](std::unique_ptr<views::View> a,
                           std::unique_ptr<views::View> b) {
    grid->AddChildView(std::move(a));
    grid->AddChildView(std::move(b));
  };
  // Folders hold pinned tabs; without the tabs there is nothing to file.
  const bool pinned = model.kind_chosen(WelcomeKind::kPinned);
  add_row(
      KindTile(model, WelcomeKind::kSpaces, kSpacesIcon,
               CountOf(source.spaces, u"space", u"spaces"), true),
      KindTile(model, WelcomeKind::kPinned, views::kKeepIcon,
               CountOf(source.pinned, u"pinned tab", u"pinned tabs"), true));
  add_row(
      KindTile(model, WelcomeKind::kFavorites, vector_icons::kStarFilledIcon,
               CountOf(source.favorites, u"favourite", u"favourites"), true),
      KindTile(model, WelcomeKind::kFolders, vector_icons::kFolderFlippableIcon,
               CountOf(source.folders, u"folder", u"folders"), pinned));
  return grid;
}

// Start fresh, with `file` (when there is one) at the far end of its line:
// the two ways of not using what was found, side by side.
std::unique_ptr<views::View> StartFresh(WelcomeModel& model,
                                        std::unique_ptr<views::Link> file) {
  auto box = std::make_unique<views::BoxLayoutView>();
  box->SetOrientation(views::BoxLayout::Orientation::kVertical);
  box->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  box->SetBetweenChildSpacing(2);
  auto* line = box->AddChildView(std::make_unique<views::BoxLayoutView>());
  line->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  auto* fresh = line->AddChildView(
      std::make_unique<AccentRadioButton>(u"Start fresh", kSourceGroup));
  fresh->SetChecked(!model.chosen_source().has_value());
  fresh->SetEnabledTextColors(kColorArciumWelcomeText);
  fresh->SetCallback(base::BindRepeating(
      [](WelcomeModel* model) { model->ChooseSource(std::nullopt); }, &model));
  line->SetFlexForView(line->AddChildView(std::make_unique<views::View>()), 1);
  if (file) {
    line->AddChildView(std::move(file));
  }
  auto* caption = box->AddChildView(MakeLabel(
      u"Set up Kyuzen without importing anything.", TextStyle::kCaption));
  caption->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(0, 28, 0, 0)));
  return box;
}

std::u16string Subtitle(const WelcomeModel& model, bool import_only) {
  if (model.searching()) {
    return u"Looking for Zen and Arc on this Mac…";
  }
  const std::vector<WelcomeSource>& sources = model.sources();
  if (sources.empty()) {
    return u"Nothing to bring over was found on this Mac. Import a file "
           u"from another Mac, or start fresh.";
  }
  if (import_only) {
    return u"Choose what comes with you. Everything you have stays as it is.";
  }
  std::u16string names = sources[0].name;
  for (size_t i = 1; i < sources.size(); ++i) {
    names += (i + 1 == sources.size() ? u" and " : u", ") + sources[i].name;
  }
  return u"We found " + names + u" on this Mac. Choose what comes with you.";
}

}  // namespace

StepContent BuildSetupStep(WelcomeModel& model, bool import_only) {
  StepContent step;
  step.title = import_only ? u"Import from Zen or Arc" : u"Bring your setup";
  step.subtitle = Subtitle(model, import_only);

  auto choices = std::make_unique<views::BoxLayoutView>();
  choices->SetOrientation(views::BoxLayout::Orientation::kVertical);
  choices->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  choices->SetBetweenChildSpacing(12);
  for (size_t i = 0; i < model.sources().size(); ++i) {
    choices->AddChildView(std::make_unique<SourceTile>(model, i));
  }
  const std::optional<size_t> chosen = model.chosen_source();
  if (chosen) {
    choices->AddChildView(Kinds(model, model.sources()[*chosen]));
  }
  // A file brought from another Mac.
  std::unique_ptr<views::Link> file;
  if (!model.searching()) {
    file = std::make_unique<views::Link>(u"Import from a file…");
    file->SetCallback(base::BindRepeating(
        [](WelcomeModel* model) { model->PickFile(); }, &model));
    file->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    file->SetEnabledColor(kColorArciumWelcomeAccent);
  }
  if (!import_only) {
    if (!model.sources().empty()) {
      choices->AddChildView(MakeRule());
    }
    choices->AddChildView(StartFresh(model, std::move(file)));
  } else if (file) {
    choices->AddChildView(std::move(file));
  }
  step.choices = std::move(choices);

  // The spaces on their way in, or the ones Start fresh suggests.
  step.panel_title = u"Your spaces";
  step.panel_subtitle = chosen
                            ? u"Preview from " + model.sources()[*chosen].name
                            : u"To start with";
  auto panel = std::make_unique<views::BoxLayoutView>();
  panel->SetOrientation(views::BoxLayout::Orientation::kVertical);
  panel->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  panel->SetBetweenChildSpacing(10);
  const std::vector<WelcomeSpace> spaces = model.spaces();
  for (size_t i = 0; i < spaces.size() && i < kPreviewSpaces; ++i) {
    auto* tile = panel->AddChildView(MakeTile());
    tile->SetLayoutManager(std::make_unique<views::FillLayout>());
    auto row = MakeSpaceRow(
        spaces[i].icon, spaces[i].name,
        chosen ? CountOf(spaces[i].pinned, u"pinned tab", u"pinned tabs")
               : spaces[i].origin);
    row->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(10, 14)));
    tile->AddChildView(std::move(row));
  }
  if (spaces.size() > kPreviewSpaces) {
    panel->AddChildView(MakeLabel(
        CountOf(spaces.size() - kPreviewSpaces, u"more space", u"more spaces"),
        TextStyle::kCaption));
  }
  step.panel = std::move(panel);
  step.panel_note = u"Your spaces will appear in the sidebar.";
  return step;
}

}  // namespace arcium::welcome
