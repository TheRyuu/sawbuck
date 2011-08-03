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
// The decomposer decomposes a given image file into a series of blocks
// and references by reference to the image's symbols and disassembled
// executable code.
#ifndef SYZYGY_PE_DECOMPOSER_H_
#define SYZYGY_PE_DECOMPOSER_H_

#include <windows.h>
#include <dbghelp.h>
#include <dia2.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "base/file_path.h"
#include "pcrecpp.h"  // NOLINT
#include "syzygy/core/basic_block_disassembler.h"
#include "syzygy/core/block_graph.h"
#include "syzygy/core/disassembler.h"
#include "syzygy/core/serialization.h"
#include "syzygy/pdb/pdb_data.h"
#include "syzygy/pe/dia_browser.h"
#include "syzygy/pe/pe_file.h"
#include "syzygy/pe/pe_file_parser.h"

namespace pe {

using pcrecpp::RE;

class Decomposer {
 public:
  // The decomposed image data.
  class DecomposedImage;
  // A struct for storing fixups.
  struct Fixup;
  // Used for storing references before the block graph is complete.
  struct IntermediateReference;
  // Statistics regarding the decomposition.
  struct CoverageStatistics;
  struct DetailedCodeBlockStatistics;

  typedef core::RelativeAddress RelativeAddress;
  typedef core::AddressSpace<RelativeAddress, size_t, std::string> DataSpace;
  typedef core::BasicBlockDisassembler BasicBlockDisassembler;
  typedef core::BlockGraph BlockGraph;
  typedef core::Disassembler Disassembler;
  typedef std::map<RelativeAddress, Fixup> FixupMap;
  typedef std::map<RelativeAddress, IntermediateReference>
      IntermediateReferenceMap;

  enum Mode {
    STANDARD_DECOMPOSITION,
    BASIC_BLOCK_DECOMPOSITION,
  };

  // Initializes the decomposer for a given image file and path.
  Decomposer(const PEFile& image_file, const FilePath& file_path);

  // Decomposes the image file into the specified DecomposedImage, which
  // has the breakdown of code and data blocks with typed references.
  // @returns true on success, false on failure. If @p stats is non-null, it
  // will be populated with decomposition coverage statistics.
  // If |decomposition_mode| is BASIC_BLOCK_DECOMPOSITION then a basic block
  // decomposition will also be performed.
  bool Decompose(DecomposedImage* image,
                 CoverageStatistics* stats,
                 Mode decomposition_mode);

  // Registers a pair of static initializer search patterns. Each of these
  // patterns will be converted to a regular expression, and they are required
  // to produce exactly one match group. The match group must be the same for
  // each of the patterns in order for the symbols to be correlated to each
  // other.
  // TODO(chrisha): Expose a mechanism for bulk-importing these via some JSON
  //     representation. We will likely want to expose this on the command-line
  //     of any utility using Decomposer.
  bool RegisterStaticInitializerPatterns(const char* begin, const char* end);

 protected:
  typedef std::map<RelativeAddress, std::string> DataLabels;
  typedef std::vector<pdb::PdbFixup> PdbFixups;

  // Create blocks for all code.
  bool CreateCodeBlocks(IDiaSymbol* globals);
  // Create blocks for all functions in @p globals.
  bool CreateFunctionBlocks(IDiaSymbol* globals);
  // Create a function block for @p function.
  // @pre @p function is a function or a thunk.
  bool CreateFunctionBlock(IDiaSymbol* function);
  // Create labels for @p function, which corresponds to @p block.
  bool CreateLabelsForFunction(IDiaSymbol* function, BlockGraph::Block* block);
  // Create blocks for all thunks in @p globals.
  // @note thunks are offspring of Compilands.
  bool CreateThunkBlocks(IDiaSymbol* globals);
  // Enumerates labels in @p globals and adds them to the corresponding (code)
  // blocks.
  bool CreateGlobalLabels(IDiaSymbol* globals);

