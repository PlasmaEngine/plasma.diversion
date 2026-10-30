#include <plEditorPluginDiversion/EditorPluginDiversionPCH.h>

#include <plEditorPluginDiversion/Panels/ChangeTree.h>

namespace
{
  plString LowerCase(plStringView s)
  {
    plStringBuilder sLower = s;
    sLower.ToLower();
    return sLower;
  }

  /// "a/b/c.txt" -> "a/b", "c.txt" -> "".
  plStringView FolderPart(plStringView sPath)
  {
    const char* szSlash = sPath.FindLastSubString("/");
    return szSlash != nullptr ? plStringView(sPath.GetStartPointer(), szSlash) : plStringView();
  }

  plStringView NamePart(plStringView sPath)
  {
    const char* szSlash = sPath.FindLastSubString("/");
    return szSlash != nullptr ? plStringView(szSlash + 1, sPath.GetEndPointer()) : sPath;
  }

  plUInt32 KindIndex(plDiversionChangeKind kind)
  {
    return static_cast<plUInt32>(kind);
  }
} // namespace

void plDiversionChangeTree::Build(const plDynamicArray<Item>& items, plStringView sProjectPrefix, plStringView sFilter)
{
  m_Folders.Clear();
  m_FolderByKey.Clear();
  m_FlatProject.Clear();
  m_FlatElsewhere.Clear();
  m_ItemFolder.Clear();
  m_ItemFolder.SetCount(items.GetCount(), plInvalidIndex);

  Folder& project = m_Folders.ExpandAndGetRef();
  project.m_sPath = sProjectPrefix;
  project.m_sKey = LowerCase(sProjectPrefix);
  project.m_sName = NamePart(sProjectPrefix);

  m_Folders.ExpandAndGetRef();

  plStringBuilder sFilterLower = sFilter;
  sFilterLower.Trim(" \t");
  sFilterLower.ToLower();

  const plUInt32 uiPrefixLength = sProjectPrefix.IsEmpty() ? 0 : sProjectPrefix.GetElementCount() + 1;

  // first every shown item's folders, so that an item naming a folder can be told apart from a file
  for (plUInt32 i = 0; i < items.GetCount(); ++i)
  {
    const Item& item = items[i];
    plStringView sRelative = item.m_sPath;
    if (!item.m_bElsewhere && uiPrefixLength > 0 && sRelative.GetElementCount() > uiPrefixLength)
      sRelative = plStringView(sRelative.GetStartPointer() + uiPrefixLength, sRelative.GetEndPointer());

    if (!sFilterLower.IsEmpty() && LowerCase(sRelative).FindSubString(sFilterLower) == nullptr)
      continue;

    const plUInt32 uiRoot = item.m_bElsewhere ? ElsewhereRoot : ProjectRoot;
    const plStringView sFolder = FolderPart(sRelative);
    m_ItemFolder[i] = sFolder.IsEmpty() ? uiRoot : FindOrAddFolder(uiRoot, item.m_bElsewhere ? plStringView() : sProjectPrefix, sFolder);
  }

  for (plUInt32 i = 0; i < items.GetCount(); ++i)
  {
    if (m_ItemFolder[i] == plInvalidIndex)
      continue;

    const Item& item = items[i];

    // a folder Diversion lists as a change of its own is that folder's row
    if (const plUInt32* pOwn = m_FolderByKey.GetValue(LowerCase(item.m_sPath)))
    {
      m_ItemFolder[i] = *pOwn;
      m_Folders[*pOwn].m_iOwnItem = static_cast<plInt32>(i);
    }
    else
    {
      m_Folders[m_ItemFolder[i]].m_Items.PushBack(i);
    }

    (item.m_bElsewhere ? m_FlatElsewhere : m_FlatProject).PushBack(i);

    for (plUInt32 uiFolder = m_ItemFolder[i]; uiFolder != plInvalidIndex; uiFolder = m_Folders[uiFolder].m_uiParent)
    {
      ++m_Folders[uiFolder].m_uiCount[KindIndex(item.m_Kind)];
      ++m_Folders[uiFolder].m_uiTotal;
    }
  }

  // folders and changes by name, case aside
  for (Folder& folder : m_Folders)
  {
    folder.m_Folders.Sort([this](plUInt32 a, plUInt32 b)
      { return m_Folders[a].m_sName.Compare_NoCase(m_Folders[b].m_sName) < 0; });

    folder.m_Items.Sort([&](plUInt32 a, plUInt32 b)
      { return NamePart(items[a].m_sPath).Compare_NoCase(NamePart(items[b].m_sPath)) < 0; });
  }
}

