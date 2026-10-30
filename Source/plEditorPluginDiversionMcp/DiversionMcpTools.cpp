#include <plEditorPluginDiversionMcp/EditorPluginDiversionMcpPCH.h>

#include <plEditorPluginDiversion/Provider/DiversionProvider.h>

#include <EditorUi/DialogHost.h>
#include <EditorUi/MessageBox.h>
#include <Foundation/IO/OSFile.h>
#include <Foundation/Threading/ThreadUtils.h>
#include <GuiFoundation/UIServices/UIServices.h>
#include <ToolsFoundation/Project/ToolsProject.h>
#include <plEditorPluginPlasmaMcp/McpJsonWriter.h>
#include <plEditorPluginPlasmaMcp/McpTools/McpTool.h>

/// \brief MCP tools for Diversion version control: read the workspace's state (status, changes, locks, a file's
/// history, branches) and, after the user allows it in the editor, commit, undo, lock, unlock and switch branch
/// (decisions D11 and D29).
class plMcpDiversionTool : public plMcpToolProvider
{
  PL_ADD_DYNAMIC_REFLECTION(plMcpDiversionTool, plMcpToolProvider);

public:
  virtual void GetSupportedTools(plDynamicArray<plMcpToolDesc>& out_tools) const override;
  virtual void Execute(plStringView sToolName, const plVariantDictionary& arguments, plMcpToolResult& out_result) override;

private:
  void Status(plDiversionProvider& ref_provider, plMcpToolResult& out_result);
  void Changes(plDiversionProvider& ref_provider, plMcpToolResult& out_result);
  void Locks(plDiversionProvider& ref_provider, plMcpToolResult& out_result);
  void History(plDiversionProvider& ref_provider, const plVariantDictionary& args, plMcpToolResult& out_result);
  void Branches(plDiversionProvider& ref_provider, plMcpToolResult& out_result);
  void Write(plDiversionProvider& ref_provider, plStringView sTool, const plVariantDictionary& args, plMcpToolResult& out_result);

  /// D29: asks in the editor before a write runs; "Allow for this session" skips the question afterwards.
  static bool AskToAllow(plStringView sAction, plStringView sDetails);
  static bool s_bAllowedForSession;
};

bool plMcpDiversionTool::s_bAllowedForSession = false;

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plMcpDiversionTool, 1, plRTTIDefaultAllocator<plMcpDiversionTool>)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

namespace
{
  /// How long a write may take before the client is told it is still running.
  constexpr plTime s_Timeout = plTime::MakeFromSeconds(120);

  plStringView GetString(const plVariantDictionary& args, plStringView sKey)
  {
    const plVariant* pValue = args.GetValue(sKey);
    return (pValue != nullptr && pValue->IsA<plString>()) ? pValue->Get<plString>().GetView() : plStringView();
  }

  /// A path as clients pass it: absolute, data-directory-relative ("Scenes/Main.plScene") or project-relative.
  plString ResolvePath(plStringView sPath)
  {
    plStringBuilder s = sPath;
    s.MakeCleanPath();
    if (plPathUtils::IsAbsolutePath(s))
      return s;

    plStringBuilder sDataDir = s;
    if (plEditorApp::GetSingleton()->MakeDataDirectoryRelativePathAbsolute(sDataDir) && plOSFile::ExistsFile(sDataDir))
      return sDataDir;

    plStringBuilder sProject = plToolsProject::GetSingleton()->GetProjectDirectory();
    sProject.AppendPath(s);
    return sProject;
  }

  /// Lets the provider deliver results (its main-thread callbacks) until bDone() or the timeout.
  template <typename DONE>
  bool Wait(plDiversionProvider& ref_provider, DONE bDone)
  {
    const plTime end = plTime::Now() + s_Timeout;
    while (!bDone() && plTime::Now() < end)
    {
      ref_provider.DeliverResults();
      plThreadUtils::Sleep(plTime::MakeFromMilliseconds(10));
    }
    return bDone();
  }

