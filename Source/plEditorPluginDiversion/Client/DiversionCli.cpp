#include <plEditorPluginDiversion/EditorPluginDiversionPCH.h>

#include <plEditorPluginDiversion/Client/DiversionAgent.h>
#include <plEditorPluginDiversion/Client/DiversionCli.h>

#include <Foundation/IO/OSFile.h>
#include <Foundation/System/EnvironmentVariableUtils.h>
#include <Foundation/System/Process.h>
#include <Foundation/Threading/Mutex.h>

plString plDiversionCli::FindExecutable()
{
  // the installer puts dv here and adds the folder to PATH, but a running editor may predate that PATH change
  plStringBuilder sInstalled = plDiversionAgent::GetDiversionFolder();
  sInstalled.AppendPath("bin/dv.exe");

  if (plOSFile::ExistsFile(sInstalled))
    return sInstalled;

  plStringBuilder sPath = plEnvironmentVariableUtils::GetValueString("PATH");
  plHybridArray<plStringView, 32> folders;
  sPath.Split(false, folders, ";");

  for (plStringView sFolder : folders)
  {
    plStringBuilder sCandidate = sFolder;
    sCandidate.AppendPath("dv.exe");

    if (plOSFile::ExistsFile(sCandidate))
      return sCandidate;
  }

  return {};
}

plDiversionCli::Result plDiversionCli::Run(plStringView sWorkingDir, plArrayPtr<const plStringView> args)
{
  Result result;

  const plString sExecutable = FindExecutable();
  if (sExecutable.IsEmpty())
  {
    result.m_sOutput = "The Diversion command line tool (dv) is not installed.";
    return result;
  }

  plProcessOptions opt;
  opt.m_sProcess = sExecutable;
  opt.m_sWorkingDirectory = sWorkingDir;
  opt.m_bHideConsoleWindow = true;

  for (plStringView sArg : args)
    opt.m_Arguments.PushBack(sArg);

  // both streams arrive on their own threads
  plMutex outputMutex;
  opt.m_onStdOut = [&](plStringView sText)
  {
    PL_LOCK(outputMutex);
    result.m_sOutput.Append(sText);
  };
  opt.m_onStdError = opt.m_onStdOut;

  result.m_bStarted = plProcess::Execute(opt, &result.m_iExitCode).Succeeded();
  return result;
}

plString plDiversionCli::MakeArgument(plStringView sText)
{
  plStringBuilder s = sText;
  s.ReplaceAll("\"", "'");
  s.Trim(" \t\r\n");

  while (s.EndsWith("\\"))
    s.Shrink(0, 1);

  return s;
}

plString plDiversionCli::FirstLine(plStringView sText)
{
  plStringBuilder s = sText;
  s.Trim(" \t\r\n");

  if (const char* szNewline = s.FindSubString("\n"))
    s.SetSubString_FromTo(s.GetData(), szNewline);

  s.Trim(" \t\r");
  return s;
}
