#include <plEditorPluginDiversion/EditorPluginDiversionPCH.h>

#include <plEditorPluginDiversion/Client/DiversionIgnore.h>
#include <plEditorPluginDiversion/Panels/ChangesPanel.h>
#include <plEditorPluginDiversion/Panels/PanelWidgets.h>
#include <plEditorPluginDiversion/Provider/DiversionProvider.h>

#include <EditorFramework/Assets/AssetBrowserFilter.h>
#include <EditorFramework/Assets/AssetBrowserWidget.h>
#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorFramework/VersionControl/VersionControl.h>
#include <EditorFramework/EditorApp/EditorApp.h>
#include <EditorFramework/EngineShell/EngineShell.h>
#include <EditorFramework/ProjectChecks/ProjectChecks.h>
#include <EditorUi/Controls.h>
#include <EditorUi/Menu.h>
#include <EditorUi/MessageBox.h>
#include <EditorUi/ModalDialog.h>
#include <EditorUi/Surface.h>
#include <EditorUi/Widgets.h>
#include <Foundation/IO/OSFile.h>
#include <Foundation/Math/ColorScheme.h>
#include <GameUiPlugin/Elements/UiElements.h>
#include <GuiFoundation/EngineUi/ApplicationPanels.h>
#include <GuiFoundation/UIServices/UIServices.h>
#include <ToolsFoundation/Document/Document.h>
#include <ToolsFoundation/Document/DocumentManager.h>

using namespace plDiversionUi;

namespace
{
  constexpr float s_fToolbarHeight = 30.0f;
  constexpr float s_fCommitWidth = 290.0f;
  constexpr float s_fHintHeight = 58.0f;
  constexpr float s_fShelfRowHeight = 34.0f;

  /// How many files an undo question names before "and N more".
  constexpr plUInt32 MaxListedFiles = 12;

  /// How long a command's outcome stays under the commit button.
  constexpr plTime ResultDuration = plTime::MakeFromSeconds(8);

  // the tree (D37-D42)
  constexpr float s_fIndentStep = 16.0f;
  constexpr float s_fChangeIndent = 14.0f;  ///< a change sits under its folder's tick box, past the disclosure
  constexpr float s_fNameLeft = 66.0f;
  constexpr float s_fMarkerReserve = 96.0f; ///< right of a change's name: unsaved note, eye, lock
  constexpr float s_fCountBox = 70.0f;      ///< a folder row's count text
  constexpr float s_fCountBase = 290.0f;    ///< where the count texts start from the right; their offsets push them right
  constexpr float s_fTotalWidth = 44.0f;
  constexpr float s_fCountGap = 8.0f;
  constexpr float s_fFilterWidth = 180.0f;
  constexpr plUInt32 s_uiPageSize = 200;
  constexpr const char* s_szViewSetting = "Diversion.ChangesView";
  constexpr const char* s_szIgnoreComment = "Added from the editor's Changes panel";

  enum MenuId : plUInt32
  {
    MenuOpen = 1,
    MenuShow,
    MenuUndo,
    MenuShelve,
    MenuOpenInDiversion,
    MenuTick,
    MenuIgnore,
  };

  /// A text entry of the blackboard; empty while nothing was typed (no entry converts to a placeholder text).
  plString BoardText(const plBlackboard& board, const char* szEntry)
  {
    const plVariant value = board.GetEntryValue(plTempHashedString(szEntry));
    return value.IsValid() ? value.ConvertTo<plString>() : plString();
  }

  plString FormatCount(plUInt32 uiCount)
  {
    // thousands separated, as the mockup shows them: 5,599
    plStringBuilder s;
    s.SetFormat("{}", uiCount);

    for (plInt32 i = static_cast<plInt32>(s.GetElementCount()) - 3; i > 0; i -= 3)
      s.Insert(s.GetData() + i, ",");

    return s;
  }

  /// sText cut with "..." to fit fWidth.
  plString FitText(plStringView sText, float fWidth, plUInt32 uiFontSize)
  {
    if (plEditorUi::MeasureText(sText, uiFontSize) <= fWidth)
      return sText;

    if (fWidth <= 0.0f)
      return {};

    const plUInt32 uiChars = plStringUtils::GetCharacterCount(sText.GetStartPointer(), sText.GetEndPointer());

    plUInt32 uiLow = 0;
    plUInt32 uiHigh = uiChars;
    plStringBuilder sBest("...");

    while (uiLow < uiHigh)
    {
      const plUInt32 uiMid = (uiLow + uiHigh + 1) / 2;

      plStringBuilder sCandidate = sText;
      sCandidate.Shrink(0, uiChars - uiMid);
      sCandidate.Append("...");

      if (plEditorUi::MeasureText(sCandidate, uiFontSize) <= fWidth)
      {
        sBest = sCandidate;
        uiLow = uiMid;
      }
      else
      {
        uiHigh = uiMid - 1;
      }
    }

    return sBest;
  }

  /// A check box with a third state, some ticked, drawn as a dash.
  plUiButton* TriStateBox(plUiElement* pParent, plStringView sAction, plStringView sOnBinding, plStringView sSomeBinding)
  {
    using namespace plEditorUi;

    plUiButton* button = plDiversionUi::CheckBox(pParent, sAction, sOnBinding);
    plUiElement* box = button->m_Children[0];

    plUiPanel* some = Panel(box);
    some->m_Color = Selection();
    some->m_fBorderWidth = 1;
    some->m_BorderColor = plColorGammaUB(Accent().r, Accent().g, Accent().b, 140);
    some->m_vCornerRadius = plVec4(3);
    some->m_sVisibleBinding = sSomeBinding;

    plUiPanel* dash = Panel(some);
    dash->m_vAnchorMin = dash->m_vAnchorMax = plVec2(0.5f);
    dash->m_vOffsets = plVec4(-4, -1, 8, 2);
    dash->m_Color = Accent();
    return button;
  }

  /// Content that moves right with the row's depth ("Indent", in px).
  plUiOffsetPanel* IndentPanel(plUiElement* pParent)
  {
    plUiOffsetPanel* indent = PL_DEFAULT_NEW(plUiOffsetPanel);
    plEditorUi::Fill(indent);
    indent->m_Color = plColorGammaUB(0, 0, 0, 0);
    indent->m_sOffsetBinding = "Indent";
    indent->m_fStep = 1.0f;
    pParent->m_Children.PushBack(indent);
    return indent;
  }

  plString Key(plStringView sPath)
  {
    plStringBuilder s = sPath;
    s.MakeCleanPath();
    s.ToLower();
    return s;
  }

  plColorGammaUB KindColor(plDiversionChangeKind kind)
  {
    switch (kind)
    {
      case plDiversionChangeKind::Added:
        return plVersionControl::GetBadgeColor(plVersionControlFileState::Added);
      case plDiversionChangeKind::Deleted:
        return plVersionControl::GetBadgeColor(plVersionControlFileState::Deleted);
      case plDiversionChangeKind::Moved:
        return plVersionControl::GetBadgeColor(plVersionControlFileState::Moved);
      default:
        return plVersionControl::GetBadgeColor(plVersionControlFileState::Modified);
    }
  }

  plStringView KindLetter(plDiversionChangeKind kind)
  {
    switch (kind)
    {
      case plDiversionChangeKind::Added:
        return "A";
      case plDiversionChangeKind::Deleted:
        return "D";
      case plDiversionChangeKind::Moved:
        return "R";
      default:
        return "M";
    }
  }

  /// An asset's dependencies (data-directory-relative paths or GUIDs) as absolute paths.
  void GetDependencies(plStringView sAbsPath, plDynamicArray<plString>& out_paths)
  {
    out_paths.Clear();

    auto pSubAsset = plAssetCurator::GetSingleton()->FindSubAsset(sAbsPath);
    if (!pSubAsset.isValid() || pSubAsset->m_pAssetInfo->m_Info == nullptr)
      return;

    const plAssetDocumentInfo& info = *pSubAsset->m_pAssetInfo->m_Info;

    plHybridArray<plString, 16> deps;
    for (const plString& sDep : info.m_TransformDependencies)
      deps.PushBack(sDep);
    for (const plString& sDep : info.m_PackageDependencies)
      deps.PushBack(sDep);

    for (const plString& sDep : deps)
    {
      if (sDep.StartsWith("{"))
      {
        auto pDep = plAssetCurator::GetSingleton()->FindSubAsset(sDep);
        if (pDep.isValid())
          out_paths.PushBack(pDep->m_pAssetInfo->m_Path.GetAbsolutePath());
      }
      else
      {
        plStringBuilder sPath = sDep;
        if (plEditorApp::GetSingleton()->MakeDataDirectoryRelativePathAbsolute(sPath))
          out_paths.PushBack(sPath);
      }
    }
  }
} // namespace

plDiversionChangesPanel::plDiversionChangesPanel()
  : plEditorUiContent("VersionControl")
{
  m_PointerSubscription = GetSurface()->m_PointerEvents.AddEventHandler(plMakeDelegate(&plDiversionChangesPanel::OnPointer, this));
  m_bTreeView = plEditorUi::LoadSetting(s_szViewSetting) != "list";
}

plDiversionChangesPanel::~plDiversionChangesPanel()
{
  GetSurface()->m_PointerEvents.RemoveEventHandler(m_PointerSubscription);
}

void plDiversionChangesPanel::Register()
{
  plEditorUiApplicationPanels::SetIcon(PanelId, plEditorUi::IconPath(plEditorUi::Icon::Layers));

  plEditorUiApplicationPanelDesc desc;
  desc.m_sId = PanelId;
  desc.m_sTabWith = "Panel.Log";
  desc.m_Role = plEditorUiDockRole::Bottom;
  desc.m_bOpenByDefault = true;
  desc.m_CreateContent = []() -> plUniquePtr<plEditorUiContent>
  { return PL_DEFAULT_NEW(plDiversionChangesPanel); };
  plEditorUiApplicationPanels::Register(desc);
}

void plDiversionChangesPanel::Unregister()
{
  plEditorUiApplicationPanels::Unregister(PanelId);
}

// Building.

