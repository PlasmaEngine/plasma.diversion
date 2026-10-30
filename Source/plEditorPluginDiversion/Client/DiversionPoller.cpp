#include <plEditorPluginDiversion/EditorPluginDiversionPCH.h>

#include <plEditorPluginDiversion/Client/DiversionCli.h>
#include <plEditorPluginDiversion/Client/DiversionParse.h>
#include <plEditorPluginDiversion/Client/DiversionPoller.h>

#include <Foundation/Threading/ThreadUtils.h>

namespace
{
  constexpr plTime PollInterval = plTime::MakeFromSeconds(1);
  constexpr plTime AgentRetryInterval = plTime::MakeFromSeconds(5);
  constexpr plTime OtherChangesInterval = plTime::MakeFromSeconds(20);
  constexpr plTime PausedDiffInterval = plTime::MakeFromSeconds(15);
  constexpr plTime ShelvesInterval = plTime::MakeFromSeconds(60);
  constexpr plTime AccessInterval = plTime::MakeFromMinutes(10);

  bool SameWorkspace(const plDiversionWorkspace& a, const plDiversionWorkspace& b)
  {
    return a.m_sWorkspaceId == b.m_sWorkspaceId && a.m_sBranchName == b.m_sBranchName && a.m_sCommitId == b.m_sCommitId && a.m_bPaused == b.m_bPaused;
  }

  bool SameSync(const plDiversionSyncState& a, const plDiversionSyncState& b)
  {
    return a.m_bComplete == b.m_bComplete && a.m_bPaused == b.m_bPaused && a.m_sSyncedCommitId == b.m_sSyncedCommitId;
  }

  // A poll that finds what the last one found publishes nothing: with thousands of changes, every snapshot costs the
  // editor a rebuild of its views.

  bool SameChanges(const plDiversionStatus& a, const plDiversionStatus& b)
  {
    if (a.m_bComplete != b.m_bComplete || a.m_Changes.GetCount() != b.m_Changes.GetCount())
      return false;

    for (plUInt32 i = 0; i < a.m_Changes.GetCount(); ++i)
    {
      const plDiversionChange& ca = a.m_Changes[i];
      const plDiversionChange& cb = b.m_Changes[i];
      if (ca.m_Kind != cb.m_Kind || ca.m_sPath != cb.m_sPath || ca.m_sPrevPath != cb.m_sPrevPath)
        return false;
    }

    return true;
  }

  bool SameLocks(const plDynamicArray<plDiversionLock>& a, const plDynamicArray<plDiversionLock>& b)
  {
    if (a.GetCount() != b.GetCount())
      return false;

    for (plUInt32 i = 0; i < a.GetCount(); ++i)
    {
      if (a[i].m_sPath != b[i].m_sPath || a[i].m_sUserId != b[i].m_sUserId || a[i].m_sUserName != b[i].m_sUserName || a[i].m_bAuto != b[i].m_bAuto)
        return false;
    }

    return true;
  }

  bool SameOtherChanges(const plDiversionOtherChanges& a, const plDiversionOtherChanges& b)
  {
    if (a.GetCount() != b.GetCount())
      return false;

    for (auto it : a)
    {
      const plDynamicArray<plDiversionOtherChange>* pOther = b.GetValue(it.Key());
      if (pOther == nullptr || pOther->GetCount() != it.Value().GetCount())
        return false;

      for (plUInt32 i = 0; i < pOther->GetCount(); ++i)
      {
        const plDiversionOtherChange& oa = it.Value()[i];
        const plDiversionOtherChange& ob = (*pOther)[i];
        if (oa.m_sUser != ob.m_sUser || oa.m_sBranchName != ob.m_sBranchName || oa.m_sWorkspaceId != ob.m_sWorkspaceId || !oa.m_Time.Compare(ob.m_Time, plTimestamp::CompareMode::Identical))
          return false;
      }
    }

    return true;
  }

  /// The project folder relative to the workspace root ("Shatterfall"), empty when they are the same folder.
  plString ProjectPrefix(plStringView sProjectDir, plStringView sWorkspaceRoot)
  {
    plStringBuilder sProject = sProjectDir;
    sProject.MakeCleanPath();
    sProject.Trim(nullptr, "/");

    if (sProject.GetElementCount() <= sWorkspaceRoot.GetElementCount())
      return {};

    return plStringView(sProject.GetData() + sWorkspaceRoot.GetElementCount() + 1, sProject.GetData() + sProject.GetElementCount());
  }
} // namespace

