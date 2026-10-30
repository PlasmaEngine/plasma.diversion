#include <plEditorPluginDiversion/EditorPluginDiversionPCH.h>

#include <plEditorPluginDiversion/Client/DiversionParse.h>
#include <plEditorPluginDiversion/Panels/ConflictsPanel.h>
#include <plEditorPluginDiversion/Panels/HistoryPanel.h>
#include <plEditorPluginDiversion/Provider/DiversionPreferences.h>
#include <plEditorPluginDiversion/Provider/DiversionProvider.h>

#include <EditorFramework/EditorApp/EditorApp.h>
#include <EditorFramework/EngineShell/EngineShell.h>
#include <EditorUi/Controls.h>
#include <EditorUi/DialogHost.h>
#include <EditorUi/DockManager.h>
#include <EditorUi/Menu.h>
#include <EditorUi/MessageBox.h>
#include <EditorUi/ModalDialog.h>
#include <EditorUi/Runtime.h>
#include <EditorUi/Surface.h>
#include <EditorUi/Widgets.h>
#include <Foundation/Utilities/ConversionUtils.h>
#include <GameUiPlugin/Elements/UiElements.h>
#include <GuiFoundation/UIServices/UIServices.h>
#include <ToolsFoundation/Document/Document.h>

#include <ctime>

namespace
{
  /// The calendar time of seconds since the Unix epoch, in the machine's time zone.
  std::tm ToLocalCalendarTime(std::time_t seconds)
  {
    std::tm local = {};
#if PL_ENABLED(PL_PLATFORM_WINDOWS)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    return local;
  }

  /// The first of several people, and how many more there are.
  plString NameWithOthers(plStringView sName, plUInt32 uiOthers)
  {
    plStringBuilder s = sName.IsEmpty() ? plStringView("Someone") : sName;
    if (uiOthers == 1)
      s.Append(" and 1 other");
    else if (uiOthers > 1)
      s.AppendFormat(" and {} others", uiOthers);
    return s;
  }

  bool ContainsEntry(plStringView sList, plStringView sEntry)
  {
    plHybridArray<plStringView, 8> entries;
    sList.Split(false, entries, ";");
    for (plStringView s : entries)
    {
      if (s == sEntry)
        return true;
    }
    return false;
  }
} // namespace

// Plan and paths.

void plDiversionProvider::UpdateLockSupport(const plDiversionSnapshot& snapshot)
{
  if (snapshot.m_State != plDiversionSnapshot::State::Connected)
    return;

  // a lock that exists proves the plan has them; the user's own plan decides for their own repositories
  plDiversionLockSupport support = plDiversionLockSupport::Unknown;
  if (!snapshot.m_Locks.IsEmpty())
    support = plDiversionLockSupport::Yes;
  else if (!snapshot.m_User.m_sId.IsEmpty() && snapshot.m_User.m_sId == snapshot.m_sRepoOwnerId)
    support = plDiversionParse::LockSupportOfTier(snapshot.m_User.m_sTier);

  if (support != plDiversionLockSupport::Yes && m_pPreferences != nullptr && ContainsEntry(m_pPreferences->m_sReposWithoutLocks, snapshot.m_Workspace.m_sRepoId))
    support = plDiversionLockSupport::No;

  if (support == m_LockSupport)
    return;

  m_LockSupport = support;
  plDocumentNotices::NotifyChanged();

  if (support == plDiversionLockSupport::No)
    plLog::Info("Diversion: the plan of repository '{}' has no hard locks; lock actions are hidden.", snapshot.m_Workspace.m_sRepoName);
  else if (support == plDiversionLockSupport::Yes)
    plLog::Info("Diversion: repository '{}' has hard locks.", snapshot.m_Workspace.m_sRepoName);
}

bool plDiversionProvider::CanForceUnlock() const
{
  if (!IsConnected())
    return false;

  const plString& sMode = m_pSnapshot->m_sAccessMode;
  return sMode.IsEqual_NoCase("OWNER") || sMode.IsEqual_NoCase("ADMIN");
}

bool plDiversionProvider::IsLockable(plStringView sAbsolutePath) const
{
  if (m_pPreferences == nullptr)
    return false;

  const plStringView sName = plPathUtils::GetFileNameAndExtension(sAbsolutePath);

  plHybridArray<plStringView, 8> patterns;
  m_pPreferences->m_sLockableFiles.GetView().Split(false, patterns, ";");

  for (plStringView sPattern : patterns)
  {
    sPattern.Trim(" ");

    // "*.plScene" matches by extension, anything else by the whole name
    if (sPattern.StartsWith("*"))
    {
      sPattern.Shrink(1, 0);
      if (!sPattern.IsEmpty() && sName.EndsWith_NoCase(sPattern))
        return true;
    }
    else if (sName.IsEqual_NoCase(sPattern))
    {
      return true;
    }
  }

  return false;
}

