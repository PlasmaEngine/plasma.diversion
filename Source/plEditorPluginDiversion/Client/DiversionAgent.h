#pragma once

#include <plEditorPluginDiversion/Client/DiversionTypes.h>

#include <Foundation/Strings/StringBuilder.h>

/// Talks to the Diversion agent that runs on this machine, and through it to Diversion's cloud API.
///
/// The agent serves a small HTTP API on 127.0.0.1 at the port it writes to ~/.diversion/.port. It answers workspace,
/// status and sync queries, and hands out a short-lived token for the cloud API, which is where "who else is editing"
/// lives. Sign-in stays with the Diversion app; the editor never stores a credential.
///
/// Blocking calls; one instance per thread.
class plDiversionAgent
{
public:
  enum class Result : plUInt8
  {
    Ok,
    NotAvailable, ///< the agent answered, but has nothing for this workspace right now (e.g. sync is paused)
    Failed,       ///< no answer, or one that did not parse; GetLastError() says why
  };

  /// ~/.diversion, with forward slashes.
  static plString GetDiversionFolder();

  /// Reads the port file and checks the agent answers. Fails when the agent is not running.
  plResult Connect();

  bool IsConnected() const { return !m_sBaseUrl.IsEmpty(); }
  void Disconnect();

  /// The agent's version, e.g. "1.6.667".
  plStringView GetVersion() const { return m_sVersion; }

  Result GetWorkspace(plStringView sProjectDir, plDiversionWorkspace& out_workspace);
  Result GetSync(const plDiversionWorkspace& workspace, plDiversionSyncState& out_sync);
  Result GetJournal(const plDiversionWorkspace& workspace, plInt64& out_iJournal);
  Result GetStatus(const plDiversionWorkspace& workspace, plDiversionStatus& out_status);

  /// Who else changed paths under sPathPrefix (workspace-relative, may be empty for all), from the cloud API.
  Result GetOtherChanges(const plDiversionWorkspace& workspace, plStringView sPathPrefix, plDiversionOtherChanges& out_changes);

  /// The repository's hard locks, from the cloud API. Listing works on every plan.
  Result GetLocks(const plDiversionWorkspace& workspace, plDynamicArray<plDiversionLock>& out_locks);

  /// The repository's branches, from the cloud API.
  Result GetBranches(const plDiversionWorkspace& workspace, plDynamicArray<plDiversionBranch>& out_branches);

  /// The commits that changed a workspace-relative path on a ref (a branch, a commit), newest first, up to 50.
  Result GetFileHistory(const plDiversionWorkspace& workspace, plStringView sRef, plStringView sPath, plDynamicArray<plDiversionCommit>& out_commits);

  /// A file's bytes as of a ref (a commit, a branch).
  Result GetFileContent(const plDiversionWorkspace& workspace, plStringView sRef, plStringView sPath, plDynamicArray<plUInt8>& out_content);

  Result GetCommit(const plDiversionWorkspace& workspace, plStringView sCommitId, plDiversionCommit& out_commit);

  /// The signed-in user, from the cloud API.
  Result GetUser(plDiversionUser& out_user);

  /// Who owns the repository, and the user's access mode in it ("OWNER", "ADMIN", ...), from the cloud API.
  Result GetAccess(const plDiversionWorkspace& workspace, plStringView sUserId, plString& out_sOwnerId, plString& out_sAccessMode);

  plStringView GetLastError() const { return m_sLastError; }

private:
  Result Get(plStringView sUrl, bool bCloud, plDynamicArray<plUInt8>& out_body);
  Result GetCloud(plStringView sPath, plDynamicArray<plUInt8>& out_body);
  plResult RefreshToken();

  plString m_sBaseUrl;
  plString m_sVersion;
  plString m_sToken;
  plTimestamp m_TokenExpires;
  plStringBuilder m_sLastError;
};
