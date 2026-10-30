#include <plEditorPluginDiversion/EditorPluginDiversionPCH.h>

#include <plEditorPluginDiversion/Client/DiversionParse.h>
#include <plEditorPluginDiversion/Provider/DiversionProvider.h>

#include <EditorFramework/EditorApp/EditorApp.h>
#include <EditorUi/DialogHost.h>
#include <EditorUi/Menu.h>
#include <EditorUi/MessageBox.h>
#include <EditorUi/ModalDialog.h>
#include <EditorUi/PopupMenu.h>
#include <EditorUi/Runtime.h>
#include <EditorUi/Widgets.h>
#include <Foundation/IO/OSFile.h>
#include <GuiFoundation/UIServices/UIServices.h>
#include <ToolsFoundation/Document/Document.h>

namespace
{
  enum MenuId : plUInt32
  {
    MenuNewBranch = 1,
    MenuPause,
    MenuRefresh,
    MenuOpenApp,
    MenuFirstBranch = 100,
  };

  plEditorUiDialogButton MakeButton(plStringView sId, plStringView sLabel, plEditorUiDialogButton::Role role)
  {
    plEditorUiDialogButton button;
    button.m_sId = sId;
    button.m_sLabel = sLabel;
    button.m_Role = role;
    return button;
  }

  /// The main documents with unsaved edits.
  void GetModifiedDocuments(plDynamicArray<plDocument*>& out_documents)
  {
    out_documents.Clear();
    for (plDocumentManager* pManager : plDocumentManager::GetAllDocumentManagers())
    {
      for (plDocument* pDocument : pManager->GetAllOpenDocuments())
      {
        if (pDocument->IsMainDocument() && pDocument->IsModified())
          out_documents.PushBack(pDocument);
      }
    }
  }
} // namespace

// The chips' menus.

void plDiversionProvider::ShowMenu(Menu menu, plEditorUiWindowHost& ref_host, const plVec2I32& vScreenPosition)
{
  if (menu == Menu::Branch)
    ShowBranchMenu(ref_host, vScreenPosition);
  else
    ShowSyncMenu(ref_host, vScreenPosition);
}

void plDiversionProvider::ShowBranchMenu(plEditorUiWindowHost& ref_host, const plVec2I32& vScreenPosition)
{
  if (!IsConnected())
    return;

  const plDiversionSnapshot& snapshot = *m_pSnapshot;
  plDynamicArray<plString> branches;
  for (const plDiversionBranch& branch : snapshot.m_Branches)
    branches.PushBack(branch.m_sName);

  plEditorUiMenu menu;
  menu.m_fMinWidth = 220.0f;
  if (branches.IsEmpty())
  {
    menu.AddAction(snapshot.m_Workspace.m_sBranchName, MenuFirstBranch).m_bEnabled = false;
  }

  for (plUInt32 i = 0; i < branches.GetCount(); ++i)
  {
    plEditorUiMenuItem& item = menu.AddAction(branches[i], MenuFirstBranch + i);
    item.m_bCheckable = true;
    item.m_bChecked = branches[i] == snapshot.m_Workspace.m_sBranchName;
  }

  menu.AddSeparator();
  menu.AddAction("New Branch...", MenuNewBranch).m_sIcon = plEditorUi::IconPath(plEditorUi::Icon::Add);
  menu.AddAction("Open in Diversion", MenuOpenApp).m_sIcon = plEditorUi::IconPath(plEditorUi::Icon::External);

  plEditorUiPopupMenu::Show(ref_host, vScreenPosition, menu, [this, branches](plUInt32 uiPicked)
    {
      // dialogs open after the menu is gone
      plEditorUiRuntime::RunAfterFrame([this, branches, uiPicked]()
        {
          if (uiPicked == MenuNewBranch)
            CreateBranch();
          else if (uiPicked == MenuOpenApp)
            OpenInDiversion();
          else if (uiPicked >= MenuFirstBranch && uiPicked - MenuFirstBranch < branches.GetCount())
            SwitchBranch(branches[uiPicked - MenuFirstBranch]); }); });
}

