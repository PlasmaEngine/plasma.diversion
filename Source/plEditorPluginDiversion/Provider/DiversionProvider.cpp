#include <plEditorPluginDiversion/EditorPluginDiversionPCH.h>

#include <plEditorPluginDiversion/Client/DiversionIgnore.h>
#include <plEditorPluginDiversion/Client/DiversionParse.h>
#include <plEditorPluginDiversion/Client/DiversionPaths.h>
#include <plEditorPluginDiversion/Panels/ChangesPanel.h>
#include <plEditorPluginDiversion/Panels/ConflictsPanel.h>
#include <plEditorPluginDiversion/Panels/HistoryPanel.h>
#include <plEditorPluginDiversion/Panels/LocksPanel.h>
#include <plEditorPluginDiversion/Provider/DiversionDifferenceMarks.h>
#include <plEditorPluginDiversion/Provider/DiversionPreferences.h>
#include <plEditorPluginDiversion/Provider/DiversionProvider.h>

#include <EditorFramework/EditorApp/EditorApp.h>
#include <EditorUi/MessageBox.h>
#include <EditorUi/Runtime.h>
#include <Foundation/IO/OSFile.h>
#include <GuiFoundation/UIServices/UIServices.h>
#include <ToolsFoundation/Document/Document.h>

plDiversionProvider* plDiversionProvider::s_pSingleton = nullptr;

namespace
{
  /// Above this many changed files the Asset Browser is told "anything may have changed" rather than given a list.
  constexpr plUInt32 MaxListedChanges = 1000;

  plVersionControlFileState ToState(plDiversionChangeKind kind)
  {
    switch (kind)
    {
      case plDiversionChangeKind::Added:
        return plVersionControlFileState::Added;
      case plDiversionChangeKind::Deleted:
        return plVersionControlFileState::Deleted;
      case plDiversionChangeKind::Moved:
        return plVersionControlFileState::Moved;
      default:
        return plVersionControlFileState::Modified;
    }
  }

  bool SameInfo(const plVersionControlFileInfo& a, const plVersionControlFileInfo& b)
  {
    if (a.m_State != b.m_State || a.m_OtherChanges.GetCount() != b.m_OtherChanges.GetCount())
      return false;

    if (a.m_bLocked != b.m_bLocked || a.m_Lock.m_sUser != b.m_Lock.m_sUser || a.m_Lock.m_bMine != b.m_Lock.m_bMine)
      return false;

    for (plUInt32 i = 0; i < a.m_OtherChanges.GetCount(); ++i)
    {
      if (a.m_OtherChanges[i].m_sUser != b.m_OtherChanges[i].m_sUser || a.m_OtherChanges[i].m_sBranch != b.m_OtherChanges[i].m_sBranch)
        return false;
    }

    return true;
  }

  plString ReadTextFile(plStringView sPath)
  {
    plOSFile file;
    if (file.Open(sPath, plFileOpenMode::Read).Failed())
      return {};

    plDynamicArray<plUInt8> content;
    file.ReadAll(content);
    file.Close();

    plStringBuilder sText;
    sText.Set(plStringView(reinterpret_cast<const char*>(content.GetData()), content.GetCount()));
    return sText;
  }
} // namespace

plDiversionProvider::plDiversionProvider()
{
  s_pSingleton = this;
}

plDiversionProvider::~plDiversionProvider()
{
  Detach();
  s_pSingleton = nullptr;
}

bool plDiversionProvider::Claims(plStringView sProjectDir)
{
  // an EditorProcessor transforms assets for the editor that started it, which already watches the workspace
  if (plEditorApp::GetSingleton() != nullptr && plEditorApp::GetSingleton()->IsInHeadlessMode())
    return false;

  // the workspace root holds a .diversion folder; the project may sit anywhere below it
  plStringBuilder sFolder = sProjectDir;
  sFolder.MakeCleanPath();

  while (!sFolder.IsEmpty())
  {
    plStringBuilder sMarker = sFolder;
    sMarker.AppendPath(".diversion");

    if (plOSFile::ExistsDirectory(sMarker))
      return true;

    const plStringBuilder sParent = sFolder.GetFileDirectory();
    plStringBuilder sTrimmed = sParent;
    sTrimmed.Trim(nullptr, "/");

    if (sTrimmed.GetElementCount() >= sFolder.GetElementCount())
      break;

    sFolder = sTrimmed;
  }

  return false;
}

