#include <plEditorPluginDiversion/EditorPluginDiversionPCH.h>

#include <plEditorPluginDiversion/Panels/ConflictsPanel.h>
#include <plEditorPluginDiversion/Panels/PanelWidgets.h>

#include <EditorUi/Runtime.h>
#include <EditorUi/Surface.h>
#include <GameUiPlugin/Elements/UiElements.h>
#include <GuiFoundation/EngineUi/ApplicationPanels.h>

using namespace plDiversionUi;

namespace
{
  constexpr float s_fToolbarHeight = 30.0f;
  constexpr float s_fRowHeight = 72.0f;
} // namespace

plDiversionConflictsPanel::plDiversionConflictsPanel()
  : plEditorUiContent("VersionControlConflicts")
{
}

void plDiversionConflictsPanel::Register()
{
  plEditorUiApplicationPanels::SetIcon(PanelId, plEditorUi::IconPath(plEditorUi::Icon::Warning));

  plEditorUiApplicationPanelDesc desc;
  desc.m_sId = PanelId;
  desc.m_sTabWith = "Panel.VersionControl";
  desc.m_Role = plEditorUiDockRole::Bottom;
  desc.m_bOpenByDefault = true;
  desc.m_CreateContent = []() -> plUniquePtr<plEditorUiContent>
  { return PL_DEFAULT_NEW(plDiversionConflictsPanel); };
  plEditorUiApplicationPanels::Register(desc);
}

void plDiversionConflictsPanel::Unregister()
{
  plEditorUiApplicationPanels::Unregister(PanelId);
}

// Building.

plUiDocumentResourceHandle plDiversionConflictsPanel::BuildRowTemplate()
{
  using namespace plEditorUi;

  plUiPanel* row = Panel();
  row->m_Color = plColorGammaUB(0, 0, 0, 0);

  plUiImage* icon = SvgImage(row, IconPath(Icon::Warning));
  Place(icon, 10, 9, 14, 14);
  icon->m_Color = Danger();

  plUiText* name = Text(row, "", "Name");
  name->m_vAnchorMax.y = 0;
  name->m_vOffsets = plVec4(32, 6, 20, 18);
  name->m_uiFontSize = FontSize - 1;

  plUiText* text = Text(row, "", "Text");
  text->m_vAnchorMax.y = 0;
  text->m_vOffsets = plVec4(32, 24, 20, 16);
  text->m_uiFontSize = FontSize - 2;
  text->m_Color = Muted();

  // the three answers, left to right below the text
  float x = 32.0f;
  for (const auto& [szLabel, szAction] : {std::pair{"Keep mine", "Conflicts.mine"}, std::pair{"Keep theirs", "Conflicts.theirs"}, std::pair{"Compare...", "Conflicts.compare"}})
  {
    const float fWidth = MeasureText(szLabel, FontSize - 2) + 18.0f;
    plUiButton* button = Button(row, "", szAction);
    button->ClearChildren();
    Place(button, x, 44, fWidth, 22);
    button->m_Color = plStringView(szAction) == "Conflicts.compare" ? Selection() : Raised();
    button->m_HoverColor = Border();
    button->m_fBorderWidth = 1;
    button->m_BorderColor = Border();
    button->m_vCornerRadius = plVec4(3);
    button->m_sDisabledBinding = "Busy";

    plUiText* label = Text(button, szLabel);
    Fill(label);
    label->m_HAlign = plUiTextHAlign::Center;
    label->m_uiFontSize = FontSize - 2;

    x += fWidth + 6.0f;
  }

  plUiPanel* rule = Panel(row);
  rule->m_vAnchorMin.y = 1;
  rule->m_vOffsets = plVec4(0, -1, 0, 1);
  rule->m_Color = plColorGammaUB(23, 23, 26);

  return Finish(row);
}

