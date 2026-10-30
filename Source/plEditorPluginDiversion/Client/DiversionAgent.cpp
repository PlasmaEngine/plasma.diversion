#include <plEditorPluginDiversion/EditorPluginDiversionPCH.h>

#include <plEditorPluginDiversion/Client/DiversionAgent.h>
#include <plEditorPluginDiversion/Client/DiversionParse.h>

#include <Foundation/IO/OSFile.h>
#include <Foundation/System/EnvironmentVariableUtils.h>
#include <ToolsFoundation/Network/HttpClient.h>

namespace
{
  constexpr const char* s_szCloudApi = "https://api.diversion.dev/v0";

  /// Percent-encodes everything outside the unreserved set of RFC 3986.
  void AppendEncoded(plStringBuilder& ref_sUrl, plStringView sValue)
  {
    constexpr const char* szHex = "0123456789ABCDEF";

    for (const char* sz = sValue.GetStartPointer(); sz < sValue.GetEndPointer(); ++sz)
    {
      const plUInt8 c = static_cast<plUInt8>(*sz);
      const bool bUnreserved = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';

      if (bUnreserved)
      {
        ref_sUrl.Append(plStringView(sz, sz + 1));
      }
      else
      {
        const char szEscape[4] = {'%', szHex[c >> 4], szHex[c & 15], 0};
        ref_sUrl.Append(szEscape);
      }
    }
  }

  void AppendWorkspacePath(plStringBuilder& ref_sUrl, plStringView sRepos, const plDiversionWorkspace& workspace, plStringView sWorkspaces)
  {
    ref_sUrl.Append("/", sRepos, "/");
    AppendEncoded(ref_sUrl, workspace.m_sRepoId);
    ref_sUrl.Append("/", sWorkspaces, "/");
    AppendEncoded(ref_sUrl, workspace.m_sWorkspaceId);
  }

  /// A repository path in a URL: each segment encoded, the slashes kept.
  void AppendPathEncoded(plStringBuilder& ref_sUrl, plStringView sPath)
  {
    plHybridArray<plStringView, 16> segments;
    sPath.Split(false, segments, "/");
    for (plUInt32 i = 0; i < segments.GetCount(); ++i)
    {
      if (i > 0)
        ref_sUrl.Append("/");
      AppendEncoded(ref_sUrl, segments[i]);
    }
  }

  /// The agent's status answer is capped; this many entries per page, up to MaxPages pages.
  constexpr plUInt32 StatusPageSize = 5000;
  constexpr plUInt32 MaxStatusPages = 10;
} // namespace

plString plDiversionAgent::GetDiversionFolder()
{
  plStringBuilder sFolder = plEnvironmentVariableUtils::GetValueString("USERPROFILE");

  if (sFolder.IsEmpty())
    sFolder = plEnvironmentVariableUtils::GetValueString("HOME");

  sFolder.AppendPath(".diversion");
  sFolder.MakeCleanPath();
  return sFolder;
}

plResult plDiversionAgent::Connect()
{
  Disconnect();

  plStringBuilder sPortFile = GetDiversionFolder();
  sPortFile.AppendPath(".port");

  plOSFile file;
  if (file.Open(sPortFile, plFileOpenMode::Read).Failed())
  {
    m_sLastError = "The Diversion agent is not running.";
    return PL_FAILURE;
  }

  plDynamicArray<plUInt8> content;
  file.ReadAll(content);
  file.Close();

  plStringBuilder sPort;
  sPort.Set(plStringView(reinterpret_cast<const char*>(content.GetData()), content.GetCount()));
  sPort.Trim(" \t\r\n");

  plInt32 iPort = 0;
  if (plConversionUtils::StringToInt(sPort, iPort).Failed() || iPort < 1 || iPort > 65535)
  {
    m_sLastError.SetFormat("The Diversion agent's port file holds '{}'.", sPort);
    return PL_FAILURE;
  }

  plStringBuilder sBaseUrl;
  sBaseUrl.SetFormat("http://127.0.0.1:{}", iPort);

  // the port file outlives the agent, so only an answer proves it runs
  m_sBaseUrl = sBaseUrl;

  plStringBuilder sUrl(sBaseUrl, "/health");
  plDynamicArray<plUInt8> body;
  if (Get(sUrl, false, body) != Result::Ok)
  {
    m_sBaseUrl.Clear();
    m_sLastError = "The Diversion agent is not running.";
    return PL_FAILURE;
  }

  plUInt32 uiMajor = 0, uiMinor = 0, uiPatch = 0;
  if (plDiversionParse::Version(plStringView(reinterpret_cast<const char*>(body.GetData()), body.GetCount()), uiMajor, uiMinor, uiPatch).Succeeded())
  {
    plStringBuilder sVersion;
    sVersion.SetFormat("{}.{}.{}", uiMajor, uiMinor, uiPatch);
    m_sVersion = sVersion;
  }

  return PL_SUCCESS;
}

