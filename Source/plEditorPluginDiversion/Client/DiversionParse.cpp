#include <plEditorPluginDiversion/EditorPluginDiversionPCH.h>

#include <plEditorPluginDiversion/Client/DiversionParse.h>

#include <Foundation/IO/JSONReader.h>
#include <Foundation/IO/MemoryStream.h>
#include <Foundation/Strings/StringBuilder.h>

namespace
{
  plResult ReadJson(plArrayPtr<const plUInt8> json, plJSONReader& out_reader)
  {
    if (json.IsEmpty())
      return PL_FAILURE;

    plRawMemoryStreamReader stream(json.GetPtr(), json.GetCount());
    return out_reader.Parse(stream);
  }

  const plVariant* Find(const plVariantDictionary& dict, plStringView sKey)
  {
    return dict.GetValue(sKey);
  }

  plStringView GetString(const plVariantDictionary& dict, plStringView sKey)
  {
    const plVariant* pValue = Find(dict, sKey);
    if (pValue == nullptr || !pValue->IsA<plString>())
      return {};

    return pValue->Get<plString>().GetView();
  }

  double GetNumber(const plVariantDictionary& dict, plStringView sKey, double fDefault = 0.0)
  {
    const plVariant* pValue = Find(dict, sKey);
    if (pValue == nullptr || !pValue->IsNumber())
      return fDefault;

    return pValue->ConvertTo<double>();
  }

  bool GetBool(const plVariantDictionary& dict, plStringView sKey, bool bDefault = false)
  {
    const plVariant* pValue = Find(dict, sKey);
    if (pValue == nullptr || !pValue->IsA<bool>())
      return bDefault;

    return pValue->Get<bool>();
  }

  const plVariantDictionary* GetDict(const plVariant* pValue)
  {
    if (pValue == nullptr || !pValue->IsA<plVariantDictionary>())
      return nullptr;

    return &pValue->Get<plVariantDictionary>();
  }

  const plVariantArray* GetArray(const plVariantDictionary& dict, plStringView sKey)
  {
    const plVariant* pValue = Find(dict, sKey);
    if (pValue == nullptr || !pValue->IsA<plVariantArray>())
      return nullptr;

    return &pValue->Get<plVariantArray>();
  }

  /// Folder paths compared without regard to slashes or case, as Windows does.
  plString CleanFolder(plStringView sPath)
  {
    plStringBuilder s = sPath;
    s.MakeCleanPath();
    s.Trim(nullptr, "/");
    return s;
  }

  bool IsSameOrBelow(plStringView sPath, plStringView sFolder)
  {
    if (!sPath.StartsWith_NoCase(sFolder))
      return false;

    const plStringView sRest(sPath.GetStartPointer() + sFolder.GetElementCount(), sPath.GetEndPointer());
    return sRest.IsEmpty() || sRest.StartsWith("/");
  }

  void AddChanges(const plVariantDictionary& items, plStringView sKey, plDiversionChangeKind kind, plDiversionStatus& out_status)
  {
    const plVariantArray* pList = GetArray(items, sKey);
    if (pList == nullptr)
      return;

    for (const plVariant& entry : *pList)
    {
      const plVariantDictionary* pEntry = GetDict(&entry);
      if (pEntry == nullptr)
        continue;

      plDiversionChange& change = out_status.m_Changes.ExpandAndGetRef();
      change.m_sPath = GetString(*pEntry, "path");
      change.m_Kind = kind;

      // the agent reports a rename as a modification whose previous path differs
      const plStringView sPrev = GetString(*pEntry, "prev_path");
      if (kind == plDiversionChangeKind::Modified && !sPrev.IsEmpty() && sPrev != change.m_sPath)
      {
        change.m_Kind = plDiversionChangeKind::Moved;
        change.m_sPrevPath = sPrev;
      }
    }
  }
} // namespace