plDiversionPoller::plDiversionPoller(plStringView sProjectDir)
  : plThread("Diversion")
  , m_sProjectDir(sProjectDir)
{
  m_pSnapshot = PL_DEFAULT_NEW(plDiversionSnapshot);
}

plDiversionPoller::~plDiversionPoller()
{
  Stop();
}

void plDiversionPoller::Stop()
{
  m_bStop = true;

  if (GetThreadStatus() == plThread::Running)
    Join();
}

plSharedPtr<const plDiversionSnapshot> plDiversionPoller::GetSnapshot(plUInt32& inout_uiKnownVersion) const
{
  PL_LOCK(m_Mutex);

  if (inout_uiKnownVersion == m_uiVersion)
    return nullptr;

  inout_uiKnownVersion = m_uiVersion;
  return m_pSnapshot;
}

plUInt32 plDiversionPoller::Run()
{
  plDiversionSnapshot state;

  while (!m_bStop)
  {
    bool bChanged = RunQueuedCommands(state);
    bChanged |= Poll(state);

    // one-off requests after the poll, so the agent is connected
    if (m_Agent.IsConnected())
      RunQueuedWork();

    if (bChanged)
      Publish(state);

    SleepFor(state.m_State == plDiversionSnapshot::State::AgentDown ? AgentRetryInterval : PollInterval);
  }

  return 0;
}

bool plDiversionPoller::Poll(plDiversionSnapshot& ref_state)
{
  const bool bWake = m_bWake.Set(false);
  const plDiversionSnapshot::State previousState = ref_state.m_State;

  if (!m_Agent.IsConnected() && m_Agent.Connect().Failed())
  {
    ref_state.m_State = plDiversionSnapshot::State::AgentDown;
    ref_state.m_sMessage = m_Agent.GetLastError();
    ref_state.m_Workspace = {};
    return previousState != ref_state.m_State;
  }

  ref_state.m_sAgentVersion = m_Agent.GetVersion();

  // asked every time: a branch switch or a pause shows up here first
  plDiversionWorkspace workspace;
  const plDiversionAgent::Result wsResult = m_Agent.GetWorkspace(m_sProjectDir, workspace);

  if (wsResult == plDiversionAgent::Result::Failed)
  {
    ref_state.m_State = plDiversionSnapshot::State::AgentDown;
    ref_state.m_sMessage = m_Agent.GetLastError();
    return previousState != ref_state.m_State;
  }

  if (wsResult == plDiversionAgent::Result::NotAvailable)
  {
    ref_state.m_State = plDiversionSnapshot::State::NoWorkspace;
    ref_state.m_sMessage = "The project folder is not in a Diversion workspace.";
    ref_state.m_Workspace = {};
    return previousState != ref_state.m_State;
  }

  bool bChanged = previousState != plDiversionSnapshot::State::Connected || !SameWorkspace(workspace, ref_state.m_Workspace);
  const bool bBranchChanged = workspace.m_sBranchName != ref_state.m_Workspace.m_sBranchName;

  ref_state.m_State = plDiversionSnapshot::State::Connected;
  ref_state.m_Workspace = workspace;

  plDiversionSyncState sync;
  const plDiversionAgent::Result syncResult = m_Agent.GetSync(workspace, sync);
  if (syncResult == plDiversionAgent::Result::NotAvailable || workspace.m_bPaused)
  {
    sync.m_bPaused = true;
    sync.m_bComplete = false;
  }

  if (!SameSync(sync, ref_state.m_Sync))
  {
    ref_state.m_Sync = sync;
    bChanged = true;
  }

  bChanged |= PollChanges(ref_state, bWake || bBranchChanged);
  bChanged |= PollOtherChanges(ref_state, bWake || bBranchChanged);
  bChanged |= PollLocks(ref_state, bWake);
  bChanged |= PollAccess(ref_state);
  bChanged |= PollBranches(ref_state, bWake || bBranchChanged);
  bChanged |= PollSyncedCommit(ref_state);
  bChanged |= PollConflicts(ref_state);
  bChanged |= PollShelves(ref_state);
  return bChanged;
}