plString plDiversionProvider::ToWorkspacePath(plStringView sAbsolutePath) const
{
  if (!IsConnected())
    return {};

  plStringBuilder sPath = sAbsolutePath;
  sPath.MakeCleanPath();

  const plString& sRoot = m_pSnapshot->m_Workspace.m_sRoot;
  if (!sPath.StartsWith_NoCase(sRoot) || sPath.GetElementCount() <= sRoot.GetElementCount() || sPath.GetData()[sRoot.GetElementCount()] != '/')
    return {};

  return plStringView(sPath.GetData() + sRoot.GetElementCount() + 1, sPath.GetData() + sPath.GetElementCount());
}

plString plDiversionProvider::ToAbsolutePath(plStringView sWorkspacePath) const
{
  if (!IsConnected())
    return {};

  return plStringBuilder(m_pSnapshot->m_Workspace.m_sRoot, "/", sWorkspacePath);
}

plString plDiversionProvider::FormatClock(plTimestamp time)
{
  const std::tm local = ToLocalCalendarTime(static_cast<std::time_t>(time.GetInt64(plSIUnitOfTime::Second)));
  const std::tm today = ToLocalCalendarTime(std::time(nullptr));
  const bool bToday = local.tm_year == today.tm_year && local.tm_yday == today.tm_yday;

  plStringBuilder s;
  if (bToday)
  {
    s.SetFormat("{}:{}", plArgU(local.tm_hour, 2, true), plArgU(local.tm_min, 2, true));
  }
  else
  {
    static const char* s_szMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    s.SetFormat("{} {} {}:{}", local.tm_mday, s_szMonths[local.tm_mon % 12], plArgU(local.tm_hour, 2, true), plArgU(local.tm_min, 2, true));
  }

  return s;
}

plString plDiversionProvider::FormatAgo(plTimestamp time)
{
  const plInt64 iMinutes = (plTimestamp::CurrentTimestamp().GetInt64(plSIUnitOfTime::Second) - time.GetInt64(plSIUnitOfTime::Second)) / 60;

  plStringBuilder s;
  if (iMinutes < 1)
    s = "just now";
  else if (iMinutes < 60)
    s.SetFormat("{} min ago", iMinutes);
  else if (iMinutes < 48 * 60)
    s.SetFormat("{} h ago", iMinutes / 60);
  else
    s.SetFormat("{} days ago", iMinutes / (24 * 60));
  return s;
}

// Lock commands.

void plDiversionProvider::Lock(plStringView sAbsolutePath, Done onDone)
{
  const plString sPath = ToWorkspacePath(sAbsolutePath);
  const plStringView args[] = {"lock", sPath};
  RunLockCommand(args, onDone);
}

void plDiversionProvider::Unlock(plStringView sAbsolutePath, bool bForce, Done onDone)
{
  const plString sPath = ToWorkspacePath(sAbsolutePath);

  plHybridArray<plStringView, 4> args;
  args.PushBack("lock");
  args.PushBack("-d");
  args.PushBack(sPath);
  if (bForce)
    args.PushBack("-f");

  RunLockCommand(args, onDone);
}

void plDiversionProvider::RunLockCommand(plArrayPtr<const plStringView> args, Done onDone)
{
  if (m_pPoller == nullptr)
    return;

  m_pPoller->QueueCommand(args, [onDone](const plDiversionCli::Result& result)
    {
      plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
      const bool bSucceeded = result.m_bStarted && result.Succeeded();

      // the plan has no hard locks: remember it, hide the actions, and say why in plain words
      if (!bSucceeded && plDiversionParse::IsLockRefusedByPlan(result.m_sOutput))
      {
        if (pProvider != nullptr)
          pProvider->LockRefusedByPlan();

        if (onDone.IsValid())
          onDone(false, "Hard locks need Diversion's Studio or Enterprise plan.");
        return;
      }

      // releasing a lock that is already gone (a commit released it) is what was wanted
      const bool bAlreadyFree = !bSucceeded && plDiversionParse::IsNotLocked(result.m_sOutput);

      if (onDone.IsValid())
        onDone(bSucceeded || bAlreadyFree, plDiversionCli::FirstLine(result.m_sOutput)); });
}

void plDiversionProvider::LockRefusedByPlan()
{
  m_LockSupport = plDiversionLockSupport::No;
  plDocumentNotices::NotifyChanged();

  if (m_pPreferences == nullptr || !IsConnected())
    return;

  const plString& sRepo = m_pSnapshot->m_Workspace.m_sRepoId;
  if (ContainsEntry(m_pPreferences->m_sReposWithoutLocks, sRepo))
    return;

  plStringBuilder sList = m_pPreferences->m_sReposWithoutLocks;
  if (!sList.IsEmpty())
    sList.Append(";");
  sList.Append(sRepo);
  m_pPreferences->m_sReposWithoutLocks = sList;
  plPreferences::SaveProjectPreferences();
}

