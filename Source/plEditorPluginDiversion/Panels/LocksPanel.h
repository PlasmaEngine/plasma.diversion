#pragma once

#include <EditorUi/Content.h>
#include <GameUiPlugin/Resources/UiDocumentResource.h>

/// The Version Control panel's Locks view: the project's hard locks, with Unlock for your own and Force unlock for
/// someone else's (owners and admins), and the files someone else is editing (soft locks), each with Open.
///
/// On a plan without hard locks a note says so in place of the hard locks (decision D19); soft locks work on every plan.
class plDiversionLocksPanel : public plEditorUiContent
{
public:
  plDiversionLocksPanel();

  static constexpr const char* PanelId = "Panel.VersionControlLocks";

  /// Adds the panel to the shell's application panels, tabbed with the Changes view.
  static void Register();
  static void Unregister();

protected:
  virtual void OnUiUpdate() override;
  virtual void OnUiEvent(const plMsgUiEvent& event) override;

private:
  enum class RowKind : plUInt8
  {
    Group,
    Lock,
    Editing,
    Empty,
  };

  struct Row
  {
    RowKind m_Kind = RowKind::Empty;
    plString m_sAbsPath;
    plString m_sTitle; ///< the group's title, the file's name or the empty line's text
    plString m_sLine;  ///< who and since when
    bool m_bMine = false;
  };

  void BuildDocument();
  plUiDocumentResourceHandle BuildRowTemplate();

  /// Re-reads the provider's locks and others' edits into m_Rows.
  void ReadRows();
  void Refresh();

  plDynamicArray<Row> m_Rows;
  plUInt32 m_uiSeenVersion = 0xFFFFFFFF;
  bool m_bBuiltWithNote = false; ///< the plan note's band is built in, not bound
  bool m_bBuilt = false;
};
