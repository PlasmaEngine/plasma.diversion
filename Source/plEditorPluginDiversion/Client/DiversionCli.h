#pragma once

#include <Foundation/Containers/HybridArray.h>
#include <Foundation/Strings/StringBuilder.h>
#include <Foundation/Time/Time.h>

/// Runs the dv command line tool, which is how the editor changes anything in Diversion: commits, locks, branches,
/// pausing sync. Reads go to the agent's HTTP API instead (plDiversionAgent), except while sync is paused, when only
/// dv can list local changes.
///
/// Note that any dv command starts the Diversion agent when it is not running, and the agent then syncs every
/// workspace that is not paused.
class plDiversionCli
{
public:
  struct Result
  {
    bool m_bStarted = false; ///< false when dv is not installed
    plInt32 m_iExitCode = -1;
    plStringBuilder m_sOutput; ///< stdout and stderr together

    bool Succeeded() const { return m_bStarted && m_iExitCode == 0; }
  };

  /// dv on PATH, else the installer's ~/.diversion/bin/dv.exe. Empty when neither exists.
  static plString FindExecutable();

  /// Runs dv in sWorkingDir (the workspace or a folder in it). Arguments are passed as they are, never formatted.
  static Result Run(plStringView sWorkingDir, plArrayPtr<const plStringView> args);

  /// The tool's own message trimmed to its first line, for a status bar or a dialog line.
  static plString FirstLine(plStringView sText);

  /// Text typed by the user (a commit message, a shelf name) as one argument: plProcess quotes arguments but does not
  /// escape quotes inside them, so those become single quotes, and a trailing backslash would escape the closing quote.
  static plString MakeArgument(plStringView sText);
};
