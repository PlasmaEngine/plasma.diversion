#pragma once

#include <plEditorPluginDiversion/Client/DiversionTypes.h>
#include <plEditorPluginDiversion/Panels/ChangeTree.h>

#include <EditorUi/Content.h>
#include <Foundation/Containers/HashSet.h>
#include <Foundation/Containers/HashTable.h>
#include <GameUiPlugin/Resources/UiDocumentResource.h>

#include <memory>

class plDocument;
struct plEditorUiPointerEvent;

/// The Version Control panel's Changes view: the workspace's uncommitted changes, ticked for a commit, an undo or a
/// shelf, and the commit message, the commit button and the shelves of the current branch.
///
/// The changes show as a tree of folders, one row per folder level (D37), or as a flat list (D40). A folder's tick box
/// ticks every change below it, and its menu undoes, shelves or ignores the folder (D39). Folders start closed and stay
/// as opened across refreshes (D38); a folder shows its first 200 changes, then "Show N more" (D41). The filter keeps
/// the changes whose path matches and their folders (D42).
///
/// The project's changes come first; changes elsewhere in the workspace (a repository that holds more than the
/// project) sit in a collapsed group and are never ticked for the user (decision D13). A ticked asset that uses new,
/// unticked files gets a hint with one button to tick them (D18). Committing a document with unsaved edits asks whether
/// to save it first (D14); undoing never deletes new files unless asked to (D15).
///
/// A ticked file someone else holds a hard lock on blocks the commit, with a hint naming them; someone else's
/// uncommitted edits only warn. A successful commit releases your locks on the committed files unless "Keep my locks"
/// is ticked (D24).
class plDiversionChangesPanel : public plEditorUiContent
{
public:
  plDiversionChangesPanel();
  ~plDiversionChangesPanel();

  static constexpr const char* PanelId = "Panel.VersionControl";

  /// Adds the panel to the shell's application panels (bottom, tabbed with the Log, open by default, decision D17).
  static void Register();
  static void Unregister();

protected:
  virtual void OnUiUpdate() override;
  virtual void OnUiEvent(const plMsgUiEvent& event) override;

private:
  struct Entry
  {
    plString m_sPath;     ///< workspace-relative, forward slashes
    plString m_sKey;      ///< m_sPath, lower case
    plString m_sAbsPath;
    plString m_sPrevPath; ///< for moves
    plDiversionChangeKind m_Kind = plDiversionChangeKind::Modified;
    bool m_bElsewhere = false; ///< outside the project folder
    plString m_sOthers;        ///< who else changed it, for the tooltip; empty for nobody
    plString m_sEditing;       ///< the first person with uncommitted edits to it in another workspace
    plString m_sLockHolder;    ///< who holds its hard lock; empty for nobody
    bool m_bMyLock = false;    ///< you hold its lock
  };

  using Row = plDiversionChangeTree::Row;
  using RowKind = plDiversionChangeTree::RowKind;

  void BuildDocument();
  plUiDocumentResourceHandle BuildRowTemplate();

  /// The commit column's band with m_sBuiltLockHint; returns its height.
  float BuildLockHint(plUiElement* pParent);
  plUiDocumentResourceHandle BuildShelfTemplate();

  /// Re-reads the provider's snapshot into m_Entries, then the rows.
  void ReadChanges();

  /// Groups m_Entries by folder under the filter and flattens them into m_Rows by what is open.
  void BuildRows();

  /// Writes the blackboard: rows, counts, buttons.
  void Refresh();

  /// A folder row's blackboard values.
  void WriteFolderRow(const Row& row, plUInt32 uiTicked, float fWidth, plVariantDictionary& ref_data) const;

  /// New files a ticked asset uses that are not ticked themselves, as entry indices.
  void FindMissingDependencies(plDynamicArray<plUInt32>& out_entries, plString& out_sFirstUser) const;

  /// What the commit column says about locks on the ticked files: someone else's lock blocks the commit, someone
  /// else's edits only warn. Empty when there is nothing to say.
  plString GetLockHint(bool& out_bBlocked) const;