void plDiversionAgent::Disconnect()
{
  m_sBaseUrl.Clear();
  m_sVersion.Clear();
}

plDiversionAgent::Result plDiversionAgent::GetWorkspace(plStringView sProjectDir, plDiversionWorkspace& out_workspace)
{
  plStringBuilder sNative = sProjectDir;
  sNative.MakePathSeparatorsNative();

  plStringBuilder sUrl(m_sBaseUrl, "/workspace?abs_path=");
  AppendEncoded(sUrl, sNative);

  plDynamicArray<plUInt8> body;
  const Result res = Get(sUrl, false, body);
  if (res != Result::Ok)
    return res;

  // an empty object: the folder is in no workspace
  if (plDiversionParse::Workspace(body, sProjectDir, out_workspace).Failed())
    return Result::NotAvailable;

  return Result::Ok;
}

plDiversionAgent::Result plDiversionAgent::GetSync(const plDiversionWorkspace& workspace, plDiversionSyncState& out_sync)
{
  plStringBuilder sUrl = m_sBaseUrl;
  AppendWorkspacePath(sUrl, "repo", workspace, "workspace");
  sUrl.Append("/sync");

  plDynamicArray<plUInt8> body;
  const Result res = Get(sUrl, false, body);
  if (res != Result::Ok)
    return res;

  if (plDiversionParse::Sync(body, out_sync).Failed())
  {
    m_sLastError = "The Diversion agent's sync answer did not parse.";
    return Result::Failed;
  }

  return Result::Ok;
}

plDiversionAgent::Result plDiversionAgent::GetJournal(const plDiversionWorkspace& workspace, plInt64& out_iJournal)
{
  plStringBuilder sUrl = m_sBaseUrl;
  AppendWorkspacePath(sUrl, "repo", workspace, "workspace");
  sUrl.Append("/journal");

  plDynamicArray<plUInt8> body;
  const Result res = Get(sUrl, false, body);
  if (res != Result::Ok)
    return res;

  if (plDiversionParse::Journal(body, out_iJournal).Failed())
  {
    m_sLastError = "The Diversion agent's journal answer did not parse.";
    return Result::Failed;
  }

  return Result::Ok;
}

plDiversionAgent::Result plDiversionAgent::GetStatus(const plDiversionWorkspace& workspace, plDiversionStatus& out_status)
{
  out_status = {};

  for (plUInt32 uiPage = 0; uiPage < MaxStatusPages; ++uiPage)
  {
    plStringBuilder sUrl = m_sBaseUrl;
    AppendWorkspacePath(sUrl, "repos", workspace, "workspaces");
    sUrl.AppendFormat("/status?limit={}&skip={}", StatusPageSize, uiPage * StatusPageSize);

    plDynamicArray<plUInt8> body;
    const Result res = Get(sUrl, false, body);
    if (res != Result::Ok)
      return res;

    plDiversionStatus page;
    if (plDiversionParse::Status(body, page).Failed())
    {
      m_sLastError = "The Diversion agent's status answer did not parse.";
      return Result::Failed;
    }

    out_status.m_Changes.PushBackRange(page.m_Changes);
    out_status.m_bComplete = page.m_bComplete;
    if (uiPage == 0)
      out_status.m_Conflicts = page.m_Conflicts;

    if (page.m_bComplete || page.m_Changes.IsEmpty())
      break;
  }

  return Result::Ok;
}

