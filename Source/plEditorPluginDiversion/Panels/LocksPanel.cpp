#include <plEditorPluginDiversion/EditorPluginDiversionPCH.h>

#include <plEditorPluginDiversion/Panels/LocksPanel.h>
#include <plEditorPluginDiversion/Panels/PanelWidgets.h>
#include <plEditorPluginDiversion/Provider/DiversionProvider.h>

#include <EditorFramework/EditorApp/EditorApp.h>
#include <EditorUi/Runtime.h>
#include <EditorUi/Surface.h>
#include <GameUiPlugin/Elements/UiElements.h>
#include <GuiFoundation/EngineUi/ApplicationPanels.h>
#include <GuiFoundation/UIServices/UIServices.h>

using namespace plDiversionUi;

namespace
{
  constexpr float s_fToolbarHeight = 30.0f;
  constexpr float s_fNoteHeight = 52.0f;
  constexpr float s_fRowHeight = 36.0f;
} // namespace

plDiversionLocksPanel::plDiversionLocksPanel()
  : plEditorUiContent("VersionControlLocks")
{
}

void plDiversionLocksPanel::Register()
{
  plEditorUiApplicationPanels::SetIcon(PanelId, plEditorUi::IconPath(plEditorUi::Icon::Locked));

  plEditorUiApplicationPanelDesc desc;
  desc.m_sId = PanelId;
  desc.m_sTabWith = "Panel.VersionControl";
  desc.m_Role = plEditorUiDockRole::Bottom;
  desc.m_bOpenByDefault = true;
  desc.m_CreateContent = []() -> plUniquePtr<plEditorUiContent>
  { return PL_DEFAULT_NEW(plDiversionLocksPanel); };
  plEditorUiApplicationPanels::Register(desc);
}

void plDiversionLocksPanel::Unregister()
{
  plEditorUiApplicationPanels::Unregister(PanelId);
}

// Building.

plUiDocumentResourceHandle plDiversionLocksPanel::BuildRowTemplate()
{
  using namespace plEditorUi;

  plUiPanel* row = Panel();
  row->m_Color = plColorGammaUB(0, 0, 0, 0);

  // a group caption, at the bottom of its row
  plUiText* title = Text(row, "", "GroupTitle");
  title->m_vAnchorMax.x = 0.5f;
  title->m_vOffsets = plVec4(10, 12, 4, 0);
  title->m_uiFontSize = MinFontSize;
  title->m_Color = Dim();
  title->m_sVisibleBinding = "IsGroup";

  plUiText* note = Text(row, "", "GroupNote");
  note->m_vAnchorMin.x = 0.5f;
  note->m_vOffsets = plVec4(4, 12, 10, 0);
  note->m_uiFontSize = FontSize - 3;
  note->m_Color = Dim();
  note->m_HAlign = plUiTextHAlign::Right;
  note->m_sVisibleBinding = "IsGroup";

  // a lock or someone's edits: icon, name, who and since when, one button
  plUiPanel* item = Panel(row);
  item->m_Color = plColorGammaUB(0, 0, 0, 0);
  item->m_sVisibleBinding = "IsItem";

  plUiBoundImage* icon = BoundImage(item, "Icon", "IconTint", "");
  Place(icon, 10, 11, 14, 14);

  plUiText* name = Text(item, "", "Name");
  name->m_vAnchorMax.y = 0;
  name->m_vOffsets = plVec4(32, 3, 130, 17);
  name->m_uiFontSize = FontSize - 1;

  plUiText* line = Text(item, "", "Line");
  line->m_vAnchorMax.y = 0;
  line->m_vOffsets = plVec4(32, 19, 130, 14);
  line->m_uiFontSize = FontSize - 3;
  line->m_Color = Dim();

  // only one of them shows on a row
  RowButton(item, "Unlock", "Locks.unlock", "CanUnlock", 10);
  RowButton(item, "Force unlock...", "Locks.force", "CanForce", 10);
  RowButton(item, "Open", "Locks.open", "CanOpen", 10);

  plUiPanel* rule = Panel(item);
  rule->m_vAnchorMin.y = 1;
  rule->m_vOffsets = plVec4(0, -1, 0, 1);
  rule->m_Color = plColorGammaUB(23, 23, 26);

  plUiText* empty = Text(row, "", "EmptyText");
  Fill(empty);
  empty->m_HAlign = plUiTextHAlign::Center;
  empty->m_uiFontSize = FontSize - 1;
  empty->m_Color = Dim();
  empty->m_sVisibleBinding = "IsEmpty";

  return Finish(row);
}

