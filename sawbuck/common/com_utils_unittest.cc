// Copyright 2011 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Unit tests for COM utils.

#include "sawbuck/common/com_utils.h"

#include <atlcomcli.h>

#include "testing/gtest/include/gtest/gtest.h"

namespace {

TEST(ComUtils, AlwaysError) {
  EXPECT_EQ(com::AlwaysError(S_OK, E_INVALIDARG), E_INVALIDARG);
  EXPECT_EQ(com::AlwaysError(E_FAIL, E_INVALIDARG), E_FAIL);

  EXPECT_EQ(com::AlwaysError(S_OK), E_FAIL);
  EXPECT_EQ(com::AlwaysError(E_FAIL), E_FAIL);

  EXPECT_EQ(com::AlwaysErrorFromWin32(NO_ERROR, E_INVALIDARG), E_INVALIDARG);
  EXPECT_EQ(com::AlwaysErrorFromWin32(NO_ERROR), E_FAIL);

  EXPECT_EQ(com::AlwaysErrorFromWin32(ERROR_ACCESS_DENIED, E_INVALIDARG),
            HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
  EXPECT_EQ(com::AlwaysErrorFromWin32(ERROR_ACCESS_DENIED),
            HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));

  DWORD last_error = ::GetLastError();

  ::SetLastError(NO_ERROR);
  EXPECT_EQ(com::AlwaysErrorFromLastError(E_INVALIDARG), E_INVALIDARG);
  EXPECT_EQ(com::AlwaysErrorFromLastError(), E_FAIL);

  ::SetLastError(ERROR_ACCESS_DENIED);
  EXPECT_EQ(com::AlwaysErrorFromLastError(E_INVALIDARG),
            HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
  EXPECT_EQ(com::AlwaysErrorFromLastError(),
            HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));

  ::SetLastError(last_error);
}

TEST(ComUtils, ToString) {
  CComBSTR filled(L"hello");
  EXPECT_STREQ(com::ToString(filled), L"hello");

  CComBSTR empty;
  EXPECT_STREQ(com::ToString(empty), L"");
}

TEST(ComUtils, HrLog) {
  {
    std::ostringstream stream;
    stream << com::LogHr(S_OK);
    std::string str = stream.str();
    EXPECT_NE(str.find("0x0,"), std::string::npos);
    EXPECT_NE(str.find("msg="), std::string::npos);
  }

  {
    std::ostringstream stream;
    stream << com::LogHr(E_FAIL);
    std::string str = stream.str();
    EXPECT_NE(str.find("0x80004005,"), std::string::npos);
    EXPECT_NE(str.find("msg=Unspecified error"), std::string::npos);
  }
}

TEST(ComUtils, WeLog) {
  {
    std::ostringstream stream;
    stream << com::LogWe(ERROR_SUCCESS);
    std::string str = stream.str();
    EXPECT_NE(str.find("[we=0,"), std::string::npos);
    EXPECT_NE(str.find("msg=The operation completed successfully"),
              std::string::npos);
  }

  {
    std::ostringstream stream;
    stream << com::LogWe(ERROR_INVALID_FUNCTION);
    std::string str = stream.str();
    EXPECT_NE(str.find("[we=1,"), std::string::npos);
    EXPECT_NE(str.find("msg=Incorrect function"), std::string::npos);
  }
}

}  // namespace