void plDiversionProvider::Attach(plStringView sProjectDir)
{
  Detach();

  plStringBuilder sDir = sProjectDir;
  sDir.MakeCleanPath();
  sDir.Trim(nullptr, "/");
  m_sProjectDir = sDir;

  // queried now so that its page shows in the preferences while the project is open
  m_pPreferences = plPreferences::QueryPreferences<plDiversionProjectPreferences>();
  m_pPreferences->m_ChangedEvent.AddEventHandler(plMakeDelegate(&plDiversionProvider::PreferencesChanged, this));

  m_bIgnoreChecked = false;
  m_uiSnapshotVersion = 0;
  m_LockSupport = plDiversionLockSupport::Unknown;
  m_EditedFiles.Clear();

  m_pPoller = PL_DEFAULT_NEW(plDiversionPoller, m_sProjectDir);
  m_pPoller->Start();

  plDocumentNotices::s_Requests.AddEventHandler(plMakeDelegate(&plDiversionProvider::DocumentNoticesRequested, this));
  plDocument::s_EventsAny.AddEventHandler(plMakeDelegate(&plDiversionProvider::DocumentEventHandler, this));
  plDocumentManager::s_SaveRequests.AddEventHandler(plMakeDelegate(&plDiversionProvider::SaveRequested, this));
  plAssetBrowserContextMenu::s_Events.AddEventHandler(plMakeDelegate(&plDiversionProvider::ContextMenuRequested, this));
  plDocumentManager::s_Events.AddEventHandler(plMakeDelegate(&plDiversionProvider::DocumentManagerEventHandler, this));

  m_pDifferenceMarks = PL_DEFAULT_NEW(plDiversionDifferenceMarks);
  plEditorUiTreeExtensionRegistry::Register(m_pDifferenceMarks.Borrow());
  plPropertyGridRowDecorator::Register(m_pDifferenceMarks.Borrow());

  plDiversionChangesPanel::Register();
  plDiversionLocksPanel::Register();
  plDiversionHistoryPanel::Register();
  plDiversionConflictsPanel::Register();
}

void plDiversionProvider::Detach()
{
  if (m_pPoller != nullptr)
  {
    // the panels' contents go before the poller they read
    plDiversionConflictsPanel::Unregister();
    plDiversionHistoryPanel::Unregister();
    plDiversionLocksPanel::Unregister();
    plDiversionChangesPanel::Unregister();

    plPropertyGridRowDecorator::Unregister(m_pDifferenceMarks.Borrow());
    plEditorUiTreeExtensionRegistry::Unregister(m_pDifferenceMarks.Borrow());
    m_pDifferenceMarks.Clear();
    m_CompareDocuments.Clear();

    plDocumentManager::s_Events.RemoveEventHandler(plMakeDelegate(&plDiversionProvider::DocumentManagerEventHandler, this));
    plAssetBrowserContextMenu::s_Events.RemoveEventHandler(plMakeDelegate(&plDiversionProvider::ContextMenuRequested, this));
    plDocumentManager::s_SaveRequests.RemoveEventHandler(plMakeDelegate(&plDiversionProvider::SaveRequested, this));
    plDocument::s_EventsAny.RemoveEventHandler(plMakeDelegate(&plDiversionProvider::DocumentEventHandler, this));
    plDocumentNotices::s_Requests.RemoveEventHandler(plMakeDelegate(&plDiversionProvider::DocumentNoticesRequested, this));

    m_pPoller->Stop();
    m_pPoller.Clear();
  }

  if (m_pPreferences != nullptr)
  {
    m_pPreferences->m_ChangedEvent.RemoveEventHandler(plMakeDelegate(&plDiversionProvider::PreferencesChanged, this));
    m_pPreferences = nullptr;
  }

  m_pSnapshot.Clear();
  m_Files.Clear();
  m_Conflicts.Clear();
  m_uiChangedFiles = 0;
  m_uiLockedByYou = 0;
  m_sProjectDir.Clear();
}