plResult plDiversionParse::Workspace(plArrayPtr<const plUInt8> json, plStringView sProjectDir, plDiversionWorkspace& out_workspace)
{
  out_workspace = {};

  plJSONReader reader;
  PL_SUCCEED_OR_RETURN(ReadJson(json, reader));

  const plString sProject = CleanFolder(sProjectDir);
  plUInt32 uiBestLength = 0;

  for (auto it : reader.GetTopLevelObject())
  {
    const plVariantDictionary* pWs = GetDict(&it.Value());
    if (pWs == nullptr)
      continue;

    const plString sRoot = CleanFolder(GetString(*pWs, "Path"));
    if (sRoot.IsEmpty() || !IsSameOrBelow(sProject, sRoot) || sRoot.GetElementCount() < uiBestLength)
      continue;

    uiBestLength = sRoot.GetElementCount();

    out_workspace.m_sWorkspaceId = GetString(*pWs, "WorkspaceID");
    out_workspace.m_sRepoId = GetString(*pWs, "RepoID");
    out_workspace.m_sRepoName = GetString(*pWs, "RepoName");
    out_workspace.m_sRoot = sRoot;
    out_workspace.m_sBranchId = GetString(*pWs, "BranchID");
    out_workspace.m_sBranchName = GetString(*pWs, "BranchName");
    out_workspace.m_sCommitId = GetString(*pWs, "CommitID");
    out_workspace.m_sTier = GetString(*pWs, "OrganizationTier");
    out_workspace.m_bPaused = GetBool(*pWs, "Paused");
  }

  return out_workspace.IsValid() ? PL_SUCCESS : PL_FAILURE;
}

plResult plDiversionParse::Status(plArrayPtr<const plUInt8> json, plDiversionStatus& out_status)
{
  out_status = {};

  plJSONReader reader;
  PL_SUCCEED_OR_RETURN(ReadJson(json, reader));

  const plVariantDictionary& top = reader.GetTopLevelObject();
  const plVariantDictionary* pItems = GetDict(Find(top, "items"));
  if (pItems == nullptr)
    return PL_FAILURE;

  AddChanges(*pItems, "new", plDiversionChangeKind::Added, out_status);
  AddChanges(*pItems, "modified", plDiversionChangeKind::Modified, out_status);
  AddChanges(*pItems, "deleted", plDiversionChangeKind::Deleted, out_status);

  out_status.m_bComplete = !GetBool(top, "incomplete_result");

  // null or missing when not calculated
  if (const plVariantArray* pConflicts = GetArray(top, "conflicts"))
  {
    for (const plVariant& path : *pConflicts)
    {
      if (path.IsA<plString>() && !path.Get<plString>().IsEmpty())
        out_status.m_Conflicts.PushBack(path.Get<plString>());
    }
  }

  return PL_SUCCESS;
}

plResult plDiversionParse::Sync(plArrayPtr<const plUInt8> json, plDiversionSyncState& out_sync)
{
  out_sync = {};

  plJSONReader reader;
  PL_SUCCEED_OR_RETURN(ReadJson(json, reader));

  const plVariantDictionary& top = reader.GetTopLevelObject();
  if (Find(top, "IsSyncComplete") == nullptr)
    return PL_FAILURE;

  out_sync.m_bComplete = GetBool(top, "IsSyncComplete", true);
  out_sync.m_bPaused = GetBool(top, "IsPaused");
  out_sync.m_sSyncedCommitId = GetString(top, "SyncedCommitID");
  out_sync.m_sSyncingToCommitId = GetString(top, "SyncingToCommitID");

  // while syncing: FileStats.Inbound.ItemsCount
  if (const plVariantDictionary* pStats = GetDict(Find(top, "FileStats")))
  {
    if (const plVariantDictionary* pInbound = GetDict(Find(*pStats, "Inbound")))
      out_sync.m_uiIncoming = static_cast<plUInt32>(plMath::Max(0.0, GetNumber(*pInbound, "ItemsCount")));
  }

  return PL_SUCCESS;
}