plUiDocumentResourceHandle plDiversionChangesPanel::BuildRowTemplate()
{
  using namespace plEditorUi;

  plUiPanel* row = Panel();
  row->m_Color = plColorGammaUB(0, 0, 0, 0);

  // selection, clicks, double-clicks and the context menu go to the row
  plUiButton* catcher = Button(row, "", "VC.row");
  catcher->m_sClickEvent.Clear();
  catcher->ClearChildren();
  Fill(catcher);
  catcher->m_Color = plColorGammaUB(0, 0, 0, 0);
  catcher->m_HoverColor = Surface();
  catcher->m_PressedColor = Surface();
  catcher->m_vCornerRadius = plVec4::MakeZero();

  plUiPanel* selected = Panel(row);
  selected->m_Color = Selection();
  selected->m_sVisibleBinding = "Selected";

  plUiPanel* rail = Panel(row);
  rail->m_vAnchorMax.x = 0;
  rail->m_vOffsets = plVec4(0, 0, 2, 0);
  rail->m_Color = Accent();
  rail->m_sVisibleBinding = "Selected";

  // a change: check box, state letter, type icon, name, folder (the list only), marker; indented in the tree
  plUiPanel* change = Panel(row);
  change->m_Color = plColorGammaUB(0, 0, 0, 0);
  change->m_sVisibleBinding = "IsChange";

  plUiOffsetPanel* changeIndent = IndentPanel(change);

  plUiButton* tick = CheckBox(changeIndent, "VC.tick", "Ticked");
  Place(tick, 4, 0, 24, RowHeight);

  plUiBoundText* letter = PL_DEFAULT_NEW(plUiBoundText);
  changeIndent->m_Children.PushBack(letter);
  Place(letter, 28, 0, 14, RowHeight);
  letter->m_sTextBinding = "Letter";
  letter->m_sTintBinding = "LetterTint";
  letter->m_hFont = GetCodeFont();
  letter->m_uiFontSize = FontSize - 3;
  letter->m_Color = plColorGammaUB(255, 255, 255);
  letter->m_HAlign = plUiTextHAlign::Center;
  letter->m_VAlign = plUiTextVAlign::Center;

  plUiBoundImage* icon = BoundImage(changeIndent, "Icon", "IconTint", "");
  Place(icon, 46, 4, 14, 14);

  // the list: name in the left half, its folder in the right; the tree: the name, fitted to what its depth leaves
  plUiText* name = Text(changeIndent, "", "Name");
  name->m_vAnchorMax.x = 0.5f;
  name->m_vOffsets = plVec4(s_fNameLeft, 0, 4, 0);
  name->m_sVisibleBinding = "Flat";

  plUiText* treeName = Text(changeIndent, "", "Name");
  Fill(treeName, plVec4(s_fNameLeft, 0, 4, 0));
  treeName->m_sVisibleBinding = "InTree";

  plUiText* folder = Text(changeIndent, "", "Folder");
  folder->m_vAnchorMin.x = 0.5f;
  folder->m_vOffsets = plVec4(4, 0, 96, 0);
  folder->m_uiFontSize = FontSize - 2;
  folder->m_Color = Dim();
  folder->m_sVisibleBinding = "Flat";

  plUiText* unsaved = Text(change, "unsaved edits", "");
  unsaved->m_vAnchorMin.x = 1;
  unsaved->m_vOffsets = plVec4(-92, 0, 70, 0);
  unsaved->m_uiFontSize = FontSize - 3;
  unsaved->m_Color = Danger();
  unsaved->m_HAlign = plUiTextHAlign::Right;
  unsaved->m_sVisibleBinding = "Unsaved";

  plUiImage* marker = SvgImage(change, IconPath(Icon::Visible));
  marker->m_vAnchorMin = marker->m_vAnchorMax = plVec2(1, 0.5f);
  marker->m_vOffsets = plVec4(-20, -6, 12, 12);
  marker->m_Color = plVersionControl::GetBadgeColor(plVersionControlFileState::Modified);
  marker->m_sVisibleBinding = "HasMarker";

  // a hard lock shows in the marker's place: the accent for yours, red for someone else's
  plUiBoundImage* lock = BoundImage(change, "LockIcon", "LockTint", "HasLock");
  lock->m_vAnchorMin = lock->m_vAnchorMax = plVec2(1, 0.5f);
  lock->m_vOffsets = plVec4(-20, -6, 12, 12);

  // a folder: disclosure, three-state tick box, folder icon, name; its counts by kind and the total at the right
  plUiPanel* folderRow = Panel(row);
  folderRow->m_Color = plColorGammaUB(0, 0, 0, 0);
  folderRow->m_sVisibleBinding = "IsFolder";

  plUiOffsetPanel* folderIndent = IndentPanel(folderRow);

  for (bool bOpen : {false, true})
  {
    plUiImage* chevron = SvgImage(folderIndent, IconPath(bOpen ? Icon::Expand : Icon::Next));
    Place(chevron, 6, 6, 10, 10);
    chevron->m_Color = Dim();
    chevron->m_sVisibleBinding = bOpen ? "Open" : "Closed";
  }

  plUiButton* folderTick = TriStateBox(folderIndent, "VC.tick", "Ticked", "Some");
  Place(folderTick, 18, 0, 24, RowHeight);

  plUiImage* folderIcon = SvgImage(folderIndent, IconPath(Icon::Folder));
  Place(folderIcon, 46, 4, 14, 14);
  folderIcon->m_Color = Muted();

  plUiText* folderName = Text(folderIndent, "", "Name");
  Fill(folderName, plVec4(s_fNameLeft, 0, 4, 0));

  // the counts pack to the right: each sits in a box at the same place and its offset pushes it into position
  struct CountText
  {
    const char* m_szText;
    const char* m_szOffset;
    plVersionControlFileState m_Color;
  };

  for (const CountText& count : {CountText{"CountA", "OffsetA", plVersionControlFileState::Added}, CountText{"CountM", "OffsetM", plVersionControlFileState::Modified}, CountText{"CountD", "OffsetD", plVersionControlFileState::Deleted}})
  {
    plUiOffsetPanel* slide = PL_DEFAULT_NEW(plUiOffsetPanel);
    folderRow->m_Children.PushBack(slide);
    slide->m_vAnchorMin = slide->m_vAnchorMax = plVec2(1, 0);
    slide->m_vOffsets = plVec4(-s_fCountBase, 0, s_fCountBox, RowHeight);
    slide->m_Color = plColorGammaUB(0, 0, 0, 0);
    slide->m_sOffsetBinding = count.m_szOffset;

    plUiText* text = Text(slide, "", count.m_szText);
    Fill(text);
    text->m_hFont = GetCodeFont();
    text->m_uiFontSize = FontSize - 3;
    text->m_Color = plVersionControl::GetBadgeColor(count.m_Color);
    text->m_HAlign = plUiTextHAlign::Right;
  }

  plUiText* total = Text(folderRow, "", "Total");
  total->m_vAnchorMin = total->m_vAnchorMax = plVec2(1, 0);
  total->m_vOffsets = plVec4(-10 - s_fTotalWidth, 0, s_fTotalWidth, RowHeight);
  total->m_hFont = GetCodeFont();
  total->m_uiFontSize = FontSize - 3;
  total->m_Color = Dim();
  total->m_HAlign = plUiTextHAlign::Right;

  // "Show N more" after a folder's first changes (D41)
  plUiPanel* more = Panel(row);
  more->m_Color = plColorGammaUB(0, 0, 0, 0);
  more->m_sVisibleBinding = "IsMore";

  plUiText* moreText = Text(IndentPanel(more), "", "MoreText");
  Fill(moreText, plVec4(s_fNameLeft, 0, 4, 0));
  moreText->m_uiFontSize = FontSize - 1;
  moreText->m_Color = Accent();

  // a group caption: disclosure, title and a dim note
  plUiPanel* group = Panel(row);
  group->m_Color = plColorGammaUB(0, 0, 0, 0);
  group->m_sVisibleBinding = "IsGroup";

  for (bool bOpen : {false, true})
  {
    plUiImage* chevron = SvgImage(group, IconPath(bOpen ? Icon::Expand : Icon::Next));
    Place(chevron, 10, 6, 10, 10);
    chevron->m_Color = Dim();
    chevron->m_sVisibleBinding = bOpen ? "Open" : "Closed";
  }

  plUiText* title = Text(group, "", "GroupTitle");
  title->m_vAnchorMax.x = 0.5f;
  title->m_vOffsets = plVec4(24, 0, 4, 0);
  title->m_uiFontSize = MinFontSize;
  title->m_Color = Dim();

  plUiText* note = Text(group, "not part of the project, never ticked for you", "");
  note->m_vAnchorMin.x = 0.5f;
  note->m_vOffsets = plVec4(4, 0, 12, 0);
  note->m_uiFontSize = FontSize - 3;
  note->m_Color = Dim();
  note->m_HAlign = plUiTextHAlign::Right;

  return Finish(row);
}

plUiDocumentResourceHandle plDiversionChangesPanel::BuildShelfTemplate()
{
  using namespace plEditorUi;

  plUiPanel* row = Panel();
  row->m_Color = plColorGammaUB(0, 0, 0, 0);

  plUiText* name = Text(row, "", "Name");
  name->m_vAnchorMax.y = 0;
  name->m_vOffsets = plVec4(0, 2, 140, 16);

  plUiText* sub = Text(row, "", "Sub");
  sub->m_vAnchorMax.y = 0;
  sub->m_vOffsets = plVec4(0, 17, 140, 14);
  sub->m_uiFontSize = FontSize - 3;
  sub->m_Color = Dim();

  struct ShelfButton
  {
    const char* m_szLabel;
    const char* m_szAction;
    float m_fRight;
    float m_fWidth;
  };

  for (const ShelfButton& b : {ShelfButton{"Delete", "VC.dropshelf", 0, 62}, ShelfButton{"Apply", "VC.apply", 66, 58}})
  {
    plUiButton* button = Button(row, b.m_szLabel, b.m_szAction);
    button->m_vAnchorMin = button->m_vAnchorMax = plVec2(1, 0.5f);
    button->m_vOffsets = plVec4(-b.m_fRight - b.m_fWidth, -10, b.m_fWidth, 20);
    button->m_Color = Raised();
    button->m_HoverColor = Border();
    button->m_sDisabledBinding = "Busy";
  }

  plUiPanel* rule = Panel(row);
  rule->m_vAnchorMin.y = 1;
  rule->m_vOffsets = plVec4(0, -1, 0, 1);
  rule->m_Color = Line();

  return Finish(row);
}

float plDiversionChangesPanel::BuildLockHint(plUiElement* pParent)
{
  using namespace plEditorUi;

  constexpr float fPad = 8.0f;
  const float fWidth = s_fCommitWidth - 20.0f;
  const float fText = MeasureWrappedHeight(m_sBuiltLockHint, fWidth - 2.0f * fPad, FontSize - 2, 16.0f);
  const float fHeight = fText + 2.0f * 6.0f;

  // someone else's lock blocks the commit (red); someone else's edits only warn (amber)
  plUiPanel* band = Panel(pParent);
  Place(band, 10, 6, fWidth, fHeight);
  band->m_Color = m_bBuiltLockHintBlocks ? plColorGammaUB(33, 18, 17) : plColorGammaUB(29, 25, 16);
  band->m_fBorderWidth = 1;
  band->m_BorderColor = m_bBuiltLockHintBlocks ? plColorGammaUB(74, 37, 35) : plColorGammaUB(59, 50, 32);
  band->m_vCornerRadius = plVec4(4);

  const plColorGammaUB textColor = m_bBuiltLockHintBlocks ? plColorGammaUB(242, 201, 197) : plColorGammaUB(233, 220, 192);
  WrappedText(band, m_sBuiltLockHint, plRectFloat(fPad, 6.0f, fWidth - 2.0f * fPad, 0.0f), textColor, FontSize - 2, 16.0f);

  return fHeight;
}

plString plDiversionChangesPanel::GetLockHint(bool& out_bBlocked) const
{
  plDynamicArray<plUInt32> ticked;
  GetTicked(ticked);

  plStringBuilder sLocked;
  plString sHolder;
  plStringBuilder sEditing;
  plString sEditor;
  plUInt32 uiLocked = 0;

  for (plUInt32 i : ticked)
  {
    const Entry& entry = m_Entries[i];
    const plStringView sName = plPathUtils::GetFileName(entry.m_sPath);

    if (!entry.m_sLockHolder.IsEmpty())
    {
      sLocked.AppendWithSeparator(", ", sName);
      sHolder = entry.m_sLockHolder;
      ++uiLocked;
    }
    else if (!entry.m_sEditing.IsEmpty())
    {
      sEditing.AppendWithSeparator(", ", sName);
      sEditor = entry.m_sEditing;
    }
  }

  out_bBlocked = uiLocked > 0;

  plStringBuilder sHint;
  if (uiLocked > 0)
    sHint.SetFormat("{} {} locked by {}. Diversion would refuse the commit. Untick {}, or ask {} to commit or unlock.", sLocked, uiLocked == 1 ? "is" : "are", sHolder, uiLocked == 1 ? "it" : "them", sHolder);
  else if (!sEditing.IsEmpty())
    sHint.SetFormat("{} is editing {} too. Committing first is fine: {} gets the conflict when they commit.", sEditor, sEditing, sEditor);

  return sHint;
}