plUInt32 plDiversionChangeTree::FindOrAddFolder(plUInt32 uiRoot, plStringView sRootPath, plStringView sRelativeFolder)
{
  plUInt32 uiFolder = uiRoot;
  plStringBuilder sPath = sRootPath;

  const char* szStart = sRelativeFolder.GetStartPointer();
  const char* szEnd = sRelativeFolder.GetEndPointer();

  while (szStart < szEnd)
  {
    const char* szSlash = szStart;
    while (szSlash < szEnd && *szSlash != '/')
      ++szSlash;

    const plStringView sName(szStart, szSlash);
    if (!sPath.IsEmpty())
      sPath.Append("/");
    sPath.Append(sName);

    const plString sKey = LowerCase(sPath);
    if (const plUInt32* pFolder = m_FolderByKey.GetValue(sKey))
    {
      uiFolder = *pFolder;
    }
    else
    {
      const plUInt32 uiNew = m_Folders.GetCount();
      Folder& folder = m_Folders.ExpandAndGetRef();
      folder.m_sName = sName;
      folder.m_sPath = sPath;
      folder.m_sKey = sKey;
      folder.m_uiParent = uiFolder;

      m_Folders[uiFolder].m_Folders.PushBack(uiNew);
      m_FolderByKey[sKey] = uiNew;
      uiFolder = uiNew;
    }

    szStart = szSlash < szEnd ? szSlash + 1 : szEnd;
  }

  return uiFolder;
}

void plDiversionChangeTree::GetItems(plUInt32 uiFolder, plDynamicArray<plUInt32>& out_items) const
{
  const Folder& folder = m_Folders[uiFolder];

  if (folder.m_iOwnItem >= 0)
    out_items.PushBack(static_cast<plUInt32>(folder.m_iOwnItem));

  out_items.PushBackRange(folder.m_Items);

  for (plUInt32 uiSub : folder.m_Folders)
    GetItems(uiSub, out_items);
}

void plDiversionChangeTree::CountTicked(plArrayPtr<const bool> ticked, plDynamicArray<plUInt32>& out_perFolder) const
{
  out_perFolder.Clear();
  out_perFolder.SetCount(m_Folders.GetCount(), 0);

  for (plUInt32 i = 0; i < ticked.GetCount() && i < m_ItemFolder.GetCount(); ++i)
  {
    if (!ticked[i])
      continue;

    for (plUInt32 uiFolder = m_ItemFolder[i]; uiFolder != plInvalidIndex; uiFolder = m_Folders[uiFolder].m_uiParent)
      ++out_perFolder[uiFolder];
  }
}

void plDiversionChangeTree::MakeRows(const View& view, plDynamicArray<Row>& out_rows) const
{
  out_rows.Clear();

  if (!view.m_bTree)
  {
    for (plUInt32 uiItem : m_FlatProject)
      out_rows.PushBack({RowKind::Change, uiItem, 0, 0});

    if (!m_FlatElsewhere.IsEmpty())
    {
      out_rows.PushBack({RowKind::Elsewhere, 0, 0, 0});

      if (view.m_bShowElsewhere)
      {
        for (plUInt32 uiItem : m_FlatElsewhere)
          out_rows.PushBack({RowKind::Change, uiItem, 0, 0});
      }
    }

    return;
  }

  AddFolderRows(view, ProjectRoot, 0, out_rows);

  if (m_Folders[ElsewhereRoot].m_uiTotal > 0)
  {
    out_rows.PushBack({RowKind::Elsewhere, 0, 0, 0});

    if (view.m_bShowElsewhere)
      AddFolderRows(view, ElsewhereRoot, 0, out_rows);
  }
}

void plDiversionChangeTree::AddFolderRows(const View& view, plUInt32 uiFolder, plUInt32 uiDepth, plDynamicArray<Row>& out_rows) const
{
  const Folder& folder = m_Folders[uiFolder];

  for (plUInt32 uiSub : folder.m_Folders)
  {
    out_rows.PushBack({RowKind::Folder, uiSub, uiDepth, 0});

    const bool bOpen = view.m_bAllOpen || (view.m_pOpen != nullptr && view.m_pOpen->Contains(m_Folders[uiSub].m_sKey));
    if (bOpen)
      AddFolderRows(view, uiSub, uiDepth + 1, out_rows);
  }

  // a folder with thousands of changes shows them a page at a time (D41); a filter shows every match
  plUInt32 uiShown = folder.m_Items.GetCount();
  if (!view.m_bAllOpen)
  {
    plUInt32 uiPages = view.m_uiPageSize;
    if (view.m_pShown != nullptr)
    {
      if (const plUInt32* pShown = view.m_pShown->GetValue(folder.m_sKey))
        uiPages = *pShown;
    }

    uiShown = plMath::Min(uiShown, uiPages);
  }

  for (plUInt32 i = 0; i < uiShown; ++i)
    out_rows.PushBack({RowKind::Change, folder.m_Items[i], uiDepth, 0});

  if (uiShown < folder.m_Items.GetCount())
    out_rows.PushBack({RowKind::More, uiFolder, uiDepth, folder.m_Items.GetCount() - uiShown});
}
