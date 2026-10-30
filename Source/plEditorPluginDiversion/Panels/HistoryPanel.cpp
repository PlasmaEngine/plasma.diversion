#include <plEditorPluginDiversion/EditorPluginDiversionPCH.h>

#include <plEditorPluginDiversion/Panels/HistoryPanel.h>
#include <plEditorPluginDiversion/Panels/PanelWidgets.h>
#include <plEditorPluginDiversion/Provider/DiversionProvider.h>

#include <EditorFramework/EngineShell/DocumentFrame.h>
#include <EditorFramework/EngineShell/EngineShell.h>
#include <EditorUi/DialogHost.h>
#include <EditorUi/DockManager.h>
#include <EditorUi/MessageBox.h>
#include <EditorUi/Runtime.h>
#include <EditorUi/Surface.h>
#include <Foundation/Algorithm/HashingUtils.h>
#include <GameUiPlugin/Elements/UiElements.h>
#include <GuiFoundation/EngineUi/ApplicationPanels.h>
#include <GuiFoundation/UIServices/UIServices.h>
#include <ToolsFoundation/Document/Document.h>

using namespace plDiversionUi;

plString plDiversionHistoryPanel::s_sRequestedTarget;

namespace
{
  constexpr float s_fToolbarHeight = 30.0f;
  constexpr float s_fRowHeight = 58.0f;

  /// A steady colour per person for the avatar circle.
  plColorGammaUB AvatarColor(plStringView sName)
  {
    static const plColorGammaUB s_Colors[] = {plColorGammaUB(45, 212, 191), plColorGammaUB(225, 175, 130), plColorGammaUB(185, 150, 205), plColorGammaUB(115, 195, 145), plColorGammaUB(150, 175, 195)};
    return s_Colors[plHashingUtils::StringHash(sName) % PL_ARRAY_SIZE(s_Colors)];
  }
} // namespace

plDiversionHistoryPanel::plDiversionHistoryPanel()
  : plEditorUiContent("VersionControlHistory")
{
}

void plDiversionHistoryPanel::Register()
{
  plEditorUiApplicationPanels::SetIcon(PanelId, plEditorUi::IconPath(plEditorUi::Icon::Clock));

  plEditorUiApplicationPanelDesc desc;
  desc.m_sId = PanelId;
  desc.m_sTabWith = "Panel.VersionControl";
  desc.m_Role = plEditorUiDockRole::Bottom;
  desc.m_bOpenByDefault = true;
  desc.m_CreateContent = []() -> plUniquePtr<plEditorUiContent>
  { return PL_DEFAULT_NEW(plDiversionHistoryPanel); };
  plEditorUiApplicationPanels::Register(desc);
}

void plDiversionHistoryPanel::Unregister()
{
  plEditorUiApplicationPanels::Unregister(PanelId);
}

void plDiversionHistoryPanel::ShowFor(plStringView sAbsolutePath)
{
  s_sRequestedTarget = sAbsolutePath;

  if (plEditorUiEngineShell* pShell = plEditorUiEngineShell::GetSingleton())
  {
    pShell->GetDock().OpenPanel(PanelId);
    pShell->GetDock().SetCurrentPanel(PanelId);
  }
}

// Building.

