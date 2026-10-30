#include <plEditorPluginDiversion/EditorPluginDiversionPCH.h>

#include <plEditorPluginDiversion/Provider/DiversionProvider.h>

#include <EditorFramework/Document/GameObjectDocument.h>
#include <EditorFramework/EditorApp/EditorApp.h>
#include <EditorFramework/EngineShell/DocumentFrame.h>
#include <EditorFramework/EngineShell/EngineShell.h>
#include <EditorUi/DialogHost.h>
#include <EditorUi/DockManager.h>
#include <EditorUi/MessageBox.h>
#include <EditorUi/Runtime.h>
#include <EditorUi/Widgets.h>
#include <Foundation/IO/OSFile.h>
#include <Foundation/Serialization/AbstractObjectGraph.h>
#include <GuiFoundation/UIServices/UIServices.h>
#include <ToolsFoundation/Document/Document.h>
#include <ToolsFoundation/FileSystem/FileSystemModel.h>
#include <ToolsFoundation/Project/ToolsProject.h>

#include <memory>

namespace
{
  /// The file a conflict backup stands for: "Scene.dv-conflict.plScene" and "Scene.dv-conflict-2.plScene" back up
  /// "Scene.plScene".
  plString OriginalOfBackup(plStringView sBackupPath)
  {
    plStringBuilder sPath = sBackupPath;
    const char* pMark = sPath.FindLastSubString(".dv-conflict");
    if (pMark == nullptr)
      return sPath;

    // up to the next dot: ".dv-conflict" or ".dv-conflict-2"
    const char* pEnd = pMark + 1;
    while (*pEnd != '\0' && *pEnd != '.')
      ++pEnd;

    plStringBuilder sOriginal(plStringView(sPath.GetData(), pMark), plStringView(pEnd, sPath.GetData() + sPath.GetElementCount()));
    return sOriginal;
  }

  plEditorUiDialogButton MakeButton(plStringView sId, plStringView sLabel, plEditorUiDialogButton::Role role)
  {
    plEditorUiDialogButton button;
    button.m_sId = sId;
    button.m_sLabel = sLabel;
    button.m_Role = role;
    return button;
  }

} // namespace

// History and restore.

void plDiversionProvider::RequestHistory(plStringView sAbsolutePath, HistoryDone onDone)
{
  const plString sPath = ToWorkspacePath(sAbsolutePath);
  if (m_pPoller == nullptr || sPath.IsEmpty())
  {
    onDone(false, {}, "The file is not in the Diversion workspace.");
    return;
  }

  struct Result
  {
    plDynamicArray<plDiversionCommit> m_Commits;
    plString m_sError;
    bool m_bOk = false;
  };
  auto pResult = std::make_shared<Result>();

  const plDiversionWorkspace workspace = m_pSnapshot->m_Workspace;
  m_pPoller->QueueAgentWork([pResult, workspace, sPath](plDiversionAgent& ref_agent)
    {
      pResult->m_bOk = ref_agent.GetFileHistory(workspace, workspace.m_sBranchId, sPath, pResult->m_Commits) == plDiversionAgent::Result::Ok;
      if (!pResult->m_bOk)
        pResult->m_sError = ref_agent.GetLastError(); },
    [pResult, onDone]()
    { onDone(pResult->m_bOk, pResult->m_Commits, pResult->m_sError); });
}

void plDiversionProvider::Restore(plStringView sAbsolutePath, plStringView sCommitId, Done onDone)
{
  NoteOwnChange(sAbsolutePath, plStringBuilder("restoring ", sCommitId));

  const plString sPath = ToWorkspacePath(sAbsolutePath);
  const plStringView args[] = {"restore", sPath, "--source", sCommitId};
  Run(args, onDone, false);
}

// Compare.