  // Creates a gap block of type @p block_type for the given range. For use by
  // CreateSectionGapBlocks.
  bool CreateGapBlock(BlockGraph::BlockType block_type,
                      RelativeAddress address,
                      BlockGraph::Size size);
  // Create blocks of type @p block_type for any gaps in the image
  // section represented by @p header.
  bool CreateSectionGapBlocks(const IMAGE_SECTION_HEADER* header,
                              BlockGraph::BlockType block_type);

  // Processes the SectionContribution table, creating code/data blocks from it.
  bool CreateBlocksFromSectionContribs(IDiaSession* session);

    // Creates data blocks.
  bool CreateDataBlocks(IDiaSymbol* global);
  // Creates data gap blocks.
  bool CreateDataGapBlocks();
  // Guesses data block alignments and padding.
  bool GuessDataBlockAlignments();
  // Process static initializer data labels, ensuring they remain contiguous.
  bool ProcessStaticInitializers();

  // These process symbols in the DIA tree via DiaBrowser and the following
  // callbacks.
  bool ProcessDataSymbols(IDiaSymbol* root);
  bool ProcessPublicSymbols(IDiaSymbol* root);

  // DiaBrowser callbacks.
  void OnDataSymbol(const DiaBrowser& dia_browser,
                    const DiaBrowser::SymTagVector& sym_tags,
                    const DiaBrowser::SymbolPtrVector& symbols,
                    DiaBrowser::BrowserDirective* directive);
  void OnPublicSymbol(const DiaBrowser& dia_browser,
                      const DiaBrowser::SymTagVector& sym_tags,
                      const DiaBrowser::SymbolPtrVector& symbols,
                      DiaBrowser::BrowserDirective* directive);

  // Translates intermediate references to block->block references.
  bool FinalizeIntermediateReferences();

  // Checks that the fixups were all visited.
  bool ConfirmFixupsVisited() const;

  // Searches through the final block graph, and labels blocks that are
  // simply padding blocks. This must be called after all references are
  // finalized.
  bool FindPaddingBlocks();

  // Invokable once we have completed our original block graphs, this breaks
  // up code-blocks into their basic sub-components.
  bool BuildBasicBlockGraph(DecomposedImage* decomposed_image);

  // Parses the various debug streams. This populates fixup_map_ as well.
  bool LoadDebugStreams(IDiaSession* dia_session,
                        std::vector<OMAP>* omap_to,
                        std::vector<OMAP>* omap_from);

  // Validates a reference against a matching fixup, or creates a new
  // intermediate reference from @p src_addr to @p dst_addr of
  // type @p type and size @p size with optional name @p name. This assumes
  // an offset of zero.
  void AddReferenceCallback(RelativeAddress src_addr,
                            BlockGraph::ReferenceType type,
                            BlockGraph::Size size,
                            RelativeAddress dst_addr,
                            const char* name);
  // Parse the relocation entries.
  bool ParseRelocs();
  // Uses the fixup map to create cross-block references. These contain
  // relative references, lookup tables, absolute references, PC-relative from
  // code references, etc.
  bool CreateReferencesFromFixups();
  // Walk relocations and validate them against the fixups.
  bool ValidateRelocs(const PEFile::RelocMap& reloc_map);
  // Creates an initial set of code labels from fixups.
  bool CreateCodeLabelsFromFixups();
  // Disassemble all code blocks and create code->code references.
  bool CreateCodeReferences();
  // Disassemble @p block and invoke @p on_instruction for each instruction
  // encountered.
  bool CreateCodeReferencesForBlock(BlockGraph::Block* block);

  // Schedules the address range covering block1 and block2 for merging.
  void ScheduleForMerging(BlockGraph::Block* block1, BlockGraph::Block* block2);

  // Parses the PE BlockGraph header and other important PE structures,
  // adds them as blocks to the image, and creates the references
  // they contain.
  bool CreatePEImageBlocksAndReferences(PEFileParser::PEHeader* header);