void plDiversionLocksPanel::BuildDocument()
{
  using namespace plEditorUi;

  m_bBuilt = true;

  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  m_bBuiltWithNote = pProvider != nullptr && pProvider->IsConnected() && !pProvider->CanUseLocks();

  plUiPanel* root = Panel();
  root->m_Color = Background();

  plUiPanel* toolbar = Panel(root);
  toolbar->m_vAnchorMax.y = 0;
  toolbar->m_vOffsets = plVec4(0, 0, 0, s_fToolbarHeight);
  toolbar->m_Color = Surface();

  plUiText* summary = Text(toolbar, "", "Locks.Summary");
  summary->m_vOffsets = plVec4(10, 0, 140, 0);
  summary->m_uiFontSize = FontSize - 1;
  summary->m_Color = Muted();

  ToolButton(toolbar, Icon::Refresh, "Refresh", "Locks.refresh", "", 8);

  plUiPanel* toolbarLine = Panel(root);
  toolbarLine->m_vAnchorMax.y = 0;
  toolbarLine->m_vOffsets = plVec4(0, s_fToolbarHeight, 0, 1);
  toolbarLine->m_Color = Line();

  float fTop = s_fToolbarHeight + 1;

  // no hard locks on this plan: a note in their place (D19)
  if (m_bBuiltWithNote)
  {
    plUiText* caption = Text(root, "HARD LOCKS");
    caption->m_vAnchorMax.y = 0;
    caption->m_vOffsets = plVec4(10, fTop + 8, 10, 14);
    caption->m_uiFontSize = MinFontSize;
    caption->m_Color = Dim();

    plUiPanel* note = Panel(root);
    note->m_vAnchorMax.y = 0;
    note->m_vOffsets = plVec4(10, fTop + 26, 10, s_fNoteHeight - 10);
    note->m_Color = Plate();
    note->m_fBorderWidth = 1;
    note->m_BorderColor = Line();
    note->m_vCornerRadius = plVec4(4);

    plUiText* first = Text(note, "Hard locks need Diversion's Studio or Enterprise plan; this repository's plan has none, so the editor doesn't offer them.");
    first->m_vAnchorMax.y = 0;
    first->m_vOffsets = plVec4(9, 4, 9, 16);
    first->m_uiFontSize = FontSize - 2;
    first->m_Color = Muted();

    plUiText* second = Text(note, "Locks taken elsewhere would still be listed here. The soft locks below work on every plan.");
    second->m_vAnchorMax.y = 0;
    second->m_vOffsets = plVec4(9, 21, 9, 16);
    second->m_uiFontSize = FontSize - 2;
    second->m_Color = Muted();

    fTop += 26 + s_fNoteHeight - 4;
  }

  plUiPanel* listArea = Panel(root);
  Fill(listArea, plVec4(0, fTop, 0, 0));
  listArea->m_Color = plColorGammaUB(0, 0, 0, 0);

  plUiItemList* list = List(listArea, "Locks.Rows", BuildRowTemplate(), s_fRowHeight);
  list->m_Color = plColorGammaUB(0, 0, 0, 0);

  // not connected: one message over everything
  plUiPanel* offline = Panel(root);
  Fill(offline, plVec4(0, s_fToolbarHeight + 1, 0, 0));
  offline->m_Color = Background();
  offline->m_sVisibleBinding = "Locks.Offline";

  plUiText* offlineText = Text(offline, "Not connected to Diversion.");
  Fill(offlineText);
  offlineText->m_HAlign = plUiTextHAlign::Center;
  offlineText->m_Color = Muted();

  SetDocument(Finish(root));
  Refresh();
}

// Data.