  const char* KindName(plDiversionChangeKind kind)
  {
    switch (kind)
    {
      case plDiversionChangeKind::Added:
        return "added";
      case plDiversionChangeKind::Deleted:
        return "deleted";
      case plDiversionChangeKind::Moved:
        return "moved";
      default:
        return "modified";
    }
  }
} // namespace

void plMcpDiversionTool::GetSupportedTools(plDynamicArray<plMcpToolDesc>& out_tools) const
{
  auto Add = [&](plStringView sName, plStringView sDescription, plStringView sSchema)
  {
    plMcpToolDesc& desc = out_tools.ExpandAndGetRef();
    desc.m_sName = sName;
    desc.m_sDescription = sDescription;
    desc.m_sInputSchema = sSchema;
  };

  constexpr const char* szNoArgs = R"({"type":"object","properties":{}})";
  constexpr const char* szPath = R"({"type":"object","properties":{"path":{"type":"string","description":"The file: absolute, data directory relative or project relative"}},"required":["path"]})";
  constexpr const char* szPaths = R"({"type":"object","properties":{"paths":{"type":"array","items":{"type":"string"},"description":"Workspace-relative paths as vcs_changes lists them"}},"required":["paths"]})";

  Add("vcs_status", "Diversion version control of the open project: connection, branch, sync state, number of changed files, incoming files, conflicts, locks you hold. Read-only.", szNoArgs);
  Add("vcs_changes", "The workspace's uncommitted changes: workspace-relative path, kind (added, modified, deleted, moved), and whether it is outside the project folder. Read-only.", szNoArgs);
  Add("vcs_locks", "Hard locks on the project's files (who, since when) and files someone else is editing (soft locks). Read-only.", szNoArgs);
  Add("vcs_history", "The commits that changed one file on the current branch, newest first: id, message, author, time. Read-only.", szPath);
  Add("vcs_branches", "The repository's branches and which one the workspace is on. Read-only.", szNoArgs);
  Add("vcs_commit", "Commits changed files to the current branch with a message; it goes straight to everyone on the branch. The user is asked in the editor first and may deny it.",
    R"({"type":"object","properties":{"paths":{"type":"array","items":{"type":"string"},"description":"Workspace-relative paths as vcs_changes lists them"},"message":{"type":"string"}},"required":["paths","message"]})");
  Add("vcs_undo", "Takes changed files back to the last commit (new files are kept). The user is asked in the editor first.", szPaths);
  Add("vcs_lock", "Takes the hard lock on a file (Diversion's Studio and Enterprise plans). The user is asked in the editor first.", szPath);
  Add("vcs_unlock", "Releases your hard lock on a file. The user is asked in the editor first.", szPath);
  Add("vcs_switch_branch", "Switches the workspace to another branch; pending changes are shelved (default) or taken along. Refused while documents have unsaved edits. The user is asked in the editor first.",
    R"({"type":"object","properties":{"branch":{"type":"string"},"changes":{"type":"string","enum":["shelve","take"]}},"required":["branch"]})");
}

void plMcpDiversionTool::Execute(plStringView sToolName, const plVariantDictionary& arguments, plMcpToolResult& out_result)
{
  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  if (pProvider == nullptr || !pProvider->IsAttached())
  {
    out_result.SetError("The open project is not under Diversion version control (or no project is open).");
    return;
  }

  if (sToolName == "vcs_status")
    Status(*pProvider, out_result);
  else if (!pProvider->IsConnected())
    out_result.SetError("Not connected to the Diversion agent; vcs_status says why.");
  else if (sToolName == "vcs_changes")
    Changes(*pProvider, out_result);
  else if (sToolName == "vcs_locks")
    Locks(*pProvider, out_result);
  else if (sToolName == "vcs_history")
    History(*pProvider, arguments, out_result);
  else if (sToolName == "vcs_branches")
    Branches(*pProvider, out_result);
  else
    Write(*pProvider, sToolName, arguments, out_result);
}

// Read tools.