void plDiversionProvider::ReleaseOwnLocks(const plDynamicArray<plString>& paths)
{
  for (const plString& sPath : paths)
  {
    const plString sAbsolute = ToAbsolutePath(sPath);

    plVersionControlFileInfo info;
    GetFileInfo(sAbsolute, info);
    if (!info.m_bLocked || !info.m_Lock.m_bMine)
      continue;

    Unlock(sAbsolute, false, [sName = plString(plPathUtils::GetFileNameAndExtension(sPath))](bool bSucceeded, plStringView sMessage)
      {
        if (bSucceeded)
          plEditorUiServices::ShowAllDocumentsTemporaryStatusBarMessage(plFmt("Released your lock on {}.", sName), plTime::MakeFromSeconds(5));
        else
          plLog::Warning("Releasing the lock on '{}' failed: {}", sName, sMessage); });
  }
}

void plDiversionProvider::AskAndForceUnlock(plStringView sAbsolutePath)
{
  plVersionControlFileInfo info;
  GetFileInfo(sAbsolutePath, info);
  if (!info.m_bLocked || info.m_Lock.m_bMine)
    return;

  const plString sName = plPathUtils::GetFileNameAndExtension(sAbsolutePath);

  plEditorUiMessageBoxDesc desc;
  desc.m_sTitle = plStringBuilder("Force unlock ", sName);
  plStringBuilder sText;
  sText.SetFormat("{} holds this lock since {}. Their uncommitted edits stay in their workspace, and may conflict with yours later. You can do "
                  "this because you are an owner or admin of the repository.",
    info.m_Lock.m_sUser, FormatClock(info.m_Lock.m_Time));
  desc.m_sText = sText;
  desc.m_Kind = plEditorUiMessageKind::Warning;

  plEditorUiDialogButton& force = desc.m_Buttons.ExpandAndGetRef();
  force.m_sId = "force";
  force.m_sLabel = "Force unlock";
  force.m_Role = plEditorUiDialogButton::Role::Accept;

  plEditorUiDialogButton& cancel = desc.m_Buttons.ExpandAndGetRef();
  cancel.m_sId = "cancel";
  cancel.m_sLabel = "Cancel";
  cancel.m_Role = plEditorUiDialogButton::Role::Reject;

  desc.m_sDefaultButton = "cancel";
  desc.m_sEscapeButton = "cancel";

  if (plEditorUiMessageBox::Show(plEditorUiServices::GetEngineDialogOwner(), desc).m_sButton != "force")
    return;

  Unlock(sAbsolutePath, true, [sName](bool bSucceeded, plStringView sMessage)
    {
      if (bSucceeded)
        plEditorUiServices::ShowAllDocumentsTemporaryStatusBarMessage(plFmt("Released the lock on {}.", sName), plTime::MakeFromSeconds(5));
      else
        plEditorUiServices::MessageBoxWarning(plFmt("Force unlock of {} failed: {}", sName, sMessage)); });
}

void plDiversionProvider::AskAndUndo(plStringView sAbsolutePath, bool bAdded)
{
  const plString sName = plPathUtils::GetFileNameAndExtension(sAbsolutePath);

  plEditorUiMessageBoxDesc desc;
  desc.m_sTitle = "Undo changes";
  plStringBuilder sText;
  if (bAdded)
    sText.SetFormat("{} is new: it is not in the last commit. Undoing keeps the file unless you tick the box below (D15).", sName);
  else
    sText.SetFormat("{} goes back to the last commit. An open document of it reloads.", sName);
  desc.m_sText = sText;
  desc.m_Kind = plEditorUiMessageKind::Question;
  if (bAdded)
    desc.m_sCheckLabel = "Also delete the new file";

  plEditorUiDialogButton& undo = desc.m_Buttons.ExpandAndGetRef();
  undo.m_sId = "undo";
  undo.m_sLabel = "Undo changes";
  undo.m_Role = plEditorUiDialogButton::Role::Accept;
  plEditorUiDialogButton& cancel = desc.m_Buttons.ExpandAndGetRef();
  cancel.m_sId = "cancel";
  cancel.m_sLabel = "Cancel";
  cancel.m_Role = plEditorUiDialogButton::Role::Reject;
  desc.m_sDefaultButton = "cancel";
  desc.m_sEscapeButton = "cancel";

  const plEditorUiMessageBoxResult result = plEditorUiMessageBox::Show(plEditorUiServices::GetEngineDialogOwner(), desc);
  if (result.m_sButton != "undo")
    return;

  plDynamicArray<plString> paths;
  paths.PushBack(ToWorkspacePath(sAbsolutePath));
  Undo(paths, bAdded && result.m_bChecked, [sName](bool bSucceeded, plStringView sMessage)
    {
      if (bSucceeded)
        plEditorUiServices::ShowAllDocumentsTemporaryStatusBarMessage(plFmt("Undid the changes to {}.", sName), plTime::MakeFromSeconds(6));
      else
        plEditorUiServices::MessageBoxWarning(plFmt("Undoing the changes to {} failed: {}", sName, sMessage)); });
}