plResult plDiversionParse::Branches(plArrayPtr<const plUInt8> json, plDynamicArray<plDiversionBranch>& out_branches)
{
  out_branches.Clear();

  plJSONReader reader;
  PL_SUCCEED_OR_RETURN(ReadJson(json, reader));

  const plVariantArray* pItems = GetArray(reader.GetTopLevelObject(), "items");
  if (pItems == nullptr)
    return PL_FAILURE;

  for (const plVariant& item : *pItems)
  {
    const plVariantDictionary* pItem = GetDict(&item);
    if (pItem == nullptr || GetBool(*pItem, "is_deleted") || GetBool(*pItem, "is_draft"))
      continue;

    plDiversionBranch& branch = out_branches.ExpandAndGetRef();
    branch.m_sId = GetString(*pItem, "branch_id");
    branch.m_sName = GetString(*pItem, "branch_name");
    branch.m_sHeadCommitId = GetString(*pItem, "commit_id");
  }

  return PL_SUCCESS;
}

namespace
{
  void ReadCommit(const plVariantDictionary& commit, plDiversionCommit& out_commit)
  {
    out_commit.m_sId = GetString(commit, "commit_id");
    out_commit.m_sMessage = GetString(commit, "commit_message");
    out_commit.m_Time = plTimestamp::MakeFromInt(static_cast<plInt64>(GetNumber(commit, "created_ts")), plSIUnitOfTime::Second);

    if (const plVariantDictionary* pAuthor = GetDict(Find(commit, "author")))
    {
      out_commit.m_sAuthorId = GetString(*pAuthor, "id");
      out_commit.m_sAuthor = GetString(*pAuthor, "full_name");
      if (out_commit.m_sAuthor.IsEmpty())
        out_commit.m_sAuthor = GetString(*pAuthor, "name");
    }
  }
} // namespace

plResult plDiversionParse::FileHistory(plArrayPtr<const plUInt8> json, plDynamicArray<plDiversionCommit>& out_commits)
{
  out_commits.Clear();

  plJSONReader reader;
  PL_SUCCEED_OR_RETURN(ReadJson(json, reader));

  const plVariantArray* pEntries = GetArray(reader.GetTopLevelObject(), "entries");
  if (pEntries == nullptr)
    return PL_FAILURE;

  for (const plVariant& entry : *pEntries)
  {
    const plVariantDictionary* pEntry = GetDict(&entry);
    const plVariantDictionary* pCommit = pEntry != nullptr ? GetDict(Find(*pEntry, "commit")) : nullptr;
    if (pCommit != nullptr)
      ReadCommit(*pCommit, out_commits.ExpandAndGetRef());
  }

  return PL_SUCCESS;
}

plResult plDiversionParse::Commit(plArrayPtr<const plUInt8> json, plDiversionCommit& out_commit)
{
  out_commit = {};

  plJSONReader reader;
  PL_SUCCEED_OR_RETURN(ReadJson(json, reader));

  ReadCommit(reader.GetTopLevelObject(), out_commit);
  return out_commit.m_sId.IsEmpty() ? PL_FAILURE : PL_SUCCESS;
}

void plDiversionParse::StatusConflicts(plStringView sText, plDynamicArray<plString>& out_paths)
{
  out_paths.Clear();

  const char* pStart = sText.FindSubString("due to conflicts in the following files:");
  if (pStart == nullptr)
    return;

  plHybridArray<plStringView, 16> lines;
  plStringView(pStart, sText.GetEndPointer()).Split(false, lines, "\n");

  // the file lines are indented; "Please run `dv update` ..." ends them
  for (plUInt32 i = 1; i < lines.GetCount(); ++i)
  {
    plStringView sLine = lines[i];
    if (!sLine.StartsWith(" ") && !sLine.StartsWith("\t"))
      break;

    sLine.Trim(" \t\r");
    if (!sLine.IsEmpty())
      out_paths.PushBack(sLine);
  }
}

plResult plDiversionParse::Journal(plArrayPtr<const plUInt8> json, plInt64& out_iJournal)
{
  plJSONReader reader;
  PL_SUCCEED_OR_RETURN(ReadJson(json, reader));

  const plVariantDictionary& top = reader.GetTopLevelObject();
  if (Find(top, "JournalID") == nullptr)
    return PL_FAILURE;

  out_iJournal = static_cast<plInt64>(GetNumber(top, "JournalID"));
  return PL_SUCCESS;
}

