:: Copyright 2010 Google Inc.
::
:: Licensed under the Apache License, Version 2.0 (the "License");
:: you may not use this file except in compliance with the License.
:: You may obtain a copy of the License at
::
::     http:::www.apache.org/licenses/LICENSE-2.0
::
:: Unless required by applicable law or agreed to in writing, software
:: distributed under the License is distributed on an "AS IS" BASIS,
:: WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
:: See the License for the specific language governing permissions and
:: limitations under the License.
@echo off

:: The -ct argument is not supported on XP, so avoid using it there.
setlocal
set clock_type=-ct cycle
ver | findstr "XP" > nul
if not %ERRORLEVEL% == 0 goto main
set clock_type=

:main
:: Create a kernel logger session and make it log image events to the file kernel.etl.
logman create trace -ets "NT Kernel Logger" %clock_type% -mode globalsequence -o kernel.etl -p "Windows Kernel Trace" (img,process)

:: Create the call trace logger session.
logman create trace -ets "Call Trace Session" %clock_type% -mode globalsequence -o call_trace.etl
:: Turn on the CallTrace provider with batch entry logging enabled
logman update trace -ets "Call Trace Session" -p {06255E36-14B0-4e57-8964-2E3D675A0E77} 0x0020 4