void plDiversionProvider::Update()
{
  if (m_pPoller == nullptr)
    return;

  // results go out after the frame: their callbacks open documents and dialogs, which must not run inside the editor's
  // tick (this runs in it)
  if (m_pPoller->HasFinishedWork() && !m_bDeliveryQueued)
  {
    m_bDeliveryQueued = true;
    plEditorUiRuntime::RunAfterFrame([]()
      {
        if (plDiversionProvider* pProvider = plDiversionProvider::GetSingleton())
        {
          pProvider->m_bDeliveryQueued = false;
          pProvider->DeliverResults();
        } });
  }

  plSharedPtr<const plDiversionSnapshot> pSnapshot = m_pPoller->GetSnapshot(m_uiSnapshotVersion);
  if (pSnapshot == nullptr)
    return;

  m_pSnapshot = pSnapshot;
  ApplySnapshot(*m_pSnapshot);
}

void plDiversionProvider::DeliverResults()
{
  if (m_pPoller != nullptr)
    m_pPoller->CallFinishedCommands();
}

void plDiversionProvider::ApplySnapshot(const plDiversionSnapshot& snapshot)
{
  plHashTable<plString, plVersionControlFileInfo> files;
  m_uiChangedFilesElsewhere = 0;

  if (snapshot.m_State == plDiversionSnapshot::State::Connected)
  {
    const plString sProjectKey = MakeKey(m_sProjectDir);
    const plString& sRoot = snapshot.m_Workspace.m_sRoot;

    auto IsInProject = [&](plStringView sKey)
    {
      return sKey.StartsWith(sProjectKey) && sKey.GetElementCount() > sProjectKey.GetElementCount() && sKey.GetStartPointer()[sProjectKey.GetElementCount()] == '/';
    };

    for (const plDiversionChange& change : snapshot.m_Status.m_Changes)
    {
      const plStringBuilder sAbsolute(sRoot, "/", change.m_sPath);
      const plString sKey = MakeKey(sAbsolute);

      // conflict backups are Diversion's, listed in Conflicts, not as changes
      if (IsProviderFile(sAbsolute))
        continue;

      if (!IsInProject(sKey))
      {
        ++m_uiChangedFilesElsewhere;
        continue;
      }

      plVersionControlFileInfo& info = files[sKey];
      info.m_State = ToState(change.m_Kind);

      if (!change.m_sPrevPath.IsEmpty())
        info.m_sMovedFrom = plStringBuilder(sRoot, "/", change.m_sPrevPath);
    }

    for (auto it : snapshot.m_OtherChanges)
    {
      const plStringBuilder sAbsolute(sRoot, "/", it.Key());
      const plString sKey = MakeKey(sAbsolute);
      if (!IsInProject(sKey))
        continue;

      for (const plDiversionOtherChange& other : it.Value())
      {
        // edits in this very workspace are our own changes, not someone else's
        if (other.m_sWorkspaceId == snapshot.m_Workspace.m_sWorkspaceId)
          continue;

        plVersionControlOtherChange& change = files[sKey].m_OtherChanges.ExpandAndGetRef();
        change.m_sUser = other.m_sUser;
        change.m_sBranch = other.m_sBranchName;
        change.m_bUncommitted = !other.m_sWorkspaceId.IsEmpty();
        change.m_Time = other.m_Time;
      }
    }

    for (const plDiversionLock& lock : snapshot.m_Locks)
    {
      const plStringBuilder sAbsolute(sRoot, "/", lock.m_sPath);
      const plString sKey = MakeKey(sAbsolute);
      if (!IsInProject(sKey))
        continue;

      plVersionControlFileInfo& info = files[sKey];
      info.m_bLocked = true;
      info.m_Lock.m_sUser = lock.m_sUserName;
      info.m_Lock.m_Time = lock.m_Time;
      info.m_Lock.m_bMine = !snapshot.m_User.m_sId.IsEmpty() && lock.m_sUserId == snapshot.m_User.m_sId;
    }
  }

  UpdateLockSupport(snapshot);
  FindConflicts(m_Conflicts);

  // what changed since the last snapshot, for the views to redraw
  plDynamicArray<plString> changed;
  for (auto it : files)
  {
    auto itOld = m_Files.Find(it.Key());
    if (!itOld.IsValid() || !SameInfo(itOld.Value(), it.Value()))
      changed.PushBack(it.Key());
  }

  for (auto it : m_Files)
  {
    if (!files.Contains(it.Key()))
      changed.PushBack(it.Key());
  }

  m_Files.Swap(files);

  m_uiChangedFiles = 0;
  m_uiLockedByYou = 0;
  for (auto it : m_Files)
  {
    if (it.Value().m_State != plVersionControlFileState::Unchanged)
      ++m_uiChangedFiles;

    if (it.Value().m_bLocked && it.Value().m_Lock.m_bMine)
      ++m_uiLockedByYou;
  }

  plVersionControl::NotifyStatusChanged();

  if (!changed.IsEmpty())
  {
    if (changed.GetCount() > MaxListedChanges)
      changed.Clear();

    plVersionControl::NotifyFilesChanged(changed);
  }

  // once per session, when the workspace is first known
  if (snapshot.m_State == plDiversionSnapshot::State::Connected && !m_bIgnoreChecked)
  {
    m_bIgnoreChecked = true;

    // a question box must not open inside the editor's frame update
    plEditorUiRuntime::RunAfterFrame([]()
      {
        if (plDiversionProvider* pProvider = plDiversionProvider::GetSingleton(); pProvider != nullptr && pProvider->IsConnected())
          pProvider->CheckIgnoreFile(false); });
  }
}