plDiversionAgent::Result plDiversionAgent::GetOtherChanges(const plDiversionWorkspace& workspace, plStringView sPathPrefix, plDiversionOtherChanges& out_changes)
{
  if (RefreshToken().Failed())
    return Result::Failed;

  plStringBuilder sUrl = s_szCloudApi;
  AppendWorkspacePath(sUrl, "repos", workspace, "workspaces");
  sUrl.Append("/other_statuses?recurse=true");

  if (!sPathPrefix.IsEmpty())
  {
    sUrl.Append("&path_prefixes=");
    AppendEncoded(sUrl, sPathPrefix);
  }

  plDynamicArray<plUInt8> body;
  const Result res = Get(sUrl, true, body);
  if (res != Result::Ok)
    return res;

  if (plDiversionParse::OtherStatuses(body, out_changes).Failed())
  {
    m_sLastError = "Diversion's answer about other people's changes did not parse.";
    return Result::Failed;
  }

  return Result::Ok;
}

plDiversionAgent::Result plDiversionAgent::GetCloud(plStringView sPath, plDynamicArray<plUInt8>& out_body)
{
  if (RefreshToken().Failed())
    return Result::Failed;

  plStringBuilder sUrl(s_szCloudApi, sPath);
  return Get(sUrl, true, out_body);
}

plDiversionAgent::Result plDiversionAgent::GetLocks(const plDiversionWorkspace& workspace, plDynamicArray<plDiversionLock>& out_locks)
{
  plStringBuilder sPath("/repos/");
  AppendEncoded(sPath, workspace.m_sRepoId);
  sPath.Append("/locks");

  plDynamicArray<plUInt8> body;
  const Result res = GetCloud(sPath, body);
  if (res != Result::Ok)
    return res;

  if (plDiversionParse::Locks(body, out_locks).Failed())
  {
    m_sLastError = "Diversion's answer about locks did not parse.";
    return Result::Failed;
  }

  return Result::Ok;
}

plDiversionAgent::Result plDiversionAgent::GetBranches(const plDiversionWorkspace& workspace, plDynamicArray<plDiversionBranch>& out_branches)
{
  plStringBuilder sPath("/repos/");
  AppendEncoded(sPath, workspace.m_sRepoId);
  sPath.Append("/branches");

  plDynamicArray<plUInt8> body;
  const Result res = GetCloud(sPath, body);
  if (res != Result::Ok)
    return res;

  if (plDiversionParse::Branches(body, out_branches).Failed())
  {
    m_sLastError = "Diversion's answer about branches did not parse.";
    return Result::Failed;
  }

  return Result::Ok;
}

plDiversionAgent::Result plDiversionAgent::GetFileHistory(const plDiversionWorkspace& workspace, plStringView sRef, plStringView sPath, plDynamicArray<plDiversionCommit>& out_commits)
{
  plStringBuilder sUrl("/repos/");
  AppendEncoded(sUrl, workspace.m_sRepoId);
  sUrl.Append("/files/history/");
  AppendEncoded(sUrl, sRef);
  sUrl.Append("/");
  AppendPathEncoded(sUrl, sPath);
  sUrl.Append("?limit=50");

  plDynamicArray<plUInt8> body;
  const Result res = GetCloud(sUrl, body);
  if (res != Result::Ok)
    return res;

  if (plDiversionParse::FileHistory(body, out_commits).Failed())
  {
    m_sLastError = "Diversion's answer about the file's history did not parse.";
    return Result::Failed;
  }

  return Result::Ok;
}

plDiversionAgent::Result plDiversionAgent::GetFileContent(const plDiversionWorkspace& workspace, plStringView sRef, plStringView sPath, plDynamicArray<plUInt8>& out_content)
{
  plStringBuilder sUrl("/repos/");
  AppendEncoded(sUrl, workspace.m_sRepoId);
  sUrl.Append("/blobs/");
  AppendEncoded(sUrl, sRef);
  sUrl.Append("/");
  AppendPathEncoded(sUrl, sPath);

  // the bytes in the answer, not a redirect to storage
  sUrl.Append("?force_blob_embedding=true");

  return GetCloud(sUrl, out_content);
}

