// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/loose_page.h"

#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"

namespace arcium {

namespace {

// On the WebContents, beside the space tag, because the page is marked before
// it becomes a tab: there is no TabInterface yet to hang it on.
class LoosePageData : public content::WebContentsUserData<LoosePageData> {
 public:
  ~LoosePageData() override = default;

  LoosePageKind kind = LoosePageKind::kNone;

 private:
  friend class content::WebContentsUserData<LoosePageData>;
  explicit LoosePageData(content::WebContents* web_contents)
      : content::WebContentsUserData<LoosePageData>(*web_contents) {}

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(LoosePageData);

}  // namespace

LoosePageKind LoosePageKindOf(const content::WebContents* web_contents) {
  if (!web_contents) {
    return LoosePageKind::kNone;
  }
  const LoosePageData* data = LoosePageData::FromWebContents(web_contents);
  return data ? data->kind : LoosePageKind::kNone;
}

bool IsLoosePage(const content::WebContents* web_contents) {
  return LoosePageKindOf(web_contents) != LoosePageKind::kNone;
}

void SetLoosePageKind(content::WebContents* web_contents, LoosePageKind kind) {
  LoosePageData::CreateForWebContents(web_contents);
  LoosePageData::FromWebContents(web_contents)->kind = kind;
}

}  // namespace arcium