plUiDocumentResourceHandle plDiversionHistoryPanel::BuildRowTemplate()
{
  using namespace plEditorUi;

  plUiPanel* row = Panel();
  row->m_Color = plColorGammaUB(0, 0, 0, 0);

  plUiBoundImage* avatar = BoundImage(row, "", "AvatarTint", "");
  Place(avatar, 10, 8, 22, 22);
  avatar->m_vCornerRadius = plVec4(11);

  plUiText* initial = Text(avatar, "", "Initial");
  Fill(initial);
  initial->m_HAlign = plUiTextHAlign::Center;
  initial->m_uiFontSize = FontSize - 3;
  initial->m_Color = plColorGammaUB(17, 17, 17);

  plUiText* message = Text(row, "", "Message");
  message->m_vAnchorMax.y = 0;
  message->m_vOffsets = plVec4(42, 5, 320, 17);
  message->m_uiFontSize = FontSize - 1;

  plUiText* meta = Text(row, "", "Meta");
  meta->m_vAnchorMax.y = 0;
  meta->m_vOffsets = plVec4(42, 22, 320, 14);
  meta->m_hFont = GetCodeFont();
  meta->m_uiFontSize = FontSize - 3;
  meta->m_Color = Dim();

  float fRight = 10.0f;
  fRight += RowButton(row, "Restore this version...", "History.restore", "CanAct", fRight) + 6.0f;
  RowButton(row, "Compare with yours", "History.compare", "CanAct", fRight);

  plUiPanel* rule = Panel(row);
  rule->m_vAnchorMin.y = 1;
  rule->m_vOffsets = plVec4(0, -1, 0, 1);
  rule->m_Color = plColorGammaUB(23, 23, 26);

  return Finish(row);
}

void plDiversionHistoryPanel::BuildDocument()
{
  using namespace plEditorUi;

  m_bBuilt = true;

  plUiPanel* root = Panel();
  root->m_Color = Background();

  plUiPanel* toolbar = Panel(root);
  toolbar->m_vAnchorMax.y = 0;
  toolbar->m_vOffsets = plVec4(0, 0, 0, s_fToolbarHeight);
  toolbar->m_Color = Surface();

  plUiText* summary = Text(toolbar, "", "History.Summary");
  summary->m_vOffsets = plVec4(10, 0, 300, 0);
  summary->m_uiFontSize = FontSize - 1;
  summary->m_Color = Muted();

  float fRight = 8.0f;
  fRight += ToolButton(toolbar, Icon::External, "Open in Diversion", "History.open", "", fRight) + 6.0f;
  ToolButton(toolbar, Icon::Refresh, "Refresh", "History.refresh", "", fRight);

  plUiPanel* toolbarLine = Panel(root);
  toolbarLine->m_vAnchorMax.y = 0;
  toolbarLine->m_vOffsets = plVec4(0, s_fToolbarHeight, 0, 1);
  toolbarLine->m_Color = Line();

  plUiPanel* listArea = Panel(root);
  Fill(listArea, plVec4(0, s_fToolbarHeight + 1, 0, 0));
  listArea->m_Color = plColorGammaUB(0, 0, 0, 0);

  plUiItemList* list = List(listArea, "History.Rows", BuildRowTemplate(), s_fRowHeight);
  list->m_Color = plColorGammaUB(0, 0, 0, 0);

  plUiText* empty = Text(listArea, "", "History.Empty");
  empty->m_vAnchorMax.y = 0;
  empty->m_vOffsets = plVec4(10, 20, 10, 20);
  empty->m_HAlign = plUiTextHAlign::Center;
  empty->m_uiFontSize = FontSize - 1;
  empty->m_Color = Dim();

  SetDocument(Finish(root));
  Refresh();
}

// Data.

plString plDiversionHistoryPanel::FindTarget()
{
  // the active document, when it is a file of the workspace (not an old version opened by Compare)
  plString sActive;
  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  plEditorUiEngineShell* pShell = plEditorUiEngineShell::GetSingleton();
  plEditorUiDocumentFrame* pFrame = pShell != nullptr ? pShell->GetActiveFrame() : nullptr;
  const plDocument* pActive = pFrame != nullptr ? pFrame->GetDocument() : nullptr;
  if (pProvider != nullptr && pActive != nullptr && !pProvider->IsCompareDocument(pActive) && !pProvider->ToWorkspacePath(pActive->GetDocumentPath()).IsEmpty())
    sActive = pActive->GetDocumentPath();

  // a file asked for stays until another document becomes the active one
  if (!s_sRequestedTarget.IsEmpty())
  {
    m_sPinned = s_sRequestedTarget;
    m_sActiveAtPin = sActive;
    s_sRequestedTarget.Clear();
  }

  if (!m_sPinned.IsEmpty() && sActive == m_sActiveAtPin)
    return m_sPinned;

  m_sPinned.Clear();
  return sActive.IsEmpty() ? m_sTarget : sActive;
}

