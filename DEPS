# Copyright 2012 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Sawbuck builds on Chrome base, uses GYP, GTest, all of which requires
# this build configuration.

vars = {
  "chrome_revision": "116861",
  "gmock_revision": "374",
  "gtest_revision": "560",
  "gyp_revision": "1135",

  "chrome_base": "http://src.chromium.org/svn/trunk",
}

deps = {
  "src/base":
    Var("chrome_base") + "/src/base@" + Var("chrome_revision"),

  "src/third_party/wtl":
    Var("chrome_base") + "/src/third_party/wtl@" +
        Var("chrome_revision"),

  "src/third_party/zlib":
    Var("chrome_base") + "/src/third_party/zlib@" +
        Var("chrome_revision"),
  "src/third_party/libevent":
    Var("chrome_base") + "/src/third_party/libevent@" +
        Var("chrome_revision"),
  "src/third_party/libjpeg":
    Var("chrome_base") + "/src/third_party/libjpeg@" +
        Var("chrome_revision"),
  "src/third_party/icu":
    Var("chrome_base") + "/deps/third_party/icu46@" +
        Var("chrome_revision"),
  "src/third_party/sqlite":
    Var("chrome_base") + "/src/third_party/sqlite@" +
        Var("chrome_revision"),
  "src/third_party/modp_b64":
    Var("chrome_base") + "/src/third_party/modp_b64@" +
        Var("chrome_revision"),

  # TODO(chrisha): Update this as soon as the AVX fix lands!
  "src/third_party/distorm/files":
    "http://distorm.googlecode.com/svn/trunk@193",

  "src/third_party/python_26":
     Var("chrome_base") + "/tools/third_party/python_26@" +
        Var("chrome_revision"),

  "src/third_party/psyco_win32":
    Var("chrome_base") + "/deps/third_party/psyco_win32@" +
        Var("chrome_revision"),

  "src/third_party/googleappengine":
      "http://googleappengine.googlecode.com/svn/trunk/python@241",

  "src/build":
    Var("chrome_base") + "/src/build@" + Var("chrome_revision"),
  "src/tools/win":
    Var("chrome_base") + "/src/tools/win@" + Var("chrome_revision"),
    
  "src/testing":
    Var("chrome_base") + "/src/testing@" + Var("chrome_revision"),
  "src/testing/gmock":
    "http://googlemock.googlecode.com/svn/trunk@" + Var("gmock_revision"),
  "src/testing/gtest":
    "http://googletest.googlecode.com/svn/trunk@" + Var("gtest_revision"),

  "src/tools/gyp":
    "http://gyp.googlecode.com/svn/trunk@" + Var("gyp_revision"),

  "src/tools/code_coverage":
    Var("chrome_base") + "/src/tools/code_coverage@" + Var("chrome_revision"),
}


include_rules = [
  # Everybody can use some things.
  "+base",
  "+build",
]

hooks = [
  {
    # A change to a .gyp, .gypi, or to GYP itself should run the generator.
    "pattern": ".",
    "action": ["python",
               "src/build/gyp_chromium",
               "src/sawbuck/sawbuck.gyp"],
  },
  {
    # A change to a .gyp, .gypi, or to GYP itself should run the generator.
    "pattern": ".",
    "action": ["python",
               "src/build/gyp_chromium",
               "src/sawdust/sawdust.gyp"],
  },
  {
    # A change to a .gyp, .gypi, or to GYP itself should run the generator.
    "pattern": ".",
    "action": ["python",
               "src/build/gyp_chromium",
               "--include=src/syzygy/syzygy.gypi",
               "src/syzygy/syzygy.gyp"],
  },
]