void plDiversionProvider::OpenReadOnly(plStringView sAbsolutePath)
{
  plDocument* pDocument = plEditorApp::GetSingleton()->OpenDocument(sAbsolutePath, plDocumentFlags::RequestWindow);
  if (pDocument == nullptr)
    return;

  // edits made before stay unsaved; read-only only stops new ones
  pDocument->SetReadOnly(true);
}

// Banners.

void plDiversionProvider::DocumentNoticesRequested(const plDocumentNotices::Request& request)
{
  if (!IsConnected() || !request.m_pDocument->IsMainDocument())
    return;

  // an old version opened by Compare says what it is, and why it is read-only; it is no file of the workspace
  if (CompareInfo* pInfo = m_CompareDocuments.GetValue(request.m_pDocument->GetGuid()))
  {
    // yours changed: the marks follow, after the banners are built
    if (UpdateDifferences(*pInfo))
    {
      plEditorUiRuntime::RunAfterFrame([guid = request.m_pDocument->GetGuid()]()
        {
          if (plDiversionProvider* pProvider = plDiversionProvider::GetSingleton())
            pProvider->ApplyHighlight(guid); });
    }

    AddCompareNotice(request.m_pDocument, *request.m_pNotices);
    request.m_bExplainsReadOnly = true;
    return;
  }

  plVersionControlFileInfo info;
  GetFileInfo(request.m_pDocument->GetDocumentPath(), info);

  AddLockNotice(request.m_pDocument, info, *request.m_pNotices);

  // the conflict banner names the commit already
  if (!AddConflictNotice(request.m_pDocument, info, *request.m_pNotices))
    AddEditingNotice(request.m_pDocument, info, *request.m_pNotices);

  AddComparedNotice(request.m_pDocument, *request.m_pNotices);
}

bool plDiversionProvider::AddConflictNotice(plDocument* pDocument, const plVersionControlFileInfo& info, plDynamicArray<plDocumentNotice>& ref_notices) const
{
  const plString sKey = MakeKey(pDocument->GetDocumentPath());

  plDynamicArray<Conflict> conflicts;
  GetConflicts(conflicts);

  const Conflict* pConflict = nullptr;
  for (const Conflict& conflict : conflicts)
  {
    if (conflict.m_bHeld && MakeKey(conflict.m_sAbsPath) == sKey)
      pConflict = &conflict;
  }

  if (pConflict == nullptr)
    return false;

  // the incoming commit is the branch's newest; its author is the one who committed to this file on the branch
  const plDiversionWorkspace& workspace = m_pSnapshot->m_Workspace;
  plString sCommit;
  for (const plDiversionBranch& branch : m_pSnapshot->m_Branches)
  {
    if (branch.m_sName == workspace.m_sBranchName)
      sCommit = branch.m_sHeadCommitId;
  }

  plString sAuthor;
  for (const plVersionControlOtherChange& other : info.m_OtherChanges)
  {
    if (!other.m_bUncommitted && other.m_sBranch == workspace.m_sBranchName)
      sAuthor = other.m_sUser;
  }

  plDocumentNotice& notice = ref_notices.ExpandAndGetRef();
  notice.m_Tone = plDocumentNotice::Tone::Blocked;
  notice.m_sIcon = plEditorUi::IconPath(plEditorUi::Icon::Warning);

  plStringBuilder s;
  if (sAuthor.IsEmpty())
    s = "A teammate's commit";
  else
    s.SetFormat("{}'s commit", sAuthor);
  if (!sCommit.IsEmpty())
    s.Append(" ", sCommit);
  s.Append(" and your synced edits both changed this file.");
  notice.m_sText = s;

  s.SetFormat("Your workspace stays on {} until you choose in Conflicts.", workspace.m_sCommitId);
  notice.m_sDetail = s;

  plDocumentNotice::Action& open = notice.m_Actions.ExpandAndGetRef();
  open.m_sLabel = "Open Conflicts";
  open.m_Callback = []()
  {
    if (plEditorUiEngineShell* pShell = plEditorUiEngineShell::GetSingleton())
    {
      pShell->GetDock().OpenPanel(plDiversionConflictsPanel::PanelId);
      pShell->GetDock().SetCurrentPanel(plDiversionConflictsPanel::PanelId);
    }
  };

  return true;
}

