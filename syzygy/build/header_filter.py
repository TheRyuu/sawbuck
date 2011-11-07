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
"""A minimal doxygen filter that'll change // comments to /// comments
in headers."""

import sys
import re

file = open(sys.argv[1], 'r')

for line in file:
  if line.startswith('#define'):
    break
  print line,

COMMENT_RE = re.compile('//')

for line in file:
  line = COMMENT_RE.sub('///', line)
  print line,