void plDiversionProvider::GetStatus(plVersionControlStatus& out_status) const
{
  out_status.m_sProvider = "Diversion";
  out_status.m_Connection = plVersionControlStatus::Connection::Unavailable;

  if (m_pSnapshot == nullptr || m_pSnapshot->m_State == plDiversionSnapshot::State::Starting)
  {
    out_status.m_sMessage = "Connecting to the Diversion agent...";
    return;
  }

  const plDiversionSnapshot& snapshot = *m_pSnapshot;

  if (snapshot.m_State == plDiversionSnapshot::State::AgentDown)
  {
    out_status.m_sMessage = "The Diversion agent is not running. Start the Diversion app, or click to retry.";
    return;
  }

  if (snapshot.m_State == plDiversionSnapshot::State::NoWorkspace)
  {
    out_status.m_sMessage = "The project folder has a .diversion folder, but the Diversion agent signed in on this machine does not know the workspace.";
    return;
  }

  out_status.m_Connection = plVersionControlStatus::Connection::Connected;
  out_status.m_sBranch = snapshot.m_Workspace.m_sBranchName;
  out_status.m_sWorkspaceRoot = snapshot.m_Workspace.m_sRoot;
  out_status.m_uiChangedFiles = m_uiChangedFiles;
  out_status.m_uiLockedByYou = m_uiLockedByYou;
  out_status.m_uiIncomingFiles = snapshot.m_Sync.m_uiIncoming;
  out_status.m_bVersionWarning = !IsVersionTested();

  out_status.m_uiConflicts = m_Conflicts.GetCount();

  if (snapshot.m_Sync.m_bPaused)
    out_status.m_Sync = plVersionControlStatus::Sync::Paused;
  else if (!snapshot.m_Sync.m_bComplete)
    out_status.m_Sync = plVersionControlStatus::Sync::Syncing;
  else
    out_status.m_Sync = plVersionControlStatus::Sync::Synced;

  plStringBuilder sMessage;
  sMessage.SetFormat("Repository {} on branch {}\nWorkspace {}", snapshot.m_Workspace.m_sRepoName, snapshot.m_Workspace.m_sBranchName, snapshot.m_Workspace.m_sRoot);

  // a workspace can hold more than the project (a repository with the game, its tools and its source art)
  const plString sPrefix = GetProjectPrefix();
  if (!sPrefix.IsEmpty())
  {
    sMessage.AppendFormat("\nThe project is the folder {}/ of the workspace; badges and the count cover only its files.", sPrefix);

    if (m_uiChangedFilesElsewhere > 0)
      sMessage.AppendFormat("\n{} more changed files are elsewhere in the workspace.", m_uiChangedFilesElsewhere);
  }

  sMessage.AppendFormat("\nDiversion agent {}", snapshot.m_sAgentVersion);
  if (out_status.m_bVersionWarning)
    sMessage.AppendFormat(", outside the versions this package was tested with ({}). Everything keeps working; if something looks wrong, the package may need an update.", TestedVersions);

  if (out_status.m_uiConflicts > 0)
    sMessage.AppendFormat("\n{} file{} in conflict with incoming changes: see Conflicts.", out_status.m_uiConflicts, out_status.m_uiConflicts == 1 ? " is" : "s are");

  if (out_status.m_Sync == plVersionControlStatus::Sync::Paused)
    sMessage.Append("\nSync is paused: saved files stay on this machine, and teammates' commits do not arrive.");

  if (!snapshot.m_Status.m_bComplete)
    sMessage.Append("\nThe workspace has more changes than Diversion lists at once; some badges may be missing.");

  if (!snapshot.m_bOtherChangesKnown)
    sMessage.Append("\nWho else is editing is not known right now (no answer from Diversion's cloud).");

  if (m_LockSupport == plDiversionLockSupport::No)
    sMessage.Append("\nHard locks need Diversion's Studio or Enterprise plan; this repository's plan has none.");

  if (!snapshot.m_sMessage.IsEmpty())
    sMessage.Append("\n", snapshot.m_sMessage);

  out_status.m_sMessage = sMessage;
}