void plDiversionProvider::AddLockNotice(plDocument* pDocument, const plVersionControlFileInfo& info, plDynamicArray<plDocumentNotice>& ref_notices) const
{
  if (!info.m_bLocked || info.m_Lock.m_bMine)
    return;

  const plString sPath = pDocument->GetDocumentPath();
  const plString& sUser = info.m_Lock.m_sUser;

  // the holder's own uncommitted edits belong in this banner, not in a second one
  bool bHolderEditing = false;
  for (const plVersionControlOtherChange& other : info.m_OtherChanges)
    bHolderEditing |= other.m_bUncommitted && other.m_sUser == sUser;

  plDocumentNotice& notice = ref_notices.ExpandAndGetRef();
  notice.m_Tone = plDocumentNotice::Tone::Blocked;
  notice.m_sIcon = plEditorUi::IconPath(plEditorUi::Icon::Locked);

  plStringBuilder s;
  s.SetFormat("Locked by {} since {}.", sUser, FormatClock(info.m_Lock.m_Time));
  notice.m_sText = s;

  s.SetFormat("{}You can edit and save, but you can't commit this file until {} commits or unlocks it.", bHolderEditing ? plStringBuilder(sUser, "'s edits aren't committed yet. ").GetView() : plStringView(), sUser);
  notice.m_sDetail = s;

  if (!pDocument->IsReadOnly() && !pDocument->IsModified())
  {
    plDocumentNotice::Action& readOnly = notice.m_Actions.ExpandAndGetRef();
    readOnly.m_sLabel = "Open read-only";
    readOnly.m_Callback = [pDocument]()
    { pDocument->SetReadOnly(true); };
  }

  if (CanForceUnlock())
  {
    plDocumentNotice::Action& force = notice.m_Actions.ExpandAndGetRef();
    force.m_sLabel = "Force unlock...";
    force.m_Callback = [sPath]()
    {
      if (plDiversionProvider* pProvider = plDiversionProvider::GetSingleton())
        pProvider->AskAndForceUnlock(sPath);
    };
  }
}

void plDiversionProvider::AddEditingNotice(plDocument* pDocument, const plVersionControlFileInfo& info, plDynamicArray<plDocumentNotice>& ref_notices) const
{
  if (info.m_OtherChanges.IsEmpty() || (m_pPreferences != nullptr && !m_pPreferences->m_bWarnOthersEditing))
    return;

  // someone's uncommitted edits weigh more than a commit on another branch; the lock holder's are in the lock banner
  const plVersionControlOtherChange* pFirst = nullptr;
  plUInt32 uiCount = 0;
  for (const plVersionControlOtherChange& other : info.m_OtherChanges)
  {
    if (info.m_bLocked && !info.m_Lock.m_bMine && other.m_bUncommitted && other.m_sUser == info.m_Lock.m_sUser)
      continue;

    ++uiCount;
    if (pFirst == nullptr || (other.m_bUncommitted && !pFirst->m_bUncommitted))
      pFirst = &other;
  }

  if (pFirst == nullptr)
    return;

  const plString sWho = NameWithOthers(pFirst->m_sUser, uiCount - 1);
  const plStringView sName = plPathUtils::GetFileName(pDocument->GetDocumentPath());
  const bool bBoth = pFirst->m_bUncommitted && m_EditedFiles.Contains(MakeKey(pDocument->GetDocumentPath()));

  plDocumentNotice& notice = ref_notices.ExpandAndGetRef();
  notice.m_Tone = plDocumentNotice::Tone::Warning;
  notice.m_sIcon = plEditorUi::IconPath(bBoth ? plEditorUi::Icon::Warning : plEditorUi::Icon::Visible);

  plStringBuilder s;
  if (bBoth)
  {
    s.SetFormat("You and {} are both editing {}.", sWho, sName);
    notice.m_sText = s;
    s.SetFormat("{}'s edits are in their workspace since {}, not committed. Whoever commits second has to resolve a conflict.", pFirst->m_sUser, FormatClock(pFirst->m_Time));
    notice.m_sDetail = s;
  }
  else if (pFirst->m_bUncommitted)
  {
    s.SetFormat("{} {} editing this in their workspace on {}.", sWho, uiCount > 1 ? "are" : "is", pFirst->m_sBranch);
    notice.m_sText = s;
    s.SetFormat("Since {}, not committed. Opening is never blocked; your edits may conflict with theirs.", FormatClock(pFirst->m_Time));
    notice.m_sDetail = s;
  }
  else if (pFirst->m_sBranch == m_pSnapshot->m_Workspace.m_sBranchName)
  {
    // committed where you are: it comes with the next sync
    s.SetFormat("{} committed a change to this {}.", sWho, FormatAgo(pFirst->m_Time));
    notice.m_sText = s;
    notice.m_sDetail = "It is not in your workspace yet; it comes with the next sync. Edits made before then have to be combined with it.";
  }
  else
  {
    s.SetFormat("{} changed this on branch {} {}.", sWho, pFirst->m_sBranch, FormatAgo(pFirst->m_Time));
    notice.m_sText = s;
    s.SetFormat("It isn't merged into {} yet. Editing it here means the merge has to combine both versions.", m_pSnapshot->m_Workspace.m_sBranchName);
    notice.m_sDetail = s;
  }

  // the lock banner offers read-only already
  const bool bLockBanner = info.m_bLocked && !info.m_Lock.m_bMine;
  if (!bLockBanner && !pDocument->IsReadOnly() && !pDocument->IsModified())
  {
    plDocumentNotice::Action& readOnly = notice.m_Actions.ExpandAndGetRef();
    readOnly.m_sLabel = "Open read-only";
    readOnly.m_Callback = [pDocument]()
    { pDocument->SetReadOnly(true); };
  }
}