void plMcpDiversionTool::Status(plDiversionProvider& ref_provider, plMcpToolResult& out_result)
{
  plVersionControlStatus status;
  ref_provider.GetStatus(status);

  const char* szSync = status.m_Sync == plVersionControlStatus::Sync::Paused ? "paused" : status.m_Sync == plVersionControlStatus::Sync::Syncing ? "syncing" : "synced";

  plMcpJsonWriter writer;
  writer.BeginObject();
  writer.AddVariableBool("connected", status.m_Connection == plVersionControlStatus::Connection::Connected);
  writer.AddVariableString("branch", status.m_sBranch);
  writer.AddVariableString("sync", szSync);
  writer.AddVariableString("workspaceRoot", status.m_sWorkspaceRoot);
  writer.AddVariableString("projectFolder", ref_provider.GetProjectPrefix());
  writer.AddVariableUInt32("changedFiles", status.m_uiChangedFiles);
  writer.AddVariableUInt32("incomingFiles", status.m_uiIncomingFiles);
  writer.AddVariableUInt32("conflicts", status.m_uiConflicts);
  writer.AddVariableUInt32("lockedByYou", status.m_uiLockedByYou);
  writer.AddVariableBool("hardLocksAvailable", ref_provider.CanUseLocks());
  writer.AddVariableString("message", status.m_sMessage);
  writer.EndObject();
  out_result.m_sText = writer.GetResult();
}

void plMcpDiversionTool::Changes(plDiversionProvider& ref_provider, plMcpToolResult& out_result)
{
  const plString sPrefix = ref_provider.GetProjectPrefix();

  plMcpJsonWriter writer;
  writer.BeginObject();
  writer.BeginArray("changes");
  for (const plDiversionChange& change : ref_provider.GetSnapshot()->m_Status.m_Changes)
  {
    writer.BeginObject();
    writer.AddVariableString("path", change.m_sPath);
    writer.AddVariableString("kind", KindName(change.m_Kind));
    if (!change.m_sPrevPath.IsEmpty())
      writer.AddVariableString("from", change.m_sPrevPath);
    writer.AddVariableBool("outsideProject", !sPrefix.IsEmpty() && !change.m_sPath.StartsWith_NoCase(plStringBuilder(sPrefix, "/")));
    writer.EndObject();
  }
  writer.EndArray();
  writer.AddVariableBool("complete", ref_provider.GetSnapshot()->m_Status.m_bComplete);
  writer.EndObject();
  out_result.m_sText = writer.GetResult();
}

void plMcpDiversionTool::Locks(plDiversionProvider& ref_provider, plMcpToolResult& out_result)
{
  const plDiversionSnapshot& snapshot = *ref_provider.GetSnapshot();

  plMcpJsonWriter writer;
  writer.BeginObject();
  writer.BeginArray("hardLocks");
  for (const plDiversionLock& lock : snapshot.m_Locks)
  {
    writer.BeginObject();
    writer.AddVariableString("path", lock.m_sPath);
    writer.AddVariableString("by", lock.m_sUserName);
    writer.AddVariableBool("mine", lock.m_sUserId == snapshot.m_User.m_sId);
    writer.AddVariableString("since", plDiversionProvider::FormatClock(lock.m_Time));
    writer.EndObject();
  }
  writer.EndArray();

  writer.BeginArray("othersEditing");
  for (auto it : snapshot.m_OtherChanges)
  {
    for (const plDiversionOtherChange& other : it.Value())
    {
      if (other.m_sWorkspaceId == snapshot.m_Workspace.m_sWorkspaceId)
        continue;

      writer.BeginObject();
      writer.AddVariableString("path", it.Key());
      writer.AddVariableString("by", other.m_sUser);
      writer.AddVariableString("branch", other.m_sBranchName);
      writer.AddVariableBool("uncommitted", !other.m_sWorkspaceId.IsEmpty());
      writer.EndObject();
    }
  }
  writer.EndArray();
  writer.AddVariableBool("hardLocksAvailable", ref_provider.CanUseLocks());
  writer.EndObject();
  out_result.m_sText = writer.GetResult();
}