plString plDiversionChangesPanel::GetTickedOwnLocks() const
{
  plDynamicArray<plUInt32> ticked;
  GetTicked(ticked);

  plStringBuilder sNames;
  for (plUInt32 i : ticked)
  {
    if (m_Entries[i].m_bMyLock)
      sNames.AppendWithSeparator(", ", plPathUtils::GetFileName(m_Entries[i].m_sPath));
  }

  return sNames;
}

void plDiversionChangesPanel::BuildDocument()
{
  using namespace plEditorUi;

  m_bBuilt = true;

  plUiPanel* root = Panel();
  root->m_Color = Background();

  // Toolbar: tick everything in the project, the count, and the actions on the ticked changes.
  plUiPanel* toolbar = Panel(root);
  toolbar->m_vAnchorMax.y = 0;
  toolbar->m_vOffsets = plVec4(0, 0, 0, s_fToolbarHeight);
  toolbar->m_Color = Surface();

  plUiButton* tickAll = CheckBox(toolbar, "VC.tickall", "VC.AllTicked");
  Place(tickAll, 4, 3, 24, 24);

  float fRight = 8.0f;
  fRight += ToolButton(toolbar, Icon::External, "Open in Diversion", "VC.open", "", fRight) + 6.0f;
  fRight += ToolButton(toolbar, Icon::Reset, "Undo changes", "VC.undo", "VC.NoTickedOrBusy", fRight) + 6.0f;
  fRight += ToolButton(toolbar, Icon::List, "Shelve", "VC.shelve", "VC.NoTickedOrBusy", fRight) + 6.0f;
  fRight += ToolButton(toolbar, Icon::Refresh, "Refresh", "VC.refresh", "", fRight) + 6.0f;

  // Tree | List (D40), joined
  for (bool bList : {true, false})
  {
    const plStringView sLabel = bList ? "List" : "Tree";
    const float fWidth = MeasureText(sLabel, FontSize - 1) + 18.0f;

    plUiButton* button = Button(toolbar, "", bList ? "VC.list" : "VC.tree");
    button->ClearChildren();
    button->m_vAnchorMin = button->m_vAnchorMax = plVec2(1, 0.5f);
    button->m_vOffsets = plVec4(-fRight - fWidth, -11, fWidth, 22);
    button->m_Color = Raised();
    button->m_HoverColor = Border();
    button->m_fBorderWidth = 1;
    button->m_BorderColor = Border();
    button->m_vCornerRadius = bList ? plVec4(0, 3, 3, 0) : plVec4(3, 0, 0, 3);

    plUiPanel* on = Panel(button);
    Fill(on, plVec4(1));
    on->m_Color = Selection();
    on->m_sVisibleBinding = bList ? "VC.ListOn" : "VC.TreeOn";

    plUiText* text = Text(button, sLabel);
    Fill(text);
    text->m_HAlign = plUiTextHAlign::Center;
    text->m_uiFontSize = FontSize - 1;

    fRight += fWidth - 1.0f;
  }

  fRight += 7.0f;

  plUiTextInput* filter = Input(toolbar, "VC.filter", "VC.Filter", "Filter changes");
  filter->m_vAnchorMin = filter->m_vAnchorMax = plVec2(1, 0.5f);
  filter->m_vOffsets = plVec4(-fRight - s_fFilterWidth, -11, s_fFilterWidth, 22);
  filter->m_uiFontSize = FontSize - 1;
  fRight += s_fFilterWidth + 8.0f;

  plUiText* summary = Text(toolbar, "", "VC.Summary");
  summary->m_vOffsets = plVec4(30, 0, fRight, 0);
  summary->m_uiFontSize = FontSize - 1;
  summary->m_Color = Muted();

  plUiPanel* toolbarLine = Panel(root);
  toolbarLine->m_vAnchorMax.y = 0;
  toolbarLine->m_vOffsets = plVec4(0, s_fToolbarHeight, 0, 1);
  toolbarLine->m_Color = Line();

  // The changes, left; the commit column, right.
  plUiPanel* changes = Panel(root);
  Fill(changes, plVec4(0, s_fToolbarHeight + 1, s_fCommitWidth + 1, 0));
  changes->m_Color = Background();

  float fListTop = 0.0f;
  if (m_bHintShown)
  {
    plUiPanel* hint = Panel(changes);
    hint->m_vAnchorMax.y = 0;
    hint->m_vOffsets = plVec4(8, 6, 8, s_fHintHeight - 10);
    hint->m_Color = plColorGammaUB(29, 25, 16);
    hint->m_fBorderWidth = 1;
    hint->m_BorderColor = plColorGammaUB(59, 50, 32);
    hint->m_vCornerRadius = plVec4(4);

    plUiImage* warn = SvgImage(hint, IconPath(Icon::Warning));
    Place(warn, 8, 7, 13, 13);
    warn->m_Color = Warning();

    plUiText* text = Text(hint, "", "VC.Hint");
    text->m_vAnchorMax.y = 0;
    text->m_vOffsets = plVec4(26, 3, 8, 20);
    text->m_uiFontSize = FontSize - 1;
    text->m_Color = plColorGammaUB(233, 220, 192);

    plUiButton* add = Button(hint, "", "VC.adddeps");
    add->ClearChildren();
    Place(add, 26, 24, 110, 20);
    add->m_Color = Selection();
    add->m_HoverColor = plColorGammaUB(31, 97, 88);
    add->m_vCornerRadius = plVec4(3);

    plUiText* addText = Text(add, "", "VC.HintButton");
    Fill(addText, plVec4(8, 0, 8, 0));
    addText->m_uiFontSize = FontSize - 1;

    fListTop = s_fHintHeight;
  }

  plUiPanel* listArea = Panel(changes);
  Fill(listArea, plVec4(0, fListTop, 0, 0));
  listArea->m_Color = plColorGammaUB(0, 0, 0, 0);

  if (!m_hRowTemplate.IsValid())
    m_hRowTemplate = BuildRowTemplate();

  plUiItemList* list = List(listArea, "VC.Rows", m_hRowTemplate, RowHeight);
  list->m_Color = plColorGammaUB(0, 0, 0, 0);

  plUiPanel* empty = EmptyState(listArea, Icon::Check, "No changes in the project", "Saved files show up here within a second or two.");
  empty->m_sVisibleBinding = "VC.Empty";

  plUiPanel* noMatch = EmptyState(listArea, Icon::Search, "No change matches the filter", "Clear the filter to see every change.");
  noMatch->m_sVisibleBinding = "VC.NoMatch";

  // The commit column.
  plUiPanel* divider = Panel(root);
  divider->m_vAnchorMin.x = 1;
  divider->m_vOffsets = plVec4(-s_fCommitWidth - 1, s_fToolbarHeight + 1, 1, 0);
  divider->m_Color = Line();

  plUiPanel* commit = Panel(root);
  commit->m_vAnchorMin.x = 1;
  commit->m_vOffsets = plVec4(-s_fCommitWidth, s_fToolbarHeight + 1, s_fCommitWidth, 0);
  commit->m_Color = Surface();

  // someone else's lock or edits on a ticked file, above everything else
  float y = 6.0f;
  if (!m_sBuiltLockHint.IsEmpty())
    y += BuildLockHint(commit) + 8.0f;

  plUiText* commitTo = Text(commit, "", "VC.CommitTo");
  Place(commitTo, 10, y, s_fCommitWidth - 20, 16);
  commitTo->m_uiFontSize = MinFontSize;
  commitTo->m_Color = Dim();

  plUiTextArea* message = TextArea(commit, "VC.msg", "VC.Message");
  Place(message, 10, y + 20, s_fCommitWidth - 20, 58);
  y += 82.0f;

  // committing releases your locks on the committed files unless they are kept (D24)
  if (m_bBuiltKeepRow)
  {
    plUiButton* keep = CheckBox(commit, "VC.keeplocks", "VC.KeepLocks");
    Place(keep, 6, y, 24, 22);

    plUiText* keepLabel = Text(commit, "", "VC.KeepLabel");
    Place(keepLabel, 32, y, s_fCommitWidth - 42, 22);
    keepLabel->m_uiFontSize = FontSize - 2;
    keepLabel->m_Color = Muted();
    y += 24.0f;
  }

  plUiText* unsaved = Text(commit, "", "VC.UnsavedNote");
  Place(unsaved, 10, y, s_fCommitWidth - 20, 16);
  unsaved->m_uiFontSize = FontSize - 2;
  unsaved->m_Color = Danger();
  unsaved->m_sVisibleBinding = "VC.HasUnsaved";
  y += 20.0f;

  plUiButton* commitButton = Button(commit, "", "VC.commit");
  commitButton->ClearChildren();
  Place(commitButton, 10, y, s_fCommitWidth - 20, 26);
  commitButton->m_Color = Selection();
  commitButton->m_HoverColor = plColorGammaUB(31, 97, 88);
  commitButton->m_fBorderWidth = 1;
  commitButton->m_BorderColor = plColorGammaUB(31, 97, 88);
  commitButton->m_vCornerRadius = plVec4(3);
  commitButton->m_sDisabledBinding = "VC.CannotCommit";

  plUiText* commitLabel = Text(commitButton, "", "VC.CommitLabel");
  Fill(commitLabel);
  commitLabel->m_HAlign = plUiTextHAlign::Center;

  y += 30.0f;

  plUiText* note = Text(commit, "", "VC.CommitNote");
  Place(note, 10, y, s_fCommitWidth - 20, 16);
  note->m_uiFontSize = FontSize - 2;
  note->m_Color = Dim();
  note->m_sVisibleBinding = "VC.NoResult";

  plUiText* result = Text(commit, "", "VC.Result");
  Place(result, 10, y, s_fCommitWidth - 20, 16);
  result->m_uiFontSize = FontSize - 2;
  result->m_Color = Accent();
  result->m_sVisibleBinding = "VC.ResultOk";

  plUiText* error = Text(commit, "", "VC.Result");
  Place(error, 10, y, s_fCommitWidth - 20, 16);
  error->m_uiFontSize = FontSize - 2;
  error->m_Color = Danger();
  error->m_sVisibleBinding = "VC.ResultError";
  y += 28.0f;

  plUiText* shelvesTitle = Text(commit, "", "VC.ShelvesTitle");
  Place(shelvesTitle, 10, y, s_fCommitWidth - 20, 16);
  shelvesTitle->m_uiFontSize = MinFontSize;
  shelvesTitle->m_Color = Dim();
  y += 18.0f;

  plUiPanel* shelves = Panel(commit);
  Fill(shelves, plVec4(10, y, 10, 4));
  shelves->m_Color = plColorGammaUB(0, 0, 0, 0);

  plUiItemList* shelfList = List(shelves, "VC.Shelves", BuildShelfTemplate(), s_fShelfRowHeight);
  shelfList->m_Color = plColorGammaUB(0, 0, 0, 0);

  plUiText* noShelves = Text(shelves, "Shelved changes wait here, off the workspace, until you apply them.", "");
  noShelves->m_vAnchorMax.y = 0;
  noShelves->m_vOffsets = plVec4(0, 0, 0, 16);
  noShelves->m_uiFontSize = FontSize - 3;
  noShelves->m_Color = Dim();
  noShelves->m_sVisibleBinding = "VC.NoShelves";

  // Not connected: one message over everything.
  plUiPanel* offline = Panel(root);
  Fill(offline);
  offline->m_Color = Background();
  offline->m_sVisibleBinding = "VC.Offline";

  plUiPanel* offlineState = EmptyState(offline, Icon::Warning, "Not connected to Diversion", "");
  offlineState->m_sVisibleBinding = "VC.Offline";

  plUiText* offlineText = Text(offline, "", "VC.OfflineText");
  offlineText->m_vAnchorMin.y = offlineText->m_vAnchorMax.y = 0.5f;
  offlineText->m_vOffsets = plVec4(20, 30, 20, 20);
  offlineText->m_HAlign = plUiTextHAlign::Center;
  offlineText->m_Color = Muted();

  SetDocument(Finish(root));
  Refresh();
}