// Editing and saving.

void plDiversionProvider::DocumentEventHandler(const plDocumentEvent& e)
{
  // yours saved: what differs from an old version open beside it changed
  if (e.m_Type == plDocumentEvent::Type::DocumentSaved)
  {
    RefreshComparisonsOf(e.m_pDocument->GetGuid(), false);
    return;
  }

  if (e.m_Type != plDocumentEvent::Type::ModifiedChanged || !e.m_pDocument->IsModified() || !e.m_pDocument->IsMainDocument())
    return;

  // only the first edit since the project opened asks or warns
  const plString sKey = MakeKey(e.m_pDocument->GetDocumentPath());
  if (m_EditedFiles.Insert(sKey))
    return;

  FirstEdit(e.m_pDocument);
}

void plDiversionProvider::FirstEdit(const plDocument* pDocument)
{
  if (!IsConnected())
    return;

  const plString sPath = pDocument->GetDocumentPath();

  plVersionControlFileInfo info;
  GetFileInfo(sPath, info);

  // someone else is editing it too: the banner changes, and the status row says so once (decision D23)
  for (const plVersionControlOtherChange& other : info.m_OtherChanges)
  {
    if (!other.m_bUncommitted)
      continue;

    plDocumentNotices::NotifyChanged();
    plEditorUiServices::ShowAllDocumentsTemporaryStatusBarMessage(
      plFmt("Warning: You and {} are both editing {}. Whoever commits second resolves the conflict.", other.m_sUser, plPathUtils::GetFileName(sPath)), plTime::MakeFromSeconds(10));
    break;
  }

  // ask to lock (D4): not inside the edit that is being made
  if (CanUseLocks() && m_bAskToLockThisSession && m_pPreferences != nullptr && m_pPreferences->m_bAskToLock && !info.m_bLocked && IsLockable(sPath))
  {
    plEditorUiRuntime::RunAfterFrame([sPath]()
      {
        if (plDiversionProvider* pProvider = plDiversionProvider::GetSingleton())
          pProvider->AskToLock(sPath); });
  }
}

void plDiversionProvider::AskToLock(plStringView sAbsolutePath)
{
  const plString sName = plPathUtils::GetFileNameAndExtension(sAbsolutePath);

  plEditorUiMessageBoxDesc desc;
  desc.m_sTitle = plStringBuilder("Lock ", sName, "?");
  desc.m_sText = "Scenes, prefabs, meshes and textures can't be merged line by line. While you hold the lock, nobody else can commit this file.";
  desc.m_Kind = plEditorUiMessageKind::Question;
  desc.m_sCheckLabel = "Don't ask again this session";

  plEditorUiDialogButton& lock = desc.m_Buttons.ExpandAndGetRef();
  lock.m_sId = "lock";
  lock.m_sLabel = "Lock and edit";
  lock.m_Role = plEditorUiDialogButton::Role::Accept;

  plEditorUiDialogButton& skip = desc.m_Buttons.ExpandAndGetRef();
  skip.m_sId = "skip";
  skip.m_sLabel = "Edit without lock";
  skip.m_Role = plEditorUiDialogButton::Role::Reject;

  desc.m_sDefaultButton = "lock";
  desc.m_sEscapeButton = "skip";

  const plEditorUiMessageBoxResult result = plEditorUiMessageBox::Show(plEditorUiServices::GetEngineDialogOwner(), desc);
  if (result.m_bChecked)
    m_bAskToLockThisSession = false;

  if (result.m_sButton != "lock")
    return;

  Lock(sAbsolutePath, [sName](bool bSucceeded, plStringView sMessage)
    {
      if (bSucceeded)
        plEditorUiServices::ShowAllDocumentsTemporaryStatusBarMessage(plFmt("Locked {}.", sName), plTime::MakeFromSeconds(5));
      else
        plEditorUiServices::MessageBoxWarning(plFmt("Locking {} failed: {}", sName, sMessage)); });
}

