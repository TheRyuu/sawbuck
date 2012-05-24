#!python
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
"""This unittest simply builds the build_all target."""

import os.path
import sys


_SYZYGY_DIR = os.path.abspath(os.path.dirname(__file__) + '/..')
_SCRIPT_DIR = os.path.join(_SYZYGY_DIR, 'py')


if _SCRIPT_DIR not in sys.path:
  sys.path.insert(0, _SCRIPT_DIR)
import test_utils.testing as testing  # pylint: disable=F0401


class BuildAll(testing.Test):
  """A test that checks to see if the 'build_all' target succeeds."""

  def __init__(self):
    testing.Test.__init__(self, _SYZYGY_DIR, 'build_all')
    self._solution_path = os.path.join(_SYZYGY_DIR, 'syzygy.sln')
    self._project_path = os.path.join(_SYZYGY_DIR, 'build_all.vcproj')

  def _Run(self, configuration):
    try:
      testing.BuildProjectConfig(self._solution_path,
                                 self._project_path,
                                 configuration)
    except testing.BuildFailure:
      # Recast this error as a test failure.
      raise testing.TestFailure, sys.exc_info()[1], sys.exc_info()[2]

    return True


def MakeTest():
  return BuildAll()


if __name__ == '__main__':
  sys.exit(MakeTest().Main())  # pylint: disable=E1101