// Data.

void plDiversionChangesPanel::ReadChanges()
{
  m_Entries.Clear();
  m_Rows.Clear();
  m_Shelves.Clear();

  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  const plDiversionSnapshot* pSnapshot = pProvider != nullptr ? pProvider->GetSnapshot() : nullptr;
  if (pSnapshot == nullptr || pSnapshot->m_State != plDiversionSnapshot::State::Connected)
  {
    // the tree must not keep folders of entries that went
    BuildRows();
    return;
  }

  const plString& sRoot = pSnapshot->m_Workspace.m_sRoot;
  const plString sPrefix = pProvider->GetProjectPrefix();
  const plString sPrefixKey = Key(sPrefix);

  plHashTable<plString, const plDiversionLock*> locks;
  for (const plDiversionLock& lock : pSnapshot->m_Locks)
    locks[Key(lock.m_sPath)] = &lock;

  m_Entries.Reserve(pSnapshot->m_Status.m_Changes.GetCount());

  for (const plDiversionChange& change : pSnapshot->m_Status.m_Changes)
  {
    // conflict backups are listed in Conflicts
    if (pProvider->IsProviderFile(change.m_sPath))
      continue;

    Entry& entry = m_Entries.ExpandAndGetRef();
    entry.m_sPath = change.m_sPath;
    entry.m_sKey = Key(change.m_sPath);
    entry.m_sAbsPath = plStringBuilder(sRoot, "/", change.m_sPath);
    entry.m_sPrevPath = change.m_sPrevPath;
    entry.m_Kind = change.m_Kind;

    const plString& sKey = entry.m_sKey;
    entry.m_bElsewhere = !sPrefix.IsEmpty() && !(sKey.StartsWith(sPrefixKey) && sKey.GetElementCount() > sPrefixKey.GetElementCount() && sKey.GetData()[sPrefixKey.GetElementCount()] == '/');

    if (const plDynamicArray<plDiversionOtherChange>* pOthers = pSnapshot->m_OtherChanges.GetValue(change.m_sPath))
    {
      plStringBuilder sOthers;
      for (const plDiversionOtherChange& other : *pOthers)
      {
        if (other.m_sWorkspaceId == pSnapshot->m_Workspace.m_sWorkspaceId)
          continue;

        sOthers.AppendWithSeparator(", ", other.m_sUser.IsEmpty() ? plStringView("someone") : other.m_sUser.GetView());

        if (entry.m_sEditing.IsEmpty() && !other.m_sWorkspaceId.IsEmpty())
          entry.m_sEditing = other.m_sUser.IsEmpty() ? plStringView("Someone") : other.m_sUser.GetView();
      }
      entry.m_sOthers = sOthers;
    }

    if (const plDiversionLock* const* ppLock = locks.GetValue(sKey))
    {
      const plDiversionLock& lock = **ppLock;
      entry.m_bMyLock = !pSnapshot->m_User.m_sId.IsEmpty() && lock.m_sUserId == pSnapshot->m_User.m_sId;
      if (!entry.m_bMyLock)
        entry.m_sLockHolder = lock.m_sUserName;
    }
  }

  // the project's changes by path, then the rest
  m_Entries.Sort([](const Entry& a, const Entry& b)
    {
      if (a.m_bElsewhere != b.m_bElsewhere)
        return !a.m_bElsewhere;
      return a.m_sKey.Compare(b.m_sKey) < 0; });

  // ticks of changes that went (committed, undone, shelved elsewhere) go too
  plHashSet<plString> stillChanged;
  for (const Entry& entry : m_Entries)
    stillChanged.Insert(entry.m_sPath);

  for (auto it = m_Ticked.GetIterator(); it.IsValid();)
  {
    if (!stillChanged.Contains(it.Key()))
      it = m_Ticked.Remove(it);
    else
      ++it;
  }

  for (const plDiversionShelf& shelf : pSnapshot->m_Shelves)
  {
    if (shelf.m_sBranch == pSnapshot->m_Workspace.m_sBranchName)
      m_Shelves.PushBack(shelf);
  }

  BuildRows();
}

void plDiversionChangesPanel::BuildRows()
{
  plDynamicArray<plDiversionChangeTree::Item> items;
  items.Reserve(m_Entries.GetCount());
  for (const Entry& entry : m_Entries)
  {
    plDiversionChangeTree::Item& item = items.ExpandAndGetRef();
    item.m_sPath = entry.m_sPath;
    item.m_Kind = entry.m_Kind;
    item.m_bElsewhere = entry.m_bElsewhere;
  }

  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  m_Tree.Build(items, pProvider != nullptr ? pProvider->GetProjectPrefix() : plString(), m_sFilter);

  plDiversionChangeTree::View view;
  view.m_bTree = m_bTreeView;
  view.m_bAllOpen = !m_sFilter.IsEmpty();
  view.m_bShowElsewhere = m_bShowElsewhere;
  view.m_pOpen = &m_OpenFolders;
  view.m_pShown = &m_ShownChanges;
  view.m_uiPageSize = s_uiPageSize;
  m_Tree.MakeRows(view, m_Rows);
}

plString plDiversionChangesPanel::GetRowIdentity(const Row& row) const
{
  switch (row.m_Kind)
  {
    case RowKind::Change:
      return m_Entries[row.m_uiIndex].m_sPath;
    case RowKind::Folder:
      return plStringBuilder(m_Tree.GetFolder(row.m_uiIndex).m_sKey, "/");
    default:
      return {};
  }
}

void plDiversionChangesPanel::ToggleOpen(plUInt32 uiFolder)
{
  const plString& sKey = m_Tree.GetFolder(uiFolder).m_sKey;
  if (!m_OpenFolders.Remove(sKey))
    m_OpenFolders.Insert(sKey);
}

void plDiversionChangesPanel::ToggleFolderTicks(plUInt32 uiFolder)
{
  plDynamicArray<plUInt32> below;
  m_Tree.GetItems(uiFolder, below);

  bool bAll = true;
  for (plUInt32 i : below)
    bAll &= m_Ticked.Contains(m_Entries[i].m_sPath);

  for (plUInt32 i : below)
  {
    if (bAll)
      m_Ticked.Remove(m_Entries[i].m_sPath);
    else
      m_Ticked.Insert(m_Entries[i].m_sPath);
  }
}

void plDiversionChangesPanel::FindMissingDependencies(plDynamicArray<plUInt32>& out_entries, plString& out_sFirstUser) const
{
  out_entries.Clear();
  out_sFirstUser.Clear();

  if (m_Ticked.IsEmpty())
    return;

  plHashTable<plString, plUInt32> byPath;
  for (plUInt32 i = 0; i < m_Entries.GetCount(); ++i)
    byPath[Key(m_Entries[i].m_sAbsPath)] = i;

  plHashSet<plUInt32> missing;

  for (const Entry& entry : m_Entries)
  {
    if (!m_Ticked.Contains(entry.m_sPath) || entry.m_Kind == plDiversionChangeKind::Deleted)
      continue;

    // the dependencies' dependencies too: a material's new texture asset and that texture's new image
    plHashSet<plString> visited;
    plHybridArray<plString, 16> queue;
    queue.PushBack(entry.m_sAbsPath);

    for (plUInt32 uiDepth = 0; uiDepth < 6 && !queue.IsEmpty(); ++uiDepth)
    {
      plHybridArray<plString, 16> next;

      for (const plString& sPath : queue)
      {
        plDynamicArray<plString> deps;
        GetDependencies(sPath, deps);

        for (const plString& sDep : deps)
        {
          const plString sKey = Key(sDep);
          if (visited.Contains(sKey))
            continue;

          visited.Insert(sKey);
          next.PushBack(sDep);

          const plUInt32* pIndex = byPath.GetValue(sKey);
          if (pIndex == nullptr)
            continue;

          const Entry& dep = m_Entries[*pIndex];
          if (dep.m_Kind == plDiversionChangeKind::Added && !m_Ticked.Contains(dep.m_sPath) && !missing.Contains(*pIndex))
          {
            missing.Insert(*pIndex);
            out_entries.PushBack(*pIndex);

            if (out_sFirstUser.IsEmpty())
              out_sFirstUser = plPathUtils::GetFileNameAndExtension(entry.m_sPath);
          }
        }
      }

      queue = next;
    }
  }
}

