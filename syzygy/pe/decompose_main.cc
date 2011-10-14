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
// Decomposes an image, dumping the DecomposedImage to file.

#include <iostream>
#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/string_util.h"
#include "base/time.h"
#include "syzygy/core/block_graph.h"
#include "syzygy/pe/decomposer.h"
#include "syzygy/pe/pe_file.h"

namespace {

int Usage(char** argv, const char* message) {
  if (message != NULL) {
    std::cout << message << std::endl << std::endl;
  }

  std::cout <<
      "Usage: " << argv[0] << " [options]" << std::endl;
  std::cout <<
      "  A tool that uses symbol information and disassembly to decompose a\n"
      "  PE image file into discrete blocks of code (and data), and to infer\n"
      "  the references between them.\n"
      "\n"
      "Required parameters\n"
      "  --image=<image file>\n"
      "Optional parameters\n"
      "  --bb\t(Enables basic block decomposition)\n"
      "  --missing-contribs=<output file>\n"
      "    Outputs a list of blocks (and their symbol information) that were\n"
      "    not parsed from section contributions.\n"
      "  --output=<output file>\n"
      "    The location of output file. If not specified, will append\n"
      "    '.bg' to the image file.\n"
      "  --benchmark-load\n"
      "    Causes the output to be deserialized after serialization,\n"
      "    for benchmarking.\n";

  return 1;
}

typedef std::set<const core::BlockGraph::Block*> BlockSet;

bool DumpBlockSet(const BlockSet& set, FILE* file) {
  DCHECK(file != NULL);

  BlockSet::const_iterator it = set.begin();
  for (; it != set.end(); ++it) {
    if (fprintf(file, "    0x%08X: %s (%s)\n", (*it)->addr().value(),
                (*it)->name(),
                core::BlockGraph::kBlockType[(*it)->type()]) < 0) {
      return false;
    }
  }

  return true;
}

bool DumpBlock(const core::BlockGraph::Block* block, FILE* file) {
  DCHECK(block != NULL);
  DCHECK(file != NULL);

  size_t base = block->addr().value();
  if (fprintf(file, "0x%08X(%d): %s (%s)\n", base, block->size(),
              block->name(), core::BlockGraph::kBlockType[block->type()]) < 0) {
    return false;
  }

  // Dump any labels.
  if (block->labels().size() > 0) {
    core::BlockGraph::Block::LabelMap::const_iterator label_it =
        block->labels().begin();
    if (fprintf(file, "  Labels:\n") < 0)
      return false;
    for (; label_it != block->labels().end(); ++label_it) {
      if (fprintf(file, "    0x%08x: %s\n", base + label_it->first,
                  label_it->second.c_str()) < 0)
        return false;
    }
  }

  // Get a list of incoming referrers. We don't care about offsets, but rather
  // unique referring blocks.
  if (block->referrers().size() > 0) {
    BlockSet blocks;
    core::BlockGraph::Block::ReferrerSet::const_iterator iref_it =
        block->referrers().begin();
    for (; iref_it != block->referrers().end(); ++iref_it) {
      DCHECK(iref_it->first != NULL);
      blocks.insert(iref_it->first);
    }
    if (fprintf(file, "  Referrers:\n") < 0 ||
        !DumpBlockSet(blocks, file))
      return false;
  }

  // Dump the outgoing references. Once again, we don't really care about
  // offsets.
  if (block->references().size() > 0) {
    BlockSet blocks;
    core::BlockGraph::Block::ReferenceMap::const_iterator oref_it =
        block->references().begin();
    for (; oref_it != block->references().end(); ++oref_it) {
      DCHECK(oref_it->second.referenced() != NULL);
      blocks.insert(oref_it->second.referenced());
    }
    if (fprintf(file, "  References:\n") < 0 ||
        !DumpBlockSet(blocks, file))
      return false;
  }

  return true;
}

bool DumpMissingSectionContributions(
    const FilePath& path,
    const pe::Decomposer::DecomposedImage& image) {
  file_util::ScopedFILE out_file(file_util::OpenFile(path, "wb"));
  core::BlockGraph::AddressSpace::RangeMapConstIter it =
      image.address_space.begin();

  core::BlockGraph::BlockAttributes skip_mask =
      core::BlockGraph::SECTION_CONTRIB | core::BlockGraph::PADDING_BLOCK |
      core::BlockGraph::PE_PARSED;

  for (; it != image.address_space.end(); ++it) {
    const core::BlockGraph::Block* block = it->second;
    if (block->attributes() & skip_mask)
      continue;

    if (!DumpBlock(block, out_file.get())) {
      LOG(ERROR) << "DumpBlock failed.";
      return false;
    }
  }

  return true;
}

}  // namespace