bool plDiversionPoller::PollBranches(plDiversionSnapshot& ref_state, bool bWake)
{
  const plTime now = plTime::Now();
  if (!bWake && !ref_state.m_Branches.IsEmpty() && now - m_LastBranches < ShelvesInterval)
    return false;

  if (!bWake && ref_state.m_Branches.IsEmpty() && now - m_LastBranches < OtherChangesInterval)
    return false;

  m_LastBranches = now;

  plDynamicArray<plDiversionBranch> branches;
  if (m_Agent.GetBranches(ref_state.m_Workspace, branches) != plDiversionAgent::Result::Ok)
    return false;

  bool bChanged = branches.GetCount() != ref_state.m_Branches.GetCount();
  for (plUInt32 i = 0; !bChanged && i < branches.GetCount(); ++i)
    bChanged = branches[i].m_sName != ref_state.m_Branches[i].m_sName || branches[i].m_sHeadCommitId != ref_state.m_Branches[i].m_sHeadCommitId;

  ref_state.m_Branches = std::move(branches);
  return bChanged;
}

bool plDiversionPoller::PollSyncedCommit(plDiversionSnapshot& ref_state)
{
  // who made the commit the workspace now has, for the banners of reloaded documents
  const plString& sCommit = ref_state.m_Sync.m_sSyncedCommitId.IsEmpty() ? ref_state.m_Workspace.m_sCommitId : ref_state.m_Sync.m_sSyncedCommitId;
  if (sCommit.IsEmpty() || sCommit == ref_state.m_SyncedCommit.m_sId)
    return false;

  plDiversionCommit commit;
  if (m_Agent.GetCommit(ref_state.m_Workspace, sCommit, commit) != plDiversionAgent::Result::Ok)
    return false;

  ref_state.m_SyncedCommit = commit;
  return true;
}

bool plDiversionPoller::PollConflicts(plDiversionSnapshot& ref_state)
{
  // the workspace stays behind its branch while synced edits clash with what came in; dv status names the files
  const plDiversionBranch* pBranch = nullptr;
  for (const plDiversionBranch& branch : ref_state.m_Branches)
  {
    if (branch.m_sName == ref_state.m_Workspace.m_sBranchName)
      pBranch = &branch;
  }

  const bool bBehind = pBranch != nullptr && !pBranch->m_sHeadCommitId.IsEmpty() && pBranch->m_sHeadCommitId != ref_state.m_Workspace.m_sCommitId && ref_state.m_Sync.m_bComplete;
  if (!bBehind || ref_state.m_Status.m_Changes.IsEmpty())
  {
    const bool bHad = !ref_state.m_Status.m_Conflicts.IsEmpty();
    ref_state.m_Status.m_Conflicts.Clear();
    m_KnownConflicts.Clear();
    return bHad;
  }

  // a new change list from the agent comes without them: the last ones found stay until the next check
  if (ref_state.m_Status.m_Conflicts.IsEmpty())
    ref_state.m_Status.m_Conflicts = m_KnownConflicts;

  const plTime now = plTime::Now();
  if (now - m_LastConflicts < OtherChangesInterval)
    return false;

  m_LastConflicts = now;

  const plStringView args[] = {"status"};
  const plDiversionCli::Result result = plDiversionCli::Run(ref_state.m_Workspace.m_sRoot, args);
  if (!result.m_bStarted)
    return false;

  plDynamicArray<plString> conflicts;
  plDiversionParse::StatusConflicts(result.m_sOutput, conflicts);

  const bool bChanged = conflicts != ref_state.m_Status.m_Conflicts;
  m_KnownConflicts = conflicts;
  ref_state.m_Status.m_Conflicts = std::move(conflicts);
  return bChanged;
}

