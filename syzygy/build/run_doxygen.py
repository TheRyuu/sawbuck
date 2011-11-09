# Copyright 2011 Google Inc.
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
"""A utility script to run Doxygen with our config file."""

import os
import os.path
import subprocess

_BUILD_DIR = os.path.dirname(__file__)
_DOXYGEN_EXE = os.path.abspath(os.path.join(_BUILD_DIR,
    "../../third_party/doxygen/files/bin/doxygen.exe"))


subprocess.call([_DOXYGEN_EXE, 'doxyfile'], cwd=_BUILD_DIR)
