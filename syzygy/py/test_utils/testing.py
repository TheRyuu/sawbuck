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
"""Defines a collection of classes for running unit-tests."""
import build_project
import cStringIO
import datetime
import logging
import optparse
import os
import presubmit
import re
import subprocess
import sys
import verifier


class Error(Exception):
  """An error class used for reporting problems while running tests."""
  pass


class BuildFailure(Error):
  """The error thrown to indicate that BuildProjectConfig has failed."""
  pass


class TestFailure(Error):
  """An error that can be thrown to indicate that a test has failed."""
  pass


def AddThirdPartyToPath():
  """Drags in the colorama module from third party."""
  third_party = os.path.abspath(os.path.join(os.path.dirname(__file__),
                                             '..', '..', '..', 'third_party'))
  if third_party not in sys.path:
    sys.path.insert(0, third_party)


AddThirdPartyToPath()
import colorama


def BuildProjectConfig(*args, **kwargs):
  """Wraps build_project.BuildProjectConfig, but ensures that if it throws
  an error it is of type testing.Error."""
  try:
    build_project.BuildProjectConfig(*args, **kwargs)
  except build_project.Error:
    # Convert the exception to an instance of testing.BuildFailure, but preserve
    # the original message and stack-trace.
    raise BuildFailure, sys.exc_info()[1], sys.exc_info()[2]