void plDiversionLocksPanel::ReadRows()
{
  m_Rows.Clear();

  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  const plDiversionSnapshot* pSnapshot = pProvider != nullptr ? pProvider->GetSnapshot() : nullptr;
  if (pSnapshot == nullptr || !pProvider->IsConnected())
    return;

  // hard locks of the project's files: yours first
  if (pProvider->CanUseLocks() || !pSnapshot->m_Locks.IsEmpty())
  {
    Row& hardGroup = m_Rows.ExpandAndGetRef();
    hardGroup.m_Kind = RowKind::Group;
    hardGroup.m_sTitle = "HARD LOCKS";

    plHybridArray<Row, 16> locks;
    for (const plDiversionLock& lock : pSnapshot->m_Locks)
    {
      const plString sAbsolute = pProvider->ToAbsolutePath(lock.m_sPath);

      plVersionControlFileInfo info;
      pProvider->GetFileInfo(sAbsolute, info);
      if (!info.m_bLocked)
        continue;

      Row& row = locks.ExpandAndGetRef();
      row.m_Kind = RowKind::Lock;
      row.m_sAbsPath = sAbsolute;
      row.m_sTitle = plPathUtils::GetFileNameAndExtension(lock.m_sPath);
      row.m_bMine = info.m_Lock.m_bMine;

      plStringBuilder sLine;
      sLine.SetFormat("{} \xC2\xB7 since {}{}", row.m_bMine ? plStringView("You") : info.m_Lock.m_sUser.GetView(), plDiversionProvider::FormatClock(info.m_Lock.m_Time), lock.m_bAuto ? " \xC2\xB7 by a repository rule" : "");
      row.m_sLine = sLine;
    }

    locks.Sort([](const Row& a, const Row& b)
      { return a.m_bMine != b.m_bMine ? a.m_bMine : a.m_sTitle.Compare_NoCase(b.m_sTitle) < 0; });

    for (const Row& row : locks)
      m_Rows.PushBack(row);

    if (locks.IsEmpty())
    {
      Row& empty = m_Rows.ExpandAndGetRef();
      empty.m_Kind = RowKind::Empty;
      empty.m_sTitle = "No hard locks.";
    }
  }

  // someone else's edits: the soft locks
  Row& group = m_Rows.ExpandAndGetRef();
  group.m_Kind = RowKind::Group;
  group.m_sTitle = "OTHERS EDITING";
  group.m_sLine = "soft locks, checked every 20 s";

  const plUInt32 uiFirstEditing = m_Rows.GetCount();
  for (auto it : pSnapshot->m_OtherChanges)
  {
    const plString sAbsolute = pProvider->ToAbsolutePath(it.Key());

    plVersionControlFileInfo info;
    pProvider->GetFileInfo(sAbsolute, info);
    if (info.m_OtherChanges.IsEmpty())
      continue;

    const plVersionControlOtherChange& other = info.m_OtherChanges[0];

    Row& row = m_Rows.ExpandAndGetRef();
    row.m_Kind = RowKind::Editing;
    row.m_sAbsPath = sAbsolute;
    row.m_sTitle = plPathUtils::GetFileNameAndExtension(it.Key());

    plStringBuilder sLine;
    if (other.m_bUncommitted)
      sLine.SetFormat("{} \xC2\xB7 uncommitted in their workspace on {} \xC2\xB7 since {}", other.m_sUser, other.m_sBranch, plDiversionProvider::FormatClock(other.m_Time));
    else
      sLine.SetFormat("{} \xC2\xB7 committed on {} \xC2\xB7 {}", other.m_sUser, other.m_sBranch, plDiversionProvider::FormatAgo(other.m_Time));

    if (info.m_OtherChanges.GetCount() > 1)
      sLine.AppendFormat(" \xC2\xB7 and {} more", info.m_OtherChanges.GetCount() - 1);

    row.m_sLine = sLine;
  }

  if (m_Rows.GetCount() == uiFirstEditing)
  {
    Row& empty = m_Rows.ExpandAndGetRef();
    empty.m_Kind = RowKind::Empty;
    empty.m_sTitle = "Nobody else is editing project files.";
  }
}