void plDiversionChangesPanel::Refresh()
{
  using namespace plEditorUi;

  plBlackboard& board = GetSurface()->GetBlackboard();

  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  const plDiversionSnapshot* pSnapshot = pProvider != nullptr ? pProvider->GetSnapshot() : nullptr;
  const bool bConnected = pSnapshot != nullptr && pSnapshot->m_State == plDiversionSnapshot::State::Connected;
  const bool bBusy = pProvider != nullptr && pProvider->IsBusy();

  board.SetEntryValue("VC.Offline", !bConnected);
  if (!bConnected)
  {
    plVersionControlStatus status;
    if (pProvider != nullptr)
      pProvider->GetStatus(status);

    board.SetEntryValue("VC.OfflineText", status.m_sMessage);
  }

  // rows; the counts and ticks of the project's changes that the filter lets through
  plUInt32 uiProject = 0;
  plUInt32 uiProjectShown = 0;
  plUInt32 uiProjectTicked = 0;
  plDynamicArray<bool> tickedFlags;
  tickedFlags.SetCount(m_Entries.GetCount(), false);
  for (plUInt32 i = 0; i < m_Entries.GetCount(); ++i)
  {
    const Entry& entry = m_Entries[i];
    tickedFlags[i] = m_Ticked.Contains(entry.m_sPath);

    if (entry.m_bElsewhere)
      continue;

    ++uiProject;
    if (!m_Tree.IsShown(i))
      continue;

    ++uiProjectShown;
    if (tickedFlags[i])
      ++uiProjectTicked;
  }

  plDynamicArray<plUInt32> tickedPerFolder;
  m_Tree.CountTicked(tickedFlags, tickedPerFolder);

  // names are fitted to what their depth leaves of the list's width
  const float fListWidth = GetSurface()->GetLogicalSize().x - s_fCommitWidth - 1.0f;
  m_fBuiltWidth = GetSurface()->GetLogicalSize().x;

  plString sLockTexture;
  SvgTexture(IconPath(Icon::Locked), &sLockTexture);

  // documents with unsaved edits, found once rather than once per row
  plHashSet<plString> unsavedDocuments;
  for (plDocumentManager* pManager : plDocumentManager::GetAllDocumentManagers())
  {
    for (plDocument* pDoc : pManager->GetAllOpenDocuments())
    {
      if (pDoc->IsModified())
        unsavedDocuments.Insert(Key(pDoc->GetDocumentPath()));
    }
  }

  plVariantArray rows;
  rows.Reserve(m_Rows.GetCount());
  for (plUInt32 r = 0; r < m_Rows.GetCount(); ++r)
  {
    const Row& row = m_Rows[r];

    plVariantDictionary data;
    data.Insert("Selected", !m_sSelected.IsEmpty() && GetRowIdentity(row) == m_sSelected);
    data.Insert("IsGroup", row.m_Kind == RowKind::Elsewhere);
    data.Insert("IsChange", row.m_Kind == RowKind::Change);
    data.Insert("IsFolder", row.m_Kind == RowKind::Folder);
    data.Insert("IsMore", row.m_Kind == RowKind::More);

    if (row.m_Kind == RowKind::Elsewhere)
    {
      plStringBuilder sTitle;
      sTitle.SetFormat("ELSEWHERE IN THE WORKSPACE \xC2\xB7 {}", FormatCount(m_Tree.GetFolder(plDiversionChangeTree::ElsewhereRoot).m_uiTotal));
      data.Insert("GroupTitle", plString(sTitle));
      data.Insert("Open", m_bShowElsewhere);
      data.Insert("Closed", !m_bShowElsewhere);
    }
    else if (row.m_Kind == RowKind::Folder)
    {
      WriteFolderRow(row, tickedPerFolder[row.m_uiIndex], fListWidth, data);
    }
    else if (row.m_Kind == RowKind::More)
    {
      data.Insert("Indent", row.m_uiDepth * s_fIndentStep + s_fChangeIndent);
      data.Insert("MoreText", plString(plStringBuilder("Show ", FormatCount(row.m_uiHidden), " more")));
    }
    else
    {
      const Entry& entry = m_Entries[row.m_uiIndex];
      const FileIcon& icon = GetFileIcon(entry.m_sAbsPath);
      const float fIndent = m_bTreeView ? row.m_uiDepth * s_fIndentStep + s_fChangeIndent : 0.0f;

      plStringBuilder sFolder;
      if (!m_bTreeView)
      {
        sFolder = plPathUtils::GetFileDirectory(entry.m_sPath);
        sFolder.Trim(nullptr, "/");
      }

      if (entry.m_Kind == plDiversionChangeKind::Moved)
        sFolder.Prepend("from ", plPathUtils::GetFileNameAndExtension(entry.m_sPrevPath), m_bTreeView ? "" : " \xC2\xB7 ");

      plString sName = plPathUtils::GetFileNameAndExtension(entry.m_sPath);
      if (m_bTreeView)
        sName = FitText(sName, fListWidth - fIndent - s_fNameLeft - s_fMarkerReserve, FontSize);

      data.Insert("Indent", fIndent);
      data.Insert("Flat", !m_bTreeView);
      data.Insert("InTree", m_bTreeView);
      data.Insert("Ticked", tickedFlags[row.m_uiIndex]);
      data.Insert("Letter", plString(KindLetter(entry.m_Kind)));
      data.Insert("LetterTint", plColor(KindColor(entry.m_Kind)));
      data.Insert("Icon", icon.m_sTexture);
      data.Insert("IconTint", plColor(icon.m_Color));
      data.Insert("Name", sName);
      data.Insert("Folder", plString(sFolder));
      const bool bLocked = entry.m_bMyLock || !entry.m_sLockHolder.IsEmpty();
      data.Insert("HasMarker", !entry.m_sOthers.IsEmpty() && !bLocked);
      data.Insert("HasLock", bLocked);
      data.Insert("LockIcon", sLockTexture);
      data.Insert("LockTint", plColor(plVersionControl::GetLockColor(entry.m_bMyLock)));
      data.Insert("Unsaved", !unsavedDocuments.IsEmpty() && unsavedDocuments.Contains(Key(entry.m_sAbsPath)));
    }

    rows.PushBack(data);
  }

  board.SetEntryValue("VC.Rows", rows);
  board.SetEntryValue("VC.Empty", bConnected && uiProject == 0);
  board.SetEntryValue("VC.NoMatch", bConnected && uiProject > 0 && m_Rows.IsEmpty());
  board.SetEntryValue("VC.AllTicked", uiProjectShown > 0 && uiProjectTicked == uiProjectShown);
  board.SetEntryValue("VC.TreeOn", m_bTreeView);
  board.SetEntryValue("VC.ListOn", !m_bTreeView);

  plDynamicArray<plUInt32> ticked;
  GetTicked(ticked);

  plStringBuilder sSummary;
  const plString sPrefix = pProvider != nullptr ? pProvider->GetProjectPrefix() : plString();
  if (!m_sFilter.IsEmpty())
    sSummary.SetFormat("{} of {} changed match", FormatCount(uiProjectShown), FormatCount(uiProject));
  else
    sSummary.SetFormat("{} changed in the project", FormatCount(uiProject));

  if (!sPrefix.IsEmpty())
    sSummary.Append("  ", sPrefix, "/");

  if (!ticked.IsEmpty())
    sSummary.Append("  \xC2\xB7  ", FormatCount(ticked.GetCount()), " ticked");

  board.SetEntryValue("VC.Summary", plString(sSummary));
  board.SetEntryValue("VC.NoTickedOrBusy", ticked.IsEmpty() || bBusy);

  // the dependency hint (D18): its band comes and goes with a rebuild
  plDynamicArray<plUInt32> missing;
  plString sFirstUser;
  FindMissingDependencies(missing, sFirstUser);

  if (missing.IsEmpty() == m_bHintShown)
  {
    m_bHintShown = !missing.IsEmpty();
    BuildDocument();
    return;
  }

  // the lock band and the keep-locks row change the commit column's layout, so they rebuild it too
  bool bBlocked = false;
  const plString sLockHint = GetLockHint(bBlocked);
  const plString sOwnLocks = GetTickedOwnLocks();
  if (sLockHint != m_sBuiltLockHint || bBlocked != m_bBuiltLockHintBlocks || sOwnLocks.IsEmpty() == m_bBuiltKeepRow)
  {
    m_sBuiltLockHint = sLockHint;
    m_bBuiltLockHintBlocks = bBlocked;
    m_bBuiltKeepRow = !sOwnLocks.IsEmpty();
    BuildDocument();
    return;
  }

  board.SetEntryValue("VC.KeepLocks", m_bKeepLocks);
  board.SetEntryValue("VC.KeepLabel", plString(plStringBuilder("Keep my locks on ", sOwnLocks)));

  if (!missing.IsEmpty())
  {
    plStringBuilder sNames;
    for (plUInt32 i : missing)
      sNames.AppendWithSeparator(", ", plPathUtils::GetFileNameAndExtension(m_Entries[i].m_sPath));

    plStringBuilder sHint;
    sHint.SetFormat("{} uses {} new file{} that {} not ticked: {}", sFirstUser, missing.GetCount(), missing.GetCount() == 1 ? "" : "s", missing.GetCount() == 1 ? "is" : "are", sNames);
    board.SetEntryValue("VC.Hint", plString(sHint));
    board.SetEntryValue("VC.HintButton", plString(missing.GetCount() == 1 ? "Tick it too" : "Tick them too"));
  }

  // the commit column
  const plString sMessage = BoardText(board, "VC.Message");
  plStringBuilder sMessageTrimmed = sMessage;
  sMessageTrimmed.Trim(" \t\r\n");

  plStringBuilder sCommitTo;
  sCommitTo.SetFormat("COMMIT TO {}", bConnected ? pSnapshot->m_Workspace.m_sBranchName.GetView() : plStringView());
  board.SetEntryValue("VC.CommitTo", plString(sCommitTo));

  plStringBuilder sUnsaved;
  for (plUInt32 i : ticked)
  {
    if (!unsavedDocuments.IsEmpty() && unsavedDocuments.Contains(Key(m_Entries[i].m_sAbsPath)))
    {
      if (sUnsaved.IsEmpty())
        sUnsaved.SetFormat("{} has unsaved edits; Commit asks first.", plPathUtils::GetFileNameAndExtension(m_Entries[i].m_sPath));
      else
        sUnsaved = "Ticked documents have unsaved edits; Commit asks first.";
    }
  }
  board.SetEntryValue("VC.HasUnsaved", !sUnsaved.IsEmpty());
  board.SetEntryValue("VC.UnsavedNote", plString(sUnsaved));

  plStringBuilder sLabel;
  if (bBusy)
    sLabel = "Working...";
  else if (ticked.IsEmpty())
    sLabel = "Commit";
  else
    sLabel.SetFormat("Commit {} file{}", FormatCount(ticked.GetCount()), ticked.GetCount() == 1 ? "" : "s");
  board.SetEntryValue("VC.CommitLabel", plString(sLabel));
  board.SetEntryValue("VC.CannotCommit", bBusy || ticked.IsEmpty() || sMessageTrimmed.IsEmpty() || bBlocked);

  plStringBuilder sNote;
  if (ticked.IsEmpty())
    sNote = "Tick the changes to commit.";
  else if (sMessageTrimmed.IsEmpty())
    sNote = "Write a message to commit.";
  else
    sNote.SetFormat("Goes straight to everyone on {}.", bConnected ? pSnapshot->m_Workspace.m_sBranchName.GetView() : plStringView("the branch"));
  board.SetEntryValue("VC.CommitNote", plString(sNote));

  const bool bShowResult = !m_sResult.IsEmpty() && plTime::Now() < m_ResultUntil;
  board.SetEntryValue("VC.Result", m_sResult);
  board.SetEntryValue("VC.NoResult", !bShowResult);
  board.SetEntryValue("VC.ResultOk", bShowResult && !m_bResultIsError);
  board.SetEntryValue("VC.ResultError", bShowResult && m_bResultIsError);

  // shelves of this branch
  plStringBuilder sShelvesTitle;
  sShelvesTitle.SetFormat("SHELVES \xC2\xB7 {}", m_Shelves.GetCount());
  board.SetEntryValue("VC.ShelvesTitle", plString(sShelvesTitle));
  board.SetEntryValue("VC.NoShelves", m_Shelves.IsEmpty());

  plVariantArray shelves;
  for (const plDiversionShelf& shelf : m_Shelves)
  {
    plStringBuilder sSub;
    if (shelf.m_iChangeCount >= 0)
      sSub.SetFormat("{} change{} \xC2\xB7 {}", shelf.m_iChangeCount, shelf.m_iChangeCount == 1 ? "" : "s", shelf.m_sCreated);
    else
      sSub = shelf.m_sCreated;

    plVariantDictionary data;
    data.Insert("Name", shelf.m_sName);
    data.Insert("Sub", plString(sSub));
    data.Insert("Busy", bBusy);
    shelves.PushBack(data);
  }
  board.SetEntryValue("VC.Shelves", shelves);
}

void plDiversionChangesPanel::WriteFolderRow(const Row& row, plUInt32 uiTicked, float fWidth, plVariantDictionary& ref_data) const
{
  using namespace plEditorUi;

  const plDiversionChangeTree::Folder& folder = m_Tree.GetFolder(row.m_uiIndex);
  const bool bOpen = !m_sFilter.IsEmpty() || m_OpenFolders.Contains(folder.m_sKey);
  const float fIndent = row.m_uiDepth * s_fIndentStep;

  ref_data.Insert("Indent", fIndent);
  ref_data.Insert("Open", bOpen);
  ref_data.Insert("Closed", !bOpen);
  ref_data.Insert("Ticked", uiTicked > 0 && uiTicked >= folder.m_uiTotal);
  ref_data.Insert("Some", uiTicked > 0 && uiTicked < folder.m_uiTotal);
  ref_data.Insert("Total", FormatCount(folder.m_uiTotal));

  // right to left after the total: deleted, modified (moves count with them), added, each as wide as its text
  struct Count
  {
    const char* m_szText;
    const char* m_szOffset;
    const char* m_szSign;
    plUInt32 m_uiCount;
  };

  const Count counts[] = {
    {"CountD", "OffsetD", "-", folder.m_uiCount[static_cast<plUInt32>(plDiversionChangeKind::Deleted)]},
    {"CountM", "OffsetM", "~", folder.m_uiCount[static_cast<plUInt32>(plDiversionChangeKind::Modified)] + folder.m_uiCount[static_cast<plUInt32>(plDiversionChangeKind::Moved)]},
    {"CountA", "OffsetA", "+", folder.m_uiCount[static_cast<plUInt32>(plDiversionChangeKind::Added)]},
  };

  float fRight = 10.0f + s_fTotalWidth + s_fCountGap;
  for (const Count& count : counts)
  {
    if (count.m_uiCount == 0)
    {
      ref_data.Insert(count.m_szText, plString());
      ref_data.Insert(count.m_szOffset, 0.0f);
      continue;
    }

    const plString sText = plStringBuilder(count.m_szSign, FormatCount(count.m_uiCount));
    ref_data.Insert(count.m_szText, sText);
    ref_data.Insert(count.m_szOffset, s_fCountBase - s_fCountBox - fRight);
    fRight += plMath::Ceil(MeasureCodeText(sText, FontSize - 3)) + s_fCountGap;
  }

  ref_data.Insert("Name", FitText(folder.m_sName, fWidth - fIndent - s_fNameLeft - fRight, FontSize));
}