void plDiversionProvider::Compare(plStringView sAbsolutePath, plStringView sRef, plStringView sLabel)
{
  const plString sPath = ToWorkspacePath(sAbsolutePath);
  if (m_pPoller == nullptr || sPath.IsEmpty())
    return;

  auto pContent = std::make_shared<plDynamicArray<plUInt8>>();
  auto pOk = std::make_shared<bool>(false);
  auto pError = std::make_shared<plString>();

  CompareInfo info;
  info.m_sSourcePath = sAbsolutePath;
  info.m_sRef = sRef;
  info.m_sLabel = sLabel;

  const plDiversionWorkspace workspace = m_pSnapshot->m_Workspace;
  m_pPoller->QueueAgentWork([pContent, pOk, pError, workspace, sPath, sRef = plString(sRef)](plDiversionAgent& ref_agent)
    {
      *pOk = ref_agent.GetFileContent(workspace, sRef, sPath, *pContent) == plDiversionAgent::Result::Ok && !pContent->IsEmpty();
      if (!*pOk)
        *pError = ref_agent.GetLastError(); },
    [this, pContent, pOk, pError, info]() mutable
    {
      if (!*pOk)
      {
        plEditorUiServices::MessageBoxWarning(plFmt("Could not get {} as of {}: {}", plPathUtils::GetFileNameAndExtension(info.m_sSourcePath), info.m_sRef, pError->IsEmpty() ? plStringView("no content") : pError->GetView()));
        return;
      }

      OpenCompareDocument(*pContent, std::move(info));
    });

  plEditorUiServices::ShowAllDocumentsTemporaryStatusBarMessage(plFmt("Getting {} as of {}...", plPathUtils::GetFileNameAndExtension(sAbsolutePath), sRef), plTime::MakeFromSeconds(5));
}

plString plDiversionProvider::GetCompareFolder()
{
  plStringBuilder sFolder = plToolsProject::GetSingleton()->GetProjectDirectory();
  sFolder.AppendPath("AssetCache", "VersionControl", "Compare");
  return sFolder;
}

void plDiversionProvider::OpenCompareDocument(const plDynamicArray<plUInt8>& content, CompareInfo&& info)
{
  // in the project's AssetCache: asset documents open from a data directory only, the curator takes nothing below an
  // AssetCache folder for an asset (so no second asset with the same guid), and version control ignores it
  const plString sFolder = GetCompareFolder();
  const plStringView sName = plPathUtils::GetFileName(info.m_sSourcePath);
  const plStringView sExtension = plPathUtils::GetFileExtension(info.m_sSourcePath);

  plStringBuilder sRef = info.m_sName.IsEmpty() ? info.m_sRef : info.m_sName;
  sRef.ReplaceAll("/", "-");

  // the bytes as they came, kept for comparing again when yours changes
  plStringBuilder sRaw = sFolder;
  sRaw.AppendFormat("/raw/{} ({}).{}", sName, sRef, sExtension);
  plStringBuilder sCopy = sFolder;
  sCopy.AppendFormat("/{} ({}).{}", sName, sRef, sExtension);

  plOSFile file;
  if (plOSFile::CreateDirectoryStructure(plPathUtils::GetFileDirectory(sRaw)).Failed() || file.Open(sRaw, plFileOpenMode::Write).Failed() || file.Write(content.GetData(), content.GetCount()).Failed())
  {
    plEditorUiServices::MessageBoxWarning(plFmt("Could not write '{}'.", sRaw));
    return;
  }
  file.Close();

  // the copy gets guids of its own, so it opens beside the document it is an old version of
  const plDocumentTypeDescriptor* pType = nullptr;
  if (plDocumentManager::FindDocumentTypeFromPath(info.m_sSourcePath, false, pType).Failed())
  {
    plEditorUiServices::MessageBoxWarning(plFmt("The editor cannot open {} files.", sExtension));
    return;
  }

  // an earlier comparison with the same version goes first
  if (plDocument* pOldCopy = pType->m_pManager->GetDocumentByPath(sCopy))
    pOldCopy->GetDocumentManager()->CloseDocument(pOldCopy);

  plUuid cloneGuid = plUuid::MakeUuid();
  if (const plStatus status = pType->m_pManager->CloneDocument(sRaw, sCopy, cloneGuid); status.Failed())
  {
    plEditorUiServices::MessageBoxWarning(plFmt("Could not prepare the old version: {}", status.m_sMessage));
    return;
  }

  info.m_sRawPath = sRaw;
  UpdateDifferences(info);

  // the copy's object guids are the old version's combined with this
  info.m_Seed = cloneGuid;
  info.m_Seed.RevertCombinationWithSeed(info.m_OldDocumentId);

  plDocument* pSource = plEditorApp::GetSingleton()->OpenDocument(info.m_sSourcePath, plDocumentFlags::RequestWindow);
  m_bOpeningCompareDocument = true;
  plDocument* pCompare = plEditorApp::GetSingleton()->OpenDocument(sCopy, plDocumentFlags::RequestWindow);
  m_bOpeningCompareDocument = false;
  if (pCompare == nullptr)
    return;

  pCompare->SetReadOnly(true);
  info.m_SourceDocument = pSource != nullptr ? pSource->GetGuid() : plUuid();
  info.m_bTree = pCompare->IsInstanceOf<plGameObjectDocument>();
  m_CompareDocuments[pCompare->GetGuid()] = std::move(info);
  ApplyHighlight(pCompare->GetGuid());
  plDocumentNotices::NotifyChanged();

  // side by side once both frames exist
  const plUuid sourceGuid = pSource != nullptr ? pSource->GetGuid() : plUuid();
  const plUuid compareGuid = pCompare->GetGuid();
  plEditorUiRuntime::RunAfterFrame([sourceGuid, compareGuid]()
    {
      plEditorUiEngineShell* pShell = plEditorUiEngineShell::GetSingleton();
      plDocument* pSourceDoc = plDocumentManager::GetDocumentByGuid(sourceGuid);
      plDocument* pCompareDoc = plDocumentManager::GetDocumentByGuid(compareGuid);
      if (pShell == nullptr || pSourceDoc == nullptr || pCompareDoc == nullptr)
        return;

      pShell->GetDock().DockPanel(plEditorUiDocumentFrame::GetPanelIdOf(pCompareDoc), plEditorUiDocumentFrame::GetPanelIdOf(pSourceDoc), plEditorUiDockSide::Right, 0.5f); });
}