void plDiversionProvider::ShowSyncMenu(plEditorUiWindowHost& ref_host, const plVec2I32& vScreenPosition)
{
  plVersionControlStatus status;
  GetStatus(status);

  plEditorUiMenu menu;
  menu.m_fMinWidth = 300.0f;

  plStringBuilder sLine;
  if (status.m_Connection != plVersionControlStatus::Connection::Connected)
    sLine = "Not connected to Diversion";
  else if (status.m_Sync == plVersionControlStatus::Sync::Paused)
    sLine.SetFormat("Sync paused on {}", status.m_sBranch);
  else if (status.m_Sync == plVersionControlStatus::Sync::Syncing && status.m_uiIncomingFiles > 0)
    sLine.SetFormat("Syncing: {} files to download", status.m_uiIncomingFiles);
  else if (status.m_Sync == plVersionControlStatus::Sync::Syncing)
    sLine = "Syncing";
  else
    sLine.SetFormat("Synced with {} at {}", status.m_sBranch, IsConnected() ? m_pSnapshot->m_Workspace.m_sCommitId.GetView() : plStringView());
  menu.AddAction(sLine, 0).m_bEnabled = false;

  if (IsConnected())
  {
    if (IsVersionTested())
      sLine.SetFormat("Diversion agent {}, a tested version", m_pSnapshot->m_sAgentVersion);
    else
      sLine.SetFormat("Diversion agent {}: not a tested version ({})", m_pSnapshot->m_sAgentVersion, TestedVersions);

    plEditorUiMenuItem& version = menu.AddAction(sLine, 0);
    version.m_bEnabled = false;
    if (!IsVersionTested())
    {
      version.m_sIcon = plEditorUi::IconPath(plEditorUi::Icon::Warning);
      version.m_sTooltip = "Everything keeps working; if something looks wrong, the package may need an update.";
    }
  }

  menu.AddSeparator();
  menu.AddAction(IsSyncPaused() ? "Resume Sync" : "Pause Sync", MenuPause).m_bEnabled = IsConnected();
  menu.AddAction("Refresh Status", MenuRefresh).m_sIcon = plEditorUi::IconPath(plEditorUi::Icon::Refresh);
  menu.AddAction("Open in Diversion", MenuOpenApp).m_sIcon = plEditorUi::IconPath(plEditorUi::Icon::External);

  plEditorUiPopupMenu::Show(ref_host, vScreenPosition, menu, [this](plUInt32 uiPicked)
    {
      if (uiPicked == MenuPause)
        SetSyncPaused(!IsSyncPaused());
      else if (uiPicked == MenuRefresh)
        Refresh();
      else if (uiPicked == MenuOpenApp)
        OpenInDiversion(); });
}

// Switching and creating branches.

