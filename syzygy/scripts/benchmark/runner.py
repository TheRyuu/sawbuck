#!/usr/bin/python2.6
# Copyright 2011 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Utility classes and functions to run benchmarks on Chrome execution and
extract metrics from ETW traces."""

import chrome_control
import ctypes
import ctypes.wintypes
import etw
import etw_db
import etw.evntrace as evn
import event_counter
import exceptions
import glob
import ibmperf
import json
import logging
import os.path
import pkg_resources
import re
import shutil
import subprocess
import sys
import tempfile
import time
import win32api


# The Windows prefetch directory, this is possibly only valid on Windows XP.
_PREFETCH_DIR = os.path.join(os.environ['WINDIR'], 'Prefetch')


# Set up a file-local logger.
_LOGGER = logging.getLogger(__name__)


_XP_MAJOR_VERSION = 5


class Prefetch:
  """This class acts as an enumeration of Prefetch modes."""

  # If Prefetch is enabled, we leave the O/S to do its own thing. We also
  # leave previously generated prefetch files in place.
  ENABLED = 0

  # If Prefetch is disabled, we delete the O/S prefetch files before and
  # after each iteration.
  DISABLED = 1

  # In this mode we remove the O/S prefetch files prior to the first
  # iteration only. From that point on, we let prefetch run normally.
  RESET_PRIOR_TO_FIRST_LAUNCH = 2


class RunnerError(Exception):
  """Exceptions raised by this module are instances of this class."""
  pass


def _DeletePrefetch():
  """Deletes all files that start with Chrome.exe in the OS prefetch cache.
  """
  files = glob.glob('%s\\Chrome.exe*.pf' % _PREFETCH_DIR)
  _LOGGER.info("Deleting %d prefetch files", len(files))
  for file in files:
    os.unlink(file)


def _GetExePath(name):
  '''Gets the path to a named executable.'''
  path = pkg_resources.resource_filename(__name__, os.path.join('exe', name))
  if not os.path.exists(path):
    # If we're not running packaged from an egg, we assume we're being
    # run from a virtual env in a build directory.
    build_dir = os.path.abspath(os.path.join(os.path.dirname(sys.executable),
                                             '../..'))
    path = os.path.join(build_dir, name)
  return path


def _GetRunInSnapshotExeResourceName():
  """Return the name of the most appropriate run_in_snapshot executable for
  the system we're running on."""
  maj, min = sys.getwindowsversion()[:2]
  # 5 is XP.
  if maj == _XP_MAJOR_VERSION:
    return 'run_in_snapshot_xp.exe'
  if maj < _XP_MAJOR_VERSION:
    raise RunnerError('Unrecognized system version.')

  # We're on Vista or better, pick the 32 or 64 bit version as appropriate.
  is_wow64 = ctypes.wintypes.BOOL()
  is_wow64_function = ctypes.windll.kernel32.IsWow64Process
  if not is_wow64_function(win32api.GetCurrentProcess(),
                           ctypes.byref(is_wow64)):
    raise Exception('IsWow64Process failed.')
  if is_wow64:
    return 'run_in_snapshot_x64.exe'

  return 'run_in_snapshot.exe'


def _GetRunInSnapshotExe():
  """Return the appropriate run_in_snapshot executable for this system."""
  return _GetExePath(_GetRunInSnapshotExeResourceName())