  // Creates a new block with the given properties, and attaches the
  // data to it. This assumes that no conflicting block exists.
  BlockGraph::Block* CreateBlock(BlockGraph::BlockType type,
                                 RelativeAddress address,
                                 BlockGraph::Size size,
                                 const char* name);

  enum FindOrCreateBlockDirective {
    // Expect that no block exists in the given range and that a block will be
    // created.
    kExpectNoBlock,
    // Allow the existence of a block with identical range to that provided.
    kAllowIdenticalBlock,
    // Allow the existence of a block that completely covers the provided range.
    kAllowCoveringBlock,
  };
  // Create block for the given @p address and @p size of the given @p type,
  // or return an existant block that has the same @p type, @p address and
  // @p size. Care must be taken in using the returned block. Regardless of the
  // provided directive, the block that is returned may be a strict superset
  // of the requested range, and offsets into it may need to be calculated.
  // @returns the block created or found, or NULL if there's a conflicting block
  //    for the address range.
  BlockGraph::Block* FindOrCreateBlock(BlockGraph::BlockType type,
                                       RelativeAddress address,
                                       BlockGraph::Size size,
                                       const char* name,
                                       FindOrCreateBlockDirective directive);

  // Called through a callback during function disassembly.
  void OnInstruction(const Disassembler& disassembler,
                     const _DInst& instruction,
                     Disassembler::CallbackDirective* directive);
  // Called through a callback during function disassembly.
  void OnBasicInstruction(const Disassembler& disassembler,
                          const _DInst& instruction,
                          Disassembler::CallbackDirective* directive);

  // Repairs the DIA "FIXUPS" with any loaded OMAP information, validates them,
  // and stores them in the given FixupMap.
  bool OmapAndValidateFixups(const std::vector<OMAP>& omap_from,
                             const PdbFixups& pdb_fixups);

  // After a successful decomposition, this will calculate statistics regarding
  // the coverage of our decomposition. This expects image_ to be non-NULL.
  void CalcCoverageStatistics(CoverageStatistics* stats) const;
  // Updates coverage statistics with information regarding the given block.
  void CalcBlockStats(const BlockGraph::Block* block,
                      CoverageStatistics* stats) const;

  // The image address space we're decomposing to.
  BlockGraph::AddressSpace* image_;

  // The image file we're decomposing and its path.
  // Note that the resultant BlockGraph will contain pointers to the
  // data in the image file, so the user must ensure the image file
  // outlives the BlockGraph.
  const PEFile& image_file_;
  FilePath file_path_;

  // Stores intermediate references before the block graph is complete.
  IntermediateReferenceMap references_;

  typedef std::set<BlockGraph::Block*> BlockSet;
  typedef std::set<BlockGraph::AddressSpace::Range> RangeSet;
  typedef std::map<BlockGraph::BlockId, DetailedCodeBlockStatistics>
      DetailedCodeBlockStatsMap;
  typedef std::map<RelativeAddress, std::string> LabelMap;
  typedef std::set<RelativeAddress> RelativeAddressSet;
  typedef std::pair<RE, RE> REPair;
  typedef std::vector<REPair> REPairs;

  // The block we're currently disassembling.
  BlockGraph::Block* current_block_;
  // Keeps track of which blocks we've yet to disassemble.
  BlockSet to_disassemble_;
  // Keeps track of address ranges that we want to merge because
  // we've found control flow from one block to another within the range,
  // either through short branches or by execution continuing past the tail
  // of a block.
  RangeSet to_merge_;
  // Keeps track of reloc entry information, which is used by various
  // pieces of the decomposer.
  PEFile::RelocSet reloc_set_;
  RelativeAddressSet reloc_refs_;
  // Keeps track of fixups, which are necessary if we want to move around
  // code and data. These are keyed by the location in the image of the
  // reference. We keep them around so that the disassembly phase can be
  // validated against them.
  FixupMap fixup_map_;
  // Keeps track of per block disassembly statistics.
  DetailedCodeBlockStatsMap code_block_stats_;
  // A set of static initializer search pattern pairs. These are used to
  // ensure we don't break up blocks of static initializer function pointers.
  REPairs static_initializer_patterns_;
};

// The results of the decomposition process are stored in this class.
class Decomposer::DecomposedImage {
 public:
  DecomposedImage() : address_space(&image),
                      basic_block_address_space(&basic_block_graph) {
  }