void plMcpDiversionTool::History(plDiversionProvider& ref_provider, const plVariantDictionary& args, plMcpToolResult& out_result)
{
  const plStringView sPath = GetString(args, "path");
  if (sPath.IsEmpty())
  {
    out_result.SetError("'path' is missing.");
    return;
  }

  bool bDone = false;
  bool bOk = false;
  plDynamicArray<plDiversionCommit> commits;
  plString sError;
  ref_provider.RequestHistory(ResolvePath(sPath), [&](bool bSucceeded, const plDynamicArray<plDiversionCommit>& result, plStringView sMessage)
    {
      bDone = true;
      bOk = bSucceeded;
      commits = result;
      sError = sMessage; });

  if (!Wait(ref_provider, [&]()
        { return bDone; }) ||
      !bOk)
  {
    out_result.SetError(bDone ? sError.GetView() : plStringView("Diversion did not answer in time."));
    return;
  }

  plMcpJsonWriter writer;
  writer.BeginObject();
  writer.BeginArray("commits");
  for (const plDiversionCommit& commit : commits)
  {
    writer.BeginObject();
    writer.AddVariableString("id", commit.m_sId);
    writer.AddVariableString("message", commit.m_sMessage);
    writer.AddVariableString("author", commit.m_sAuthor);
    writer.AddVariableString("time", plDiversionProvider::FormatClock(commit.m_Time));
    writer.EndObject();
  }
  writer.EndArray();
  writer.EndObject();
  out_result.m_sText = writer.GetResult();
}

void plMcpDiversionTool::Branches(plDiversionProvider& ref_provider, plMcpToolResult& out_result)
{
  const plDiversionSnapshot& snapshot = *ref_provider.GetSnapshot();

  plMcpJsonWriter writer;
  writer.BeginObject();
  writer.AddVariableString("current", snapshot.m_Workspace.m_sBranchName);
  writer.BeginArray("branches");
  for (const plDiversionBranch& branch : snapshot.m_Branches)
  {
    writer.BeginObject();
    writer.AddVariableString("name", branch.m_sName);
    writer.AddVariableString("headCommit", branch.m_sHeadCommitId);
    writer.EndObject();
  }
  writer.EndArray();
  writer.EndObject();
  out_result.m_sText = writer.GetResult();
}

// Write tools.

bool plMcpDiversionTool::AskToAllow(plStringView sAction, plStringView sDetails)
{
  if (s_bAllowedForSession)
    return true;

  plEditorUiMessageBoxDesc desc;
  desc.m_sTitle = "An MCP client asks";
  desc.m_sText = plStringBuilder("An MCP client wants to ", sAction, ".\n\n", sDetails);
  desc.m_Kind = plEditorUiMessageKind::Question;
  desc.m_sCheckLabel = "Allow version control actions from MCP for this session";

  plEditorUiDialogButton& allow = desc.m_Buttons.ExpandAndGetRef();
  allow.m_sId = "allow";
  allow.m_sLabel = "Allow";
  allow.m_Role = plEditorUiDialogButton::Role::Accept;
  plEditorUiDialogButton& deny = desc.m_Buttons.ExpandAndGetRef();
  deny.m_sId = "deny";
  deny.m_sLabel = "Deny";
  deny.m_Role = plEditorUiDialogButton::Role::Reject;
  desc.m_sDefaultButton = "deny";
  desc.m_sEscapeButton = "deny";

  const plEditorUiMessageBoxResult result = plEditorUiMessageBox::Show(plEditorUiServices::GetEngineDialogOwner(), desc);
  if (result.m_sButton != "allow")
    return false;

  if (result.m_bChecked)
    s_bAllowedForSession = true;
  return true;
}