void plDiversionHistoryPanel::RequestHistory()
{
  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  if (pProvider == nullptr || m_sTarget.IsEmpty())
    return;

  m_bLoading = true;
  m_sError.Clear();
  m_Commits.Clear();
  m_bDirty = true;

  pProvider->RequestHistory(m_sTarget, [this, alive = std::weak_ptr<bool>(m_pAlive), sTarget = m_sTarget](bool bSucceeded, const plDynamicArray<plDiversionCommit>& commits, plStringView sError)
    {
      if (alive.expired() || sTarget != m_sTarget)
        return;

      m_bLoading = false;
      m_Commits = commits;
      m_sError = bSucceeded ? plStringView() : sError;
      m_bDirty = true; });
}

void plDiversionHistoryPanel::Refresh()
{
  plBlackboard& board = GetSurface()->GetBlackboard();

  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  const plString sBranch = pProvider != nullptr && pProvider->IsConnected() ? pProvider->GetSnapshot()->m_Workspace.m_sBranchName : plString();

  plStringBuilder sSummary;
  if (m_sTarget.IsEmpty())
    sSummary = "History";
  else
    sSummary.SetFormat("History of {} on {}", plPathUtils::GetFileNameAndExtension(m_sTarget), sBranch);
  board.SetEntryValue("History.Summary", plString(sSummary));

  plStringBuilder sEmpty;
  if (m_sTarget.IsEmpty())
    sEmpty = "Select History on an asset, or open a document, to see the commits that changed it.";
  else if (m_bLoading)
    sEmpty = "Getting the history from Diversion...";
  else if (!m_sError.IsEmpty())
    sEmpty.SetFormat("The history is not available: {}", m_sError);
  else if (m_Commits.IsEmpty())
    sEmpty = "No commit changed this file yet: it is new, or not committed.";
  board.SetEntryValue("History.Empty", plString(sEmpty));

  const bool bBusy = pProvider != nullptr && pProvider->IsBusy();
  plVariantArray rows;
  for (plUInt32 i = 0; i < m_Commits.GetCount(); ++i)
  {
    const plDiversionCommit& commit = m_Commits[i];

    plStringBuilder sMeta;
    sMeta.SetFormat("{} \xC2\xB7 {} \xC2\xB7 {}", commit.m_sAuthor, commit.m_sId, plDiversionProvider::FormatAgo(commit.m_Time));
    if (i == 0)
      sMeta.Append(" \xC2\xB7 the latest");

    plStringBuilder sInitial = commit.m_sAuthor.IsEmpty() ? plStringView("?") : plStringView(commit.m_sAuthor.GetData(), commit.m_sAuthor.GetData() + 1);
    sInitial.ToUpper();

    plVariantDictionary data;
    data.Insert("Message", commit.m_sMessage.IsEmpty() ? plString("(no message)") : commit.m_sMessage);
    data.Insert("Meta", plString(sMeta));
    data.Insert("Initial", plString(sInitial));
    data.Insert("AvatarTint", plColor(AvatarColor(commit.m_sAuthor)));
    data.Insert("CanAct", i > 0);
    data.Insert("Busy", bBusy);
    rows.PushBack(data);
  }
  board.SetEntryValue("History.Rows", rows);
}