bool plDiversionPoller::PollLocks(plDiversionSnapshot& ref_state, bool bWake)
{
  const plTime now = plTime::Now();
  if (!bWake && ref_state.m_bLocksKnown && now - m_LastLocks < OtherChangesInterval)
    return false;

  m_LastLocks = now;

  plDynamicArray<plDiversionLock> locks;
  if (m_Agent.GetLocks(ref_state.m_Workspace, locks) != plDiversionAgent::Result::Ok)
  {
    const bool bWasKnown = ref_state.m_bLocksKnown;
    ref_state.m_bLocksKnown = false;
    ref_state.m_Locks.Clear();
    return bWasKnown;
  }

  const bool bChanged = !ref_state.m_bLocksKnown || !SameLocks(locks, ref_state.m_Locks);
  ref_state.m_Locks = std::move(locks);
  ref_state.m_bLocksKnown = true;
  return bChanged;
}

bool plDiversionPoller::PollAccess(plDiversionSnapshot& ref_state)
{
  // the user and their rights rarely change: once per repository, then every few minutes
  const plTime now = plTime::Now();
  const bool bNewRepo = ref_state.m_Workspace.m_sRepoId != m_sAccessRepoId;
  if (!bNewRepo && !ref_state.m_User.m_sId.IsEmpty() && now - m_LastAccess < AccessInterval)
    return false;

  if (!bNewRepo && ref_state.m_User.m_sId.IsEmpty() && now - m_LastAccess < OtherChangesInterval)
    return false;

  m_LastAccess = now;
  m_sAccessRepoId = ref_state.m_Workspace.m_sRepoId;

  plDiversionUser user;
  plString sOwnerId;
  plString sAccessMode;
  if (m_Agent.GetUser(user) != plDiversionAgent::Result::Ok || m_Agent.GetAccess(ref_state.m_Workspace, user.m_sId, sOwnerId, sAccessMode) != plDiversionAgent::Result::Ok)
  {
    // asked again later; until then lock actions show, and a refusal hides them
    plLog::Warning("Diversion: who you are in the repository is not known yet: {}", m_Agent.GetLastError());
    return false;
  }

  const bool bChanged = user.m_sId != ref_state.m_User.m_sId || user.m_sTier != ref_state.m_User.m_sTier || sOwnerId != ref_state.m_sRepoOwnerId || sAccessMode != ref_state.m_sAccessMode;
  ref_state.m_User = user;
  ref_state.m_sRepoOwnerId = sOwnerId;
  ref_state.m_sAccessMode = sAccessMode;
  return bChanged;
}

bool plDiversionPoller::PollChanges(plDiversionSnapshot& ref_state, bool bWake)
{
  const plDiversionWorkspace& workspace = ref_state.m_Workspace;

  if (ref_state.m_Sync.m_bPaused)
  {
    // the agent keeps no change list for a paused workspace; dv diff compares the files itself, which takes seconds
    // in a big project
    m_iLastJournal = -1;

    const plTime now = plTime::Now();
    if (!bWake && now - m_LastPausedDiff < PausedDiffInterval)
      return false;

    m_LastPausedDiff = now;

    // only the project's folder: in a workspace that holds more than the project, the rest can be most of the changes
    const plString sPrefix = ProjectPrefix(m_sProjectDir, workspace.m_sRoot);

    plHybridArray<plStringView, 5> args;
    args.PushBack("diff");
    args.PushBack("--name-status");
    args.PushBack("--color");
    args.PushBack("never");
    if (!sPrefix.IsEmpty())
      args.PushBack(sPrefix);

    plDiversionCli::Result result = plDiversionCli::Run(workspace.m_sRoot, args);
    if (!result.Succeeded())
    {
      ref_state.m_sMessage = plDiversionCli::FirstLine(result.m_sOutput);
      return false;
    }

    plDiversionStatus status;
    plDiversionParse::NameStatus(result.m_sOutput, status);
    return TakeChanges(ref_state, std::move(status));
  }

  plInt64 iJournal = 0;
  if (m_Agent.GetJournal(workspace, iJournal) != plDiversionAgent::Result::Ok)
    return false;

  if (!bWake && iJournal == m_iLastJournal)
    return false;

  plDiversionStatus status;
  if (m_Agent.GetStatus(workspace, status) != plDiversionAgent::Result::Ok)
  {
    ref_state.m_sMessage = m_Agent.GetLastError();
    return false;
  }

  m_iLastJournal = iJournal;
  return TakeChanges(ref_state, std::move(status));
}