class Test(object):
  """A base class embodying the notion of a test. A test has a name, and is
  invokable by calling 'Run' with a given configuration. Upon success, the
  test will create a success file in the appropriate configuration
  directory.

  The 'Main' routine of any Test object may also be called to have it
  run as as stand-alone command line test."""

  def __init__(self, project_dir, name):
    self._project_dir = project_dir
    self._name = name
    self._force = False

    # Tests are to direct all of their output to these streams.
    # NOTE: These streams aren't directly compatible with subprocess.Popen.
    self._stdout = cStringIO.StringIO()
    self._stderr = cStringIO.StringIO()

  def GetSuccessFilePath(self, configuration):
    """Returns the path to the success file associated with this test."""
    build_path = os.path.join(self._project_dir, '../build')
    success_path = presubmit.GetTestSuccessPath(build_path,
                                                configuration,
                                                self._name)
    return success_path

  def LastRunTime(self, configuration):
    """Returns the time this test was last run in the given configuration.
    Returns 0 if the test has no success file (equivalent to never having
    been run)."""
    try:
      return os.stat(self.GetSuccessFilePath(configuration)).st_mtime
    except (IOError, WindowsError):
      return 0

  def _CanRun(self, configuration):
    """Indicates whether this test can run the given configuration.

    Derived classes may override this in order to indicate that they
    should not be run in certain configurations. This stub always returns
    True.

    If the derived class wants to indicate that the test has failed it can
    also raise a TestFailure error here.

    Args:
      configuration: the configuration to test.

    Returns:
      True if this test can run in the given configuration, False otherwise.

    Raises:
      TestFailure if the test should be considered as failed.
    """
    return True

  def _NeedToRun(self, configuration):
    """Determines whether this test needs to be run in the given configuration.

    Derived classes may override this if they can determine ahead of time
    whether the given test needs to be run. This stub always returns True.

    If the derived class wants to indicate that the test has failed it can
    also raise a TestFailure error here.

    Args:
      configuration: the configuration to test.

    Returns:
      True if this test can run in the given configuration, False otherwise.

    Raises:
      TestFailure if the test should be considered as failed.
    """
    return True

  def _MakeSuccessFile(self, configuration):
    """Makes the success file corresponding to this test in the given
    configuration."""
    success_path = self.GetSuccessFilePath(configuration)
    logging.info('Creating success file "%s".',
                 os.path.relpath(success_path, self._project_dir))
    success_file = open(success_path, 'wb')
    success_file.write(str(datetime.datetime.now()))
    success_file.close()

  def _Run(self, configuration):
    """This is as a stub of the functionality that must be implemented by
    child classes.

    Args:
      configuration: the configuration in which to run the test.

    Returns:
      True on success, False on failure. If a test fails by returning False
      all of the others test will continue to run. If an exception is raised
      then all tests are stopped."""
    raise NotImplementedError('_Run not overridden.')

  def _WriteStdout(self, value):
    """Appends a value to stdout.

    Args:
      value: the value to append to stdout.
    """
    self._stdout.write(value)
    return

  def _WriteStderr(self, value):
    """Appends a value to stderr.

    Args:
      value: the value to append to stderr.
    """
    self._stderr.write(value)
    return

  def _GetStdout(self):
    """Returns any accumulated stdout, and erases the buffer."""
    stdout = self._stdout.getvalue()
    self._stdout = cStringIO.StringIO()
    return stdout

  def _GetStderr(self):
    """Returns any accumulated stderr, and erases the buffer."""
    stderr = self._stderr.getvalue()
    self._stderr = cStringIO.StringIO()
    return stderr

  def Run(self, configuration, force=False, app_verifier=False):
    """Runs the test in the given configuration. The derived instance of Test
    must implement '_Run(self, configuration)', which raises an exception on
    error or does nothing on success. Upon success of _Run, this will generate
    the appropriate success file. If the test fails, the exception is left
    to propagate.

    Args:
      configuration: The configuration in which to run.
      force: If True, this will force the test to re-run even if _NeedToRun
          would return False.
      app_verifier: If True, this will run the given test using the
          AppVerifier tool.

    Returns:
      True on success, False otherwise.
    """
    # Store optional arguments in a side-channel, so as to allow additions
    # without changing the _Run/_NeedToRun/_CanRun API.
    self._force = force
    self._app_verifier = app_verifier

    success = True
    try:
      if not self._CanRun(configuration):
        logging.info('Skipping test "%s" in invalid configuration "%s".',
                     self._name, configuration)
        return

      # Always run _NeedToRun, even if force is true. This is because it may
      # do some setup work that is required prior to calling _Run.
      logging.info('Checking to see if we need to run test "%s" in '
                   'configuration "%s".', self._name, configuration)
      need_to_run = self._NeedToRun(configuration)

      if need_to_run:
        logging.info('Running test "%s" in configuration "%s".',
                     self._name, configuration)
      else:
        logging.info('No need to re-run test "%s" in configuration "%s".',
                     self._name, configuration)

      if not need_to_run and force:
        logging.info('Forcing re-run of test "%s" in configuration "%s".',
                     self._name, configuration)
        need_to_run = True

      if need_to_run:
        if not self._Run(configuration):
          raise TestFailure('Test "%s" failed in configuration "%s".' %
                                (self._name, configuration))

      self._MakeSuccessFile(configuration)
    except TestFailure, e:
      fore = colorama.Fore
      style = colorama.Style
      self._WriteStdout(style.BRIGHT + fore.RED + str(e) + '\n' +
                            style.RESET_ALL)
      success = False
    finally:
      # Forward the stdout, which we've caught and stuffed in a string.
      sys.stdout.write(self._GetStdout())

    return success

  @staticmethod
  def _GetOptParser():
    """Builds an option parser for this class. This function is static as
    it may be called by the constructor of derived classes before the object
    is fully initialized. It may also be overridden by derived classes so that
    they may augment the option parser with additional options."""
    parser = optparse.OptionParser()
    parser.add_option('-c', '--config', dest='configs',
                      action='append', default=[],
                      help='The configuration in which you wish to run '
                           'this test. This option may be invoked multiple '
                           'times. If not specified, defaults to '
                           '["Debug", "Release"].')
    parser.add_option('-f', '--force', dest='force',
                      action='store_true', default=False,
                      help='Force tests to re-run even if not necessary.')
    parser.add_option('--app-verifier', dest='app_verifier',
                      action='store_true', default=False,
                      help='Run tests using the AppVerifier.')
    parser.add_option('--verbose', dest='log_level', action='store_const',
                      const=logging.INFO, default=logging.WARNING,
                      help='Run the script with verbose logging.')
    return parser

  def Main(self):
    colorama.init()

    opt_parser = self._GetOptParser()
    options, unused_args = opt_parser.parse_args()

    logging.basicConfig(level=options.log_level)

    # If no configurations are specified, run all configurations.
    if not options.configs:
      options.configs = ['Debug', 'Release']

    result = 0
    for config in set(options.configs):
      # We don't catch any exceptions that may be raised as these indicate
      # something has gone really wrong, and we want them to interrupt further
      # tests.
      if not self.Run(config,
                      force=options.force,
                      app_verifier=options.app_verifier):
        logging.error('Configuration "%s" of test "%s" failed.',
                      config, self._name)
        result = 1

    # Now dump all error messages.
    sys.stdout.write(self._GetStderr())

    return result


def _AppVerifierColorize(text):
  """Colorizes the given app verifier output with ANSI color codes."""
  fore = colorama.Fore
  style = colorama.Style
  def _ColorizeLine(line):
    line = re.sub('^(Error\([^,]+, [^\)]+\):)( .*)',
                  style.BRIGHT + fore.RED + '\\1' + fore.YELLOW + '\\2' +
                      style.RESET_ALL,
                  line)
    return line

  return '\n'.join([_ColorizeLine(line) for line in text.split('\n')])