void plDiversionChangesPanel::OnUiUpdate()
{
  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  const plUInt32 uiVersion = pProvider != nullptr ? pProvider->GetSnapshotVersion() : 0;
  const bool bBusy = pProvider != nullptr && pProvider->IsBusy();

  if (uiVersion != m_uiSeenVersion)
  {
    m_uiSeenVersion = uiVersion;
    m_bReadDirty = true;
  }

  // the button labels follow running commands, the result line its timeout, the commit button the message text
  if (bBusy != m_bWasBusy || (!m_sResult.IsEmpty() && plTime::Now() >= m_ResultUntil))
  {
    m_bWasBusy = bBusy;
    if (plTime::Now() >= m_ResultUntil)
      m_sResult.Clear();
    m_bDirty = true;
  }

  plBlackboard& board = GetSurface()->GetBlackboard();

  // typing into the filter rebuilds the rows (D42)
  const plString sFilter = BoardText(board, "VC.Filter");
  if (sFilter != m_sFilter)
  {
    m_sFilter = sFilter;
    m_bRowsDirty = true;
  }

  // the names are fitted to the width
  if (plMath::Abs(GetSurface()->GetLogicalSize().x - m_fBuiltWidth) > 0.5f)
    m_bDirty = true;

  if (!m_bBuilt)
  {
    ReadChanges();
    BuildDocument();
    m_bReadDirty = m_bRowsDirty = m_bDirty = false;
    return;
  }

  if (m_bReadDirty || m_bRowsDirty || m_bDirty)
  {
    if (m_bReadDirty)
      ReadChanges();
    else if (m_bRowsDirty)
      BuildRows();

    m_bReadDirty = m_bRowsDirty = m_bDirty = false;
    Refresh();
    return;
  }

  // typing into the message only enables the commit button
  plStringBuilder sMessage = BoardText(board, "VC.Message");
  sMessage.Trim(" \t\r\n");

  // every tick is of a listed change: ReadChanges drops the others
  board.SetEntryValue("VC.CannotCommit", bBusy || m_Ticked.IsEmpty() || sMessage.IsEmpty() || m_bBuiltLockHintBlocks);
}

// Input.

void plDiversionChangesPanel::OnUiEvent(const plMsgUiEvent& event)
{
  const plStringView sId = event.m_sIdentifier.GetView();
  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();

  if (sId == "VC.tick" && event.m_iItemIndex >= 0 && event.m_iItemIndex < static_cast<plInt32>(m_Rows.GetCount()))
  {
    const Row& row = m_Rows[event.m_iItemIndex];
    if (row.m_Kind == RowKind::Change)
    {
      const plString& sPath = m_Entries[row.m_uiIndex].m_sPath;
      if (m_Ticked.Contains(sPath))
        m_Ticked.Remove(sPath);
      else
        m_Ticked.Insert(sPath);
    }
    else if (row.m_Kind == RowKind::Folder)
    {
      ToggleFolderTicks(row.m_uiIndex);
    }
  }
  else if (sId == "VC.tickall")
  {
    // the project's changes only (D13), and only those the filter shows (D42)
    ToggleFolderTicks(plDiversionChangeTree::ProjectRoot);
  }
  else if (sId == "VC.tree" || sId == "VC.list")
  {
    m_bTreeView = sId == "VC.tree";
    plEditorUi::SaveSetting(s_szViewSetting, m_bTreeView ? "tree" : "list");
    m_bRowsDirty = true;
  }
  else if (sId == "VC.keeplocks")
  {
    m_bKeepLocks = !m_bKeepLocks;
  }
  else if (sId == "VC.adddeps")
  {
    plDynamicArray<plUInt32> missing;
    plString sFirstUser;
    FindMissingDependencies(missing, sFirstUser);
    for (plUInt32 i : missing)
      m_Ticked.Insert(m_Entries[i].m_sPath);
  }
  else if (sId == "VC.refresh")
  {
    if (pProvider != nullptr)
      pProvider->Refresh();
  }
  else if (sId == "VC.open")
  {
    if (pProvider != nullptr)
      pProvider->OpenInDiversion();
  }
  else if (sId == "VC.commit")
  {
    Commit();
  }
  else if (sId == "VC.undo")
  {
    plDynamicArray<plUInt32> ticked;
    GetTicked(ticked);
    Undo(ticked, ticked.GetCount() == 1 ? plPathUtils::GetFileNameAndExtension(m_Entries[ticked[0]].m_sPath) : plStringView("the ticked changes"));
  }
  else if (sId == "VC.shelve")
  {
    plDynamicArray<plUInt32> ticked;
    GetTicked(ticked);
    Shelve(ticked);
  }
  else if (sId == "VC.apply" && event.m_iItemIndex >= 0)
  {
    ApplyShelf(static_cast<plUInt32>(event.m_iItemIndex));
  }
  else if (sId == "VC.dropshelf" && event.m_iItemIndex >= 0)
  {
    DeleteShelf(static_cast<plUInt32>(event.m_iItemIndex));
  }
  else
  {
    return;
  }

  m_bDirty = true;
}

void plDiversionChangesPanel::OnPointer(const plEditorUiPointerEvent& e)
{
  if (e.m_sElement != "VC.row" || e.m_iItemIndex < 0 || e.m_iItemIndex >= static_cast<plInt32>(m_Rows.GetCount()))
    return;

  const plUInt32 uiRow = static_cast<plUInt32>(e.m_iItemIndex);
  const Row row = m_Rows[uiRow];
  const bool bLeft = e.m_Type == plEditorUiPointerEvent::Type::Press && e.m_Button == plEditorUiPointerEvent::Button::Left;
  const bool bRight = e.m_Type == plEditorUiPointerEvent::Type::Press && e.m_Button == plEditorUiPointerEvent::Button::Right;

  switch (row.m_Kind)
  {
    case RowKind::Elsewhere:
      if (bLeft)
      {
        m_bShowElsewhere = !m_bShowElsewhere;
        m_bRowsDirty = true;
      }
      break;

    case RowKind::More:
      if (bLeft)
      {
        // the next page of the folder's changes (D41)
        const plString& sKey = m_Tree.GetFolder(row.m_uiIndex).m_sKey;
        const plUInt32* pShown = m_ShownChanges.GetValue(sKey);
        m_ShownChanges[sKey] = (pShown != nullptr ? *pShown : s_uiPageSize) + s_uiPageSize;
        m_bRowsDirty = true;
      }
      break;

    case RowKind::Folder:
      if (bLeft)
      {
        // a click opens or closes the folder; the disclosure is the row (D38)
        m_sSelected = GetRowIdentity(row);
        if (m_sFilter.IsEmpty())
          ToggleOpen(row.m_uiIndex);
        m_bRowsDirty = true;
      }
      else if (bRight)
      {
        m_sSelected = GetRowIdentity(row);
        m_bDirty = true;
        ShowFolderMenu(row.m_uiIndex, e.m_vPosition);
      }
      break;

    case RowKind::Change:
      if (bLeft)
      {
        m_sSelected = GetRowIdentity(row);
        m_bDirty = true;
      }
      else if (bRight)
      {
        m_sSelected = GetRowIdentity(row);
        m_bDirty = true;
        ShowRowMenu(uiRow, e.m_vPosition);
      }
      else if (e.m_Type == plEditorUiPointerEvent::Type::DoubleClick)
      {
        OpenEntry(row.m_uiIndex);
      }
      break;
  }
}

void plDiversionChangesPanel::ShowRowMenu(plUInt32 uiRow, const plVec2& vPosition)
{
  const plUInt32 uiEntry = m_Rows[uiRow].m_uiIndex;
  const bool bExists = m_Entries[uiEntry].m_Kind != plDiversionChangeKind::Deleted;

  plEditorUiMenu menu;
  menu.AddAction("Open", MenuOpen).m_bEnabled = bExists;
  menu.AddAction("Show in Asset Browser", MenuShow).m_bEnabled = bExists && !m_Entries[uiEntry].m_bElsewhere;
  menu.AddSeparator();
  menu.AddAction("Undo Changes...", MenuUndo).m_bDanger = true;
  menu.AddAction("Shelve...", MenuShelve);
  menu.AddSeparator();
  menu.AddAction("Open in Diversion", MenuOpenInDiversion);

  plEditorUiMenuView::Show(*GetSurface(), vPosition, menu, [this, uiEntry](plUInt32 uiId)
    {
      if (uiEntry >= m_Entries.GetCount())
        return;

      // the menu acts on the ticked changes when the row is one of them, else on the row alone
      plDynamicArray<plUInt32> entries;
      if (m_Ticked.Contains(m_Entries[uiEntry].m_sPath))
        GetTicked(entries);
      else
        entries.PushBack(uiEntry);

      switch (uiId)
      {
        case MenuOpen:
          OpenEntry(uiEntry);
          break;
        case MenuShow:
          if (plEditorUiEngineShell* pShell = plEditorUiEngineShell::GetSingleton(); pShell != nullptr && pShell->GetAssetBrowser() != nullptr)
            pShell->GetAssetBrowser()->SetSelectedFile(m_Entries[uiEntry].m_sAbsPath);
          break;
        case MenuUndo:
          Undo(entries, entries.GetCount() == 1 ? plPathUtils::GetFileNameAndExtension(m_Entries[entries[0]].m_sPath) : plStringView("the ticked changes"));
          break;
        case MenuShelve:
          Shelve(entries);
          break;
        case MenuOpenInDiversion:
          if (plDiversionProvider* pProvider = plDiversionProvider::GetSingleton())
            pProvider->OpenInDiversion();
          break;
      }

      m_bDirty = true; });
}