void plDiversionProvider::SwitchBranch(plStringView sBranch)
{
  if (!IsConnected() || sBranch == m_pSnapshot->m_Workspace.m_sBranchName)
    return;

  // unsaved documents first: the switch rewrites the project's files (D26)
  plDynamicArray<plDocument*> modified;
  GetModifiedDocuments(modified);

  plDynamicArray<plString> reopen;
  if (!modified.IsEmpty())
  {
    plStringBuilder sNames;
    for (const plDocument* pDocument : modified)
      sNames.Append("\n    ", plPathUtils::GetFileNameAndExtension(pDocument->GetDocumentPath()));

    plEditorUiMessageBoxDesc desc;
    desc.m_sTitle = plStringBuilder("Switch to ", sBranch);
    plStringBuilder sText;
    sText.SetFormat("{} unsaved edits. The switch rewrites the project's files, so they are saved or dropped first.\n{}", modified.GetCount() == 1 ? "This document has" : "These documents have", sNames);
    desc.m_sText = sText;
    desc.m_Kind = plEditorUiMessageKind::Question;
    desc.m_Buttons.PushBack(MakeButton("save", "Save and switch", plEditorUiDialogButton::Role::Accept));
    desc.m_Buttons.PushBack(MakeButton("discard", "Discard and switch", plEditorUiDialogButton::Role::Action));
    desc.m_Buttons.PushBack(MakeButton("cancel", "Cancel", plEditorUiDialogButton::Role::Reject));
    desc.m_sDefaultButton = "save";
    desc.m_sEscapeButton = "cancel";

    const plString sAnswer = plEditorUiMessageBox::Show(plEditorUiServices::GetEngineDialogOwner(), desc).m_sButton;
    if (sAnswer == "save")
    {
      for (plDocument* pDocument : modified)
      {
        if (pDocument->SaveDocument().Failed())
        {
          plEditorUiServices::MessageBoxWarning(plFmt("Not switched: saving {} failed.", plPathUtils::GetFileNameAndExtension(pDocument->GetDocumentPath())));
          return;
        }
      }
    }
    else if (sAnswer == "discard")
    {
      // closed without saving, opened again on the new branch where their files still are
      for (plDocument* pDocument : modified)
      {
        reopen.PushBack(pDocument->GetDocumentPath());
        pDocument->GetDocumentManager()->CloseDocument(pDocument);
      }
    }
    else
    {
      return;
    }
  }

  // pending changes: shelve them or take them along (D12)
  bool bShelve = false;
  if (!m_pSnapshot->m_Status.m_Changes.IsEmpty())
  {
    plEditorUiMessageBoxDesc desc;
    desc.m_sTitle = plStringBuilder("Switch to ", sBranch);
    plStringBuilder sText;
    sText.SetFormat("{} changes are not committed. Shelve them on {} (Changes lists the shelf; apply it when you come back), or take them along to {}.",
      m_pSnapshot->m_Status.m_Changes.GetCount(), m_pSnapshot->m_Workspace.m_sBranchName, sBranch);
    desc.m_sText = sText;
    desc.m_Kind = plEditorUiMessageKind::Question;
    desc.m_Buttons.PushBack(MakeButton("shelve", "Shelve them and switch", plEditorUiDialogButton::Role::Accept));
    desc.m_Buttons.PushBack(MakeButton("take", "Take them along", plEditorUiDialogButton::Role::Action));
    desc.m_Buttons.PushBack(MakeButton("cancel", "Cancel", plEditorUiDialogButton::Role::Reject));
    desc.m_sDefaultButton = "shelve";
    desc.m_sEscapeButton = "cancel";

    const plString sAnswer = plEditorUiMessageBox::Show(plEditorUiServices::GetEngineDialogOwner(), desc).m_sButton;
    if (sAnswer != "shelve" && sAnswer != "take")
    {
      // what was closed unsaved opens again as it is on disk
      for (const plString& sPath : reopen)
        plEditorApp::GetSingleton()->OpenDocument(sPath, plDocumentFlags::RequestWindow);
      return;
    }

    bShelve = sAnswer == "shelve";
  }

  RunCheckout(sBranch, bShelve, reopen);
}

void plDiversionProvider::RunCheckout(plStringView sBranch, bool bShelve, const plDynamicArray<plString>& reopen)
{
  if (m_pPoller == nullptr)
    return;

  // a shelf of the target branch is never applied by itself: the Changes panel lists it with Apply
  plHybridArray<plStringView, 4> args;
  args.PushBack("checkout");
  args.PushBack(sBranch);
  args.PushBack(bShelve ? plStringView("--shelve-changes") : plStringView("--take-changes"));
  args.PushBack("--ignore-shelf");

  plEditorUiServices::ShowAllDocumentsTemporaryStatusBarMessage(plFmt("Switching to {}...", sBranch), plTime::MakeFromSeconds(10));
  NoteOwnChange({}, plStringBuilder("switching to ", sBranch));

  m_pPoller->QueueCommand(args, [sBranch = plString(sBranch), reopen](const plDiversionCli::Result& result)
    {
      if (!result.m_bStarted || !result.Succeeded())
      {
        plEditorUiServices::MessageBoxWarning(plFmt("Switching to {} failed: {}", sBranch, plDiversionCli::FirstLine(result.m_sOutput)));
        return;
      }

      // the reload watcher reloads open documents whose files changed and closes those whose files are gone
      plEditorUiServices::ShowAllDocumentsTemporaryStatusBarMessage(plFmt("Switched to {}.", sBranch), plTime::MakeFromSeconds(6));

      for (const plString& sPath : reopen)
      {
        if (plOSFile::ExistsFile(sPath))
          plEditorApp::GetSingleton()->OpenDocument(sPath, plDocumentFlags::RequestWindow);
      } });
}

