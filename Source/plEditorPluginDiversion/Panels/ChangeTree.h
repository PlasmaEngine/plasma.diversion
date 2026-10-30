#pragma once

#include <plEditorPluginDiversion/Client/DiversionTypes.h>

#include <Foundation/Containers/HashSet.h>
#include <Foundation/Containers/HashTable.h>

/// The Changes panel's changes grouped by folder, with counts per kind, and flattened into rows by what is open.
///
/// The project's changes hang below one root, changes elsewhere in the workspace below another. A folder Diversion lists
/// as a change of its own (a new or deleted folder) is that folder's row, not a row inside it. Depends on Foundation
/// only, so the tests compile it in.
class plDiversionChangeTree
{
public:
  struct Item
  {
    plString m_sPath; ///< workspace-relative, forward slashes
    plDiversionChangeKind m_Kind = plDiversionChangeKind::Modified;
    bool m_bElsewhere = false; ///< outside the project folder
  };

  struct Folder
  {
    plString m_sName;
    plString m_sPath; ///< workspace-relative; the project folder or "" for the roots
    plString m_sKey;  ///< m_sPath, lower case
    plUInt32 m_uiParent = plInvalidIndex;
    plDynamicArray<plUInt32> m_Folders; ///< subfolders, by name
    plDynamicArray<plUInt32> m_Items;   ///< changes directly inside, by name
    plInt32 m_iOwnItem = -1;            ///< the change for the folder itself
    plUInt32 m_uiCount[4] = {};         ///< changes below, by plDiversionChangeKind, the own one included
    plUInt32 m_uiTotal = 0;
  };

  static constexpr plUInt32 ProjectRoot = 0;
  static constexpr plUInt32 ElsewhereRoot = 1;

  /// Groups items whose project-relative path (workspace-relative for the elsewhere ones) contains sFilter, ignoring
  /// case. Item indices stay those of items; the flat list keeps their order.
  void Build(const plDynamicArray<Item>& items, plStringView sProjectPrefix, plStringView sFilter);

  const plDynamicArray<Folder>& GetFolders() const { return m_Folders; }
  const Folder& GetFolder(plUInt32 uiFolder) const { return m_Folders[uiFolder]; }

  /// Whether the filter let the item in.
  bool IsShown(plUInt32 uiItem) const { return m_ItemFolder[uiItem] != plInvalidIndex; }

  /// Every shown item below the folder, the folder's own change included.
  void GetItems(plUInt32 uiFolder, plDynamicArray<plUInt32>& out_items) const;

  /// For each folder, how many of the shown items below it are ticked.
  void CountTicked(plArrayPtr<const bool> ticked, plDynamicArray<plUInt32>& out_perFolder) const;

  enum class RowKind : plUInt8
  {
    Change,
    Folder,
    More,      ///< "Show N more" at the end of a folder's first changes
    Elsewhere, ///< the caption of the changes outside the project
  };

  struct Row
  {
    RowKind m_Kind = RowKind::Change;
    plUInt32 m_uiIndex = 0; ///< the item, or the folder (More: the folder whose changes go on)
    plUInt32 m_uiDepth = 0;
    plUInt32 m_uiHidden = 0; ///< More: how many changes it stands for
  };

  struct View
  {
    bool m_bTree = true;
    bool m_bAllOpen = false;                             ///< while filtering, every folder of a match is open
    bool m_bShowElsewhere = false;                       ///< the elsewhere caption is open
    const plHashSet<plString>* m_pOpen = nullptr;        ///< open folders by key
    const plHashTable<plString, plUInt32>* m_pShown = nullptr; ///< changes shown per folder key beyond the first page
    plUInt32 m_uiPageSize = 200;
  };

  /// The rows to show: the tree, or the flat list (project changes, then the elsewhere caption and its changes).
  void MakeRows(const View& view, plDynamicArray<Row>& out_rows) const;

private:
  plUInt32 FindOrAddFolder(plUInt32 uiRoot, plStringView sRootPath, plStringView sRelativeFolder);
  void AddFolderRows(const View& view, plUInt32 uiFolder, plUInt32 uiDepth, plDynamicArray<Row>& out_rows) const;

  plDynamicArray<Folder> m_Folders;
  plHashTable<plString, plUInt32> m_FolderByKey; ///< folder key -> index, roots excluded
  plDynamicArray<plUInt32> m_ItemFolder;         ///< item -> the folder it counts in (its own folder for an own change)
  plDynamicArray<plUInt32> m_FlatProject;   ///< shown project items in path order
  plDynamicArray<plUInt32> m_FlatElsewhere; ///< shown elsewhere items in path order
};
