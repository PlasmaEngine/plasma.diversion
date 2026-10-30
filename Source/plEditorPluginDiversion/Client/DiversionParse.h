#pragma once

#include <plEditorPluginDiversion/Client/DiversionTypes.h>

#include <Foundation/Types/ArrayPtr.h>

/// Turns what the Diversion agent, its cloud API and the dv command line return into plain structs. Kept apart from
/// the transport so that recorded answers can be parsed in tests.
namespace plDiversionParse
{
  /// The agent's GET /workspace?abs_path= answer: workspaces keyed by id. Picks the one whose root contains
  /// sProjectDir most closely.
  plResult Workspace(plArrayPtr<const plUInt8> json, plStringView sProjectDir, plDiversionWorkspace& out_workspace);

  /// GET /repos/{repo}/workspaces/{ws}/status, from the agent or the cloud API.
  plResult Status(plArrayPtr<const plUInt8> json, plDiversionStatus& out_status);

  /// The agent's GET /repo/{repo}/workspace/{ws}/sync.
  plResult Sync(plArrayPtr<const plUInt8> json, plDiversionSyncState& out_sync);

  /// The agent's GET /repo/{repo}/workspace/{ws}/journal: a number that changes whenever the workspace does.
  plResult Journal(plArrayPtr<const plUInt8> json, plInt64& out_iJournal);

  /// The agent's GET /token/core: a bearer token for the cloud API and when it expires.
  plResult CoreToken(plArrayPtr<const plUInt8> json, plString& out_sToken, plTimestamp& out_expires);

  /// The cloud API's GET /repos/{repo}/workspaces/{ws}/other_statuses: who else changed which paths.
  plResult OtherStatuses(plArrayPtr<const plUInt8> json, plDiversionOtherChanges& out_changes);

  /// The cloud API's GET /repos/{repo}/branches, without deleted and draft branches.
  plResult Branches(plArrayPtr<const plUInt8> json, plDynamicArray<plDiversionBranch>& out_branches);

  /// The cloud API's GET /repos/{repo}/files/history/{ref}/{path}: the commits that changed the path, newest first.
  plResult FileHistory(plArrayPtr<const plUInt8> json, plDynamicArray<plDiversionCommit>& out_commits);

  /// The cloud API's GET /repos/{repo}/commits/{commit}.
  plResult Commit(plArrayPtr<const plUInt8> json, plDiversionCommit& out_commit);

  /// The files after "can't be brought up to date ... due to conflicts in the following files:" in `dv status`.
  void StatusConflicts(plStringView sText, plDynamicArray<plString>& out_paths);

  /// The cloud API's GET /repos/{repo}/locks: {"items": [{path, locked_by, locked_by_display_name, locked_at,
  /// lock_source}]}.
  plResult Locks(plArrayPtr<const plUInt8> json, plDynamicArray<plDiversionLock>& out_locks);

  /// The cloud API's GET /user: the signed-in user's id, name and plan.
  plResult User(plArrayPtr<const plUInt8> json, plDiversionUser& out_user);

  /// The owner's user id in the cloud API's GET /repos/{repo}.
  plResult RepoOwner(plArrayPtr<const plUInt8> json, plString& out_sOwnerId);

  /// sUserId's access mode ("OWNER", "ADMIN", "WRITE", "READ") in the cloud API's GET /repos/{repo}/collaborators;
  /// empty when the user is not listed.
  plResult AccessMode(plArrayPtr<const plUInt8> json, plStringView sUserId, plString& out_sAccessMode);

  /// Whether the plan of a user's account has hard locks: Studio and Enterprise do.
  plDiversionLockSupport LockSupportOfTier(plStringView sTier);

  /// Whether dv's output says the repository's plan has no hard locks ("Hard locks require a Studio or Enterprise
  /// subscription", HTTP 403).
  bool IsLockRefusedByPlan(plStringView sText);

  /// Whether `dv lock -d` said the path was not locked (it exits with 3 then).
  bool IsNotLocked(plStringView sText);

  /// The text of `dv diff --name-status --color never`: one "A|M|D|R<tab>path" line per change, renames as
  /// "R<tab>old -> new".
  void NameStatus(plStringView sText, plDiversionStatus& out_status);

  /// The text of `dv shelf`: "shelf <name> (<id>)" lines, each followed by "Created:" and "Branch:" lines.
  void Shelves(plStringView sText, plDynamicArray<plDiversionShelf>& out_shelves);

  /// The N of "Shelf <name> contains N change(s):" in `dv shelf show`, or -1.
  plInt32 ShelfChangeCount(plStringView sText);

  /// The id in "New commit ID: dv.commit.12" from `dv commit`, or empty.
  plString CommitId(plStringView sText);

  /// The version in `dv version` or the agent's /health ("v1.6.667" -> 1, 6, 667).
  plResult Version(plStringView sText, plUInt32& out_uiMajor, plUInt32& out_uiMinor, plUInt32& out_uiPatch);
} // namespace plDiversionParse