void plDiversionLocksPanel::Refresh()
{
  using namespace plEditorUi;

  plBlackboard& board = GetSurface()->GetBlackboard();

  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  const bool bConnected = pProvider != nullptr && pProvider->IsConnected();
  const bool bBusy = pProvider != nullptr && pProvider->IsBusy();
  const bool bCanForce = bConnected && pProvider->CanForceUnlock();

  board.SetEntryValue("Locks.Offline", !bConnected);

  const plString sPrefix = bConnected ? pProvider->GetProjectPrefix() : plString();
  board.SetEntryValue("Locks.Summary", plString(plStringBuilder("Locks in ", sPrefix.IsEmpty() ? plStringView("the project") : sPrefix.GetView(), sPrefix.IsEmpty() ? "" : "/")));

  const plString sLock = IconPath(Icon::Locked);
  const plString sEye = IconPath(Icon::Visible);
  plString sLockTexture;
  plString sEyeTexture;
  SvgTexture(sLock, &sLockTexture);
  SvgTexture(sEye, &sEyeTexture);

  plVariantArray rows;
  for (const Row& row : m_Rows)
  {
    plVariantDictionary data;
    data.Insert("IsGroup", row.m_Kind == RowKind::Group);
    data.Insert("IsItem", row.m_Kind == RowKind::Lock || row.m_Kind == RowKind::Editing);
    data.Insert("IsEmpty", row.m_Kind == RowKind::Empty);
    data.Insert("Busy", bBusy);

    if (row.m_Kind == RowKind::Group)
    {
      data.Insert("GroupTitle", row.m_sTitle);
      data.Insert("GroupNote", row.m_sLine);
    }
    else if (row.m_Kind == RowKind::Empty)
    {
      data.Insert("EmptyText", row.m_sTitle);
    }
    else
    {
      const bool bLock = row.m_Kind == RowKind::Lock;
      data.Insert("Icon", bLock ? sLockTexture : sEyeTexture);
      data.Insert("IconTint", plColor(bLock ? plVersionControl::GetLockColor(row.m_bMine) : plVersionControl::GetBadgeColor(plVersionControlFileState::Modified)));
      data.Insert("Name", row.m_sTitle);
      data.Insert("Line", row.m_sLine);
      data.Insert("CanUnlock", bLock && row.m_bMine);
      data.Insert("CanForce", bLock && !row.m_bMine && bCanForce);
      data.Insert("CanOpen", !bLock);
    }

    rows.PushBack(data);
  }

  board.SetEntryValue("Locks.Rows", rows);
}

void plDiversionLocksPanel::OnUiUpdate()
{
  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  const plUInt32 uiVersion = pProvider != nullptr ? pProvider->GetSnapshotVersion() : 0;

  // the plan note is part of the layout: a change of plan builds the panel again
  const bool bNote = pProvider != nullptr && pProvider->IsConnected() && !pProvider->CanUseLocks();
  if (!m_bBuilt || bNote != m_bBuiltWithNote)
  {
    m_uiSeenVersion = uiVersion;
    ReadRows();
    BuildDocument();
    return;
  }

  if (uiVersion == m_uiSeenVersion)
    return;

  m_uiSeenVersion = uiVersion;
  ReadRows();
  Refresh();
}

// Input.

void plDiversionLocksPanel::OnUiEvent(const plMsgUiEvent& event)
{
  const plStringView sId = event.m_sIdentifier.GetView();
  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  if (pProvider == nullptr)
    return;

  if (sId == "Locks.refresh")
  {
    pProvider->Refresh();
    return;
  }

  if (event.m_iItemIndex < 0 || event.m_iItemIndex >= static_cast<plInt32>(m_Rows.GetCount()))
    return;

  const plString sPath = m_Rows[event.m_iItemIndex].m_sAbsPath;
  const plString sName = plPathUtils::GetFileNameAndExtension(sPath);

  if (sId == "Locks.unlock")
  {
    pProvider->Unlock(sPath, false, [sName](bool bSucceeded, plStringView sMessage)
      {
        if (bSucceeded)
          plEditorUiServices::ShowAllDocumentsTemporaryStatusBarMessage(plFmt("Unlocked {}.", sName), plTime::MakeFromSeconds(5));
        else
          plEditorUiServices::MessageBoxWarning(plFmt("Unlocking {} failed: {}", sName, sMessage)); });
  }
  else if (sId == "Locks.force")
  {
    // a question box must not open inside the panel's event
    plEditorUiRuntime::RunAfterFrame([sPath]()
      {
        if (plDiversionProvider* pProvider = plDiversionProvider::GetSingleton())
          pProvider->AskAndForceUnlock(sPath); });
  }
  else if (sId == "Locks.open")
  {
    plEditorApp::GetSingleton()->OpenDocument(sPath, plDocumentFlags::RequestWindow);
  }
}
