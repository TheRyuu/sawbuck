// Copyright 2012 Google Inc.
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
// Declares a generic command-line application framework.
//
// An application can be declared as follows in a library:
//
//     class MyApp : public common::AppImplBase {
//      public:
//       bool ParseCommandLine(const CommandLine* command_line);
//       int Run();
//      protected:
//       bool InternalFunc();
//     };
//
// The application class can then be unit-tested as appropriate. See the
// declaration of common::AppImplBase for the entire interface expected
// by the application framework. Note that derivation from AppImplBase
// is optional, as the integration with the application framework is
// by template expansion, not virtual function invocation; AppImplBase
// is purely a convenience base class to allow you to elide defining
// parts of the interface you don't need to specialize.
//
// The main() function for the executable can be reduced to:
//
//     int main(int argc, const char* const* argv) {
//       base::AtExitManager at_exit_manager;
//       CommandLine::Init(argc, argv);
//       return common::Application<MyApp>().Run();
//     }
//
// To test how your application implementation interacts with the
// application framework. You can run the application directly from
// a unittest as follows:
//
//     TEST(FixtureName, TestName) {
//       using common::Application;
//
//       file_util::ScopedFILE in(file_util::OpenFile("NUL", "r"));
//       file_util::ScopedFILE out(file_util::OpenFile("NUL", "w"));
//       file_util::ScopedFILE err(file_util::OpenFile("NUL", "w"));
//       ASSERT_TRUE(in.get() != NULL);
//       ASSERT_TRUE(out.get() != NULL);
//       ASSERT_TRUE(err.get() != NULL);
//
//       CommandLine cmd_line(FilePath(L"program"));
//       Application<MyTestApp, LOG_INIT_NO> test_app(&cmd_line,
//                                                    in.get(),
//                                                    out.get(),
//                                                    err.get());
//
//       ASSERT_TRUE(test_app.implementation().SomeFunc());
//       ASSERT_EQ(0, test_app.Run());
//     }
//

#ifndef SYZYGY_COMMON_APPLICATION_H_
#define SYZYGY_COMMON_APPLICATION_H_

#include <objbase.h>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "sawbuck/common/com_utils.h"

namespace common {

// A convenience base class that describes the interface an application
// implementation is expected to expose. This class provides empty default
// method implementations.
//
// @note Each method is responsible for logging its own errors as it deems
//     appropriate. No log messages are otherwise generated if one of the
//     AppImplBase methods returns an error.
class AppImplBase {
 public:
  // Initializes an application implementation with the standard IO streams.
  // Use the stream IO accessors to customize the IO streams.
  // @param name the name of the application.
  explicit AppImplBase(const base::StringPiece& name);

  // Parse the given command line in preparation for execution.
  bool ParseCommandLine(const CommandLine* command_line);

  // A hook called just before Run().
  bool SetUp();

  // The main logic for the application implementation.
  // @returns the exit status for the application.
  int Run();

  // A hook called just after Run().
  void TearDown();

  // Get the application name.
  const std::string& name() const { return name_; }

  // @name IO Stream Accessors
  // @{
  FILE* in() const { return in_; }
  FILE* out() const { return out_; }
  FILE* err() const { return err_; }

  void set_in(FILE* f) {
    DCHECK(f != NULL);
    in_ = f;
  }

  void set_out(FILE* f) {
    DCHECK(f != NULL);
    out_ = f;
  }

  void set_err(FILE* f) {
    DCHECK(f != NULL);
    err_ = f;
  }
  // @}

  // A helper function to return an absolute path (if possible) for the given
  // path. If the conversion to an absolute path fails, the original path is
  // returned.
  static FilePath AbsolutePath(const FilePath& path);

 protected:
  // The name of this application.
  std::string name_;

  // @name Standard file streams.
  // @{
  FILE* in_;
  FILE* out_;
  FILE* err_;
  // @}
};

// Flags controlling the initialization of the logging subsystem.
enum AppLoggingFlag { INIT_LOGGING_NO, INIT_LOGGING_YES };

// The Application template class.
//
// @tparam Implementation The class which implements the application logic.
// @tparam kInitLogging Tracks whether or not the application should
//     (re-)initialize the logging subsystem on startup. Under testing,
//     for example, one might want to skip initializing the logging
//     subsystem.
template <typename Impl, AppLoggingFlag kInitLogging = INIT_LOGGING_YES>
class Application {
 public:
  // The application implementation class.
  typedef typename Impl Implementation;

  // Initializes the application with the current processes command line and
  // the standard IO streams.
  //
  // @pre CommandLine::Init() has been called prior to the creation of the
  //     application object.
  Application();

  // Accessor for the underlying implementation.
  Implementation& implementation() { return implementation_; }

  // @name Accessors for the command line.
  // @{
  const CommandLine* command_line() const { return command_line_; }

  void set_command_line(const CommandLine* command_line) {
    DCHECK(command_line != NULL);
    command_line_ = command_line;
  }
  // @}

  // Get the application name.
  const std::string& name() const { return implementation_.name(); }

  // The main skeleton for actually running an application.
  // @returns the exit status for the application.
  int Run();

  // @name IO Stream Accessors
  // @{
  FILE* in() const { return implementation_.in(); }
  FILE* out() const { return implementation_.out(); }
  FILE* err() const { return implementation_.err(); }

  void set_in(FILE* f) { implementation_.set_in(f); }
  void set_out(FILE* f) { implementation_.set_out(f); }
  void set_err(FILE* f) { implementation_.set_err(f); }
  // @}

 protected:
  // Initializes the logging subsystem for this application. This includes
  // checking the command line for the --verbose[=level] flag and handling
  // it appropriately.
  bool InitializeLogging();

  // The command line for this application. The referred instance must outlive
  // the application instance.
  const CommandLine* command_line_;

  // The implementation instance for this application. Execution will be
  // delegated to this object.
  Implementation implementation_;

 private:
  DISALLOW_COPY_AND_ASSIGN(Application);
};

// A helper class for timing an activity within a scope.
class ScopedTimeLogger {
 public:
  explicit ScopedTimeLogger(const char* label)
      : label_(label), start_(base::Time::Now()) {
    DCHECK(label != NULL);
    LOG(INFO) << label_ << ".";
  }

  ~ScopedTimeLogger() {
    base::TimeDelta duration = base::Time::Now() - start_;
    LOG(INFO) << label_ << " took " << duration.InSecondsF() << " seconds.";
  }

 private:
  // A labeling phrase for the activity being timed.
  const char* const label_;

  // The time at which the activity began.
  const base::Time start_;
};

}  // namespace common

#include "syzygy/common/application_impl.h"

#endif  // SYZYGY_COMMON_APPLICATION_H_