int main(int argc, char** argv) {
  HRESULT hr = ::CoInitialize(NULL);
  if (FAILED(hr)) {
    LOG(ERROR) << "CoInitialize failed with " << hr;
    return 1;
  }

  base::AtExitManager at_exit_manager;
  CommandLine::Init(argc, argv);

  if (!logging::InitLogging(L"", logging::LOG_ONLY_TO_SYSTEM_DEBUG_LOG,
      logging::DONT_LOCK_LOG_FILE, logging::APPEND_TO_OLD_LOG_FILE,
      logging::ENABLE_DCHECK_FOR_NON_OFFICIAL_RELEASE_BUILDS)) {
    return 1;
  }

  CommandLine* cmd_line = CommandLine::ForCurrentProcess();
  DCHECK(cmd_line != NULL);

  FilePath image = cmd_line->GetSwitchValuePath("image");
  if (image.empty())
    return Usage(argv, "Must specify '--image' parameter!");

  // If no output file specified, use default.
  FilePath output = cmd_line->GetSwitchValuePath("output");
  if (output.empty()) {
    output = FilePath(image.value() + L".bg");
    LOG(INFO) << "Inferring output path from image path.";
  }

  FilePath missing_contribs = cmd_line->GetSwitchValuePath("missing-contribs");
  bool benchmark_load = cmd_line->HasSwitch("benchmark-load");

  pe::Decomposer::Mode mode = cmd_line->HasSwitch("bb") ?
      pe::Decomposer::BASIC_BLOCK_DECOMPOSITION :
      pe::Decomposer::STANDARD_DECOMPOSITION;

  LOG(INFO) << "Processing \"" << image.value() << "\".\n";
  LOG(INFO) << "Parsing PE file.\n";
  base::Time time = base::Time::Now();
  pe::PEFile pe_file;
  if (!pe_file.Init(image))
    return 1;
  LOG(INFO) << "Parsing PE file took " <<
      (base::Time::Now() - time).InSecondsF() << " seconds.";

  LOG(INFO) << "Decomposing image.";
  time = base::Time::Now();
  pe::Decomposer::DecomposedImage decomposed_image;
  pe::Decomposer decomposer(pe_file, image);
  if (!decomposer.Decompose(&decomposed_image, NULL, mode)) {
    LOG(ERROR) << "Decomposition failed.";
    return 1;
  }
  LOG(INFO) << "Decomposing image took " <<
      (base::Time::Now() - time).InSecondsF() << " seconds.";

  if (!missing_contribs.empty()) {
    LOG(INFO) << "Writing missing section contributions to \""
              << missing_contribs.value().c_str() << "\".\n";
    if (!DumpMissingSectionContributions(missing_contribs, decomposed_image))
      return 1;
  }

  // This is scoped so that the output file is closed prior to loading it.
  {
    LOG(INFO) << "Saving decomposed image to \"" << output.value().c_str()
              << "\".\n";
    time = base::Time::Now();
    file_util::ScopedFILE out_file(file_util::OpenFile(output, "wb"));
    core::FileOutStream out_stream(out_file.get());
    core::NativeBinaryOutArchive out_archive(&out_stream);
    if (!pe::SaveDecomposition(pe_file, decomposed_image, &out_archive))
      return 1;
    LOG(INFO) << "Saving decomposed image took " <<
        (base::Time::Now() - time).InSecondsF() << " seconds.";
  }

  if (benchmark_load) {
    pe::PEFile in_pe_file;
    pe::Decomposer::DecomposedImage in_image;

    LOG(INFO) << "Benchmarking decomposed image load.\n";
    time = base::Time::Now();
    file_util::ScopedFILE in_file(file_util::OpenFile(output, "rb"));
    core::FileInStream in_stream(in_file.get());
    core::NativeBinaryInArchive in_archive(&in_stream);
    if (!pe::LoadDecomposition(&in_pe_file, &in_image, &in_archive))
      return 1;
    LOG(INFO) << "Loading decomposed image took " <<
        (base::Time::Now() - time).InSecondsF() << " seconds.";
  }

  return 0;
}
