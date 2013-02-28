// Copyright 2013 Google Inc. All Rights Reserved.
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
// Declares a utility class for getting and storing quick and dirty stack
// captures.

#ifndef SYZYGY_AGENT_ASAN_STACK_CAPTURE_H_
#define SYZYGY_AGENT_ASAN_STACK_CAPTURE_H_

#include <windows.h>

#include "base/logging.h"

namespace agent {
namespace asan {

// A simple class for holding a stack trace capture.
class StackCapture {
 public:
  // From http://msdn.microsoft.com/en-us/library/bb204633.aspx,
  // The maximum number of frames which CaptureStackBackTrace can be asked
  // to traverse must be less than 63, so set it to 62.
  static const size_t kMaxNumFrames = 62;

  // This corresponds to the the type used by ::CaptureStackBackTrace's hash
  // for a stack-trace.
  typedef ULONG StackId;

  StackCapture()
      : stack_id_(0), num_frames_(0), max_num_frames_(kMaxNumFrames) {
  }

  explicit StackCapture(size_t max_num_frames)
      : stack_id_(0), num_frames_(0), max_num_frames_(0) {
    DCHECK_LT(0u, max_num_frames);
    DCHECK_GE(kMaxNumFrames, max_num_frames);
    max_num_frames_ = max_num_frames;
  }

  // Calculate the size necessary to store a StackCapture with the given
  // number of stack frames.
  // @param max_num_frames The maximum number of stack frames the object needs
  //     to be able to hold.
  // @returns the size of a StackCapture object with the given number of frames.
  static size_t GetSize(size_t max_num_frames);

  // @returns the size of this initialized StackCapture object.
  size_t Size() const { return GetSize(max_num_frames_); }

  // @returns true if this stack trace capture contains valid frame pointers.
  bool IsValid() const { return num_frames_ != 0; }

  // @returns the ID associated with this stack trace.
  StackId stack_id() const { return stack_id_; }

  // @returns the number of valid frame pointers in this stack trace capture.
  size_t num_frames() const { return num_frames_; }

  // @returns the maximum number of valid frame pointers in this stack trace
  //     capture.
  size_t max_num_frames() const { return max_num_frames_; }

  // @returns a pointer to the captured stack frames, or NULL if no stack
  //     frames have been captured.
  const void* const* frames() const { return IsValid() ? frames_ : NULL; }

  // Sets the stack ID for a given trace.
  // @param The stack ID to set.
  void set_stack_id(StackId stack_id) { stack_id_ = stack_id; }

  // Initializes a stack trace from an array of frame pointers, a count and
  // a StackId (such as returned by ::CaptureStackBackTrace).
  // @param stack_id The ID of the stack back trace.
  // @param frames an array of frame pointers.
  // @param num_frames the number of valid frame pointers in @frames. Note
  //     that at most kMaxNumFrames frame pointers will be copied to this
  //     stack trace capture.
  void InitFromBuffer(StackId stack_id,
                      const void* const* frames,
                      size_t num_frames);

  // Initializes a stack trace using ::CaptureStackBackTrace. This is inlined so
  // that it doesn't further pollute the stack trace, but rather makes it
  // reflect the actual point of the call.
  __forceinline void InitFromStack() {
    num_frames_ = ::CaptureStackBackTrace(
        0, max_num_frames_, frames_, &stack_id_);
  }

  // The hash comparison functor for use with MSDN's stdext::hash_set.
  struct HashCompare {
    static const size_t bucket_size = 4;
    static const size_t min_buckets = 8;
    // Calculates a hash value for the given stack_capture.
    size_t operator()(const StackCapture* stack_capture) const;
    // Value comparison operator.
    bool operator()(const StackCapture* stack_capture1,
                    const StackCapture* stack_capture2) const;
  };

 protected:
  // The unique ID of this hash. This is used for storing the hash in the set.
  StackId stack_id_;

  // The number of valid frames in this stack trace capture, and the maximum
  // number it can represent. We use uint8s here because we're limited to
  // kMaxNumFrames by the OS machinery and want this data structure to be as
  // compact as possible.
  uint8 num_frames_;
  uint8 max_num_frames_;

  // The array or frame pointers comprising this stack trace capture.
  // This is a runtime dynamic array whose actual length is max_num_frames_, but
  // we use the maximum length here so that other users of StackCapture can
  // capture full stack traces if they so desire.
  // NOTE: This must be the last member of the class.
  void* frames_[kMaxNumFrames];

 private:
  DISALLOW_COPY_AND_ASSIGN(StackCapture);
};

}  // namespace asan
}  // namespace agent

#endif  // SYZYGY_AGENT_ASAN_STACK_CAPTURE_H_