plResult plDiversionParse::CoreToken(plArrayPtr<const plUInt8> json, plString& out_sToken, plTimestamp& out_expires)
{
  plJSONReader reader;
  PL_SUCCEED_OR_RETURN(ReadJson(json, reader));

  const plVariantDictionary& top = reader.GetTopLevelObject();
  out_sToken = GetString(top, "AccessToken");
  out_expires = plTimestamp::MakeFromInt(static_cast<plInt64>(GetNumber(top, "ExpiresAt")), plSIUnitOfTime::Second);

  return out_sToken.IsEmpty() ? PL_FAILURE : PL_SUCCESS;
}

plResult plDiversionParse::OtherStatuses(plArrayPtr<const plUInt8> json, plDiversionOtherChanges& out_changes)
{
  out_changes.Clear();

  plJSONReader reader;
  PL_SUCCEED_OR_RETURN(ReadJson(json, reader));

  // no "statuses" at all is a valid answer: nobody else changed anything
  const plVariantArray* pStatuses = GetArray(reader.GetTopLevelObject(), "statuses");
  if (pStatuses == nullptr)
    return PL_SUCCESS;

  for (const plVariant& entry : *pStatuses)
  {
    const plVariantDictionary* pEntry = GetDict(&entry);
    if (pEntry == nullptr)
      continue;

    const plVariantArray* pFileStatuses = GetArray(*pEntry, "file_statuses");
    const plStringView sPath = GetString(*pEntry, "path");
    if (pFileStatuses == nullptr || sPath.IsEmpty())
      continue;

    plDynamicArray<plDiversionOtherChange>& list = out_changes[sPath];

    for (const plVariant& status : *pFileStatuses)
    {
      const plVariantDictionary* pStatus = GetDict(&status);
      if (pStatus == nullptr)
        continue;

      plDiversionOtherChange& change = list.ExpandAndGetRef();
      change.m_sBranchName = GetString(*pStatus, "branch_name");
      change.m_sWorkspaceId = GetString(*pStatus, "workspace_id");
      change.m_Time = plTimestamp::MakeFromInt(static_cast<plInt64>(GetNumber(*pStatus, "mtime")), plSIUnitOfTime::Second);

      if (const plVariantDictionary* pAuthor = GetDict(Find(*pStatus, "author")))
      {
        change.m_sUser = GetString(*pAuthor, "full_name");

        if (change.m_sUser.IsEmpty())
          change.m_sUser = GetString(*pAuthor, "name");

        if (change.m_sUser.IsEmpty())
          change.m_sUser = GetString(*pAuthor, "email");
      }
    }
  }

  return PL_SUCCESS;
}

plResult plDiversionParse::Locks(plArrayPtr<const plUInt8> json, plDynamicArray<plDiversionLock>& out_locks)
{
  out_locks.Clear();

  plJSONReader reader;
  PL_SUCCEED_OR_RETURN(ReadJson(json, reader));

  const plVariantArray* pItems = GetArray(reader.GetTopLevelObject(), "items");
  if (pItems == nullptr)
    return PL_FAILURE;

  for (const plVariant& item : *pItems)
  {
    const plVariantDictionary* pItem = GetDict(&item);
    if (pItem == nullptr || GetString(*pItem, "path").IsEmpty())
      continue;

    plDiversionLock& lock = out_locks.ExpandAndGetRef();
    plStringBuilder sPath = GetString(*pItem, "path");
    sPath.MakeCleanPath();
    sPath.Trim("/", nullptr);
    lock.m_sPath = sPath;
    lock.m_sUserId = GetString(*pItem, "locked_by");
    lock.m_sUserName = GetString(*pItem, "locked_by_display_name");
    lock.m_Time = plTimestamp::MakeFromInt(static_cast<plInt64>(GetNumber(*pItem, "locked_at")), plSIUnitOfTime::Second);
    lock.m_bAuto = GetString(*pItem, "lock_source") == "auto";

    if (lock.m_sUserName.IsEmpty())
      lock.m_sUserName = lock.m_sUserId;
  }

  return PL_SUCCESS;
}

