#pragma once

#include <plEditorPluginDiversion/Provider/DiversionProvider.h>

#include <EditorUi/Content.h>
#include <GameUiPlugin/Resources/UiDocumentResource.h>

/// The Version Control panel's Conflicts view: files where a teammate's commit and your changes meet, with Keep mine,
/// Keep theirs and Compare (D8). Both kinds Diversion has: a backup of your unsynced version (Name.dv-conflict.ext)
/// beside the incoming one, and synced edits that hold the workspace back until they are resolved.
class plDiversionConflictsPanel : public plEditorUiContent
{
public:
  plDiversionConflictsPanel();

  static constexpr const char* PanelId = "Panel.VersionControlConflicts";

  static void Register();
  static void Unregister();

protected:
  virtual void OnUiUpdate() override;
  virtual void OnUiEvent(const plMsgUiEvent& event) override;

private:
  void BuildDocument();
  plUiDocumentResourceHandle BuildRowTemplate();
  void Refresh();

  plDynamicArray<plDiversionProvider::Conflict> m_Conflicts;
  plUInt32 m_uiSeenVersion = 0xFFFFFFFF;
  bool m_bBuilt = false;
};