void plDiversionProvider::AddCompareNotice(plDocument* pDocument, plDynamicArray<plDocumentNotice>& ref_notices) const
{
  const CompareInfo* pInfo = m_CompareDocuments.GetValue(pDocument->GetGuid());
  if (pInfo == nullptr)
    return;

  plDocumentNotice& notice = ref_notices.ExpandAndGetRef();
  notice.m_Tone = plDocumentNotice::Tone::Info;
  notice.m_sIcon = plEditorUi::IconPath(plEditorUi::Icon::Clock);

  plStringBuilder s;
  s.SetFormat("{} of {}, read-only.", pInfo->m_sLabel, plPathUtils::GetFileNameAndExtension(pInfo->m_sSourcePath));
  notice.m_sText = s;
  notice.m_sDetail = DescribeDifferences(*pInfo);

  const plUuid guid = pDocument->GetGuid();
  if (pInfo->m_bTree)
  {
    // marked in both documents' trees and views (D33 to D36)
    if (!pInfo->m_Marks.IsEmpty())
      AddDifferenceActions(guid, *pInfo, notice);
  }
  else if (!pInfo->m_Differences.IsEmpty())
  {
    plDocumentNotice::Action& list = notice.m_Actions.ExpandAndGetRef();
    list.m_sLabel = "Show differences";
    list.m_Callback = [guid]()
    {
      plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
      if (const CompareInfo* pFound = pProvider != nullptr ? pProvider->m_CompareDocuments.GetValue(guid) : nullptr)
        pProvider->ShowDifferences(*pFound);
    };
  }

  if (pInfo->m_bConflict)
  {
    // the conflict's choice, from where both versions are in view
    const plString sSource = pInfo->m_sSourcePath;
    for (bool bMine : {true, false})
    {
      plDocumentNotice::Action& choice = notice.m_Actions.ExpandAndGetRef();
      choice.m_sLabel = bMine ? "Keep mine" : "Keep theirs";
      choice.m_Callback = [sSource, bMine]()
      {
        plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
        if (pProvider == nullptr)
          return;

        plDynamicArray<Conflict> conflicts;
        pProvider->GetConflicts(conflicts);
        for (const Conflict& conflict : conflicts)
        {
          if (conflict.m_sAbsPath == sSource)
          {
            pProvider->AskAndResolve(conflict, bMine);
            break;
          }
        }
      };
    }
    return;
  }

  plDocumentNotice::Action& restore = notice.m_Actions.ExpandAndGetRef();
  restore.m_sLabel = "Restore this version";
  restore.m_Callback = [sSource = pInfo->m_sSourcePath, sRef = pInfo->m_sRef]()
  {
    plEditorUiMessageBoxDesc desc;
    desc.m_sTitle = plStringBuilder("Restore ", plPathUtils::GetFileNameAndExtension(sSource));
    plStringBuilder sText;
    sText.SetFormat("The file goes back to how it was in {}. That is a change like any other: Changes lists it, and you commit or undo it.", sRef);
    desc.m_sText = sText;
    desc.m_Kind = plEditorUiMessageKind::Question;
    desc.m_Buttons.PushBack(MakeButton("restore", "Restore", plEditorUiDialogButton::Role::Accept));
    desc.m_Buttons.PushBack(MakeButton("cancel", "Cancel", plEditorUiDialogButton::Role::Reject));
    desc.m_sDefaultButton = "restore";
    desc.m_sEscapeButton = "cancel";
    if (plEditorUiMessageBox::Show(plEditorUiServices::GetEngineDialogOwner(), desc).m_sButton != "restore")
      return;

    if (plDiversionProvider* pProvider = plDiversionProvider::GetSingleton())
    {
      pProvider->Restore(sSource, sRef, [sSource, sRef](bool bSucceeded, plStringView sMessage)
        {
          if (bSucceeded)
            plEditorUiServices::ShowAllDocumentsTemporaryStatusBarMessage(plFmt("Restored {} from {}; commit or undo it in Changes.", plPathUtils::GetFileNameAndExtension(sSource), sRef), plTime::MakeFromSeconds(8));
          else
            plEditorUiServices::MessageBoxWarning(plFmt("Restoring failed: {}", sMessage)); });
    }
  };
}

