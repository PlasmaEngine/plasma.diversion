#pragma once

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Containers/HashTable.h>
#include <Foundation/Strings/String.h>
#include <Foundation/Time/Timestamp.h>

/// One Diversion workspace as the agent describes it.
struct plDiversionWorkspace
{
  plString m_sWorkspaceId;
  plString m_sRepoId;
  plString m_sRepoName;
  plString m_sRoot; ///< absolute folder, forward slashes
  plString m_sBranchId;
  plString m_sBranchName;
  plString m_sCommitId;
  plString m_sTier; ///< the organisation's plan, e.g. "INDIE"; may be empty
  bool m_bPaused = false;

  bool IsValid() const { return !m_sWorkspaceId.IsEmpty(); }
};

enum class plDiversionChangeKind : plUInt8
{
  Added,
  Modified,
  Deleted,
  Moved,
};

/// A local, uncommitted change. Paths are relative to the workspace root, with forward slashes.
struct plDiversionChange
{
  plString m_sPath;
  plString m_sPrevPath; ///< for Moved
  plDiversionChangeKind m_Kind = plDiversionChangeKind::Modified;
};

/// The workspace's sync with the cloud.
struct plDiversionSyncState
{
  bool m_bComplete = true;
  bool m_bPaused = false;
  plString m_sSyncedCommitId;
  plString m_sSyncingToCommitId;
  plUInt32 m_uiIncoming = 0; ///< files still to download while syncing; 0 when the agent does not say
};

/// A branch of the repository.
struct plDiversionBranch
{
  plString m_sId;   ///< dv.branch.<n>
  plString m_sName; ///< "main", "art/lighting"
  plString m_sHeadCommitId;
};

/// A commit, as history and incoming changes show it.
struct plDiversionCommit
{
  plString m_sId; ///< dv.commit.<n>
  plString m_sMessage;
  plString m_sAuthorId;
  plString m_sAuthor; ///< as people see the name
  plTimestamp m_Time;
};

/// Someone else's change to a path: uncommitted edits in another workspace, or a commit on another branch.
struct plDiversionOtherChange
{
  plString m_sUser;
  plString m_sBranchName;
  plString m_sWorkspaceId; ///< empty for a commit on another branch
  plTimestamp m_Time;
};

/// All local changes; m_bComplete is false when the agent cut the list short.
struct plDiversionStatus
{
  plDynamicArray<plDiversionChange> m_Changes;
  bool m_bComplete = true;

  /// Files whose synced changes clash with incoming commits: the workspace stays behind until they are resolved.
  plDynamicArray<plString> m_Conflicts;
};

using plDiversionOtherChanges = plHashTable<plString, plDynamicArray<plDiversionOtherChange>>;

/// A hard lock: only its holder can commit the path.
struct plDiversionLock
{
  plString m_sPath; ///< workspace-relative, forward slashes
  plString m_sUserId;
  plString m_sUserName; ///< as people see it
  plTimestamp m_Time;
  bool m_bAuto = false; ///< taken by a repository rule (auto-lock), not by hand
};

/// Who the signed-in Diversion user is.
struct plDiversionUser
{
  plString m_sId;
  plString m_sName;
  plString m_sTier; ///< the account's plan, e.g. "INDIE"
};

/// Whether the repository's plan has hard locks.
enum class plDiversionLockSupport : plUInt8
{
  Unknown, ///< nobody tried yet in someone else's repository; lock actions show
  Yes,
  No, ///< Indie or Pro: lock actions are hidden (decision D19)
};

/// Changes put aside in the cloud, off the workspace, until applied.
struct plDiversionShelf
{
  plString m_sId; ///< dv.shelf.<uuid>
  plString m_sName;
  plString m_sCreated; ///< as dv prints it
  plString m_sBranch;
  plInt32 m_iChangeCount = -1; ///< -1 while not known
};
