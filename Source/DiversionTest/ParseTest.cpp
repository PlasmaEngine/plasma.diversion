#include <DiversionTest/DiversionTestPCH.h>

// The parsers depend on Foundation only, so the test compiles them in rather than linking the editor plugin.
#include <plEditorPluginDiversion/Client/DiversionIgnore.cpp>
#include <plEditorPluginDiversion/Client/DiversionParse.cpp>
#include <plEditorPluginDiversion/Client/DiversionPaths.cpp>
#include <plEditorPluginDiversion/Panels/ChangeTree.cpp>

// The answers below were recorded from Diversion agent v1.6.667 and dv v1.6.667 on 2026-10-20 (Tests/Fixtures, with
// names replaced).

namespace
{
  plArrayPtr<const plUInt8> Bytes(plStringView sText)
  {
    return plArrayPtr<const plUInt8>(reinterpret_cast<const plUInt8*>(sText.GetStartPointer()), sText.GetElementCount());
  }

  const plDiversionChange* FindChange(const plDiversionStatus& status, plStringView sPath)
  {
    for (const plDiversionChange& change : status.m_Changes)
    {
      if (change.m_sPath == sPath)
        return &change;
    }

    return nullptr;
  }
} // namespace

PL_CREATE_SIMPLE_TEST_GROUP(Diversion);