void plDiversionProvider::Checkout(plStringView sBranch, bool bShelve, Done onDone)
{
  NoteOwnChange({}, plStringBuilder("switching to ", sBranch));

  const plStringView args[] = {"checkout", sBranch, bShelve ? plStringView("--shelve-changes") : plStringView("--take-changes"), "--ignore-shelf"};
  Run(args, onDone, false);
}

void plDiversionProvider::CreateBranch()
{
  if (!IsConnected() || m_pPoller == nullptr)
    return;

  plString sName;
  plStringBuilder sLabel;
  sLabel.SetFormat("The new branch starts at {} and becomes the workspace's branch; your pending changes come along.", m_pSnapshot->m_Workspace.m_sCommitId);
  if (!plEditorUiModalDialog::AskText(plEditorUiServices::GetEngineDialogOwner(), "New branch", sLabel, sName))
    return;

  const plString sSafeName = plDiversionCli::MakeArgument(sName);
  const plStringView args[] = {"branch", "-c", sSafeName};
  m_pPoller->QueueCommand(args, [sSafeName](const plDiversionCli::Result& result)
    {
      if (!result.m_bStarted || !result.Succeeded())
        plEditorUiServices::MessageBoxWarning(plFmt("Creating the branch {} failed: {}", sSafeName, plDiversionCli::FirstLine(result.m_sOutput)));
      else
        plEditorUiServices::ShowAllDocumentsTemporaryStatusBarMessage(plFmt("Created {} and switched to it.", sSafeName), plTime::MakeFromSeconds(6)); });
}

// Versions and incoming changes.

bool plDiversionProvider::IsVersionTested() const
{
  if (!IsConnected() || m_pSnapshot->m_sAgentVersion.IsEmpty())
    return true;

  plUInt32 uiMajor = 0, uiMinor = 0, uiPatch = 0;
  if (plDiversionParse::Version(m_pSnapshot->m_sAgentVersion, uiMajor, uiMinor, uiPatch).Failed())
    return false;

  // 1.6.567 to 1.6.x
  return uiMajor == 1 && uiMinor == 6 && uiPatch >= 567;
}

void plDiversionProvider::NoteOwnChange(plStringView sAbsolutePath, plStringView sDescription)
{
  // the command runs, then the file watcher and the reload take a few seconds more
  OwnChange note;
  note.m_sDescription = sDescription;
  note.m_Until = plTime::Now() + plTime::MakeFromMinutes(2);

  if (sAbsolutePath.IsEmpty())
    m_OwnChangeOfAll = std::move(note);
  else
    m_OwnChanges[MakeKey(sAbsolutePath)] = std::move(note);
}

plString plDiversionProvider::DescribeIncomingChange(plStringView sAbsolutePath) const
{
  // a command of the editor's own: a restore, undo, resolve or branch switch
  const plTime now = plTime::Now();
  if (const OwnChange* pNote = m_OwnChanges.GetValue(MakeKey(sAbsolutePath)); pNote != nullptr && now < pNote->m_Until)
    return pNote->m_sDescription;
  if (now < m_OwnChangeOfAll.m_Until)
    return m_OwnChangeOfAll.m_sDescription;

  // the latest commit the workspace received; a file that changed with a sync came with it
  if (!IsConnected() || m_pSnapshot->m_SyncedCommit.m_sId.IsEmpty())
    return {};

  const plDiversionCommit& commit = m_pSnapshot->m_SyncedCommit;
  plStringBuilder s;
  if (commit.m_sAuthor.IsEmpty())
    s.SetFormat("Commit {}", commit.m_sId);
  else
    s.SetFormat("{}'s commit {}", commit.m_sAuthor, commit.m_sId);
  return s;
}

void plDiversionProvider::GetPendingFiles(plDynamicArray<plString>& out_files) const
{
  out_files.Clear();
  if (m_pSnapshot == nullptr)
    return;

  for (const plDiversionChange& change : m_pSnapshot->m_Status.m_Changes)
    out_files.PushBack(ToAbsolutePath(change.m_sPath));
}