void plDiversionProvider::ShowDifferences(const CompareInfo& info) const
{
  plStringBuilder sText;
  sText.SetFormat("{} compared with yours: {} added, {} removed, {} changed.\n", info.m_sLabel, info.m_uiAdded, info.m_uiRemoved, info.m_uiChanged);

  // a message box stays readable up to a few dozen lines; the count says how many more there are
  constexpr plUInt32 uiShown = 40;
  for (plUInt32 i = 0; i < info.m_Differences.GetCount() && i < uiShown; ++i)
    sText.Append("\n", info.m_Differences[i]);

  const plUInt32 uiTotal = info.m_uiAdded + info.m_uiRemoved + info.m_uiChanged;
  if (uiTotal > uiShown)
    sText.AppendFormat("\n... and {} more", uiTotal - uiShown);

  plEditorUiMessageBoxDesc desc;
  desc.m_sTitle = plStringBuilder("Differences in ", plPathUtils::GetFileNameAndExtension(info.m_sSourcePath));
  desc.m_sText = sText;
  desc.m_Kind = plEditorUiMessageKind::Information;
  desc.m_fWidth = 560.0f;
  plEditorUiMessageBox::SetStandardButtons(plEditorUiMessageButton::Ok, plEditorUiMessageButton::Ok, desc);
  plEditorUiMessageBox::Show(plEditorUiServices::GetEngineDialogOwner(), desc);
}

// Conflicts.

void plDiversionProvider::GetConflicts(plDynamicArray<Conflict>& out_conflicts) const
{
  out_conflicts = m_Conflicts;
}

void plDiversionProvider::FindConflicts(plDynamicArray<Conflict>& out_conflicts) const
{
  out_conflicts.Clear();
  if (!IsConnected())
    return;

  // backups of unsynced edits that met an incoming commit
  for (const plDiversionChange& change : m_pSnapshot->m_Status.m_Changes)
  {
    const plString sAbsolute = ToAbsolutePath(change.m_sPath);
    if (change.m_Kind == plDiversionChangeKind::Deleted || !IsProviderFile(sAbsolute))
      continue;

    Conflict& conflict = out_conflicts.ExpandAndGetRef();
    conflict.m_sBackupAbsPath = sAbsolute;
    conflict.m_sAbsPath = OriginalOfBackup(sAbsolute);
  }

  // synced edits that hold the workspace back
  for (const plString& sPath : m_pSnapshot->m_Status.m_Conflicts)
  {
    Conflict& conflict = out_conflicts.ExpandAndGetRef();
    conflict.m_sAbsPath = ToAbsolutePath(sPath);
    conflict.m_bHeld = true;
  }
}

void plDiversionProvider::AskAndResolve(const Conflict& conflict, bool bKeepMine)
{
  const plString sName = plPathUtils::GetFileNameAndExtension(conflict.m_sAbsPath);

  plStringBuilder sText;
  if (bKeepMine)
    sText.SetFormat("The incoming changes to {} are replaced by yours. Yours becomes a change to commit.", sName);
  else
    sText.SetFormat("Your changes to {} are replaced by the incoming ones. This cannot be undone from the editor.", sName);

  // Diversion updates a held-back workspace as a whole
  if (conflict.m_bHeld && m_pSnapshot != nullptr && m_pSnapshot->m_Status.m_Conflicts.GetCount() > 1)
  {
    sText.Append("\n\nDiversion resolves every held file the same way:");
    for (const plString& sPath : m_pSnapshot->m_Status.m_Conflicts)
      sText.Append("\n    ", plPathUtils::GetFileNameAndExtension(sPath));
  }

  plEditorUiMessageBoxDesc desc;
  desc.m_sTitle = bKeepMine ? "Keep mine" : "Keep theirs";
  desc.m_sText = sText;
  desc.m_Kind = bKeepMine ? plEditorUiMessageKind::Question : plEditorUiMessageKind::Warning;
  desc.m_Buttons.PushBack(MakeButton("go", bKeepMine ? "Keep mine" : "Keep theirs", plEditorUiDialogButton::Role::Accept));
  desc.m_Buttons.PushBack(MakeButton("cancel", "Cancel", plEditorUiDialogButton::Role::Reject));
  desc.m_sDefaultButton = "cancel";
  desc.m_sEscapeButton = "cancel";

  if (plEditorUiMessageBox::Show(plEditorUiServices::GetEngineDialogOwner(), desc).m_sButton == "go")
    Resolve(conflict, bKeepMine);
}

