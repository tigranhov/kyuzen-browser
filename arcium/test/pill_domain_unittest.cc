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
  // "com", which is not where the reader is.
  EXPECT_EQ(u"www.com", PillDomain(GURL("https://www.com/")));
}

TEST(PillDomainTest, KeepsASubdomain) {
  // The registrable domain would say google.com here, which would be the bar
  // telling the reader they are somewhere they are not.
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