bool plDiversionPoller::TakeChanges(plDiversionSnapshot& ref_state, plDiversionStatus&& status)
{
  const bool bChanged = !ref_state.m_sMessage.IsEmpty() || !SameChanges(status, ref_state.m_Status);
  ref_state.m_sMessage.Clear();

  if (!bChanged)
    return false;

  // the conflicts come from dv status (PollConflicts), not with the change list
  status.m_Conflicts = std::move(ref_state.m_Status.m_Conflicts);
  ref_state.m_Status = std::move(status);
  return true;
}

bool plDiversionPoller::PollOtherChanges(plDiversionSnapshot& ref_state, bool bWake)
{
  const plTime now = plTime::Now();
  if (!bWake && ref_state.m_bOtherChangesKnown && now - m_LastOtherChanges < OtherChangesInterval)
    return false;

  // retried at the same pace when it fails, so an offline editor does not hammer the cloud
  m_LastOtherChanges = now;

  const plString sPrefix = ProjectPrefix(m_sProjectDir, ref_state.m_Workspace.m_sRoot);

  plDiversionOtherChanges changes;
  if (m_Agent.GetOtherChanges(ref_state.m_Workspace, sPrefix, changes) != plDiversionAgent::Result::Ok)
  {
    const bool bWasKnown = ref_state.m_bOtherChangesKnown;
    ref_state.m_bOtherChangesKnown = false;
    ref_state.m_OtherChanges.Clear();
    return bWasKnown;
  }

  const bool bChanged = !ref_state.m_bOtherChangesKnown || !SameOtherChanges(changes, ref_state.m_OtherChanges);
  ref_state.m_OtherChanges = std::move(changes);
  ref_state.m_bOtherChangesKnown = true;
  return bChanged;
}

void plDiversionPoller::QueueCommand(plArrayPtr<const plStringView> args, CommandDone onDone)
{
  {
    PL_LOCK(m_Mutex);

    Command& command = m_Commands.ExpandAndGetRef();
    for (plStringView sArg : args)
      command.m_Args.PushBack(sArg);

    command.m_OnDone = onDone;
  }

  m_iPendingCommands.Increment();
  m_bWake = true;
}

void plDiversionPoller::QueueAgentWork(AgentWork work, WorkDone onDone)
{
  {
    PL_LOCK(m_Mutex);
    Work& entry = m_Work.ExpandAndGetRef();
    entry.m_Work = std::move(work);
    entry.m_OnDone = std::move(onDone);
  }

  m_bWake = true;
}

void plDiversionPoller::RunQueuedWork()
{
  plDynamicArray<Work> work;
  {
    PL_LOCK(m_Mutex);
    work.Swap(m_Work);
  }

  for (Work& entry : work)
  {
    if (entry.m_Work)
      entry.m_Work(m_Agent);

    if (entry.m_OnDone)
    {
      PL_LOCK(m_Mutex);
      m_WorkDone.PushBack(std::move(entry.m_OnDone));
    }
  }
}

bool plDiversionPoller::HasFinishedWork() const
{
  PL_LOCK(m_Mutex);
  return !m_Finished.IsEmpty() || !m_WorkDone.IsEmpty();
}

void plDiversionPoller::CallFinishedCommands()
{
  plDynamicArray<Command> finished;
  plDynamicArray<WorkDone> workDone;
  {
    PL_LOCK(m_Mutex);
    finished.Swap(m_Finished);
    workDone.Swap(m_WorkDone);
  }

  for (const Command& command : finished)
    command.m_OnDone(command.m_Result);

  for (const WorkDone& onDone : workDone)
    onDone();
}

bool plDiversionPoller::IsBusy() const
{
  return m_iPendingCommands > 0;
}

