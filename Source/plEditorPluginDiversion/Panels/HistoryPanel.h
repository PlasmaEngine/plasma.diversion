#pragma once

#include <plEditorPluginDiversion/Client/DiversionTypes.h>

#include <EditorUi/Content.h>
#include <GameUiPlugin/Resources/UiDocumentResource.h>

#include <memory>

/// The Version Control panel's History view: the commits that changed a file on the current branch, newest first,
/// each with Compare with yours (the old version read-only beside the document, D8) and Restore (a change to commit,
/// D27).
///
/// The file is the one the Asset Browser's History asked for, or else the active document.
class plDiversionHistoryPanel : public plEditorUiContent
{
public:
  plDiversionHistoryPanel();

  static constexpr const char* PanelId = "Panel.VersionControlHistory";

  static void Register();
  static void Unregister();

  /// Opens the panel on a file's history.
  static void ShowFor(plStringView sAbsolutePath);

protected:
  virtual void OnUiUpdate() override;
  virtual void OnUiEvent(const plMsgUiEvent& event) override;

private:
  void BuildDocument();
  plUiDocumentResourceHandle BuildRowTemplate();
  void Refresh();

  /// The file whose history to show: one asked for, or the active document; empty for none.
  plString FindTarget();
  void RequestHistory();

  plString m_sTarget;
  plString m_sSyncedCommit; ///< the workspace's commit and its branch's newest when the history was asked for
  plString m_sPinned;      ///< asked for through ShowFor
  plString m_sActiveAtPin; ///< the active document then; another one ends the pin
  plDynamicArray<plDiversionCommit> m_Commits;
  plString m_sError;
  bool m_bLoading = false;
  bool m_bBuilt = false;
  bool m_bDirty = true;

  std::shared_ptr<bool> m_pAlive = std::make_shared<bool>(true);

  static plString s_sRequestedTarget;
};