void plDiversionProvider::GetFileInfo(plStringView sAbsolutePath, plVersionControlFileInfo& out_info) const
{
  auto it = m_Files.Find(MakeKey(sAbsolutePath));
  if (!it.IsValid())
    return;

  out_info = it.Value();

  if (m_pPreferences != nullptr && !m_pPreferences->m_bShowBadges)
    out_info.m_State = plVersionControlFileState::Unchanged;

  if (m_pPreferences != nullptr && !m_pPreferences->m_bShowOthersEditing)
    out_info.m_OtherChanges.Clear();
}

bool plDiversionProvider::IsProviderFile(plStringView sAbsolutePath) const
{
  // conflict backups: "Scene.dv-conflict.plScene", "Scene.dv-conflict-1.plScene"
  return plPathUtils::GetFileNameAndExtension(sAbsolutePath).FindSubString_NoCase(".dv-conflict") != nullptr;
}

bool plDiversionProvider::IsConnected() const
{
  return m_pSnapshot != nullptr && m_pSnapshot->m_State == plDiversionSnapshot::State::Connected;
}

bool plDiversionProvider::IsSyncPaused() const
{
  return IsConnected() && m_pSnapshot->m_Sync.m_bPaused;
}

void plDiversionProvider::SetSyncPaused(bool bPaused)
{
  if (m_pPoller == nullptr)
    return;

  const plStringView args[] = {"workspace", bPaused ? plStringView("pause") : plStringView("resume")};
  m_pPoller->QueueCommand(args);
}

void plDiversionProvider::Refresh()
{
  if (m_pPoller != nullptr)
    m_pPoller->Wake();
}

void plDiversionProvider::OpenInDiversion()
{
  if (m_pPoller == nullptr)
    return;

  const plStringView args[] = {"view"};
  m_pPoller->QueueCommand(args);
}