void plDiversionProvider::SaveRequested(plDocumentManager::SaveRequest& ref_request)
{
  if (!CanUseLocks() || ref_request.m_Status.Failed())
    return;

  struct Row
  {
    plString m_sPath;
    plString m_sName;
    plString m_sHolder; ///< someone else's lock
    bool m_bOffer = false; ///< lockable and not locked: lock and save, or save without
    bool m_bLock = true;
  };

  plHybridArray<Row, 8> rows;
  bool bAsk = false;
  for (plDocument* pDocument : ref_request.m_Documents)
  {
    plVersionControlFileInfo info;
    GetFileInfo(pDocument->GetDocumentPath(), info);

    Row& row = rows.ExpandAndGetRef();
    row.m_sPath = pDocument->GetDocumentPath();
    row.m_sName = plPathUtils::GetFileNameAndExtension(row.m_sPath);
    if (info.m_bLocked && !info.m_Lock.m_bMine)
      row.m_sHolder = info.m_Lock.m_sUser;
    row.m_bOffer = !info.m_bLocked && m_bAskToLockThisSession && m_pPreferences != nullptr && m_pPreferences->m_bAskToLock && IsLockable(row.m_sPath);

    bAsk |= row.m_bOffer || !row.m_sHolder.IsEmpty();
  }

  if (!bAsk)
    return;

  using namespace plEditorUi;
  constexpr float fPad = 16.0f;
  constexpr float fRow = 30.0f;
  constexpr float fWidth = 460.0f;

  const plVec2 vSize(fWidth, 2.0f * fPad + 70.0f + fRow * rows.GetCount() + plEditorUiDialogHost::FooterHeight);
  plStringBuilder sTitle;
  sTitle.SetFormat("Save {} document{}", rows.GetCount(), rows.GetCount() == 1 ? "" : "s");

  plEditorUiModalDialog dialog(plEditorUiServices::GetEngineDialogOwner(), "DiversionSave", sTitle, vSize);
  if (!dialog.IsReady())
    return;

  plEditorUiDialogButton buttons[2];
  buttons[0].m_sId = "save";
  buttons[0].m_sLabel = "Save";
  buttons[0].m_Role = plEditorUiDialogButton::Role::Accept;
  buttons[1].m_sId = "cancel";
  buttons[1].m_sLabel = "Cancel";
  buttons[1].m_Role = plEditorUiDialogButton::Role::Reject;
  dialog.GetHost().SetButtons(buttons);

  plHybridArray<plRectFloat, 8> dropdowns;
  dropdowns.SetCount(rows.GetCount());

  dialog.m_BuildBody = [&](plUiElement* pBody, const plVec2& vBody)
  {
    float y = fPad;
    for (plUInt32 i = 0; i < rows.GetCount(); ++i)
    {
      const Row& row = rows[i];

      plUiText* name = Text(pBody, row.m_sName);
      name->m_vAnchorMin = name->m_vAnchorMax = plVec2::MakeZero();
      name->m_vOffsets = plVec4(fPad, y, 230, ControlHeight);
      name->m_uiFontSize = FontSize - 1;

      const plRectFloat right(fPad + 240.0f, y, vBody.x - 2.0f * fPad - 240.0f, ControlHeight);
      if (row.m_bOffer)
      {
        dropdowns[i] = right;
        plStringBuilder sAction;
        sAction.SetFormat("Save.{}", i);
        Dropdown(pBody, dialog.GetSurface().GetBlackboard(), right, row.m_bLock ? "Lock and save" : "Save without lock", sAction);
      }
      else
      {
        plStringBuilder sState;
        if (!row.m_sHolder.IsEmpty())
          sState.SetFormat("{}'s lock: saved, can't be committed yet", row.m_sHolder);
        else
          sState = "ready";

        plUiText* state = Text(pBody, sState);
        state->m_vAnchorMin = state->m_vAnchorMax = plVec2::MakeZero();
        state->m_vOffsets = plVec4(right.x, right.y, right.width, right.height);
        state->m_uiFontSize = FontSize - 2;
        state->m_Color = Dim();
      }

      y += fRow;
    }

    WrappedText(pBody, "Every file is saved. Diversion only refuses the commit of a file someone else has locked.", plRectFloat(fPad, y + 8.0f, vBody.x - 2.0f * fPad, 0.0f), Muted());
  };

  dialog.m_UiEvent = [&](const plMsgUiEvent& event)
  {
    plStringView sId = event.m_sIdentifier.GetView();
    if (!sId.StartsWith("Save."))
      return;

    sId.Shrink(5, 0);
    plUInt32 uiRow = 0;
    if (plConversionUtils::StringToUInt(sId, uiRow).Failed() || uiRow >= rows.GetCount())
      return;

    plEditorUiMenu menu;
    menu.m_fMinWidth = dropdowns[uiRow].width;
    menu.AddAction("Lock and save", 1).m_bCheckable = true;
    menu.m_Items.PeekBack().m_bChecked = rows[uiRow].m_bLock;
    menu.AddAction("Save without lock", 2).m_bCheckable = true;
    menu.m_Items.PeekBack().m_bChecked = !rows[uiRow].m_bLock;

    plEditorUiMenuView::Show(dialog.GetSurface(), plVec2(dropdowns[uiRow].x, dropdowns[uiRow].Bottom()), menu, [&, uiRow](plUInt32 uiPicked)
      {
        if (uiPicked == 1 || uiPicked == 2)
        {
          rows[uiRow].m_bLock = uiPicked == 1;
          dialog.Rebuild();
        } });
  };

  if (dialog.Run() != "save")
  {
    ref_request.m_Status = plStatus("Saving was canceled.");
    return;
  }

  for (const Row& row : rows)
  {
    if (!row.m_bOffer || !row.m_bLock)
      continue;

    Lock(row.m_sPath, [sName = row.m_sName](bool bSucceeded, plStringView sMessage)
      {
        if (!bSucceeded)
          plEditorUiServices::MessageBoxWarning(plFmt("Locking {} failed: {}", sName, sMessage)); });
  }
}