class ChromeRunner(object):
  """A utility class to manage the running of Chrome for some number of
  iterations."""

  def __init__(self, chrome_exe, profile_dir, initialize_profile=True):
    """Initialize instance.

    Args:
        chrome_exe: path to the Chrome executable to benchmark.
        profile_dir: path to the profile directory for Chrome. If None,
            defaults to a temporary directory.
        initialize_profile: if True, the profile directory will be erased and
            Chrome will be launched once to initialize it.
    """
    self._chrome_exe = chrome_exe
    self._profile_dir = profile_dir
    self._initialize_profile = initialize_profile
    self._call_trace_service = None
    self._call_trace_log_path = None
    self._call_trace_log_file = None

    self._profile_dir_is_temp = self._profile_dir == None
    if self._profile_dir_is_temp:
      self._profile_dir = tempfile.mkdtemp(prefix='chrome-profile')
      _LOGGER.info('Using temporary profile directory "%s".' %
          self._profile_dir)

  @staticmethod
  def StartLoggingEtw(log_dir):
    """Starts ETW Logging to the files provided.

    Args:
        log_dir: Directory where kernel.etl, call_trace.etl and chrome.etl
                 will be created.
    """
    # Best effort cleanup in case the log sessions are already running.
    subprocess.call([_GetExePath('call_trace_control.exe'), 'stop'])

    kernel_file = os.path.abspath(os.path.join(log_dir, 'kernel.etl'))
    call_trace_file = os.path.abspath(os.path.join(log_dir, 'call_trace.etl'))
    chrome_file = os.path.abspath(os.path.join(log_dir, 'chrome.etl'))
    cmd = [_GetExePath('call_trace_control.exe'),
           'start',
           '--kernel-file=%s' % kernel_file,
           '--call-trace-file=%s' % call_trace_file,
           '--chrome-file=%s' % chrome_file]
    _LOGGER.info('Starting ETW logging to "%s", "%s" and "%s".',
        kernel_file, call_trace_file, chrome_file)
    ret = subprocess.call(cmd)
    if ret != 0:
      raise RunnerError('Failed to start ETW logging.')

  @staticmethod
  def StopLoggingEtw():
    cmd = [_GetExePath('call_trace_control.exe'), 'stop']
    _LOGGER.info('Stopping ETW logging.')
    ret = subprocess.call(cmd)
    if ret != 0:
      raise RunnerError('Failed to stop ETW logging.')

  def StartLoggingRpc(self, log_dir):
    """Starts RPC Logging to the directory provided.

    Args:
        log_dir: Directory where call_trace log files will be created.
    """
    _LOGGER.info('Starting RPC logging.')

    # Setup the call-trace service command line to start accepting clients
    # and to log traces to log_dir.
    exe_file = _GetExePath('call_trace_service.exe')
    exe_dir = os.path.dirname(exe_file)
    command = [exe_file, 'start', '--trace-dir=%s' % log_dir]

    # Create a log file to which the call-trace service can direct its
    # standard error stream. Keep it around so we can dump it at the end.
    self._call_trace_log_path = os.path.join(
        log_dir, 'call_trace_service_log.txt')
    self._call_trace_log_file = open(self._call_trace_log_path, 'w+b')

    # Launch the call-trace service process.
    self._call_trace_service = subprocess.Popen(
        command, bufsize=-1, cwd=exe_dir,
        stdout=self._call_trace_log_file, stderr=subprocess.STDOUT)

    # The call-trace service process will continue to run in the "background"
    # unless there's a problem. Before we return, let's give it a few seconds
    # to see if it keeps running.
    for _ in xrange(5):
      time.sleep(1)
      status = self._call_trace_service.poll()
      if status is not None:
        self._DumpCallTraceLog(_LOGGER.error)
        self._call_trace_service = None
        self._call_trace_log_path = None
        raise RunnerError('Failed to start RPC logging (%s)' % status)

  def StopLoggingRpc(self):
    """Stops RPC Logging."""
    _LOGGER.info('Stopping RPC logging.')

    # Use the call-trace service's command line to stop the singleton
    # call-trace service instance. We can't just terminate the process
    # because we need it to shutdown cleanly and sending it a control
    # message (Ctrl-C, Ctrl-Break) to request a shutdown isn't reliable
    # from Python. So, we use it's exposed RPC interface to request a
    # shutdown.
    exe_file = _GetExePath('call_trace_service.exe')
    exe_dir = os.path.dirname(exe_file)
    command = [exe_file, 'stop']
    status = subprocess.call(command, cwd=exe_dir)
    if status != 0:
      raise RunnerError('Failed to stop call-trace service')

    # Wait for the process to close (and remember its shutdown status),
    # then dump its error logs to our log stream.
    status = self._call_trace_service.wait()
    self._DumpCallTraceLog(_LOGGER.info)
    self._call_trace_service = None
    if status != 0:
      raise RunnerError('RPC logging returned an error (%s).' % status)

  def _DumpCallTraceLog(self, logger):
    """Dumps the contents of the call trace log file to the given logger.

    Args:
        logger: The logging function to use (i.e., _LOGGER.error,
            _LOGGER.info, etc).
    """
    self._call_trace_log_file.close()
    self._call_trace_log_file = None
    with open(self._call_trace_log_path, 'r') as log_file:
      for line in log_file:
        logger('-- %s', line.strip())
    self._call_trace_log_path = None

  def Run(self, iterations):
    """Runs the benchmark for a given number of iterations.

    Args:
      iterations: number of iterations to run.
    """
    self._SetUp()

    try:
      # Run the benchmark for the number of iterations specified.
      for i in range(iterations):
        _LOGGER.info("Starting iteration %d.", i)
        self._PreIteration(i)
        self._RunOneIteration(i)
        self._PostIteration(i, True)

      # Output the results after completing all iterations.
      self._ProcessResults()

    except:
      _LOGGER.exception('Failure in iteration %d.', i)

      # Clean up after the failed iteration.
      self._PostIteration(i, False)

      # Reraise the error so that the script will return non-zero on failure.
      raise

    finally:
      self._TearDown()

  def _SetUp(self):
    """Invoked once before a set of iterations."""
    if chrome_control.IsProfileRunning(self._profile_dir):
      _LOGGER.warning(
          'Chrome already running in profile "%s", shutting it down.',
          self._profile_dir)
      chrome_control.ShutDown(self._profile_dir)

    if self._initialize_profile:
      shutil.rmtree(self._profile_dir, True)

    if not os.path.isdir(self._profile_dir):
      self._InitializeProfileDir()

  def _TearDown(self):
    """Invoked once after all iterations are complete, or on failure."""
    if self._profile_dir_is_temp:
      _LOGGER.info('Deleting temporary profile directory "%s".' %
          self._profile_dir)
      shutil.rmtree(self._profile_dir, ignore_errors=True)

  def _RunOneIteration(self, i):
    """Perform the iteration."""
    _LOGGER.info("Iteration: %d", i)

    process = self._LaunchChrome()
    self._WaitTillChromeRunning(process)
    try:
      self._DoIteration(i)
    finally:
      _LOGGER.info("Shutting down Chrome Profile: %s", self._profile_dir)
      chrome_control.ShutDown(self._profile_dir)

  def _DoIteration(self, it):
    """Invoked each iteration after Chrome has successfully launched."""
    pass

  def _PreIteration(self, it):
    """Invoked prior to each iteration."""
    pass

  def _PostIteration(self, i, success):
    """Invoked after each iteration.

    Args:
      i: the iteration number.
      success: set to True if the iteration was successful, False otherwise.
          If False, this routine should only perform any necessary cleanup."""
    pass

  def _ProcessResults(self):
    """Invoked after all iterations have succeeded."""
    pass

  def _LaunchChrome(self, extra_arguments=None):
    """Launch the Chrome instance for this iteration. Returns the
    subprocess.Popen object wrapping the launched process."""
    return self._LaunchChromeImpl(extra_arguments)

  def _LaunchChromeImpl(self, extra_arguments=None):
    """Launch a Chrome instance in our profile dir, with extra_arguments.
    Returns the subprocess.Popen wrapping the launched process."""
    cmd_line = [self._chrome_exe,
                '--user-data-dir=%s' % self._profile_dir]
    if extra_arguments:
      cmd_line.extend(extra_arguments)

    _LOGGER.info('Launching command line [%s].', cmd_line)
    return subprocess.Popen(cmd_line)

  def _InitializeProfileDir(self):
    """Initialize a Chrome profile directory by launching, then stopping
    Chrome in that directory.
    """
    _LOGGER.info('Initializing profile dir "%s".', self._profile_dir)
    process = self._LaunchChromeImpl(['--no-first-run'])
    self._WaitTillChromeRunning(process)
    chrome_control.ShutDown(self._profile_dir)

  def _WaitTillChromeRunning(self, process):
    """Wait until Chrome is running in our profile directory.

    Args:
      process: the subprocess.Popen object wrapping the launched Chrome
        process.

    Raises:
      RunnerError if Chrome is not running after a 5 minute wait, or if
          it terminates early.
    """
    _LOGGER.debug('Waiting until Chrome is running.')
    # Use a long timeout just in case the machine is REALLY bogged down.
    # This could be the case on the build-bot slave, for example.
    for i in xrange(5 * 60):
      if chrome_control.IsProfileRunning(self._profile_dir):
        _LOGGER.debug('Found running instance of Chrome.')
        return

      # Check if the process has returned early.
      if process.poll() != None:
        raise RunnerError('Chrome process terminated early.')

      time.sleep(1)

    raise RunnerError('Timeout waiting for Chrome.')