PL_CREATE_SIMPLE_TEST(Diversion, AgentAnswers)
{
  PL_TEST_BLOCK(plTestBlock::Enabled, "Workspace lookup")
  {
    constexpr const char* szJson =
      R"({"dv.ws.34e4f8ed":{"WorkspaceID":"dv.ws.34e4f8ed","RepoID":"dv.repo.f963d1b9","Path":"E:\\PlasmaTech\\DvSpike\\ws1","AccountID":"Account_0001","BranchID":"dv.branch.1","BranchName":"main","CommitID":"dv.commit.3","Paused":false,"ShouldDownload":false,"RepoName":"plasma-dv-spike","ReadOnly":false,"DigestMethod":"sha1"}})";

    plDiversionWorkspace ws;
    PL_TEST_BOOL(plDiversionParse::Workspace(Bytes(szJson), "E:/PlasmaTech/DvSpike/ws1/Project", ws).Succeeded());
    PL_TEST_STRING(ws.m_sWorkspaceId, "dv.ws.34e4f8ed");
    PL_TEST_STRING(ws.m_sRepoId, "dv.repo.f963d1b9");
    PL_TEST_STRING(ws.m_sRoot, "E:/PlasmaTech/DvSpike/ws1");
    PL_TEST_STRING(ws.m_sBranchName, "main");
    PL_TEST_BOOL(!ws.m_bPaused);

    // a folder beside the workspace is not in it, even with the same prefix
    PL_TEST_BOOL(plDiversionParse::Workspace(Bytes(szJson), "E:/PlasmaTech/DvSpike/ws10", ws).Failed());

    // the agent's answer for a folder in no workspace
    PL_TEST_BOOL(plDiversionParse::Workspace(Bytes("{}"), "E:/Elsewhere", ws).Failed());
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Workspace status")
  {
    constexpr const char* szJson =
      R"({"changed_items_count":4,"changed_files_count":4,"incomplete_result":false,"items":{)"
      R"("new":[{"path":"Content/Materials/Arch_Stone_N.png","hash":"f6b1","status":2,"mode":33188,"mtime":"2026-10-20T14:25:32Z"}],)"
      R"("modified":[{"path":"Content/Scenes/Courtyard.plScene","prev_path":"Content/Scenes/Courtyard.plScene","status":3},)"
      R"({"path":"Content/Materials/Arch_Stone2.plMaterialAsset","prev_path":"Content/Materials/Arch_Stone.plMaterialAsset","status":3}],)"
      R"("deleted":[{"path":"Content/Materials/Arch_Stone_D.png","status":4}]},"journal_id":32})";

    plDiversionStatus status;
    PL_TEST_BOOL(plDiversionParse::Status(Bytes(szJson), status).Succeeded());
    PL_TEST_INT(status.m_Changes.GetCount(), 4);
    PL_TEST_BOOL(status.m_bComplete);

    const plDiversionChange* pAdded = FindChange(status, "Content/Materials/Arch_Stone_N.png");
    PL_TEST_BOOL(pAdded != nullptr && pAdded->m_Kind == plDiversionChangeKind::Added);

    // the same previous path is a plain modification
    const plDiversionChange* pModified = FindChange(status, "Content/Scenes/Courtyard.plScene");
    PL_TEST_BOOL(pModified != nullptr && pModified->m_Kind == plDiversionChangeKind::Modified);

    const plDiversionChange* pMoved = FindChange(status, "Content/Materials/Arch_Stone2.plMaterialAsset");
    PL_TEST_BOOL(pMoved != nullptr && pMoved->m_Kind == plDiversionChangeKind::Moved);
    PL_TEST_STRING(pMoved->m_sPrevPath, "Content/Materials/Arch_Stone.plMaterialAsset");

    const plDiversionChange* pDeleted = FindChange(status, "Content/Materials/Arch_Stone_D.png");
    PL_TEST_BOOL(pDeleted != nullptr && pDeleted->m_Kind == plDiversionChangeKind::Deleted);

    PL_TEST_BOOL(plDiversionParse::Status(Bytes(R"({"incomplete_result":true,"items":{"new":[],"modified":[],"deleted":[]}})"), status).Succeeded());
    PL_TEST_BOOL(!status.m_bComplete);

    PL_TEST_BOOL(plDiversionParse::Status(Bytes(""), status).Failed());
    PL_TEST_BOOL(plDiversionParse::Status(Bytes("404 page not found"), status).Failed());
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Sync, journal, token")
  {
    plDiversionSyncState sync;
    PL_TEST_BOOL(plDiversionParse::Sync(Bytes(R"({"IsSyncComplete":true,"LastAppliedChanges":{"ClientChange":{"path":"a","status":2}},"SyncedCommitID":"dv.commit.3"})"), sync).Succeeded());
    PL_TEST_BOOL(sync.m_bComplete);
    PL_TEST_BOOL(!sync.m_bPaused);
    PL_TEST_STRING(sync.m_sSyncedCommitId, "dv.commit.3");

    // what a paused workspace answers
    PL_TEST_BOOL(plDiversionParse::Sync(Bytes(R"({"IsSyncComplete":false,"IsPaused":true})"), sync).Succeeded());
    PL_TEST_BOOL(!sync.m_bComplete);
    PL_TEST_BOOL(sync.m_bPaused);

    plInt64 iJournal = 0;
    PL_TEST_BOOL(plDiversionParse::Journal(Bytes(R"({"JournalID":31})"), iJournal).Succeeded());
    PL_TEST_INT(iJournal, 31);
    PL_TEST_BOOL(plDiversionParse::Journal(Bytes(""), iJournal).Failed());

    plString sToken;
    plTimestamp expires;
    PL_TEST_BOOL(plDiversionParse::CoreToken(Bytes(R"({"AccessToken":"abc.def","ExpiresAt":1792509567})"), sToken, expires).Succeeded());
    PL_TEST_STRING(sToken, "abc.def");
    PL_TEST_INT(expires.GetInt64(plSIUnitOfTime::Second), 1792509567);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Other people's changes")
  {
    constexpr const char* szJson =
      R"({"statuses": [{"path": "Content/Materials/Arch_Stone_N.png", "file_statuses": [{"workspace_id": null, "commit_id": "dv.commit.4", "branch_id": "dv.branch.3", "branch_name": "art/lighting", "status": 2, "mtime": 1792506119, "author": {"id": "Account_0001", "name": "alex_0001", "full_name": "Alex Example", "email": "alex@example.com"}}]},)"
      R"( {"path": "Content/Scenes/Courtyard.plScene", "file_statuses": [{"workspace_id": "dv.ws.0481321c", "commit_id": "dv.commit.3", "branch_id": "dv.branch.1", "branch_name": "main", "status": 3, "mtime": 1792506205, "author": {"id": "Account_0002", "name": "sam_0002", "full_name": "", "email": "sam@example.com"}}]}]})";

    plDiversionOtherChanges changes;
    PL_TEST_BOOL(plDiversionParse::OtherStatuses(Bytes(szJson), changes).Succeeded());
    PL_TEST_INT(changes.GetCount(), 2);

    const plDynamicArray<plDiversionOtherChange>* pBranch = changes.GetValue("Content/Materials/Arch_Stone_N.png");
    PL_TEST_BOOL(pBranch != nullptr && pBranch->GetCount() == 1);
    PL_TEST_STRING((*pBranch)[0].m_sUser, "Alex Example");
    PL_TEST_STRING((*pBranch)[0].m_sBranchName, "art/lighting");
    PL_TEST_BOOL((*pBranch)[0].m_sWorkspaceId.IsEmpty());

    // uncommitted edits in another workspace; no full name falls back to the user name
    const plDynamicArray<plDiversionOtherChange>* pEditing = changes.GetValue("Content/Scenes/Courtyard.plScene");
    PL_TEST_BOOL(pEditing != nullptr && pEditing->GetCount() == 1);
    PL_TEST_STRING((*pEditing)[0].m_sUser, "sam_0002");
    PL_TEST_STRING((*pEditing)[0].m_sWorkspaceId, "dv.ws.0481321c");

    // nobody else changed anything
    PL_TEST_BOOL(plDiversionParse::OtherStatuses(Bytes(R"({"statuses":null})"), changes).Succeeded());
    PL_TEST_INT(changes.GetCount(), 0);
  }
}

PL_CREATE_SIMPLE_TEST(Diversion, CommandLine)
{
  PL_TEST_BLOCK(plTestBlock::Enabled, "dv diff --name-status")
  {
    constexpr const char* szText = "R\tContent/Materials/Arch_Stone.plMaterialAsset -> Content/Materials/Arch_Stone2.plMaterialAsset\r\n"
                                   "D\tContent/Materials/Arch_Stone_D.png\n"
                                   "M\tContent/Scenes/Courtyard.plScene\n"
                                   "A\tContent/Materials/Arch_Stone_N.png\n"
                                   "No changes detected\n";

    plDiversionStatus status;
    plDiversionParse::NameStatus(szText, status);
    PL_TEST_INT(status.m_Changes.GetCount(), 4);

    const plDiversionChange* pMoved = FindChange(status, "Content/Materials/Arch_Stone2.plMaterialAsset");
    PL_TEST_BOOL(pMoved != nullptr && pMoved->m_Kind == plDiversionChangeKind::Moved);
    PL_TEST_STRING(pMoved->m_sPrevPath, "Content/Materials/Arch_Stone.plMaterialAsset");

    PL_TEST_BOOL(FindChange(status, "Content/Materials/Arch_Stone_D.png")->m_Kind == plDiversionChangeKind::Deleted);
    PL_TEST_BOOL(FindChange(status, "Content/Scenes/Courtyard.plScene")->m_Kind == plDiversionChangeKind::Modified);
    PL_TEST_BOOL(FindChange(status, "Content/Materials/Arch_Stone_N.png")->m_Kind == plDiversionChangeKind::Added);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Versions")
  {
    plUInt32 a = 0, b = 0, c = 0;
    PL_TEST_BOOL(plDiversionParse::Version("v1.6.667", a, b, c).Succeeded());
    PL_TEST_BOOL(a == 1 && b == 6 && c == 667);

    PL_TEST_BOOL(plDiversionParse::Version("Diversion client: v1.6.567 (0356d3e2)", a, b, c).Succeeded());
    PL_TEST_BOOL(a == 1 && b == 6 && c == 567);

    PL_TEST_BOOL(plDiversionParse::Version(R"({"Version":"v1.6.667","SupportsAgentsMd":true})", a, b, c).Succeeded());
    PL_TEST_BOOL(c == 667);

    PL_TEST_BOOL(plDiversionParse::Version("no version here", a, b, c).Failed());
  }
}

PL_CREATE_SIMPLE_TEST(Diversion, Shelves)
{
  PL_TEST_BLOCK(plTestBlock::Enabled, "dv shelf")
  {
    constexpr const char* szText = "\r\n"
                                   "shelf Automatic shelf (10-20-2026 15:22:46) (dv.shelf.fddbd451-5d88-4e26-9f9b-9ef2c8d1aa7d)\r\n"
                                   "  Created: 10-20-2026 15:22:46\r\n"
                                   "  Branch:  main\r\n"
                                   "\r\n"
                                   "shelf Lighting WIP (dv.shelf.3658f8ce-7660-4ac9-be43-bf7ef65271e7)\r\n"
                                   "  Created: 10-20-2026 17:03:01\r\n"
                                   "  Branch:  art/lighting\r\n";

    plDynamicArray<plDiversionShelf> shelves;
    plDiversionParse::Shelves(szText, shelves);
    if (PL_TEST_INT(shelves.GetCount(), 2))
    {
      // the name keeps its own brackets; the id is the last pair
      PL_TEST_STRING(shelves[0].m_sName, "Automatic shelf (10-20-2026 15:22:46)");
      PL_TEST_STRING(shelves[0].m_sId, "dv.shelf.fddbd451-5d88-4e26-9f9b-9ef2c8d1aa7d");
      PL_TEST_STRING(shelves[0].m_sCreated, "10-20-2026 15:22:46");
      PL_TEST_STRING(shelves[0].m_sBranch, "main");
      PL_TEST_STRING(shelves[1].m_sName, "Lighting WIP");
      PL_TEST_STRING(shelves[1].m_sBranch, "art/lighting");
      PL_TEST_INT(shelves[1].m_iChangeCount, -1);
    }

    plDiversionParse::Shelves("No shelves found.\n", shelves);
    PL_TEST_INT(shelves.GetCount(), 0);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "dv shelf show, dv commit")
  {
    PL_TEST_INT(plDiversionParse::ShelfChangeCount("Shelf Automatic shelf (10-20-2026 15:22:46) contains 12 change(s):\n\nModified:\n\t x"), 12);
    PL_TEST_INT(plDiversionParse::ShelfChangeCount("Shelf X: 2 change(s)"), -1);

    PL_TEST_STRING(plDiversionParse::CommitId("Committing 3 changes...\nNew commit ID: dv.commit.12\n"), "dv.commit.12");
    PL_TEST_STRING(plDiversionParse::CommitId("Nothing to commit"), "");
  }
}

PL_CREATE_SIMPLE_TEST(Diversion, Locks)
{
  PL_TEST_BLOCK(plTestBlock::Enabled, "Lock list")
  {
    // the shape of Diversion's FileLockList (its Unity plugin's API model); an Indie repository can hold none, so this
    // one is written by hand
    constexpr const char* szJson =
      R"({"items":[{"path":"Game/Scenes/Lantern.plPrefab","locked_by":"Account_0002","locked_by_display_name":"Sam Rivera","locked_at":1792481520,"lock_source":"manual"},)"
      R"({"path":"/Game/Meshes/Arch.plMeshAsset","locked_by":"Account_0001","locked_by_display_name":"","locked_at":1792483500,"lock_source":"auto"}]})";

    plDynamicArray<plDiversionLock> locks;
    PL_TEST_BOOL(plDiversionParse::Locks(Bytes(szJson), locks).Succeeded());
    if (PL_TEST_INT(locks.GetCount(), 2))
    {
      PL_TEST_STRING(locks[0].m_sPath, "Game/Scenes/Lantern.plPrefab");
      PL_TEST_STRING(locks[0].m_sUserId, "Account_0002");
      PL_TEST_STRING(locks[0].m_sUserName, "Sam Rivera");
      PL_TEST_INT(locks[0].m_Time.GetInt64(plSIUnitOfTime::Second), 1792481520);
      PL_TEST_BOOL(!locks[0].m_bAuto);

      // a leading slash goes, a missing display name falls back to the id
      PL_TEST_STRING(locks[1].m_sPath, "Game/Meshes/Arch.plMeshAsset");
      PL_TEST_STRING(locks[1].m_sUserName, "Account_0001");
      PL_TEST_BOOL(locks[1].m_bAuto);
    }

    // recorded: an Indie repository's answer
    PL_TEST_BOOL(plDiversionParse::Locks(Bytes(R"({"items":[]})"), locks).Succeeded());
    PL_TEST_INT(locks.GetCount(), 0);

    PL_TEST_BOOL(plDiversionParse::Locks(Bytes(R"({"status":404,"detail":"No lock found for this path"})"), locks).Failed());
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "User, owner, access")
  {
    constexpr const char* szUser = R"({"id":"Account_0001","name":"Alex_5622caa8","email":"alex@example.com","image":"","full_name":"Alex Morgan","tier":"INDIE","education_eligible":false,"kyc_status":"skipped"})";

    plDiversionUser user;
    PL_TEST_BOOL(plDiversionParse::User(Bytes(szUser), user).Succeeded());
    PL_TEST_STRING(user.m_sId, "Account_0001");
    PL_TEST_STRING(user.m_sName, "Alex Morgan");
    PL_TEST_STRING(user.m_sTier, "INDIE");

    constexpr const char* szRepo = R"({"repo_id":"dv.repo.f963d1b9","repo_name":"plasma-dv-spike","description":"","size_bytes":0,"owner_user_id":"Account_0001","default_branch_name":"main"})";
    plString sOwner;
    PL_TEST_BOOL(plDiversionParse::RepoOwner(Bytes(szRepo), sOwner).Succeeded());
    PL_TEST_STRING(sOwner, "Account_0001");

    constexpr const char* szCollaborators = R"({"collaborators":[{"collaboration_id":143963,"user_id":"Account_0001","name":"Alex Morgan","email":"alex@example.com","image":"","access_mode":"OWNER"},)"
                                            R"({"collaboration_id":143964,"user_id":"Account_0002","name":"Sam Rivera","email":"sam@example.com","image":"","access_mode":"WRITE"}],"invites":[]})";
    plString sMode;
    PL_TEST_BOOL(plDiversionParse::AccessMode(Bytes(szCollaborators), "Account_0002", sMode).Succeeded());
    PL_TEST_STRING(sMode, "WRITE");
    PL_TEST_BOOL(plDiversionParse::AccessMode(Bytes(szCollaborators), "Account_0001", sMode).Succeeded());
    PL_TEST_STRING(sMode, "OWNER");
    PL_TEST_BOOL(plDiversionParse::AccessMode(Bytes(szCollaborators), "Account_0009", sMode).Succeeded());
    PL_TEST_STRING(sMode, "");
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Plans")
  {
    PL_TEST_BOOL(plDiversionParse::LockSupportOfTier("INDIE") == plDiversionLockSupport::No);
    PL_TEST_BOOL(plDiversionParse::LockSupportOfTier("PRO") == plDiversionLockSupport::No);
    PL_TEST_BOOL(plDiversionParse::LockSupportOfTier("STUDIO") == plDiversionLockSupport::Yes);
    PL_TEST_BOOL(plDiversionParse::LockSupportOfTier("ENTERPRISE") == plDiversionLockSupport::Yes);
    PL_TEST_BOOL(plDiversionParse::LockSupportOfTier("UNKNOWN") == plDiversionLockSupport::Unknown);
    PL_TEST_BOOL(plDiversionParse::LockSupportOfTier("") == plDiversionLockSupport::Unknown);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "dv lock")
  {
    // recorded on an Indie repository
    constexpr const char* szRefused = "Failed to lock file: Oh no, looks like something went wrong!\n\n"
                                      "An engineer has been notified - we're on it. Your files are safe and you can continue working normally.\n\n"
                                      "Please run `dv support` if the issue persists.\n"
                                      "[failed to execute PUT '/repos/{repo_id}/locks/{path}' (SrcHandlersv2FileLocksLockFile) [b1c90982]: decode response: "
                                      "request failed: {\"status\":403,\"detail\":\"Hard locks require a Studio or Enterprise subscription\"}\n"
                                      ": unexpected status code: 403]\n";
    PL_TEST_BOOL(plDiversionParse::IsLockRefusedByPlan(szRefused));
    PL_TEST_BOOL(!plDiversionParse::IsLockRefusedByPlan("File Game/Scenes/Lantern.plPrefab is not locked\n"));

    PL_TEST_BOOL(plDiversionParse::IsNotLocked("File PlasmaProject/RuntimeConfigs/Plugins.ddl is not locked\n"));
    PL_TEST_BOOL(!plDiversionParse::IsNotLocked("No active locks\n"));
  }
}

PL_CREATE_SIMPLE_TEST(Diversion, BranchesAndHistory)
{
  PL_TEST_BLOCK(plTestBlock::Enabled, "Branches")
  {
    // recorded, names replaced; a deleted branch added by hand
    constexpr const char* szJson =
      R"({"object":"Branch","items":[{"branch_id":"dv.branch.1","branch_name":"main","author":{"id":"Account_0001","name":"Alex_5622caa8","full_name":"Alex Morgan","email":"alex@example.com"},"time":1792506091,"commit_id":"dv.commit.12","branch_description":null,"is_deleted":false,"is_draft_commit":false,"is_draft":false,"is_protected":false},)"
      R"({"branch_id":"dv.branch.3","branch_name":"art/lighting","author":{"id":"Account_0001","name":"Alex_5622caa8","full_name":"Alex Morgan","email":"alex@example.com"},"time":1792506146,"commit_id":"dv.commit.4","branch_description":null,"is_deleted":false,"is_draft_commit":false,"is_draft":false,"is_protected":false},)"
      R"({"branch_id":"dv.branch.4","branch_name":"old","commit_id":"dv.commit.5","is_deleted":true}]})";

    plDynamicArray<plDiversionBranch> branches;
    PL_TEST_BOOL(plDiversionParse::Branches(Bytes(szJson), branches).Succeeded());
    if (PL_TEST_INT(branches.GetCount(), 2))
    {
      PL_TEST_STRING(branches[0].m_sId, "dv.branch.1");
      PL_TEST_STRING(branches[0].m_sName, "main");
      PL_TEST_STRING(branches[0].m_sHeadCommitId, "dv.commit.12");
      PL_TEST_STRING(branches[1].m_sName, "art/lighting");
    }
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "File history and commits")
  {
    // recorded, names replaced
    constexpr const char* szHistory =
      R"({"entries":[{"commit":{"commit_id":"dv.commit.7","commit_message":"Plasma scratch project","created_ts":1792509895,"branch_id":"dv.branch.1","author":{"id":"Account_0001","name":"Alex_5622caa8","full_name":"Alex Morgan","email":"alex@example.com","image":""},"parents":["dv.commit.6"],"parent_branches":[{"id":"dv.branch.1","name":"main"}]},)"
      R"("entry":{"path":"PlasmaProject/Scenes/Main.plScene","hash":"137c598d","status":2,"mode":33188,"mtime":"2026-10-20T15:24:37+00:00","blob":{"storage_uri":"x","storage_backend":1101,"size":30963,"sha":"6dea45a2"}}}]})";

    plDynamicArray<plDiversionCommit> commits;
    PL_TEST_BOOL(plDiversionParse::FileHistory(Bytes(szHistory), commits).Succeeded());
    if (PL_TEST_INT(commits.GetCount(), 1))
    {
      PL_TEST_STRING(commits[0].m_sId, "dv.commit.7");
      PL_TEST_STRING(commits[0].m_sMessage, "Plasma scratch project");
      PL_TEST_STRING(commits[0].m_sAuthor, "Alex Morgan");
      PL_TEST_STRING(commits[0].m_sAuthorId, "Account_0001");
      PL_TEST_INT(commits[0].m_Time.GetInt64(plSIUnitOfTime::Second), 1792509895);
    }

    PL_TEST_BOOL(plDiversionParse::FileHistory(Bytes(R"({"entries":[]})"), commits).Succeeded());
    PL_TEST_INT(commits.GetCount(), 0);

    constexpr const char* szCommit = R"({"commit_id":"dv.commit.12","commit_message":"remove vc test","created_ts":1792512210,"branch_id":"dv.branch.1","author":{"id":"Account_0001","name":"Alex_5622caa8","full_name":"Alex Morgan","email":"alex@example.com","image":""},"parents":["dv.commit.10"]})";
    plDiversionCommit commit;
    PL_TEST_BOOL(plDiversionParse::Commit(Bytes(szCommit), commit).Succeeded());
    PL_TEST_STRING(commit.m_sId, "dv.commit.12");
    PL_TEST_STRING(commit.m_sAuthor, "Alex Morgan");
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Incoming files and conflicts")
  {
    // the shape of the agent's WorkspaceSyncProgress while it downloads
    constexpr const char* szSync = R"({"IsSyncComplete":false,"IsPaused":false,"SyncedCommitID":"dv.commit.12","SyncingToCommitID":"dv.commit.13","FileStats":{"Inbound":{"ItemsCount":3000,"ProgressStatus":"InProgress"},"Outbound":{"ItemsCount":0}}})";
    plDiversionSyncState sync;
    PL_TEST_BOOL(plDiversionParse::Sync(Bytes(szSync), sync).Succeeded());
    PL_TEST_BOOL(!sync.m_bComplete);
    PL_TEST_INT(sync.m_uiIncoming, 3000);

    // the cloud schema's conflicts list
    constexpr const char* szStatus = R"({"items":{"new":[],"modified":[{"path":"Game/Scenes/Sponza.plScene","prev_path":"Game/Scenes/Sponza.plScene"}],"deleted":[]},"incomplete_result":false,"conflicts":["Game/Scenes/Sponza.plScene"]})";
    plDiversionStatus status;
    PL_TEST_BOOL(plDiversionParse::Status(Bytes(szStatus), status).Succeeded());
    if (PL_TEST_INT(status.m_Conflicts.GetCount(), 1))
      PL_TEST_STRING(status.m_Conflicts[0], "Game/Scenes/Sponza.plScene");

    // recorded in P0 (Tests/Fixtures/spike/status_conflict.txt)
    constexpr const char* szText = "Local workspace is synced to commit: dv.commit.3\n"
                                   "Total modified paths: 2 (2 files)\n"
                                   "Modified:\n"
                                   "\t Content/Scenes/Courtyard.plScene\n"
                                   "\n"
                                   "Your workspace can't be brought up to date with branch main due to conflicts in the following files:\n"
                                   "     Content/Scenes/Courtyard.plScene\n"
                                   "Please run `dv update` to resolve.\n";
    plDynamicArray<plString> conflicts;
    plDiversionParse::StatusConflicts(szText, conflicts);
    if (PL_TEST_INT(conflicts.GetCount(), 1))
      PL_TEST_STRING(conflicts[0], "Content/Scenes/Courtyard.plScene");

    plDiversionParse::StatusConflicts("Your workspace has no changes.\n", conflicts);
    PL_TEST_INT(conflicts.GetCount(), 0);
  }
}

PL_CREATE_SIMPLE_TEST(Diversion, PathArguments)
{
  plDynamicArray<plString> all;
  plDynamicArray<plString> ticked;
  plDynamicArray<plString> args;

  PL_TEST_BLOCK(plTestBlock::Enabled, "Short lists go as they are")
  {
    all.PushBack("Game/Scenes/A.plScene");
    all.PushBack("Game/Scenes/B.plScene");
    ticked = all;

    plDiversionPaths::MakeArguments(ticked, all, args);
    PL_TEST_INT(args.GetCount(), 2);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Long lists fold fully ticked folders")
  {
    all.Clear();
    ticked.Clear();

    // 400 long paths in one folder, all ticked, and one unticked change beside them in the parent
    for (plUInt32 i = 0; i < 400; ++i)
    {
      plStringBuilder s;
      s.SetFormat("Game/Textures/Generated/Some/Deep/Folder/Texture_With_A_Long_Name_{}.plTextureAsset", i);
      all.PushBack(s);
      ticked.PushBack(s);
    }

    all.PushBack("Game/Textures/Untouched.plTextureAsset");
    all.PushBack("Game/Scenes/Main.plScene");
    ticked.PushBack("Game/Scenes/Main.plScene");

    plDiversionPaths::MakeArguments(ticked, all, args);
    if (PL_TEST_INT(args.GetCount(), 2))
    {
      // Game/Textures holds an untouched change, so the fold stops below it; Game/Scenes is fully ticked
      PL_TEST_STRING(args[0], "Game/Textures/Generated");
      PL_TEST_STRING(args[1], "Game/Scenes");
    }
  }
}

PL_CREATE_SIMPLE_TEST(Diversion, IgnoreFile)
{
  plDynamicArray<plString> missing;

  PL_TEST_BLOCK(plTestBlock::Enabled, "Project at the workspace root")
  {
    plDiversionIgnore::FindMissingLines("", "", missing);
    PL_TEST_INT(missing.GetCount(), 2);
    PL_TEST_STRING(missing[0], "AssetCache/");
    PL_TEST_STRING(missing[1], "Plugins/");

    plDiversionIgnore::FindMissingLines("AssetCache/\r\nPlugins/\r\n", "", missing);
    PL_TEST_INT(missing.GetCount(), 0);

    plDiversionIgnore::FindMissingLines("/AssetCache/**\n# Plugins/\n!Plugins/\n", "", missing);
    PL_TEST_INT(missing.GetCount(), 1);
    PL_TEST_STRING(missing[0], "Plugins/");
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Project below the workspace root")
  {
    // what Shatterfall's .dvignore has: AssetCache is covered, the package store is not
    constexpr const char* szIgnore = "Intermediate/*\nPlugins/*/Intermediate/*\n\n# Plasma's generated asset output\nShatterfall/AssetCache/*\n";

    plDiversionIgnore::FindMissingLines(szIgnore, "Shatterfall", missing);
    PL_TEST_INT(missing.GetCount(), 1);
    PL_TEST_STRING(missing[0], "Shatterfall/Plugins/");

    // a plain name matches at any depth, an anchored one only at the root
    plDiversionIgnore::FindMissingLines("AssetCache\n/Plugins/\n", "Shatterfall", missing);
    PL_TEST_INT(missing.GetCount(), 1);
    PL_TEST_STRING(missing[0], "Shatterfall/Plugins/");

    plDiversionIgnore::FindMissingLines("**/AssetCache/\nshatterfall/plugins\n", "Shatterfall", missing);
    PL_TEST_INT(missing.GetCount(), 0);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Appending keeps the line endings")
  {
    plDynamicArray<plString> lines;
    lines.PushBack("AssetCache/");

    PL_TEST_STRING(plDiversionIgnore::AppendLines("*.tmp\r\n", lines), "*.tmp\r\n\r\n# Plasma: transform output and installed packages, rebuilt on every machine\r\nAssetCache/\r\n");
    PL_TEST_STRING(plDiversionIgnore::AppendLines("*.tmp", lines), "*.tmp\n\n# Plasma: transform output and installed packages, rebuilt on every machine\nAssetCache/\n");
    PL_TEST_STRING(plDiversionIgnore::AppendLines("", lines), "# Plasma: transform output and installed packages, rebuilt on every machine\nAssetCache/\n");
    PL_TEST_STRING(plDiversionIgnore::AppendLines("", lines, "Added from the editor"), "# Added from the editor\nAssetCache/\n");
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "A folder of the Changes panel")
  {
    PL_TEST_BOOL(!plDiversionIgnore::IsFolderIgnored("Intermediate/*\n", "Shatterfall/TMP"));
    PL_TEST_BOOL(plDiversionIgnore::IsFolderIgnored("Intermediate/*\n/Shatterfall/TMP/\n", "Shatterfall/TMP"));
    PL_TEST_BOOL(plDiversionIgnore::IsFolderIgnored("TMP/\n", "Shatterfall/TMP"));
    PL_TEST_BOOL(!plDiversionIgnore::IsFolderIgnored("# /Shatterfall/TMP/\n", "Shatterfall/TMP"));
  }
}

PL_CREATE_SIMPLE_TEST(Diversion, ChangeTree)
{
  // as Diversion lists Shatterfall's changes: a new folder comes as a change of its own, before its files
  plDynamicArray<plDiversionChangeTree::Item> items;
  auto Add = [&](plStringView sPath, plDiversionChangeKind kind, bool bElsewhere = false)
  {
    plDiversionChangeTree::Item& item = items.ExpandAndGetRef();
    item.m_sPath = sPath;
    item.m_Kind = kind;
    item.m_bElsewhere = bElsewhere;
  };

  Add("Shatterfall/Editor/Packages.ddl", plDiversionChangeKind::Modified);
  Add("Shatterfall/TMP/Face", plDiversionChangeKind::Added);
  Add("Shatterfall/TMP/Face/Brows.fbx", plDiversionChangeKind::Added);
  Add("Shatterfall/TMP/Face/Head.fbx", plDiversionChangeKind::Added);
  Add("Shatterfall/TMP/LocalMultiplayer/a/client.log", plDiversionChangeKind::Deleted);
  Add("Shatterfall/TMP/build.log", plDiversionChangeKind::Added);
  Add("Shatterfall/main.plScene", plDiversionChangeKind::Modified);
  Add("Tools/build.py", plDiversionChangeKind::Modified, true);

  plDiversionChangeTree tree;
  tree.Build(items, "Shatterfall", "");

  auto FindFolder = [&](plStringView sPath) -> plUInt32
  {
    for (plUInt32 i = 0; i < tree.GetFolders().GetCount(); ++i)
    {
      if (tree.GetFolder(i).m_sPath == sPath)
        return i;
    }
    return plInvalidIndex;
  };

  PL_TEST_BLOCK(plTestBlock::Enabled, "Folders and counts")
  {
    const plDiversionChangeTree::Folder& project = tree.GetFolder(plDiversionChangeTree::ProjectRoot);
    PL_TEST_INT(project.m_uiTotal, 7);
    PL_TEST_INT(project.m_Folders.GetCount(), 2); // Editor, TMP
    PL_TEST_INT(project.m_Items.GetCount(), 1);   // main.plScene
    PL_TEST_STRING(tree.GetFolder(project.m_Folders[0]).m_sName, "Editor");

    const plUInt32 uiTmp = FindFolder("Shatterfall/TMP");
    PL_TEST_BOOL(uiTmp != plInvalidIndex);
    const plDiversionChangeTree::Folder& tmp = tree.GetFolder(uiTmp);
    PL_TEST_INT(tmp.m_uiTotal, 5);
    PL_TEST_INT(tmp.m_uiCount[static_cast<plUInt32>(plDiversionChangeKind::Added)], 4);
    PL_TEST_INT(tmp.m_uiCount[static_cast<plUInt32>(plDiversionChangeKind::Deleted)], 1);
    PL_TEST_INT(tmp.m_Items.GetCount(), 1); // build.log; the Face entry is the Face folder

    const plUInt32 uiFace = FindFolder("Shatterfall/TMP/Face");
    PL_TEST_INT(tree.GetFolder(uiFace).m_iOwnItem, 1);
    PL_TEST_INT(tree.GetFolder(uiFace).m_uiTotal, 3);

    PL_TEST_INT(tree.GetFolder(plDiversionChangeTree::ElsewhereRoot).m_uiTotal, 1);

    plDynamicArray<plUInt32> below;
    tree.GetItems(uiTmp, below);
    PL_TEST_INT(below.GetCount(), 5);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Ticks per folder")
  {
    plDynamicArray<bool> ticked;
    ticked.SetCount(items.GetCount(), false);
    ticked[2] = true; // Brows.fbx
    ticked[5] = true; // build.log

    plDynamicArray<plUInt32> perFolder;
    tree.CountTicked(ticked, perFolder);
    PL_TEST_INT(perFolder[FindFolder("Shatterfall/TMP")], 2);
    PL_TEST_INT(perFolder[FindFolder("Shatterfall/TMP/Face")], 1);
    PL_TEST_INT(perFolder[plDiversionChangeTree::ProjectRoot], 2);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Rows")
  {
    using Kind = plDiversionChangeTree::RowKind;

    plHashSet<plString> open;
    plDiversionChangeTree::View view;
    view.m_pOpen = &open;

    // closed: the top-level folders, the root's files, the elsewhere caption
    plDynamicArray<plDiversionChangeTree::Row> rows;
    tree.MakeRows(view, rows);
    PL_TEST_INT(rows.GetCount(), 4);
    PL_TEST_BOOL(rows[0].m_Kind == Kind::Folder && rows[1].m_Kind == Kind::Folder && rows[2].m_Kind == Kind::Change && rows[3].m_Kind == Kind::Elsewhere);

    // one row per level (D37 b): TMP open shows Face and LocalMultiplayer closed, then build.log
    open.Insert("shatterfall/tmp");
    tree.MakeRows(view, rows);
    PL_TEST_INT(rows.GetCount(), 7);
    PL_TEST_STRING(tree.GetFolder(rows[2].m_uiIndex).m_sName, "Face");
    PL_TEST_INT(rows[2].m_uiDepth, 1);
    PL_TEST_STRING(tree.GetFolder(rows[3].m_uiIndex).m_sName, "LocalMultiplayer");
    PL_TEST_BOOL(rows[4].m_Kind == Kind::Change && rows[4].m_uiIndex == 5);

    // a page of one change
    open.Insert("shatterfall/tmp/face");
    view.m_uiPageSize = 1;
    tree.MakeRows(view, rows);
    PL_TEST_BOOL(rows[3].m_Kind == Kind::Change && rows[4].m_Kind == Kind::More && rows[4].m_uiHidden == 1);

    // the flat list keeps the given order and the elsewhere ones behind their caption
    view.m_bTree = false;
    view.m_bShowElsewhere = true;
    tree.MakeRows(view, rows);
    PL_TEST_INT(rows.GetCount(), 9);
    PL_TEST_BOOL(rows[7].m_Kind == Kind::Elsewhere && rows[8].m_uiIndex == 7);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Filter")
  {
    tree.Build(items, "Shatterfall", "FACE/b");
    PL_TEST_INT(tree.GetFolder(plDiversionChangeTree::ProjectRoot).m_uiTotal, 1);
    PL_TEST_BOOL(tree.IsShown(2));
    PL_TEST_BOOL(!tree.IsShown(1));

    // the project folder's own name does not match everything
    tree.Build(items, "Shatterfall", "shatter");
    PL_TEST_INT(tree.GetFolder(plDiversionChangeTree::ProjectRoot).m_uiTotal, 0);
  }
}