plString plDiversionProvider::GetProjectPrefix() const
{
  if (!IsConnected())
    return {};

  const plString& sRoot = m_pSnapshot->m_Workspace.m_sRoot;
  if (m_sProjectDir.GetElementCount() <= sRoot.GetElementCount())
    return {};

  return plStringView(m_sProjectDir.GetData() + sRoot.GetElementCount() + 1, m_sProjectDir.GetData() + m_sProjectDir.GetElementCount());
}

bool plDiversionProvider::IsBusy() const
{
  return m_pPoller != nullptr && m_pPoller->IsBusy();
}

void plDiversionProvider::GetAllChangedPaths(plDynamicArray<plString>& out_paths) const
{
  out_paths.Clear();

  if (m_pSnapshot == nullptr)
    return;

  for (const plDiversionChange& change : m_pSnapshot->m_Status.m_Changes)
    out_paths.PushBack(change.m_sPath);
}

void plDiversionProvider::Run(plArrayPtr<const plStringView> args, Done onDone, bool bCommit)
{
  if (m_pPoller == nullptr)
  {
    if (onDone.IsValid())
      onDone(false, "The project is not connected to Diversion.");
    return;
  }

  m_pPoller->QueueCommand(args, [onDone, bCommit](const plDiversionCli::Result& result)
    {
      if (!onDone.IsValid())
        return;

      if (!result.m_bStarted)
        onDone(false, plDiversionCli::FirstLine(result.m_sOutput));
      else if (!result.Succeeded())
        onDone(false, plDiversionCli::FirstLine(result.m_sOutput));
      else if (bCommit)
        onDone(true, plDiversionParse::CommitId(result.m_sOutput));
      else
        onDone(true, plDiversionCli::FirstLine(result.m_sOutput)); });
}

void plDiversionProvider::Commit(const plDynamicArray<plString>& paths, plStringView sMessage, Done onDone)
{
  plDynamicArray<plString> all;
  GetAllChangedPaths(all);

  plDynamicArray<plString> arguments;
  plDiversionPaths::MakeArguments(paths, all, arguments);

  const plString sSafeMessage = plDiversionCli::MakeArgument(sMessage);

  plDynamicArray<plStringView> args;
  args.PushBack("commit");
  for (const plString& sPath : arguments)
    args.PushBack(sPath);
  args.PushBack("-m");
  args.PushBack(sSafeMessage);

  Run(args, onDone, true);
}

void plDiversionProvider::Undo(const plDynamicArray<plString>& paths, bool bDeleteNew, Done onDone)
{
  for (const plString& sPath : paths)
    NoteOwnChange(ToAbsolutePath(sPath), "undoing your changes");

  plDynamicArray<plString> all;
  GetAllChangedPaths(all);

  plDynamicArray<plString> arguments;
  plDiversionPaths::MakeArguments(paths, all, arguments);

  plDynamicArray<plStringView> args;
  args.PushBack("reset");
  for (const plString& sPath : arguments)
    args.PushBack(sPath);

  // without --clean, dv leaves new files alone
  if (bDeleteNew)
    args.PushBack("--clean");

  args.PushBack("-f");
  Run(args, onDone, false);
}

void plDiversionProvider::Shelve(plStringView sName, const plDynamicArray<plString>& paths, Done onDone)
{
  plDynamicArray<plString> all;
  GetAllChangedPaths(all);

  plDynamicArray<plString> arguments;
  plDiversionPaths::MakeArguments(paths, all, arguments);

  const plString sSafeName = plDiversionCli::MakeArgument(sName);

  plDynamicArray<plStringView> args;
  args.PushBack("shelf");
  args.PushBack("create");
  args.PushBack(sSafeName);
  for (const plString& sPath : arguments)
    args.PushBack(sPath);

  Run(args, onDone, false);
}

void plDiversionProvider::ApplyShelf(plStringView sShelfId, Done onDone)
{
  const plStringView args[] = {"shelf", "apply", sShelfId, "-f"};
  Run(args, onDone, false);
}