  /// The ticked files you hold a lock on, by name.
  plString GetTickedOwnLocks() const;

  void OnPointer(const plEditorUiPointerEvent& e);
  void ShowRowMenu(plUInt32 uiRow, const plVec2& vPosition);
  void ShowFolderMenu(plUInt32 uiFolder, const plVec2& vPosition);
  void OpenEntry(plUInt32 uiEntry);

  /// Ticks every shown change below the folder, or unticks them when all are ticked.
  void ToggleFolderTicks(plUInt32 uiFolder);

  /// Adds the folder's line to the workspace's .dvignore, after asking.
  void IgnoreFolder(plUInt32 uiFolder);

  /// Filters the Asset Browser to the folder; false when it is outside the data directories.
  bool ShowFolderInAssetBrowser(plUInt32 uiFolder, bool bOnlyCheck) const;

  /// Opens or closes a folder row.
  void ToggleOpen(plUInt32 uiFolder);

  /// What a row is, for keeping the selection across rebuilds: an entry's path, a folder's key with a trailing "/".
  plString GetRowIdentity(const Row& row) const;

  void GetTicked(plDynamicArray<plUInt32>& out_entries) const;
  void Commit();

  /// Asks, then undoes the entries; sWhere names them in the question ("TMP/", a file name, "the ticked changes").
  void Undo(const plDynamicArray<plUInt32>& entries, plStringView sWhere);
  void Shelve(const plDynamicArray<plUInt32>& entries);
  void ApplyShelf(plUInt32 uiShelf);
  void DeleteShelf(plUInt32 uiShelf);

  /// Shows a command's outcome under the commit button for a while.
  void SetResult(const plFormatString& text, bool bError);

  /// Command results arrive after the panel may have closed; callbacks hold this and check it.
  std::shared_ptr<bool> m_pAlive = std::make_shared<bool>(true);

  static plDocument* FindOpenDocument(plStringView sAbsPath);

  struct FileIcon
  {
    plString m_sTexture;
    plColorGammaUB m_Color;
  };

  /// The type icon of files with sAbsPath's extension, baked once per extension: a big change list asks for thousands.
  const FileIcon& GetFileIcon(plStringView sAbsPath);

  plHashTable<plString, FileIcon> m_FileIcons; ///< by lower-case extension

  plHashSet<plString> m_Ticked; ///< workspace-relative paths
  plDynamicArray<Entry> m_Entries;
  plDiversionChangeTree m_Tree;
  plDynamicArray<Row> m_Rows;
  plDynamicArray<plDiversionShelf> m_Shelves; ///< of the current branch
  plString m_sSelected;                       ///< GetRowIdentity of the selected row
  bool m_bShowElsewhere = false;

  bool m_bTreeView = true;                       ///< or the flat list (D40), remembered in the editor settings
  plString m_sFilter;                            ///< what the filter field held when the rows were built
  plHashSet<plString> m_OpenFolders;             ///< folder keys
  plHashTable<plString, plUInt32> m_ShownChanges; ///< folder key -> changes shown, when more than the first page
  float m_fBuiltWidth = 0.0f;                    ///< the list width the row names were fitted to

  plUInt32 m_uiSeenVersion = 0xFFFFFFFF;
  plUiDocumentResourceHandle m_hRowTemplate;
  bool m_bBuilt = false;
  bool m_bHintShown = false;
  bool m_bWasBusy = false;
  bool m_bReadDirty = true; ///< the snapshot changed: ReadChanges
  bool m_bRowsDirty = true; ///< the filter, the view or an open folder changed: BuildRows
  bool m_bDirty = true;     ///< ticks, selection or the commit column changed: Refresh

  // the commit column's lock band and keep-locks row are built in; they change the layout
  plString m_sBuiltLockHint;
  bool m_bBuiltLockHintBlocks = false;
  bool m_bBuiltKeepRow = false;
  bool m_bKeepLocks = false; ///< "Keep my locks": committing does not release them (D24)

  plString m_sResult;
  bool m_bResultIsError = false;
  plTime m_ResultUntil;

  plEventSubscriptionID m_PointerSubscription = 0;
};