plDiversionAgent::Result plDiversionAgent::GetCommit(const plDiversionWorkspace& workspace, plStringView sCommitId, plDiversionCommit& out_commit)
{
  plStringBuilder sUrl("/repos/");
  AppendEncoded(sUrl, workspace.m_sRepoId);
  sUrl.Append("/commits/");
  AppendEncoded(sUrl, sCommitId);

  plDynamicArray<plUInt8> body;
  const Result res = GetCloud(sUrl, body);
  if (res != Result::Ok)
    return res;

  if (plDiversionParse::Commit(body, out_commit).Failed())
  {
    m_sLastError = "Diversion's answer about a commit did not parse.";
    return Result::Failed;
  }

  return Result::Ok;
}

plDiversionAgent::Result plDiversionAgent::GetUser(plDiversionUser& out_user)
{
  plDynamicArray<plUInt8> body;
  const Result res = GetCloud("/user", body);
  if (res != Result::Ok)
    return res;

  if (plDiversionParse::User(body, out_user).Failed())
  {
    m_sLastError = "Diversion's answer about the signed-in user did not parse.";
    return Result::Failed;
  }

  return Result::Ok;
}

plDiversionAgent::Result plDiversionAgent::GetAccess(const plDiversionWorkspace& workspace, plStringView sUserId, plString& out_sOwnerId, plString& out_sAccessMode)
{
  plStringBuilder sPath("/repos/");
  AppendEncoded(sPath, workspace.m_sRepoId);

  plDynamicArray<plUInt8> body;
  Result res = GetCloud(sPath, body);
  if (res != Result::Ok)
    return res;

  if (plDiversionParse::RepoOwner(body, out_sOwnerId).Failed())
  {
    m_sLastError = "Diversion's answer about the repository did not parse.";
    return Result::Failed;
  }

  sPath.Append("/collaborators");
  body.Clear();
  res = GetCloud(sPath, body);
  if (res != Result::Ok)
    return res;

  if (plDiversionParse::AccessMode(body, sUserId, out_sAccessMode).Failed())
  {
    m_sLastError = "Diversion's answer about the repository's collaborators did not parse.";
    return Result::Failed;
  }

  return Result::Ok;
}

plDiversionAgent::Result plDiversionAgent::Get(plStringView sUrl, bool bCloud, plDynamicArray<plUInt8>& out_body)
{
  plHttpClient::Request request;
  request.m_sUrl = sUrl;
  request.m_Headers.PushBack("X-DV-App-Name: PlasmaEditor");
  request.m_Timeout = plTime::MakeFromSeconds(bCloud ? 20 : 5);

  if (bCloud)
  {
    plStringBuilder sAuth("Authorization: Bearer ", m_sToken);
    request.m_Headers.PushBack(sAuth);
  }

  plUInt32 uiStatus = 0;
  if (plHttpClient::Send(request, uiStatus, out_body, m_sLastError).Failed())
  {
    if (!bCloud)
      Disconnect();

    return Result::Failed;
  }

  if (uiStatus == 401 && bCloud)
  {
    // the token went stale early; the next call fetches a new one
    m_sToken.Clear();
    m_sLastError = "Diversion turned down the sign-in. Check that the Diversion app is signed in.";
    return Result::Failed;
  }

  // the agent answers 404 / 412 for a workspace whose sync is paused
  if (uiStatus == 404 || uiStatus == 412)
    return Result::NotAvailable;

  if (uiStatus >= 400)
  {
    m_sLastError.SetFormat("Diversion answered with HTTP {}.", uiStatus);
    return Result::Failed;
  }

  return Result::Ok;
}

plResult plDiversionAgent::RefreshToken()
{
  const plTimestamp now = plTimestamp::CurrentTimestamp();
  if (!m_sToken.IsEmpty() && m_TokenExpires.IsValid() && m_TokenExpires.Compare(now + plTime::MakeFromSeconds(60), plTimestamp::CompareMode::Newer))
    return PL_SUCCESS;

  m_sToken.Clear();

  plStringBuilder sUrl(m_sBaseUrl, "/token/core");
  plDynamicArray<plUInt8> body;
  if (Get(sUrl, false, body) != Result::Ok)
    return PL_FAILURE;

  if (plDiversionParse::CoreToken(body, m_sToken, m_TokenExpires).Failed())
  {
    m_sLastError = "The Diversion agent has no sign-in. Sign in in the Diversion app.";
    return PL_FAILURE;
  }

  return PL_SUCCESS;
}
