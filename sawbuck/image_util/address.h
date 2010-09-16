// Copyright 2010 Google Inc.
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
#ifndef SAWBUCK_IMAGE_UTIL_ADDRESS_H_
#define SAWBUCK_IMAGE_UTIL_ADDRESS_H_

#include "base/basictypes.h"
#include <iosfwd>

namespace image_util {

enum AddressType {
  kRelativeAddressType,
  kAbsoluteAddressType,
  kFileOffsetAddressType,
};

// This class implements an address in a PE image file.
// Addresses are of three varieties:
// - Relative addresses are relative to the base of the image, and thus do not
//   change when the image is relocated. Bulk of the addresses in the PE image
//   format itself are of this variety, and that's where relative addresses
//   crop up most frequently.
// - Absolute addresses are as the name indicates absolute, and those change
//   when an image is relocated. Absolute addresses mostly occur in initialized
//   data, and for each absolute datum in an image file, there will be a
//   relocation entry calling out its location in the image.
// - File offset addresses occur only in the debug data directory that I'm
//   aware of, where the debug data is referred to both by a relative address
//   and (presumably for convenience) by a file offset address.
// This class is a lightweight wrapper for an integer, which can be freely
// copied. The different address types are deliberately assignment
// incompatible, which helps to avod confusion when handling different
// types of addresses in implementation.
template <AddressType type> class AddressImpl {
 public:
  AddressImpl() : value_(0) {
  }
  explicit AddressImpl(uint32 value) : value_(value) {
  }
  AddressImpl(const AddressImpl<type>& other)  // NOLINT
      : value_(other.value_) {
  }

  bool operator<(const AddressImpl<type>& other) const {
    return value_ < other.value_;
  }
  bool operator<=(const AddressImpl<type>& other) const {
    return value_ <= other.value_;
  }
  bool operator>(const AddressImpl<type>& other) const {
    return value_ > other.value_;
  }
  bool operator>=(const AddressImpl<type>& other) const {
    return value_ >= other.value_;
  }

  bool operator==(const AddressImpl<type>& other) const {
    return value_ == other.value_;
  }
  bool operator!=(const AddressImpl<type>& other) const {
    return value_ != other.value_;
  }

  void operator=(const AddressImpl<type>& other) {
    value_ = other.value_;
  }
  void operator+=(int32 offset) {
    value_ += offset;
  }
  void operator-=(int32 offset) {
    value_ -= offset;
  }

  AddressImpl<type> operator+(size_t offset) const {
    return AddressImpl<type>(value() + offset);
  }

  AddressImpl<type> operator-(size_t offset) const {
    return AddressImpl<type>(value() - offset);
  }

  int32 operator-(const AddressImpl<type>& other) const {
    return value_ - other.value_;
  }

  uint32 value() const { return value_; }
  void set_value(uint32 value) {
    value_ = value;
  }

 private:
  uint32 value_;
};

// These types represent the different addressing formats used in PE images.

// A virtual address relative to the image base, often termed
// RVA in documentation and in data structure comments.
typedef AddressImpl<kRelativeAddressType> RelativeAddress;
// An absolute address.
typedef AddressImpl<kAbsoluteAddressType> AbsoluteAddress;
// A file offset within an image file.
typedef AddressImpl<kFileOffsetAddressType> FileOffsetAddress;

// Host function for compile asserts.
inline void CompileAsserts() {
  COMPILE_ASSERT(sizeof(RelativeAddress) == sizeof(uint32),
                 relative_address_must_be_4_byte);
  COMPILE_ASSERT(sizeof(AbsoluteAddress) == sizeof(uint32),
                 absolute_address_must_be_4_byte);
  COMPILE_ASSERT(sizeof(FileOffsetAddress) == sizeof(uint32),
                 file_offset_must_be_4_byte);
}


} // namespace image_util

#endif  // SAWBUCK_IMAGE_UTIL_ADDRESS_H_