void plDiversionHistoryPanel::OnUiUpdate()
{
  if (!m_bBuilt)
    BuildDocument();

  // asked again for another file, and when the workspace or its branch got a commit (it may have changed the file)
  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  plStringBuilder sSyncedCommit;
  if (pProvider != nullptr && pProvider->IsConnected())
  {
    const plDiversionSnapshot& snapshot = *pProvider->GetSnapshot();
    sSyncedCommit = snapshot.m_SyncedCommit.m_sId;
    for (const plDiversionBranch& branch : snapshot.m_Branches)
    {
      if (branch.m_sName == snapshot.m_Workspace.m_sBranchName)
        sSyncedCommit.Append(" ", branch.m_sHeadCommitId);
    }
  }

  const plString sTarget = FindTarget();
  if (sTarget != m_sTarget || sSyncedCommit != m_sSyncedCommit)
  {
    m_sTarget = sTarget;
    m_sSyncedCommit = sSyncedCommit;
    RequestHistory();
  }

  if (m_bDirty)
  {
    m_bDirty = false;
    Refresh();
  }
}

// Input.

void plDiversionHistoryPanel::OnUiEvent(const plMsgUiEvent& event)
{
  const plStringView sId = event.m_sIdentifier.GetView();
  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  if (pProvider == nullptr)
    return;

  if (sId == "History.refresh")
  {
    RequestHistory();
    return;
  }

  if (sId == "History.open")
  {
    pProvider->OpenInDiversion();
    return;
  }

  if (event.m_iItemIndex <= 0 || event.m_iItemIndex >= static_cast<plInt32>(m_Commits.GetCount()))
    return;

  const plDiversionCommit commit = m_Commits[event.m_iItemIndex];
  const plString sTarget = m_sTarget;

  if (sId == "History.compare")
  {
    plStringBuilder sLabel;
    sLabel.SetFormat("{} by {}", commit.m_sId, commit.m_sAuthor);
    pProvider->Compare(sTarget, commit.m_sId, sLabel);
  }
  else if (sId == "History.restore")
  {
    // the question opens after the panel's event
    plEditorUiRuntime::RunAfterFrame([sTarget, commit]()
      {
        plEditorUiMessageBoxDesc desc;
        desc.m_sTitle = plStringBuilder("Restore ", plPathUtils::GetFileNameAndExtension(sTarget));
        plStringBuilder sText;
        sText.SetFormat("{} goes back to how it was in {} (\"{}\"). That is a change like any other: Changes lists it, and you commit or undo it.", plPathUtils::GetFileNameAndExtension(sTarget), commit.m_sId, commit.m_sMessage);
        desc.m_sText = sText;
        desc.m_Kind = plEditorUiMessageKind::Question;

        plEditorUiDialogButton& restore = desc.m_Buttons.ExpandAndGetRef();
        restore.m_sId = "restore";
        restore.m_sLabel = "Restore";
        restore.m_Role = plEditorUiDialogButton::Role::Accept;
        plEditorUiDialogButton& cancel = desc.m_Buttons.ExpandAndGetRef();
        cancel.m_sId = "cancel";
        cancel.m_sLabel = "Cancel";
        cancel.m_Role = plEditorUiDialogButton::Role::Reject;
        desc.m_sDefaultButton = "restore";
        desc.m_sEscapeButton = "cancel";

        if (plEditorUiMessageBox::Show(plEditorUiServices::GetEngineDialogOwner(), desc).m_sButton != "restore")
          return;

        if (plDiversionProvider* pProvider = plDiversionProvider::GetSingleton())
        {
          pProvider->Restore(sTarget, commit.m_sId, [sTarget, sId = commit.m_sId](bool bSucceeded, plStringView sMessage)
            {
              if (bSucceeded)
                plEditorUiServices::ShowAllDocumentsTemporaryStatusBarMessage(plFmt("Restored {} from {}; commit or undo it in Changes.", plPathUtils::GetFileNameAndExtension(sTarget), sId), plTime::MakeFromSeconds(8));
              else
                plEditorUiServices::MessageBoxWarning(plFmt("Restoring failed: {}", sMessage)); });
        } });
  }
}
