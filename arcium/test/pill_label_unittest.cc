// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/pill_label.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

TEST(PillLabelTest, DropsAWwwThatIsAPrefixAndNotAName) {
  EXPECT_EQ(u"google.com", PillLabel(GURL("https://www.google.com/")));
  // "www.com" is a name in its own right: dropping the prefix would leave
  // "com", which is not where the reader is.
  EXPECT_EQ(u"www.com", PillLabel(GURL("https://www.com/")));
}

TEST(PillLabelTest, KeepsASubdomain) {
  // The registrable domain would say google.com here, which would be the bar
  // telling the reader they are somewhere they are not.
  EXPECT_EQ(u"mail.google.com",
            PillLabel(GURL("https://mail.google.com/mail/u/0")));
}

TEST(PillLabelTest, KeepsAPortAndAnAddress) {
  EXPECT_EQ(u"localhost:8899", PillLabel(GURL("http://localhost:8899/x")));
  EXPECT_EQ(u"127.0.0.1", PillLabel(GURL("https://127.0.0.1/")));
}

TEST(PillLabelTest, LeavesPunycodeAsPunycode) {
  // Unicode here is how a spoofed host hides. Punycode is ugly and honest.
  EXPECT_EQ(u"xn--80ak6aa92e.com",
            PillLabel(GURL("https://xn--80ak6aa92e.com/")));
}

TEST(PillLabelTest, SaysNewTabForAPageThatIsNotThereYet) {
  EXPECT_EQ(u"New tab", PillLabel(GURL()));
  EXPECT_EQ(u"New tab", PillLabel(GURL("about:blank")));
  EXPECT_EQ(u"New tab", PillLabel(GURL("chrome://newtab/")));
  EXPECT_EQ(u"New tab", PillLabel(GURL("chrome://new-tab-page/")));
}

TEST(PillLabelTest, NamesTheFileForALocalFile) {
  // The name is what the reader recognises; the path it sits in would not fit
  // the pill and is one keystroke away in the box.
  EXPECT_EQ(u"x.html", PillLabel(GURL("file:///tmp/x.html")));
  EXPECT_EQ(u"a report.pdf",
            PillLabel(GURL("file:///Users/me/a%20report.pdf")));
  // A directory listing ends in a slash and has no file to name.
  EXPECT_EQ(u"Local file", PillLabel(GURL("file:///tmp/")));
}

TEST(PillLabelTest, NamesTheBrowserPageForAPageOfItsOwn) {
  EXPECT_EQ(u"chrome://settings", PillLabel(GURL("chrome://settings")));
  EXPECT_EQ(u"chrome://extensions",
            PillLabel(GURL("chrome://extensions/shortcuts")));
  // A scheme with nothing host-shaped under it still says which scheme it is,
  // because an empty pill says nothing at all.
  EXPECT_EQ(u"data:", PillLabel(GURL("data:text/html,hello")));
}

TEST(PillLabelTest, SplitsOutTheDomainAPersonShouldTrust) {
  const PillLabelParts parts =
      SplitPillLabel(GURL("https://en.wikipedia.org/wiki/X"));
  EXPECT_EQ(u"en.", parts.prefix);
  EXPECT_EQ(u"wikipedia.org", parts.domain);
  EXPECT_EQ(u"", parts.suffix);
}

TEST(PillLabelTest, ASuffixOfSeveralPartsStaysWithItsDomain) {
  // Cut at the last dot, this would brighten "co.uk", which is a registry
  // and says nothing about who the reader is talking to.
  const PillLabelParts parts =
      SplitPillLabel(GURL("https://docs.example.co.uk/"));
  EXPECT_EQ(u"docs.", parts.prefix);
  EXPECT_EQ(u"example.co.uk", parts.domain);
  EXPECT_EQ(u"", parts.suffix);
}

TEST(PillLabelTest, APortIsDimmedBehindTheDomain) {
  const PillLabelParts parts =
      SplitPillLabel(GURL("https://docs.example.co.uk:8443/"));
  EXPECT_EQ(u"docs.", parts.prefix);
  EXPECT_EQ(u"example.co.uk", parts.domain);
  EXPECT_EQ(u":8443", parts.suffix);
}

TEST(PillLabelTest, TheRunsSpellTheWholeLabel) {
  // Only the emphasis changes; nothing the label says is dropped.
  for (const char* spec :
       {"https://www.google.com/", "https://mail.google.com:444/",
        "https://docs.example.co.uk/", "http://localhost:8899/",
        "https://127.0.0.1/", "chrome://settings", "file:///tmp/x.html"}) {
    const GURL url(spec);
    const PillLabelParts parts = SplitPillLabel(url);
    EXPECT_EQ(PillLabel(url), parts.prefix + parts.domain + parts.suffix)
        << spec;
  }
}

TEST(PillLabelTest, AnythingWithoutARegistrableDomainIsAllDomain) {
  for (const char* spec : {"http://localhost:8899/x", "https://127.0.0.1/",
                           "chrome://settings", "file:///tmp/x.html"}) {
    const GURL url(spec);
    const PillLabelParts parts = SplitPillLabel(url);
    EXPECT_EQ(u"", parts.prefix) << spec;
    EXPECT_EQ(u"", parts.suffix) << spec;
  }
}

}  // namespace
}  // namespace arcium