 public:
  BlockGraph image;
  BlockGraph::AddressSpace address_space;
  PEFileParser::PEHeader header;

  BlockGraph basic_block_graph;
  BlockGraph::AddressSpace basic_block_address_space;
  std::vector<OMAP> omap_to;
  std::vector<OMAP> omap_from;
};

// This is for serializing a PEFile/DecomposedImage pair. This allows
// us to avoid doing decomposition repeatedly. This also stores
// toolchain metadata for input validation.
bool SaveDecomposition(const PEFile& pe_file,
                       const Decomposer::DecomposedImage& image,
                       core::OutArchive* out_archive);
bool LoadDecomposition(PEFile* pe_file,
                       Decomposer::DecomposedImage* image,
                       core::InArchive* in_archive);

// This stores fixups, but in a format more convenient for us than the
// basic PdbFixup struct.
struct Decomposer::Fixup {
  BlockGraph::ReferenceType type;
  bool refers_to_code;
  bool is_data;
  // Has this fixup been visited by our decomposition?
  bool visited;
  RelativeAddress location;
  RelativeAddress base;
};

// During decomposition we collect references in this format, e.g.
// address->address. After thunking up the entire image into blocks,
// we convert them to block->block references.
// TODO(siggi): Is there reason to keep these in an address space to guard
//     against overlapping references?
struct Decomposer::IntermediateReference {
  BlockGraph::ReferenceType type;
  BlockGraph::Size size;
  // A reference actually takes the form of a pointer that is offset
  // from a base address (its intended target). Direct references will
  // have offset = 0, but this allows us to represent offset references
  // into data as seen in loop induction variables, etc.
  RelativeAddress base;
  BlockGraph::Offset offset;
  std::string name;
};

// For storing detailed statistics regarding a code block.
struct Decomposer::DetailedCodeBlockStatistics {
  size_t code_bytes;
  size_t data_bytes;
  size_t padding_bytes;
  size_t unknown_bytes;
  size_t code_count;
  size_t data_count;
  size_t padding_count;
};

// Coverage statistics are stored in this class.
struct Decomposer::CoverageStatistics {
  // Keeps information regarding Sections.
  struct SectionStatistics {
    size_t section_count;
    size_t virtual_size;
    size_t data_size;
  };

  // Stores data broken down by Section type.
  struct {
    SectionStatistics summary;
    SectionStatistics code;
    SectionStatistics data;
    SectionStatistics unknown;
  } sections;

  // Keeps simple information regarding blocks.
  struct SimpleBlockStatistics {
    size_t virtual_size;
    size_t data_size;
    size_t block_count;
  };

  // Keeps more detailed information regarding blocks, splitting it down
  // depending on if the block was a gap block or not.
  struct BlockStatistics {
    SimpleBlockStatistics summary;
    SimpleBlockStatistics normal;
    SimpleBlockStatistics gap;
  };

  // Stores information about code blocks.
  struct CodeBlockStatistics : public BlockStatistics {
    DetailedCodeBlockStatistics detail;
  };

  // Stores information about blocks, broken down by type.
  struct {
    CodeBlockStatistics code;
    BlockStatistics data;
    SimpleBlockStatistics no_section;
  } blocks;
};

}  // namespace pe

#endif  // SYZYGY_PE_DECOMPOSER_H_