// The Asset Browser's context menu.

void plDiversionProvider::ContextMenuRequested(const plAssetBrowserContextMenuEvent& e)
{
  if (!IsConnected() || e.m_SelectedFiles.GetCount() != 1)
    return;

  const plString sPath = e.m_SelectedFiles[0];
  if (ToWorkspacePath(sPath).IsEmpty())
    return;

  const plString sLockIcon = plEditorUi::IconPath(plEditorUi::Icon::Locked);
  const plString sUnlockIcon = plEditorUi::IconPath(plEditorUi::Icon::Unlocked);

  const plDocumentTypeDescriptor* pType = nullptr;
  if (plDocumentManager::FindDocumentTypeFromPath(sPath, false, pType).Succeeded())
  {
    e.m_AddAction("Open Read-Only", plEditorUi::IconPath(plEditorUi::Icon::Visible), [sPath]()
      { plDiversionProvider::OpenReadOnly(sPath); });
  }

  e.m_AddAction("History", plEditorUi::IconPath(plEditorUi::Icon::Clock), [sPath]()
    { plDiversionHistoryPanel::ShowFor(sPath); });

  plVersionControlFileInfo info;
  GetFileInfo(sPath, info);

  if (info.m_State != plVersionControlFileState::Unchanged)
  {
    e.m_AddAction("Undo Changes...", plEditorUi::IconPath(plEditorUi::Icon::Reset), [sPath, bAdded = info.m_State == plVersionControlFileState::Added]()
      {
        // after the menu: a question
        plEditorUiRuntime::RunAfterFrame([sPath, bAdded]()
          {
            if (plDiversionProvider* pProvider = plDiversionProvider::GetSingleton())
              pProvider->AskAndUndo(sPath, bAdded); }); });
  }

  if (!CanUseLocks())
    return;

  const plString sName = plPathUtils::GetFileNameAndExtension(sPath);
  auto Report = [sName](plStringView sDone)
  {
    return [sName, sDone = plString(sDone)](bool bSucceeded, plStringView sMessage)
    {
      if (bSucceeded)
        plEditorUiServices::ShowAllDocumentsTemporaryStatusBarMessage(plFmt("{} {}.", sDone, sName), plTime::MakeFromSeconds(5));
      else
        plEditorUiServices::MessageBoxWarning(plFmt("{}: {}", sName, sMessage));
    };
  };

  if (!info.m_bLocked)
  {
    e.m_AddAction("Lock", sLockIcon, [sPath, Report]()
      {
        if (plDiversionProvider* pProvider = plDiversionProvider::GetSingleton())
          pProvider->Lock(sPath, Report("Locked")); });
  }
  else if (info.m_Lock.m_bMine)
  {
    e.m_AddAction("Unlock", sUnlockIcon, [sPath, Report]()
      {
        if (plDiversionProvider* pProvider = plDiversionProvider::GetSingleton())
          pProvider->Unlock(sPath, false, Report("Unlocked")); });
  }
  else if (CanForceUnlock())
  {
    e.m_AddAction("Force Unlock...", sLockIcon, [sPath]()
      {
        if (plDiversionProvider* pProvider = plDiversionProvider::GetSingleton())
          pProvider->AskAndForceUnlock(sPath); });
  }
}