void plDiversionChangesPanel::ShowFolderMenu(plUInt32 uiFolder, const plVec2& vPosition)
{
  const plDiversionChangeTree::Folder& folder = m_Tree.GetFolder(uiFolder);

  plDynamicArray<plUInt32> below;
  m_Tree.GetItems(uiFolder, below);

  bool bAllTicked = !below.IsEmpty();
  for (plUInt32 i : below)
    bAllTicked &= m_Ticked.Contains(m_Entries[i].m_sPath);

  const bool bBusy = plDiversionProvider::GetSingleton() == nullptr || plDiversionProvider::GetSingleton()->IsBusy();

  plEditorUiMenu menu;
  menu.AddAction(bAllTicked ? "Untick All" : "Tick All", MenuTick, FormatCount(below.GetCount()));
  menu.AddSeparator();

  plEditorUiMenuItem& undo = menu.AddAction("Undo Changes in Folder...", MenuUndo);
  undo.m_bDanger = true;
  undo.m_bEnabled = !bBusy;
  menu.AddAction("Shelve Folder...", MenuShelve).m_bEnabled = !bBusy;
  menu.AddSeparator();
  menu.AddAction("Show in Asset Browser", MenuShow).m_bEnabled = ShowFolderInAssetBrowser(uiFolder, true);
  menu.AddAction("Add to .dvignore...", MenuIgnore);

  plEditorUiMenuView::Show(*GetSurface(), vPosition, menu, [this, uiFolder, sKey = folder.m_sKey](plUInt32 uiId)
    {
      // the rows may have been rebuilt meanwhile
      if (uiFolder >= m_Tree.GetFolders().GetCount() || m_Tree.GetFolder(uiFolder).m_sKey != sKey)
        return;

      plDynamicArray<plUInt32> entries;
      m_Tree.GetItems(uiFolder, entries);

      plStringBuilder sWhere = m_Tree.GetFolder(uiFolder).m_sName;
      sWhere.Append("/");

      switch (uiId)
      {
        case MenuTick:
          ToggleFolderTicks(uiFolder);
          break;
        case MenuUndo:
          Undo(entries, sWhere);
          break;
        case MenuShelve:
          Shelve(entries);
          break;
        case MenuShow:
          ShowFolderInAssetBrowser(uiFolder, false);
          break;
        case MenuIgnore:
          IgnoreFolder(uiFolder);
          break;
      }

      m_bDirty = true; });
}

bool plDiversionChangesPanel::ShowFolderInAssetBrowser(plUInt32 uiFolder, bool bOnlyCheck) const
{
  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  plEditorUiEngineShell* pShell = plEditorUiEngineShell::GetSingleton();
  if (pProvider == nullptr || pShell == nullptr || pShell->GetAssetBrowser() == nullptr)
    return false;

  // the Asset Browser shows the data directories only
  plStringBuilder sPath = pProvider->ToAbsolutePath(m_Tree.GetFolder(uiFolder).m_sPath);
  if (!plEditorApp::GetSingleton()->MakePathDataDirectoryRelative(sPath))
    return false;

  if (!bOnlyCheck)
  {
    sPath.Trim("/");
    pShell->GetAssetBrowser()->GetAssetBrowserFilter()->SetPathFilter(sPath);
  }

  return true;
}

void plDiversionChangesPanel::IgnoreFolder(plUInt32 uiFolder)
{
  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  const plDiversionSnapshot* pSnapshot = pProvider != nullptr ? pProvider->GetSnapshot() : nullptr;
  if (pSnapshot == nullptr || pSnapshot->m_State != plDiversionSnapshot::State::Connected)
    return;

  const plDiversionChangeTree::Folder& folder = m_Tree.GetFolder(uiFolder);

  plStringBuilder sIgnorePath = pSnapshot->m_Workspace.m_sRoot;
  sIgnorePath.AppendPath(".dvignore");

  plString sIgnoreFile;
  {
    plOSFile file;
    if (file.Open(sIgnorePath, plFileOpenMode::Read).Succeeded())
    {
      plDynamicArray<plUInt8> content;
      file.ReadAll(content);
      plStringBuilder sText;
      sText.Set(plStringView(reinterpret_cast<const char*>(content.GetData()), content.GetCount()));
      sIgnoreFile = sText;
    }
  }

  if (plDiversionIgnore::IsFolderIgnored(sIgnoreFile, folder.m_sPath))
  {
    SetResult(plFmt("{}/ is in .dvignore already.", folder.m_sName), false);
    return;
  }

  // what is in the repository keeps syncing: an ignore line only keeps new files out
  const plUInt32 uiTracked = folder.m_uiCount[static_cast<plUInt32>(plDiversionChangeKind::Modified)] + folder.m_uiCount[static_cast<plUInt32>(plDiversionChangeKind::Deleted)] + folder.m_uiCount[static_cast<plUInt32>(plDiversionChangeKind::Moved)];

  const plString sLine = plStringBuilder("/", folder.m_sPath, "/");

  plStringBuilder sText;
  sText.SetFormat("Add this line to the workspace's .dvignore?\n\n    {}\n\nNew files below it stop syncing.", sLine);
  if (uiTracked > 0)
    sText.AppendFormat(" {} of its changes are to files already in the repository. An ignore line does not take them out: they keep syncing until they are deleted from the repository, e.g. in the Diversion app.", FormatCount(uiTracked));

  plEditorUiMessageBoxDesc desc;
  desc.m_sTitle = "Add to .dvignore";
  desc.m_sText = sText;
  desc.m_Kind = plEditorUiMessageKind::Question;
  desc.m_fWidth = 480.0f;

  plEditorUiDialogButton& add = desc.m_Buttons.ExpandAndGetRef();
  add.m_sId = "add";
  add.m_sLabel = "Add to .dvignore";
  add.m_Role = plEditorUiDialogButton::Role::Accept;

  plEditorUiDialogButton& cancel = desc.m_Buttons.ExpandAndGetRef();
  cancel.m_sId = "cancel";
  cancel.m_sLabel = "Cancel";
  cancel.m_Role = plEditorUiDialogButton::Role::Reject;

  desc.m_sDefaultButton = "add";
  desc.m_sEscapeButton = "cancel";

  if (plEditorUiMessageBox::Show(plEditorUiServices::GetEngineDialogOwner(), desc).m_sButton != "add")
    return;

  plDynamicArray<plString> lines;
  lines.PushBack(sLine);
  const plString sNewFile = plDiversionIgnore::AppendLines(sIgnoreFile, lines, s_szIgnoreComment);

  plOSFile file;
  if (file.Open(sIgnorePath, plFileOpenMode::Write).Failed() || file.Write(sNewFile.GetData(), sNewFile.GetElementCount()).Failed())
  {
    SetResult(plFmt("Could not write '{}'.", sIgnorePath), true);
    return;
  }

  file.Close();
  SetResult(plFmt("Added {} to .dvignore.", sLine), false);
  pProvider->Refresh();
}

void plDiversionChangesPanel::OpenEntry(plUInt32 uiEntry)
{
  const Entry& entry = m_Entries[uiEntry];
  if (entry.m_Kind == plDiversionChangeKind::Deleted)
    return;

  plEditorApp::GetSingleton()->OpenDocumentQueued(entry.m_sAbsPath);
}

// Commands.

void plDiversionChangesPanel::GetTicked(plDynamicArray<plUInt32>& out_entries) const
{
  out_entries.Clear();

  for (plUInt32 i = 0; i < m_Entries.GetCount(); ++i)
  {
    if (m_Ticked.Contains(m_Entries[i].m_sPath))
      out_entries.PushBack(i);
  }
}

plDocument* plDiversionChangesPanel::FindOpenDocument(plStringView sAbsPath)
{
  const plString sKey = Key(sAbsPath);

  for (plDocumentManager* pManager : plDocumentManager::GetAllDocumentManagers())
  {
    for (plDocument* pDoc : pManager->GetAllOpenDocuments())
    {
      if (Key(pDoc->GetDocumentPath()) == sKey)
        return pDoc;
    }
  }

  return nullptr;
}

const plDiversionChangesPanel::FileIcon& plDiversionChangesPanel::GetFileIcon(plStringView sAbsPath)
{
  plStringBuilder sExtension = plPathUtils::GetFileExtension(sAbsPath);
  sExtension.ToLower();

  bool bExisted = false;
  FileIcon& icon = m_FileIcons.FindOrAdd(sExtension, &bExisted);
  if (bExisted)
    return icon;

  // by document type rather than through the asset curator: deleted files and files outside the data directories
  // get their type's icon too
  plStringView sIcon = ":/GuiFoundation/Icons/Document.svg";
  icon.m_Color = plEditorUi::Dim();

  const plDocumentTypeDescriptor* pType = nullptr;
  if (plDocumentManager::FindDocumentTypeFromPath(sAbsPath, false, pType).Succeeded() && pType != nullptr && !pType->m_sIcon.IsEmpty())
  {
    sIcon = pType->m_sIcon;
    if (pType->m_sAssetCategory.IsEmpty())
      icon.m_Color = plEditorUi::Muted();
    else
      icon.m_Color = plColorScheme::GetCategoryColor(pType->m_sAssetCategory, plColorScheme::CategoryColorUsage::OverlayIcon);
  }

  plEditorUi::SvgTexture(sIcon, &icon.m_sTexture);
  return icon;
}

void plDiversionChangesPanel::SetResult(const plFormatString& text, bool bError)
{
  plStringBuilder sText;
  m_sResult = text.GetText(sText);
  m_bResultIsError = bError;
  m_ResultUntil = plTime::Now() + ResultDuration;
  m_bDirty = true;
}

