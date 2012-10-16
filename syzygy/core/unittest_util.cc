// Copyright 2012 Google Inc. All Rights Reserved.
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

#include "syzygy/core/unittest_util.h"

#include "base/path_service.h"

namespace testing {

FilePath GetSrcRelativePath(const wchar_t* rel_path) {
  FilePath src_dir;
  PathService::Get(base::DIR_SOURCE_ROOT, &src_dir);
  return src_dir.Append(rel_path);
}

FilePath GetExeRelativePath(const wchar_t* rel_path) {
  FilePath exe_dir;
  PathService::Get(base::DIR_EXE, &exe_dir);
  return exe_dir.Append(rel_path);
}

FilePath GetOutputRelativePath(const wchar_t* rel_path) {
#if defined(_DEBUG)
  // TODO(chrisha): Expose $(ProjectDir) and $(OutputDir) via defines in the
  //     project gyp file.
  #if defined(_COVERAGE_BUILD)
    static const wchar_t kOutputDir[] = L"Coverage";
  #else
    static const wchar_t kOutputDir[] = L"Debug";
  #endif
#else
#if defined(NDEBUG)
  static const wchar_t kOutputDir[] = L"Release";
#else
#error Unknown build profile.
#endif
#endif

  FilePath src_dir;
  PathService::Get(base::DIR_SOURCE_ROOT, &src_dir);
  src_dir = src_dir.Append(L"build");
  src_dir = src_dir.Append(kOutputDir);
  return src_dir.Append(rel_path);
}

FilePath GetExeTestDataRelativePath(const wchar_t* rel_path) {
  FilePath exe_dir;
  PathService::Get(base::DIR_EXE, &exe_dir);
  FilePath test_data = exe_dir.Append(L"test_data");
  return test_data.Append(rel_path);
}

FilePath GetRelativePath(const FilePath& abs_path, const FilePath& root_path) {
  DCHECK(abs_path.IsAbsolute());
  DCHECK(!abs_path.empty());
  DCHECK(root_path.IsAbsolute());
  DCHECK(!root_path.empty());

  typedef std::vector<FilePath::StringType> PathComponents;

  // Get the components of the target path.
  PathComponents abs_parts;
  abs_path.GetComponents(&abs_parts);

  // Get the components of the current working directory.
  PathComponents root_parts;
  root_path.GetComponents(&root_parts);

  // Make sure they have a common root.
  if (!FilePath::CompareEqualIgnoreCase(root_parts[0], abs_parts[0]))
    return FilePath();

  // Figure out how much is shared.
  size_t i = 1;
  while (i < std::min(root_parts.size(), abs_parts.size()) &&
         FilePath::CompareEqualIgnoreCase(root_parts[i], abs_parts[i])) {
    ++i;
  }

  FilePath rel_path;

  // Add parent directory traversal.
  for (size_t j = i; j < root_parts.size(); ++j)
    rel_path = rel_path.Append(FilePath::kParentDirectory);

  // Append the rest of the path.
  for (size_t k = i; k < abs_parts.size(); ++k)
    rel_path = rel_path.Append(abs_parts[k]);

  if (rel_path.empty())
    rel_path = FilePath(FilePath::kCurrentDirectory);

  return rel_path;
}

FilePath GetRelativePath(const FilePath& abs_path) {
  FilePath cur_dir;
  file_util::GetCurrentDirectory(&cur_dir);
  return GetRelativePath(abs_path, cur_dir);
}


}  // namespace testing