class BenchmarkRunner(ChromeRunner):
  """A utility class to manage the running of Chrome startup time benchmarks.

  This class can run a given Chrome instance through a few different
  configurable scenarios:
    * With Chrome.dll preloading turned on or turned off.
    * Cold/Warm start. To simulate cold start, the volume containing the
      Chrome executable under test will be remounted to a new drive letter
      using the Windows shadow volume service.
    * With Windows XP OS prefetching enabled or disabled.
  """

  def __init__(self, chrome_exe, profile_dir, preload, cold_start, prefetch,
               keep_temp_dirs, initialize_profile, ibmperf_dir, ibmperf_run,
               ibmperf_metrics):
    """Initialize instance.

    Args:
        chrome_exe: path to the Chrome executable to benchmark.
        profile_dir: path to the existing profile directory for Chrome.
            If 'None', creates a temporary directory.
        preload: specifies the state of Chrome.dll preload to use for
            benchmark.
        cold_start: if True, chrome_exe will be launched from a shadow volume
            freshly minted and mounted for each iteration.
        prefetch: must be one of the Prefetch enumeration values.
        keep_temp_dirs: if True, the script will not clean up the temporary
            directories it creates. This is handy if you want to e.g. manually
            inspect the log files generated.
        initialize_profile: if True, the profile directory will be erased and
            Chrome will be launched once to initialize it.
        ibmperf_dir: The directory where the IBM Performance Inspector may
            be found.
        ibmperf_run: If True, IBM Performance Inspector metrics will be
            gathered.
        ibmperf_metrics: List of metrics to be gathered using ibmperf.
    """
    super(BenchmarkRunner, self).__init__(
        chrome_exe, profile_dir, initialize_profile=initialize_profile)
    self._preload = preload
    self._cold_start = cold_start
    self._prefetch = prefetch
    self._keep_temp_dirs = keep_temp_dirs
    self._results = {}
    self._temp_dir = None
    self._SetupIbmPerf(ibmperf_dir, ibmperf_run, ibmperf_metrics)

  def Run(self, iterations):
    """Overrides ChromeRunner.Run. We do this so that we can multiply
    the number of iterations by the number of performance metric groups, if
    we're gathering them. This is because we can only gather a fixed number of
    metrics at a time."""
    if self._ibmperf:
      iterations = iterations * len(self._ibmperf_groups)
    super(BenchmarkRunner, self).Run(iterations)

  def _SetUp(self):
    super(BenchmarkRunner, self)._SetUp()
    self._old_preload = chrome_control.GetPreload()
    chrome_control.SetPreload(self._preload)
    self._temp_dir = tempfile.mkdtemp(prefix='chrome-bench')
    _LOGGER.info('Created temporary directory "%s".', self._temp_dir)

  def _TearDown(self):
    chrome_control.SetPreload(*self._old_preload)
    if self._temp_dir and not self._keep_temp_dirs:
      _LOGGER.info('Deleting temporary directory "%s".', self._temp_dir)
      shutil.rmtree(self._temp_dir, ignore_errors=True)
      self._temp_dir = None
    super(BenchmarkRunner, self)._TearDown()

  def _LaunchChrome(self):
    """Launches Chrome, wrapping it in run_in_snapshot if cold-start is
    enabled. Returns the subprocess.Popen object wrapping the process."""
    if self._cold_start:
      (drive, path) = os.path.splitdrive(self._chrome_exe)
      chrome_exe = os.path.join('M:', path)
      run_in_snapshot = _GetRunInSnapshotExe()
      cmd_line = [run_in_snapshot,
                  '--volume=%s\\' % drive,
                  '--snapshot=M:',
                  '--',
                  chrome_exe,
                  '--user-data-dir=%s' % self._profile_dir]
    else:
      cmd_line = [self._chrome_exe,
                  '--user-data-dir=%s' % self._profile_dir]

    _LOGGER.info('Launching command line [%s].', cmd_line)
    return subprocess.Popen(cmd_line)

  def _DoIteration(self, it):
    # Give our Chrome instance 10 seconds to settle.
    time.sleep(10)

    # This must be called in _DoIteration, as the Chrome process needs to
    # still be running. By the time _PostIteration is called, it's dead.
    self._ProcessIbmPerfResults()
    self._CaptureWorkingSetMetrics()

  def _PreIteration(self, i):
    self._StartLogging()
    if (self._prefetch == Prefetch.DISABLED or
        (i == 0 and self._prefetch == Prefetch.RESET_PRIOR_TO_FIRST_LAUNCH)):
      _DeletePrefetch()
    self._StartIbmPerf(i)

  def _PostIteration(self, i, success):
    self._StopIbmPerf()
    self._StopLogging()

    if not success:
      return

    self._ProcessLogs()

    if self._prefetch == Prefetch.DISABLED:
      _DeletePrefetch()

  def _StartLogging(self):
    self.StartLoggingEtw(self._temp_dir)
    self._kernel_file = os.path.join(self._temp_dir, 'kernel.etl')
    self._chrome_file = os.path.join(self._temp_dir, 'chrome.etl')

  def _StopLogging(self):
    self.StopLoggingEtw()

  def _ProcessLogs(self):
    parser = etw.consumer.TraceEventSource()
    parser.OpenFileSession(self._kernel_file)
    parser.OpenFileSession(self._chrome_file)

    file_db = etw_db.FileNameDatabase()
    module_db = etw_db.ModuleDatabase()
    process_db = etw_db.ProcessThreadDatabase()
    counter = event_counter.LogEventCounter(file_db, module_db, process_db)
    parser.AddHandler(file_db)
    parser.AddHandler(module_db)
    parser.AddHandler(process_db)
    parser.AddHandler(counter)
    parser.Consume()
    counter.FinalizeCounts()

    # TODO(siggi): Other metrics, notably:
    #   Time from launch of browser to interesting TRACE_EVENT metrics
    #     in browser and renderers.

    for (module_name, count) in counter._hardfaults.items():
      self._AddResult('Chrome', 'HardPageFaults[%s]' % module_name, count)

    for (module_name, module_info) in counter._softfaults.items():
      for (fault_type, count) in module_info.items():
        self._AddResult('Chrome',
                        'SoftPageFaults[%s][%s]' % (module_name, fault_type),
                        count)

    if (counter._message_loop_begin and len(counter._message_loop_begin) > 0 and
        counter._process_launch and len(counter._process_launch) > 0):
      browser_start = counter._process_launch[0]
      loop_start = counter._message_loop_begin[0]
      self._AddResult('Chrome', 'MessageLoopStartTime',
          loop_start - browser_start, 's')

    if counter._process_launch and len(counter._process_launch) >= 2:
      browser_start = counter._process_launch.pop(0)
      renderer_start = counter._process_launch.pop(0)
      self._AddResult('Chrome',
                      'RendererLaunchTime',
                      renderer_start - browser_start,
                      's')

    # We leave it to TearDown to delete any files we've created.
    self._kernel_file = None
    self._chrome_file = None

  def _ProcessResults(self):
    """Outputs the benchmark results in the format required by the
    GraphingLogProcessor class, which is:

    RESULT <graph name>: <trace name>= [<comma separated samples>] <units>

    Example:
      RESULT Chrome: RendererLaunchTime= [0.1, 0.2, 0.3] s
    """
    for key in sorted(self._results.keys()):
      (graph_name, trace_name) = key
      (units, results) = self._results[key]
      print "RESULT %s: %s= %s %s" % (graph_name, trace_name,
                                      str(results), units)

  def _AddResult(self, graph_name, trace_name, sample, units=''):
    _LOGGER.info("Adding result %s, %s, %s, %s",
                 graph_name, trace_name, str(sample), units)
    results = self._results.setdefault((graph_name, trace_name), (units, []))
    results[1].append(sample)

  def _SetupIbmPerf(self, ibmperf_dir, ibmperf_run, ibmperf_metrics):
    """Initializes the IBM Performance Inspector variables. Given the
    metrics provided on the command-line, splits them into groups, determining
    how many independent runs of the benchmark are required to gather all
    of the requested metrics."""
    if not ibmperf_run:
      self._ibmperf = None
      return

    hpc = ibmperf.HardwarePerformanceCounter(ibmperf_dir=ibmperf_dir)

    # If no metrics are specified, use them all.
    metrics = set(ibmperf_metrics)
    if len(metrics) == 0:
      metrics = set(hpc.metrics.keys())

    # Always measure 'free' metrics. This ensures that we always measure
    # the CYCLES metric, which we use for ordering the output of other
    # metrics.
    metrics.update(hpc.free_metrics)

    # Create groups of metrics that will run simultaneously.
    nonfree = list(metrics.intersection(hpc.non_free_metrics))
    if len(nonfree) > 0:
      groups = [set(nonfree[i:i + hpc.max_counters]).union(hpc.free_metrics)
          for i in range(0, len(nonfree), hpc.max_counters)]
    else:
      groups = [hpc.free_metrics]

    _LOGGER.info('Performance counters require %d runs per iteration.'
        % len(groups))

    self._ibmperf = hpc
    self._ibmperf_metrics = metrics
    self._ibmperf_groups = groups

  def _StartIbmPerf(self, i):
    """If configured to run, starts the hardware performance counters for the
    given iteration."""
    if self._ibmperf:
      group = i % len(self._ibmperf_groups)
      metrics = self._ibmperf_groups[group]
      self._ibmperf.Start(metrics)

  def _ProcessIbmPerfResults(self):
    """If they are running, processes the hardware performance counters for
    chrome, outputting their values as results."""
    if not self._ibmperf:
      return

    results = self._ibmperf.Query('chrome')

    # We always have CYCLES statistics. To report all stats in consistent
    # order across multiple runs, we output the PIDs in the order of decreasing
    # CYCLES counts.
    pids = results['CYCLES']
    pids = [(count, pid) for (pid, count) in pids.items()]
    pids = sorted(pids, reverse=True)
    pids = [pid for (count, pid) in pids]

    for (metric, values) in results.items():
      for i in xrange(len(pids)):
        pid = pids[i]
        count = values[pid]
        name = 'IbmPerf[%s][%d]' % (metric, i)
        self._AddResult('Chrome', name, count)

  def _StopIbmPerf(self):
    """If running, stops the hardware performance counters."""
    if self._ibmperf:
      self._ibmperf.Stop()

  _WS_METRIC_NAMES = ("pages",
                      "shareable_pages",
                      "shared_pages",
                      "read_only_pages",
                      "writable_pages",
                      "executable_pages")
  _WS_OUTPUT_NAMES = ("Pages",
                      "Shareable",
                      "Shared",
                      "ReadOnly",
                      "Writable",
                      "Executable")

  def _CaptureWorkingSetMetrics(self):
    cmd = [_GetExePath('wsdump.exe'), '--process-name=chrome.exe']
    wsdump = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    stdout, stderr = wsdump.communicate()
    returncode = wsdump.returncode
    if returncode != 0:
      raise RunnerError('Failed to get working set stats.')

    working_sets = json.loads(stdout)
    results = []
    for process in working_sets:
      total_ws = None
      chrome_ws = None
      for module in process.get('modules'):
        module_name = module.get('module_name')
        if module_name == 'Total':
          total_ws = map(module.get, self._WS_METRIC_NAMES)
        if module_name.endswith('\\chrome.dll'):
          chrome_ws = map(module.get, self._WS_METRIC_NAMES)

      results.append((total_ws, chrome_ws))

    # Order the results to make the metrics output order somewhat stable.
    results.sort()
    for i in xrange(len(results)):
      for value, name in zip(results[i][0], self._WS_OUTPUT_NAMES):
        self._AddResult('Chrome', 'TotalWs[%i][%s]' % (i, name), value)
      for value, name in zip(results[i][1], self._WS_OUTPUT_NAMES):
        self._AddResult('Chrome', 'ChromeDllWs[%i][%s]' % (i, name), value)