void plDiversionChangesPanel::Commit()
{
  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  if (pProvider == nullptr || pProvider->IsBusy())
    return;

  plDynamicArray<plUInt32> ticked;
  GetTicked(ticked);

  plStringBuilder sMessage = BoardText(GetSurface()->GetBlackboard(), "VC.Message");
  sMessage.Trim(" \t\r\n");

  if (ticked.IsEmpty() || sMessage.IsEmpty())
    return;

  // a commit takes the files on disk: unsaved edits are saved first, or left out, as the user says (D14)
  plHybridArray<plDocument*, 4> unsaved;
  for (plUInt32 i : ticked)
  {
    plDocument* pDoc = FindOpenDocument(m_Entries[i].m_sAbsPath);
    if (pDoc != nullptr && pDoc->IsModified())
      unsaved.PushBack(pDoc);
  }

  if (!unsaved.IsEmpty())
  {
    plStringBuilder sNames;
    for (const plDocument* pDoc : unsaved)
      sNames.Append("\n    ", plPathUtils::GetFileNameAndExtension(pDoc->GetDocumentPath()));

    plEditorUiMessageBoxDesc desc;
    desc.m_sTitle = "Unsaved edits";
    plStringBuilder sText;
    sText.SetFormat("{} edits that are not saved. A commit takes the files on disk.\n{}", unsaved.GetCount() == 1 ? "This document has" : "These documents have", sNames);
    desc.m_sText = sText;
    desc.m_Kind = plEditorUiMessageKind::Question;

    plEditorUiDialogButton& save = desc.m_Buttons.ExpandAndGetRef();
    save.m_sId = "save";
    save.m_sLabel = "Save and commit";
    save.m_Role = plEditorUiDialogButton::Role::Accept;

    plEditorUiDialogButton& disk = desc.m_Buttons.ExpandAndGetRef();
    disk.m_sId = "disk";
    disk.m_sLabel = "Commit the saved version";

    plEditorUiDialogButton& cancel = desc.m_Buttons.ExpandAndGetRef();
    cancel.m_sId = "cancel";
    cancel.m_sLabel = "Cancel";
    cancel.m_Role = plEditorUiDialogButton::Role::Reject;

    desc.m_sDefaultButton = "save";
    desc.m_sEscapeButton = "cancel";

    const plEditorUiMessageBoxResult result = plEditorUiMessageBox::Show(plEditorUiServices::GetEngineDialogOwner(), desc);
    if (result.m_sButton == "cancel" || result.m_sButton.IsEmpty())
      return;

    if (result.m_sButton == "save")
    {
      for (plDocument* pDoc : unsaved)
      {
        const plStatus status = pDoc->SaveDocument();
        if (status.Failed())
        {
          SetResult(plFmt("Not committed: saving {} failed.", plPathUtils::GetFileNameAndExtension(pDoc->GetDocumentPath())), true);
          return;
        }
      }
    }
  }

  // the project's checks for these files: a failed blocking check stops the commit, unless overridden with a reason that
  // goes into the commit message
  {
    plDynamicArray<plString> absolute;
    for (plUInt32 i : ticked)
      absolute.PushBack(m_Entries[i].m_sAbsPath);

    SetResult("Running the project checks...", false);
    const plStatus gate = plVersionControl::RunSubmitGate(absolute);
    if (gate.Failed())
    {
      plEditorUiMessageBoxDesc desc;
      desc.m_sTitle = "Project checks failed";
      desc.m_sText = plStringBuilder(gate.m_sMessage, "\n\nThe Validation panel lists every problem. Fix them there, or waive one with a reason; the reason goes into the commit message.");
      desc.m_Kind = plEditorUiMessageKind::Warning;

      plEditorUiDialogButton& cancel = desc.m_Buttons.ExpandAndGetRef();
      cancel.m_sId = "cancel";
      cancel.m_sLabel = "Cancel";
      cancel.m_Role = plEditorUiDialogButton::Role::Reject;

      plEditorUiDialogButton& open = desc.m_Buttons.ExpandAndGetRef();
      open.m_sId = "open";
      open.m_sLabel = "Open Validation";

      plEditorUiDialogButton& anyway = desc.m_Buttons.ExpandAndGetRef();
      anyway.m_sId = "anyway";
      anyway.m_sLabel = "Commit anyway";

      desc.m_sDefaultButton = "open";
      desc.m_sEscapeButton = "cancel";

      const plEditorUiMessageBoxResult result = plEditorUiMessageBox::Show(plEditorUiServices::GetEngineDialogOwner(), desc);
      if (result.m_sButton == "open")
      {
        if (plEditorUiEngineShell* pShell = plEditorUiEngineShell::GetSingleton())
          pShell->SetPanelOpen("Panel.Validation", true);
      }

      if (result.m_sButton != "anyway")
      {
        SetResult("Not committed: a blocking project check failed.", true);
        return;
      }

      // the override is part of the record
      plStringView sFirstLine = gate.m_sMessage.GetView();
      if (const char* pBreak = sFirstLine.FindSubString("\n"))
        sFirstLine = plStringView(pBreak + 1, sFirstLine.GetEndPointer());
      if (const char* pBreak = sFirstLine.FindSubString("\n"))
        sFirstLine = plStringView(sFirstLine.GetStartPointer(), pBreak);
      sMessage.Append("\n\nCommitted past failed project checks: ", sFirstLine);
    }
    else if (plProjectChecks* pChecks = plProjectChecks::GetSingleton())
    {
      // what ran and what was waived, for whoever reads the commit (D1)
      const plString sTrailer = pChecks->GetCommitTrailer();
      if (!sTrailer.IsEmpty())
        sMessage.Append("\n\n", sTrailer);

      SetResult(plFmt("{}", pChecks->GetLastGateSummary()), false);
    }
  }

  plDynamicArray<plString> paths;
  for (plUInt32 i : ticked)
    paths.PushBack(m_Entries[i].m_sPath);

  const plUInt32 uiCount = paths.GetCount();
  plString sBranch = pProvider->GetSnapshot() != nullptr ? pProvider->GetSnapshot()->m_Workspace.m_sBranchName : plString();

  pProvider->Commit(paths, sMessage, [this, alive = std::weak_ptr<bool>(m_pAlive), uiCount, sBranch, paths, bKeepLocks = m_bKeepLocks](bool bSucceeded, plStringView sResult)
    {
      // your locks on the committed files go, unless kept (D24), even when the panel closed meanwhile
      plDiversionProvider* pCommitProvider = plDiversionProvider::GetSingleton();
      if (bSucceeded && !bKeepLocks && pCommitProvider != nullptr)
        pCommitProvider->ReleaseOwnLocks(paths);

      if (alive.expired())
        return;

      if (!bSucceeded)
      {
        SetResult(plFmt("Not committed: {}", sResult), true);
        return;
      }

      // the waivers went into this commit's message
      if (plProjectChecks* pChecks = plProjectChecks::GetSingleton())
        pChecks->OnSubmitted();

      GetSurface()->GetBlackboard().SetEntryValue("VC.Message", plString());

      plStringBuilder sText;
      sText.SetFormat("Committed {} file{} to {} as {}.", uiCount, uiCount == 1 ? "" : "s", sBranch, sResult);
      SetResult(plFmt("{}", sText), false);
      plEditorUiServices::ShowAllDocumentsTemporaryStatusBarMessage(plFmt("{}", sText), plTime::MakeFromSeconds(6)); });
}

void plDiversionChangesPanel::Undo(const plDynamicArray<plUInt32>& entries, plStringView sWhere)
{
  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  if (pProvider == nullptr || pProvider->IsBusy() || entries.IsEmpty())
    return;

  plUInt32 uiNew = 0;
  plUInt32 uiModified = 0;
  plUInt32 uiDeleted = 0;
  plStringBuilder sFiles;
  for (plUInt32 n = 0; n < entries.GetCount(); ++n)
  {
    const Entry& entry = m_Entries[entries[n]];
    if (entry.m_Kind == plDiversionChangeKind::Added)
      ++uiNew;
    else if (entry.m_Kind == plDiversionChangeKind::Deleted)
      ++uiDeleted;
    else
      ++uiModified;

    if (n < MaxListedFiles)
      sFiles.AppendFormat("\n    {}  {}", KindLetter(entry.m_Kind), entry.m_sPath);
  }

  // a few files by name; a folder's worth by kind
  plStringBuilder sDetails;
  if (entries.GetCount() <= MaxListedFiles)
  {
    sDetails = sFiles;
  }
  else
  {
    if (uiModified > 0)
      sDetails.AppendFormat("\n    {}  modified: get the committed version back", FormatCount(uiModified));
    if (uiDeleted > 0)
      sDetails.AppendFormat("\n    {}  deleted: come back", FormatCount(uiDeleted));
    if (uiNew > 0)
      sDetails.AppendFormat("\n    {}  new: kept, as never committed, unless deleted below", FormatCount(uiNew));
  }

  plEditorUiMessageBoxDesc desc;
  desc.m_sTitle = "Undo changes";
  plStringBuilder sText;
  sText.SetFormat("{} change{} in {} go{} back to how {} in the last commit. Open documents among them reload.\n{}", FormatCount(entries.GetCount()), entries.GetCount() == 1 ? "" : "s", sWhere, entries.GetCount() == 1 ? "es" : "", entries.GetCount() == 1 ? "it is" : "they are", sDetails);
  desc.m_sText = sText;
  desc.m_Kind = plEditorUiMessageKind::Warning;
  desc.m_fWidth = 680.0f;

  // new files are never deleted unless asked (D15)
  if (uiNew > 0)
  {
    plStringBuilder sCheck;
    sCheck.SetFormat("Also delete the {} new file{}. They were never committed, so nothing can bring them back.", FormatCount(uiNew), uiNew == 1 ? "" : "s");
    desc.m_sCheckLabel = sCheck;
    desc.m_bChecked = false;
  }

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

  const bool bDeleteNew = uiNew > 0 && result.m_bChecked;

  plDynamicArray<plString> paths;
  for (plUInt32 i : entries)
  {
    if (m_Entries[i].m_Kind == plDiversionChangeKind::Added && !bDeleteNew)
      continue;

    paths.PushBack(m_Entries[i].m_sPath);
  }

  if (paths.IsEmpty())
    return;

  const plUInt32 uiKept = bDeleteNew ? 0 : uiNew;
  pProvider->Undo(paths, bDeleteNew, [this, alive = std::weak_ptr<bool>(m_pAlive), uiCount = paths.GetCount(), uiKept](bool bSucceeded, plStringView sResult)
    {
      if (alive.expired())
        return;

      if (!bSucceeded)
      {
        SetResult(plFmt("Not undone: {}", sResult), true);
        return;
      }

      if (uiKept > 0)
        SetResult(plFmt("Undid {} change{}; kept {} new file{}.", uiCount, uiCount == 1 ? "" : "s", uiKept, uiKept == 1 ? "" : "s"), false);
      else
        SetResult(plFmt("Undid {} change{}.", uiCount, uiCount == 1 ? "" : "s"), false); });
}

void plDiversionChangesPanel::Shelve(const plDynamicArray<plUInt32>& entries)
{
  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  if (pProvider == nullptr || pProvider->IsBusy() || entries.IsEmpty())
    return;

  plStringBuilder sDefaultName;
  sDefaultName.SetFormat("Shelf {}", m_Shelves.GetCount() + 1);
  plString sName = sDefaultName;

  plStringBuilder sLabel;
  sLabel.SetFormat("The {} change{} move{} to a shelf in the cloud and leave the workspace. Apply the shelf to get them back.", entries.GetCount(), entries.GetCount() == 1 ? "" : "s", entries.GetCount() == 1 ? "s" : "");

  if (!plEditorUiModalDialog::AskText(plEditorUiServices::GetEngineDialogOwner(), "Shelve changes", sLabel, sName))
    return;

  plStringBuilder sTrimmed = sName;
  sTrimmed.Trim(" \t");
  if (sTrimmed.IsEmpty())
    return;

  plDynamicArray<plString> paths;
  for (plUInt32 i : entries)
    paths.PushBack(m_Entries[i].m_sPath);

  const plString sShelf = sTrimmed;
  pProvider->Shelve(sShelf, paths, [this, alive = std::weak_ptr<bool>(m_pAlive), sShelf, uiCount = paths.GetCount()](bool bSucceeded, plStringView sResult)
    {
      if (alive.expired())
        return;

      if (!bSucceeded)
        SetResult(plFmt("Not shelved: {}", sResult), true);
      else
        SetResult(plFmt("Shelved {} change{} as '{}'.", uiCount, uiCount == 1 ? "" : "s", sShelf), false); });
}

void plDiversionChangesPanel::ApplyShelf(plUInt32 uiShelf)
{
  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  if (pProvider == nullptr || pProvider->IsBusy() || uiShelf >= m_Shelves.GetCount())
    return;

  const plString sName = m_Shelves[uiShelf].m_sName;
  pProvider->ApplyShelf(m_Shelves[uiShelf].m_sId, [this, alive = std::weak_ptr<bool>(m_pAlive), sName](bool bSucceeded, plStringView sResult)
    {
      if (alive.expired())
        return;

      if (!bSucceeded)
        SetResult(plFmt("Not applied: {}", sResult), true);
      else
        SetResult(plFmt("Applied '{}'; its changes arrive in a moment.", sName), false); });
}

void plDiversionChangesPanel::DeleteShelf(plUInt32 uiShelf)
{
  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  if (pProvider == nullptr || pProvider->IsBusy() || uiShelf >= m_Shelves.GetCount())
    return;

  const plString sName = m_Shelves[uiShelf].m_sName;

  plEditorUiMessageBoxDesc desc;
  desc.m_sTitle = "Delete shelf";
  plStringBuilder sText;
  sText.SetFormat("Delete the shelf '{}'? Its changes are gone for good.", sName);
  desc.m_sText = sText;
  desc.m_Kind = plEditorUiMessageKind::Warning;
  plEditorUiMessageBox::SetStandardButtons(plEditorUiMessageButton::Yes | plEditorUiMessageButton::No, plEditorUiMessageButton::No, desc);

  if (plEditorUiMessageBox::FromButtonId(plEditorUiMessageBox::Show(plEditorUiServices::GetEngineDialogOwner(), desc).m_sButton) != plEditorUiMessageButton::Yes)
    return;

  pProvider->DeleteShelf(m_Shelves[uiShelf].m_sId, [this, alive = std::weak_ptr<bool>(m_pAlive), sName](bool bSucceeded, plStringView sResult)
    {
      if (alive.expired())
        return;

      if (!bSucceeded)
        SetResult(plFmt("Not deleted: {}", sResult), true);
      else
        SetResult(plFmt("Deleted the shelf '{}'.", sName), false); });
}