plResult plDiversionParse::User(plArrayPtr<const plUInt8> json, plDiversionUser& out_user)
{
  out_user = {};

  plJSONReader reader;
  PL_SUCCEED_OR_RETURN(ReadJson(json, reader));

  const plVariantDictionary& top = reader.GetTopLevelObject();
  out_user.m_sId = GetString(top, "id");
  out_user.m_sName = GetString(top, "full_name");
  out_user.m_sTier = GetString(top, "tier");

  if (out_user.m_sName.IsEmpty())
    out_user.m_sName = GetString(top, "name");

  return out_user.m_sId.IsEmpty() ? PL_FAILURE : PL_SUCCESS;
}

plResult plDiversionParse::RepoOwner(plArrayPtr<const plUInt8> json, plString& out_sOwnerId)
{
  out_sOwnerId.Clear();

  plJSONReader reader;
  PL_SUCCEED_OR_RETURN(ReadJson(json, reader));

  out_sOwnerId = GetString(reader.GetTopLevelObject(), "owner_user_id");
  return out_sOwnerId.IsEmpty() ? PL_FAILURE : PL_SUCCESS;
}

plResult plDiversionParse::AccessMode(plArrayPtr<const plUInt8> json, plStringView sUserId, plString& out_sAccessMode)
{
  out_sAccessMode.Clear();

  plJSONReader reader;
  PL_SUCCEED_OR_RETURN(ReadJson(json, reader));

  const plVariantArray* pCollaborators = GetArray(reader.GetTopLevelObject(), "collaborators");
  if (pCollaborators == nullptr)
    return PL_FAILURE;

  for (const plVariant& entry : *pCollaborators)
  {
    const plVariantDictionary* pEntry = GetDict(&entry);
    if (pEntry != nullptr && GetString(*pEntry, "user_id") == sUserId)
    {
      out_sAccessMode = GetString(*pEntry, "access_mode");
      break;
    }
  }

  return PL_SUCCESS;
}

plDiversionLockSupport plDiversionParse::LockSupportOfTier(plStringView sTier)
{
  if (sTier.IsEmpty() || sTier.IsEqual_NoCase("UNKNOWN"))
    return plDiversionLockSupport::Unknown;

  if (sTier.FindSubString_NoCase("STUDIO") != nullptr || sTier.FindSubString_NoCase("ENTERPRISE") != nullptr)
    return plDiversionLockSupport::Yes;

  return plDiversionLockSupport::No;
}

bool plDiversionParse::IsLockRefusedByPlan(plStringView sText)
{
  return sText.FindSubString_NoCase("Hard locks require") != nullptr;
}

bool plDiversionParse::IsNotLocked(plStringView sText)
{
  return sText.FindSubString_NoCase("is not locked") != nullptr;
}

void plDiversionParse::NameStatus(plStringView sText, plDiversionStatus& out_status)
{
  out_status = {};

  plHybridArray<plStringView, 64> lines;
  sText.Split(false, lines, "\n");

  for (plStringView sLine : lines)
  {
    sLine.Trim("\r");

    const char* szTab = sLine.FindSubString("\t");
    if (szTab == nullptr || szTab == sLine.GetStartPointer())
      continue;

    const plStringView sCode(sLine.GetStartPointer(), szTab);
    plStringView sPath(szTab + 1, sLine.GetEndPointer());

    plDiversionChange change;

    if (sCode == "A")
      change.m_Kind = plDiversionChangeKind::Added;
    else if (sCode == "M")
      change.m_Kind = plDiversionChangeKind::Modified;
    else if (sCode == "D")
      change.m_Kind = plDiversionChangeKind::Deleted;
    else if (sCode.StartsWith("R"))
      change.m_Kind = plDiversionChangeKind::Moved;
    else
      continue;

    if (change.m_Kind == plDiversionChangeKind::Moved)
    {
      const char* szArrow = sPath.FindSubString(" -> ");
      if (szArrow == nullptr)
        continue;

      change.m_sPrevPath = plStringView(sPath.GetStartPointer(), szArrow);
      sPath = plStringView(szArrow + 4, sPath.GetEndPointer());
    }

    change.m_sPath = sPath;
    out_status.m_Changes.PushBack(std::move(change));
  }
}