class ExecutableTest(Test):
  """An executable test is a Test that is run by launching a single
  executable file, and inspecting its return value."""

  def __init__(self, project_dir, name):
    Test.__init__(self, project_dir, name)

  def _GetTestPath(self, configuration):
    """Returns the path to the test executable. This stub may be overridden,
    but it defaults to 'project_dir/../build/configuration/test_name.exe'."""
    return os.path.join(self._project_dir, '../build',
        configuration, '%s.exe' % self._name)

  def _NeedToRun(self, configuration):
    test_path = self._GetTestPath(configuration)
    return os.stat(test_path).st_mtime > self.LastRunTime(configuration)

  def _Run(self, configuration):
    test_path = self._GetTestPath(configuration)
    rel_test_path = os.path.relpath(test_path, self._project_dir)

    # Create the app verifier test runner.
    runner = None
    image_name = None
    if self._app_verifier:
      runner = verifier.AppverifierTestRunner(False)
      image_name = os.path.basename(test_path)

      # Set up the verifier configuration.
      runner.SetImageDefaults(image_name)
      runner.ClearImageLogs(image_name)

    # Run the executable.
    command = [test_path]
    popen = subprocess.Popen(command, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT)
    (stdout, unused_stderr) = popen.communicate()
    self._WriteStdout(stdout)
    if popen.returncode != 0:
      # If the test failed, mirror its output to stderr as well. All stderrs of
      # all failing tests will be concatenated at the end of the top-most
      # unittest, making errors have better visibility.
      self._WriteStderr(stdout)

    # Process the AppVerifier logs, outputting any errors.
    app_verifier_errors = []
    if self._app_verifier:
      app_verifier_errors = runner.ProcessLogs(image_name)
      for error in app_verifier_errors:
        msg = _AppVerifierColorize(str(error) + '\n')
        self._WriteStdout(msg)
        self._WriteStderr(msg)

      # Clear the verifier settings for the image.
      runner.ResetImage(image_name)

    # Bail if we had any errors.
    if popen.returncode != 0 or app_verifier_errors:
      msg = 'Test "%s" failed in configuration "%s". Exit code %d.' %
                (self._name, configuration, popen.returncode)
      if self._app_verifier:
        msg = msg + ' %d AppVerifier errors.' % len(app_verifier_errors)
      raise TestFailure(msg)

    # If we get here, all has gone well.
    return True


def _GTestColorize(text):
  """Colorizes the given Gtest output with ANSI color codes."""
  fore = colorama.Fore
  style = colorama.Style
  def _ColorizeLine(line):
    line = re.sub('^(\[\s*(?:-+|=+|RUN|PASSED|OK)\s*\])',
                  style.BRIGHT + fore.GREEN + '\\1' + style.RESET_ALL,
                  line)
    line = re.sub('^(\[\s*FAILED\s*\])',
                  style.BRIGHT + fore.RED + '\\1' + style.RESET_ALL,
                  line)
    line = re.sub('^(\s*(?:Note:|YOU HAVE .* DISABLED TEST).*)',
                  style.BRIGHT + fore.YELLOW + '\\1' + style.RESET_ALL,
                  line)
    return line

  return '\n'.join([_ColorizeLine(line) for line in text.split('\n')])


class GTest(ExecutableTest):
  """This wraps a GTest unittest, with colorized output."""
  def __init__(self, *args, **kwargs):
    return super(GTest, self).__init__(*args, **kwargs)

  def _WriteStdout(self, value):
    """Colorizes the stdout of this test."""
    return super(GTest, self)._WriteStdout(_GTestColorize(value))

  def _WriteStderr(self, value):
    """Colorizes the stderr of this test."""
    return super(GTest, self)._WriteStderr(_GTestColorize(value))


class TestSuite(Test):
  """A test suite is a collection of tests that generates a catch-all
  success file upon successful completion. It is itself an instance of a
  Test, so may be nested."""

  def __init__(self, project_dir, name, tests):
    Test.__init__(self, project_dir, name)
    # tests may be anything iterable, but we want it to be a list when
    # stored internally.
    self._tests = list(tests)

  def AddTest(self, test):
    self._tests.append(test)

  def AddTests(self, tests):
    self._tests.extend(self, tests)

  def _NeedToRun(self, configuration):
    """Determines if any of the tests in this suite need to run in the given
    configuration."""
    for test in self._tests:
      try:
        if test._NeedToRun(configuration):
          return True
      except:
        # Output some context before letting the exception continue.
        logging.error('Configuration "%s" of test "%s" failed.',
            configuration, test._name)
        raise
    return False

  def _Run(self, configuration):
    """Implementation of this Test object.

    Runs the provided collection of tests, generating a global success file
    upon completion of them all. Runs all tests even if any test fails. Stops
    running all tests if any of them raises an exception."""
    success = True
    for test in self._tests:
      if not test.Run(configuration,
                      force=self._force,
                      app_verifier=self._app_verifier):
        # Keep a cumulative log of all stderr from each test that fails.
        self._WriteStderr(test._GetStderr())
        success = False

    return success