void plDiversionConflictsPanel::BuildDocument()
{
  using namespace plEditorUi;

  m_bBuilt = true;

  plUiPanel* root = Panel();
  root->m_Color = Background();

  plUiPanel* toolbar = Panel(root);
  toolbar->m_vAnchorMax.y = 0;
  toolbar->m_vOffsets = plVec4(0, 0, 0, s_fToolbarHeight);
  toolbar->m_Color = Surface();

  plUiText* summary = Text(toolbar, "", "Conflicts.Summary");
  summary->m_vOffsets = plVec4(10, 0, 140, 0);
  summary->m_uiFontSize = FontSize - 1;
  summary->m_Color = Muted();

  ToolButton(toolbar, Icon::Refresh, "Refresh", "Conflicts.refresh", "", 8);

  plUiPanel* toolbarLine = Panel(root);
  toolbarLine->m_vAnchorMax.y = 0;
  toolbarLine->m_vOffsets = plVec4(0, s_fToolbarHeight, 0, 1);
  toolbarLine->m_Color = Line();

  plUiPanel* listArea = Panel(root);
  Fill(listArea, plVec4(0, s_fToolbarHeight + 1, 0, 0));
  listArea->m_Color = plColorGammaUB(0, 0, 0, 0);

  plUiItemList* list = List(listArea, "Conflicts.Rows", BuildRowTemplate(), s_fRowHeight);
  list->m_Color = plColorGammaUB(0, 0, 0, 0);

  plUiText* empty = Text(listArea, "No conflicts. When a teammate's commit and your changes meet in one file, it shows up here.");
  empty->m_vAnchorMax.y = 0;
  empty->m_vOffsets = plVec4(10, 20, 10, 20);
  empty->m_HAlign = plUiTextHAlign::Center;
  empty->m_uiFontSize = FontSize - 1;
  empty->m_Color = Dim();
  empty->m_sVisibleBinding = "Conflicts.None";

  SetDocument(Finish(root));
  Refresh();
}

// Data.

void plDiversionConflictsPanel::Refresh()
{
  plBlackboard& board = GetSurface()->GetBlackboard();

  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  const bool bBusy = pProvider != nullptr && pProvider->IsBusy();

  bool bHeld = false;
  for (const plDiversionProvider::Conflict& conflict : m_Conflicts)
    bHeld |= conflict.m_bHeld;

  plStringBuilder sSummary;
  if (m_Conflicts.IsEmpty())
    sSummary = "Conflicts";
  else
    sSummary.SetFormat("{} conflict{}{}", m_Conflicts.GetCount(), m_Conflicts.GetCount() == 1 ? "" : "s", bHeld ? " \xC2\xB7 the workspace is held back" : "");
  board.SetEntryValue("Conflicts.Summary", plString(sSummary));
  board.SetEntryValue("Conflicts.None", m_Conflicts.IsEmpty());

  const plString sBranch = pProvider != nullptr && pProvider->IsConnected() ? pProvider->GetSnapshot()->m_Workspace.m_sBranchName : plString();

  plVariantArray rows;
  for (const plDiversionProvider::Conflict& conflict : m_Conflicts)
  {
    plStringBuilder sName;
    sName.SetFormat("{} \xC2\xB7 {}", plPathUtils::GetFileNameAndExtension(conflict.m_sAbsPath), conflict.m_bHeld ? "synced edits meet an incoming commit" : "unsynced edits met an incoming commit");

    plStringBuilder sText;
    if (conflict.m_bHeld)
      sText.SetFormat("A commit on {} changed it, and so did your synced edits. Diversion keeps the workspace where it is until you choose.", sBranch);
    else
      sText.SetFormat("Diversion kept your version as {} and wrote the incoming one in its place.", plPathUtils::GetFileNameAndExtension(conflict.m_sBackupAbsPath));

    plVariantDictionary data;
    data.Insert("Name", plString(sName));
    data.Insert("Text", plString(sText));
    data.Insert("Busy", bBusy);
    rows.PushBack(data);
  }
  board.SetEntryValue("Conflicts.Rows", rows);
}

void plDiversionConflictsPanel::OnUiUpdate()
{
  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  const plUInt32 uiVersion = pProvider != nullptr ? pProvider->GetSnapshotVersion() : 0;

  if (m_bBuilt && uiVersion == m_uiSeenVersion)
    return;

  m_uiSeenVersion = uiVersion;
  if (pProvider != nullptr)
    pProvider->GetConflicts(m_Conflicts);
  else
    m_Conflicts.Clear();

  if (!m_bBuilt)
    BuildDocument();
  else
    Refresh();
}

// Input.

void plDiversionConflictsPanel::OnUiEvent(const plMsgUiEvent& event)
{
  const plStringView sId = event.m_sIdentifier.GetView();
  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  if (pProvider == nullptr)
    return;

  if (sId == "Conflicts.refresh")
  {
    pProvider->Refresh();
    return;
  }

  if (event.m_iItemIndex < 0 || event.m_iItemIndex >= static_cast<plInt32>(m_Conflicts.GetCount()))
    return;

  // the questions open after the panel's event
  const plDiversionProvider::Conflict conflict = m_Conflicts[event.m_iItemIndex];
  if (sId == "Conflicts.mine" || sId == "Conflicts.theirs")
  {
    const bool bMine = sId == "Conflicts.mine";
    plEditorUiRuntime::RunAfterFrame([conflict, bMine]()
      {
        if (plDiversionProvider* pProvider = plDiversionProvider::GetSingleton())
          pProvider->AskAndResolve(conflict, bMine); });
  }
  else if (sId == "Conflicts.compare")
  {
    pProvider->CompareConflict(conflict);
  }
}
