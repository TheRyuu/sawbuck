// Copyright 2012 Google Inc.
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
#include "syzygy/pe/find.h"

#include "base/win/scoped_handle.h"
#include "gtest/gtest.h"
#include "sawbuck/common/com_utils.h"
#include "syzygy/core/file_util.h"
#include "syzygy/core/unittest_util.h"
#include "syzygy/pe/unittest_util.h"

namespace pe {

namespace {

// A utility for ensuring that two file paths point to the same file. Upon
// failure, outputs the actual paths as well.
::testing::AssertionResult AssertAreSameFile(const char* path1_expr,
                                             const char* path2_expr,
                                             const FilePath& path1,
                                             const FilePath& path2) {
  core::FilePathCompareResult result = core::CompareFilePaths(path1, path2);
  if (result == core::kEquivalentFilePaths)
    return ::testing::AssertionSuccess();

  return ::testing::AssertionFailure() << "FilePathsReferToSameFile("
      << path1_expr << ", " << path2_expr << ") returned " << result
      << ", expected " << core::kEquivalentFilePaths << " (" << path1_expr
      << " = \"" << path1.value() << "\", " << path2_expr << " = \""
      << path2.value() << "\").";
}

// A gtest-like macro for ensuring two paths refer to the same file.
#define EXPECT_SAME_FILE(path1, path2) \
    EXPECT_PRED_FORMAT2(AssertAreSameFile, path1, path2)

class FindTest: public testing::PELibUnitTest {
  // Insert your customizations here.
};

}  // namespace

TEST_F(FindTest, PeAndPdbAreMatchedMissingFiles) {
  EXPECT_FALSE(PeAndPdbAreMatched(
      FilePath(L"nonexistent_pe_file.dll"),
      FilePath(L"nonexistent_pdb_file.pdb")));
}

TEST_F(FindTest, PeAndPdbAreMatchedMismatchedInputs) {
  EXPECT_FALSE(PeAndPdbAreMatched(
      testing::GetOutputRelativePath(kDllName),
      testing::GetOutputRelativePath(L"pe_unittests.pdb")));
}

TEST_F(FindTest, PeAndPdbAreMatched) {
  EXPECT_TRUE(PeAndPdbAreMatched(
      testing::GetOutputRelativePath(kDllName),
      testing::GetOutputRelativePath(kDllPdbName)));
}

TEST_F(FindTest, FindTestDll) {
  const FilePath module_path(testing::GetOutputRelativePath(kDllName));

  PEFile pe_file;
  ASSERT_TRUE(pe_file.Init(module_path));

  PEFile::Signature module_signature;
  pe_file.GetSignature(&module_signature);

  FilePath found_path;
  EXPECT_TRUE(FindModuleBySignature(module_signature, &found_path));

  EXPECT_SAME_FILE(module_path, found_path);
}

TEST_F(FindTest, FindTestDllPdb) {
  // We have to be careful to use the output relative path, rather than simply
  // the executable relative path. This is because in the coverage unittests
  // pe_unittests.exe and test_dll.dll are copied to a new output directory
  // that contains the instrumented binaries. The copied test_dll.dll still
  // refers to the original test_dll.pdb in the Debug or Release output
  // directory, so that's the one that will be found first.
  const FilePath module_path(testing::GetOutputRelativePath(kDllName));
  const FilePath pdb_path(testing::GetOutputRelativePath(kDllPdbName));

  FilePath found_path;
  EXPECT_TRUE(FindPdbForModule(module_path, &found_path));

  EXPECT_SAME_FILE(pdb_path, found_path);
}

}  // namespace pe