void plDiversionParse::Shelves(plStringView sText, plDynamicArray<plDiversionShelf>& out_shelves)
{
  out_shelves.Clear();

  plHybridArray<plStringView, 32> lines;
  sText.Split(false, lines, "\n");

  for (plStringView sLine : lines)
  {
    sLine.Trim(" \t\r");

    if (sLine.StartsWith("shelf "))
    {
      // the name may hold brackets itself ("Automatic shelf (10-20-2026 15:22:46)"), the id is in the last pair
      const char* szOpen = sLine.FindLastSubString(" (dv.shelf.");
      if (szOpen == nullptr || !sLine.EndsWith(")"))
        continue;

      plDiversionShelf& shelf = out_shelves.ExpandAndGetRef();
      shelf.m_sName = plStringView(sLine.GetStartPointer() + 6, szOpen);
      shelf.m_sId = plStringView(szOpen + 2, sLine.GetEndPointer() - 1);
    }
    else if (!out_shelves.IsEmpty() && sLine.StartsWith("Created:"))
    {
      plStringView sValue(sLine.GetStartPointer() + 8, sLine.GetEndPointer());
      sValue.Trim(" \t");
      out_shelves.PeekBack().m_sCreated = sValue;
    }
    else if (!out_shelves.IsEmpty() && sLine.StartsWith("Branch:"))
    {
      plStringView sValue(sLine.GetStartPointer() + 7, sLine.GetEndPointer());
      sValue.Trim(" \t");
      out_shelves.PeekBack().m_sBranch = sValue;
    }
  }
}

plInt32 plDiversionParse::ShelfChangeCount(plStringView sText)
{
  const char* szContains = sText.FindSubString(" contains ");
  if (szContains == nullptr)
    return -1;

  plInt32 iCount = 0;
  bool bDigits = false;
  for (const char* sz = szContains + 10; sz < sText.GetEndPointer() && *sz >= '0' && *sz <= '9'; ++sz)
  {
    iCount = iCount * 10 + (*sz - '0');
    bDigits = true;
  }

  return bDigits ? iCount : -1;
}

plString plDiversionParse::CommitId(plStringView sText)
{
  const char* szId = sText.FindSubString("dv.commit.");
  if (szId == nullptr)
    return {};

  const char* szEnd = szId + 10;
  while (szEnd < sText.GetEndPointer() && *szEnd >= '0' && *szEnd <= '9')
    ++szEnd;

  return plStringView(szId, szEnd);
}

plResult plDiversionParse::Version(plStringView sText, plUInt32& out_uiMajor, plUInt32& out_uiMinor, plUInt32& out_uiPatch)
{
  // the first "N.N.N" in the text, so "v1.6.667", "v1.6.667 (0356d3e2)" and a JSON answer all work
  const char* szPos = sText.GetStartPointer();
  const char* szEnd = sText.GetEndPointer();

  while (szPos < szEnd && (*szPos < '0' || *szPos > '9'))
    ++szPos;

  plUInt32* pOut[3] = {&out_uiMajor, &out_uiMinor, &out_uiPatch};
  for (plUInt32 i = 0; i < 3; ++i)
  {
    if (szPos >= szEnd || *szPos < '0' || *szPos > '9')
      return PL_FAILURE;

    plUInt32 uiValue = 0;
    while (szPos < szEnd && *szPos >= '0' && *szPos <= '9')
    {
      uiValue = uiValue * 10 + static_cast<plUInt32>(*szPos - '0');
      ++szPos;
    }

    *pOut[i] = uiValue;

    if (i < 2)
    {
      if (szPos >= szEnd || *szPos != '.')
        return PL_FAILURE;

      ++szPos;
    }
  }

  return PL_SUCCESS;
}