bool plDiversionPoller::RunQueuedCommands(plDiversionSnapshot& ref_state)
{
  plDynamicArray<Command> commands;
  {
    PL_LOCK(m_Mutex);
    commands.Swap(m_Commands);
  }

  if (commands.IsEmpty())
    return false;

  bool bChanged = false;

  for (Command& command : commands)
  {
    if (ref_state.m_Workspace.IsValid())
    {
      plHybridArray<plStringView, 8> args;
      for (const plString& sArg : command.m_Args)
        args.PushBack(sArg);

      command.m_Result = plDiversionCli::Run(ref_state.m_Workspace.m_sRoot, args);
    }
    else
    {
      command.m_Result.m_sOutput = "The project is not connected to a Diversion workspace.";
    }

    if (!command.m_OnDone.IsValid() && !command.m_Result.Succeeded())
    {
      ref_state.m_sMessage = plDiversionCli::FirstLine(command.m_Result.m_sOutput);
      bChanged = true;
    }

    if (command.m_OnDone.IsValid())
    {
      PL_LOCK(m_Mutex);
      m_Finished.PushBack(std::move(command));
    }

    m_iPendingCommands.Decrement();
  }

  // whatever the commands changed shows up in a full refresh, shelves included
  m_bShelvesRequested = true;
  m_bWake = true;
  return bChanged;
}

bool plDiversionPoller::PollShelves(plDiversionSnapshot& ref_state)
{
  const plTime now = plTime::Now();
  if (!m_bShelvesRequested && now - m_LastShelves < ShelvesInterval)
    return false;

  m_bShelvesRequested = false;
  m_LastShelves = now;

  const plStringView listArgs[] = {"shelf"};
  const plDiversionCli::Result list = plDiversionCli::Run(ref_state.m_Workspace.m_sRoot, listArgs);
  if (!list.Succeeded())
    return false;

  plDynamicArray<plDiversionShelf> shelves;
  plDiversionParse::Shelves(list.m_sOutput, shelves);

  // the count needs one call per shelf; known shelves keep theirs
  for (plDiversionShelf& shelf : shelves)
  {
    for (const plDiversionShelf& known : ref_state.m_Shelves)
    {
      if (known.m_sId == shelf.m_sId)
        shelf.m_iChangeCount = known.m_iChangeCount;
    }

    if (shelf.m_iChangeCount >= 0)
      continue;

    const plStringView showArgs[] = {"shelf", "show", shelf.m_sId};
    const plDiversionCli::Result show = plDiversionCli::Run(ref_state.m_Workspace.m_sRoot, showArgs);
    if (show.Succeeded())
      shelf.m_iChangeCount = plDiversionParse::ShelfChangeCount(show.m_sOutput);
  }

  bool bChanged = shelves.GetCount() != ref_state.m_Shelves.GetCount();
  for (plUInt32 i = 0; !bChanged && i < shelves.GetCount(); ++i)
    bChanged = shelves[i].m_sId != ref_state.m_Shelves[i].m_sId || shelves[i].m_iChangeCount != ref_state.m_Shelves[i].m_iChangeCount;

  ref_state.m_Shelves = std::move(shelves);
  return bChanged;
}

void plDiversionPoller::Publish(const plDiversionSnapshot& state)
{
  plSharedPtr<plDiversionSnapshot> pCopy = PL_DEFAULT_NEW(plDiversionSnapshot);
  pCopy->m_State = state.m_State;
  pCopy->m_sMessage = state.m_sMessage;
  pCopy->m_sAgentVersion = state.m_sAgentVersion;
  pCopy->m_Workspace = state.m_Workspace;
  pCopy->m_Sync = state.m_Sync;
  pCopy->m_Status = state.m_Status;
  pCopy->m_OtherChanges = state.m_OtherChanges;
  pCopy->m_bOtherChangesKnown = state.m_bOtherChangesKnown;
  pCopy->m_Shelves = state.m_Shelves;
  pCopy->m_Locks = state.m_Locks;
  pCopy->m_bLocksKnown = state.m_bLocksKnown;
  pCopy->m_User = state.m_User;
  pCopy->m_sRepoOwnerId = state.m_sRepoOwnerId;
  pCopy->m_sAccessMode = state.m_sAccessMode;
  pCopy->m_Branches = state.m_Branches;
  pCopy->m_SyncedCommit = state.m_SyncedCommit;

  PL_LOCK(m_Mutex);
  m_pSnapshot = pCopy;
  ++m_uiVersion;
}

void plDiversionPoller::SleepFor(plTime duration)
{
  // in slices, so Stop() and Wake() take effect quickly
  const plTime end = plTime::Now() + duration;

  while (!m_bStop && !m_bWake && plTime::Now() < end)
  {
    plThreadUtils::Sleep(plTime::MakeFromMilliseconds(50));
  }
}
