/*
* Copyright (c) 2018 Rony Shapiro <ronys@pwsafe.org>.
* All rights reserved. Use of the code is allowed under the
* Artistic License 2.0 terms, as specified in the LICENSE file
* distributed with this code, or available from
* http://www.opensource.org/licenses/artistic-license-2.0.php
*/
// UtilTest.cpp: Unit test for selected functions in Util.cpp

#include "core/Util.h"
#include "gtest/gtest.h"

TEST(UtilTest1, convert_test_ascii)
{
  StringX src(L"abc");
  unsigned char *dst = nullptr;
  size_t dst_size = 0;
  ConvertString(src, dst, dst_size);
  EXPECT_EQ(dst_size, 3);
  EXPECT_STREQ("abc", reinterpret_cast<const char *>(dst));
}

TEST(UtilTest2, convert_test_nonascii)
{
  StringX src(L"אבג");
  unsigned char *dst = nullptr;
  size_t dst_size = 0;
  ConvertString(src, dst, dst_size);
  EXPECT_EQ(dst_size, 3);
  EXPECT_STREQ("אבג", reinterpret_cast<const char *>(dst));
}