void plDiversionProvider::Resolve(const Conflict& conflict, bool bKeepMine)
{
  const plString sName = plPathUtils::GetFileNameAndExtension(conflict.m_sAbsPath);
  NoteOwnChange(conflict.m_sAbsPath, bKeepMine ? "keeping yours" : "taking theirs");

  if (conflict.m_bHeld)
  {
    const plStringView args[] = {"update", "--conflict_resolution", bKeepMine ? plStringView("keep-current") : plStringView("accept-incoming")};
    Run(args, [sName, bKeepMine](bool bSucceeded, plStringView sMessage)
      {
        if (bSucceeded)
          plEditorUiServices::ShowAllDocumentsTemporaryStatusBarMessage(plFmt("{} for {}; the workspace is up to date.", bKeepMine ? "Kept yours" : "Took the incoming version", sName), plTime::MakeFromSeconds(8));
        else
          plEditorUiServices::MessageBoxWarning(plFmt("Resolving the conflict failed: {}", sMessage)); },
      false);
    return;
  }

  // a backup: yours replaces theirs, or goes
  if (bKeepMine && plOSFile::CopyFile(conflict.m_sBackupAbsPath, conflict.m_sAbsPath).Failed())
  {
    plEditorUiServices::MessageBoxWarning(plFmt("Could not copy your version back over {}.", sName));
    return;
  }

  if (plOSFile::DeleteFile(conflict.m_sBackupAbsPath).Failed())
    plLog::Warning("Could not delete the conflict backup '{}'.", conflict.m_sBackupAbsPath);

  if (plFileSystemModel* pModel = plFileSystemModel::GetSingleton())
  {
    pModel->NotifyOfChange(conflict.m_sAbsPath);
    pModel->NotifyOfChange(conflict.m_sBackupAbsPath);
  }

  Refresh();
  plEditorUiServices::ShowAllDocumentsTemporaryStatusBarMessage(plFmt(bKeepMine ? "Kept yours of {}; the backup is gone." : "Kept the incoming {}; your backup is deleted.", sName), plTime::MakeFromSeconds(8));
}

void plDiversionProvider::CompareConflict(const Conflict& conflict)
{
  if (!IsConnected())
    return;

  if (conflict.m_bHeld)
  {
    // the file on disk is yours; theirs is the branch's newest version
    const plDiversionWorkspace& workspace = m_pSnapshot->m_Workspace;
    plStringBuilder sLabel("Theirs, the incoming version on ", workspace.m_sBranchName);

    const plString sPath = ToWorkspacePath(conflict.m_sAbsPath);
    auto pContent = std::make_shared<plDynamicArray<plUInt8>>();
    auto pOk = std::make_shared<bool>(false);

    CompareInfo info;
    info.m_sSourcePath = conflict.m_sAbsPath;
    info.m_sRef = workspace.m_sBranchId;
    info.m_sName = "theirs";
    info.m_sLabel = sLabel;
    info.m_bConflict = true;

    m_pPoller->QueueAgentWork([pContent, pOk, workspace, sPath](plDiversionAgent& ref_agent)
      { *pOk = ref_agent.GetFileContent(workspace, workspace.m_sBranchId, sPath, *pContent) == plDiversionAgent::Result::Ok && !pContent->IsEmpty(); },
      [this, pContent, pOk, info]() mutable
      {
        if (*pOk)
          OpenCompareDocument(*pContent, std::move(info));
        else
          plEditorUiServices::MessageBoxWarning(plFmt("Could not get the incoming version of {}.", plPathUtils::GetFileNameAndExtension(info.m_sSourcePath)));
      });
    return;
  }

  // the file on disk is theirs; yours is in the backup
  plOSFile file;
  plDynamicArray<plUInt8> content;
  if (file.Open(conflict.m_sBackupAbsPath, plFileOpenMode::Read).Failed())
    return;
  file.ReadAll(content);
  file.Close();

  CompareInfo info;
  info.m_sSourcePath = conflict.m_sAbsPath;
  info.m_sRef = "backup";
  info.m_sName = "yours";
  info.m_sLabel = "Yours, the backup Diversion kept";
  info.m_bConflict = true;
  OpenCompareDocument(content, std::move(info));
}