void plMcpDiversionTool::Write(plDiversionProvider& ref_provider, plStringView sTool, const plVariantDictionary& args, plMcpToolResult& out_result)
{
  bool bDone = false;
  bool bOk = false;
  plString sMessage;
  auto onDone = [&](bool bSucceeded, plStringView sResult)
  {
    bDone = true;
    bOk = bSucceeded;
    sMessage = sResult;
  };

  plDynamicArray<plString> paths;
  if (const plVariant* pPaths = args.GetValue("paths"); pPaths != nullptr && pPaths->IsA<plVariantArray>())
  {
    for (const plVariant& path : pPaths->Get<plVariantArray>())
    {
      if (path.IsA<plString>())
        paths.PushBack(path.Get<plString>());
    }
  }

  plStringBuilder sList;
  for (const plString& sPath : paths)
    sList.Append("\n    ", sPath);

  const plString sBranch = ref_provider.GetSnapshot()->m_Workspace.m_sBranchName;

  if (sTool == "vcs_commit")
  {
    const plStringView sCommitMessage = GetString(args, "message");
    if (paths.IsEmpty() || sCommitMessage.IsEmpty())
    {
      out_result.SetError("'paths' and 'message' are needed.");
      return;
    }

    plStringBuilder sDetails;
    sDetails.SetFormat("Commit to {}, straight to everyone on it, with the message \"{}\":{}", sBranch, sCommitMessage, sList);
    if (!AskToAllow("commit", sDetails))
    {
      out_result.SetError("The user denied the commit; nothing ran.");
      return;
    }

    ref_provider.Commit(paths, sCommitMessage, onDone);
  }
  else if (sTool == "vcs_undo")
  {
    if (paths.IsEmpty())
    {
      out_result.SetError("'paths' is needed.");
      return;
    }

    if (!AskToAllow("undo changes", plStringBuilder("These files go back to the last commit (new files are kept):", sList)))
    {
      out_result.SetError("The user denied undoing the changes; nothing ran.");
      return;
    }

    ref_provider.Undo(paths, false, onDone);
  }
  else if (sTool == "vcs_lock" || sTool == "vcs_unlock")
  {
    const plString sPath = ResolvePath(GetString(args, "path"));
    const bool bLock = sTool == "vcs_lock";
    if (!ref_provider.CanUseLocks())
    {
      out_result.SetError("This repository's plan has no hard locks (Studio and Enterprise have them).");
      return;
    }

    if (!AskToAllow(bLock ? "take a lock" : "release a lock", plStringBuilder(bLock ? "Lock " : "Unlock ", plPathUtils::GetFileNameAndExtension(sPath), ".")))
    {
      out_result.SetError("The user denied it; nothing ran.");
      return;
    }

    if (bLock)
      ref_provider.Lock(sPath, onDone);
    else
      ref_provider.Unlock(sPath, false, onDone);
  }
  else if (sTool == "vcs_switch_branch")
  {
    const plStringView sTarget = GetString(args, "branch");
    const bool bShelve = GetString(args, "changes") != "take";
    if (sTarget.IsEmpty())
    {
      out_result.SetError("'branch' is needed.");
      return;
    }

    // unsaved edits are the user's to decide about, not a client's
    for (plDocumentManager* pManager : plDocumentManager::GetAllDocumentManagers())
    {
      for (plDocument* pDocument : pManager->GetAllOpenDocuments())
      {
        if (pDocument->IsModified())
        {
          out_result.SetError(plStringBuilder(plPathUtils::GetFileNameAndExtension(pDocument->GetDocumentPath()), " has unsaved edits. The user has to save or discard them before a switch."));
          return;
        }
      }
    }

    plStringBuilder sDetails;
    sDetails.SetFormat("Switch the workspace from {} to {}; pending changes are {}.", sBranch, sTarget, bShelve ? "shelved" : "taken along");
    if (!AskToAllow("switch branch", sDetails))
    {
      out_result.SetError("The user denied the switch; nothing ran.");
      return;
    }

    ref_provider.Checkout(sTarget, bShelve, onDone);
  }
  else
  {
    out_result.SetError(plStringBuilder("Unknown tool '", sTool, "'."));
    return;
  }

  if (!Wait(ref_provider, [&]()
        { return bDone; }))
  {
    out_result.SetError("Diversion did not finish in time; it may still be running. vcs_status shows the result.");
    return;
  }

  if (!bOk)
  {
    out_result.SetError(sMessage);
    return;
  }

  plMcpJsonWriter writer;
  writer.BeginObject();
  writer.AddVariableBool("done", true);
  writer.AddVariableString("result", sMessage);
  writer.EndObject();
  out_result.m_sText = writer.GetResult();
}

PL_STATICLINK_FILE(EditorPluginDiversionMcp, EditorPluginDiversionMcp_DiversionMcpTools);