void plDiversionProvider::DeleteShelf(plStringView sShelfId, Done onDone)
{
  const plStringView args[] = {"shelf", "delete", sShelfId, "-f"};
  Run(args, onDone, false);
}

void plDiversionProvider::CheckIgnoreFile(bool bAlways)
{
  if (!IsConnected() || m_pPreferences == nullptr)
    return;

  if (!bAlways && !m_pPreferences->m_bCheckIgnoreFile)
    return;

  plStringBuilder sIgnorePath = m_pSnapshot->m_Workspace.m_sRoot;
  sIgnorePath.AppendPath(".dvignore");

  const plString sPrefix = GetProjectPrefix();
  const plString sIgnoreFile = ReadTextFile(sIgnorePath);

  plDynamicArray<plString> missing;
  plDiversionIgnore::FindMissingLines(sIgnoreFile, sPrefix, missing);

  // files that changed below these folders were committed at some point: ignoring them now does not stop them syncing
  plDynamicArray<plString> wanted;
  plDiversionIgnore::GetWantedLines(sPrefix, wanted);

  plUInt32 uiTracked = 0;
  for (const plDiversionChange& change : m_pSnapshot->m_Status.m_Changes)
  {
    if (change.m_Kind == plDiversionChangeKind::Added)
      continue;

    for (const plString& sFolder : wanted)
    {
      if (change.m_sPath.StartsWith_NoCase(sFolder))
      {
        ++uiTracked;
        break;
      }
    }
  }

  plStringBuilder sDetails;
  if (uiTracked > 0)
  {
    sDetails.SetFormat("{} changed files in these folders are already in the repository. An ignore line does not take them out: they keep "
                       "syncing until they are deleted from the repository, e.g. in the Diversion app.",
      uiTracked);
  }

  if (missing.IsEmpty())
  {
    if (bAlways)
    {
      plStringBuilder sText("The workspace's .dvignore already ignores the editor's transform output (AssetCache) and installed packages (Plugins).");
      if (!sDetails.IsEmpty())
        sText.Append("\n\n", sDetails);

      plEditorUiServices::MessageBoxInformation(plFmt("{}", sText));
    }

    return;
  }

  plStringBuilder sText;
  sText.Set("The editor rebuilds these folders on every machine, so they should not sync. Add them to the workspace's .dvignore?\n");
  for (const plString& sLine : missing)
    sText.Append("\n    ", sLine);

  sText.Append("\n\nInstalled packages come back from Editor/Packages.ddl, and assets are transformed again on each machine.");

  const plString buttons[] = {"Add to .dvignore", "Not now", "Don't ask again"};
  const plInt32 iChoice = plEditorUiServices::MessageBoxCustom("Diversion", sText, plEditorUiMessageKind::Question, buttons, 0, 1, sDetails);

  if (iChoice == 2)
  {
    m_pPreferences->m_bCheckIgnoreFile = false;
    plPreferences::SaveProjectPreferences();
    return;
  }

  if (iChoice != 0)
    return;

  const plString sNewFile = plDiversionIgnore::AppendLines(sIgnoreFile, missing);

  plOSFile file;
  if (file.Open(sIgnorePath, plFileOpenMode::Write).Failed() || file.Write(sNewFile.GetData(), sNewFile.GetElementCount()).Failed())
  {
    plEditorUiServices::MessageBoxWarning(plFmt("Could not write '{}'.", sIgnorePath));
    return;
  }

  file.Close();
  plLog::Success("Added {} lines to '{}'.", missing.GetCount(), sIgnorePath);
}

void plDiversionProvider::PreferencesChanged(plPreferences* pPreferences)
{
  PL_IGNORE_UNUSED(pPreferences);

  // every badge may look different now
  plVersionControl::NotifyFilesChanged({});
}

plString plDiversionProvider::MakeKey(plStringView sAbsolutePath)
{
  plStringBuilder sKey = sAbsolutePath;
  sKey.MakeCleanPath();
  sKey.ToLower();
  return sKey;
}
