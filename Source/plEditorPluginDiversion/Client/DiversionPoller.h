#pragma once

#include <plEditorPluginDiversion/Client/DiversionAgent.h>
#include <plEditorPluginDiversion/Client/DiversionCli.h>
#include <plEditorPluginDiversion/Client/DiversionTypes.h>

#include <Foundation/Threading/Mutex.h>
#include <Foundation/Threading/Thread.h>
#include <Foundation/Types/SharedPtr.h>

#include <functional>

/// What the poller last found out about the project's workspace. plDiversionPoller::Publish copies it field by field:
/// a new field goes there too.
struct plDiversionSnapshot : public plRefCounted
{
  enum class State : plUInt8
  {
    Starting,     ///< nothing known yet
    AgentDown,    ///< the agent is not running
    NoWorkspace,  ///< the project is in no Diversion workspace
    Connected,
  };

  State m_State = State::Starting;
  plString m_sMessage; ///< why, for AgentDown / NoWorkspace, or the last problem while Connected
  plString m_sAgentVersion;

  plDiversionWorkspace m_Workspace;
  plDiversionSyncState m_Sync;

  plDiversionStatus m_Status;            ///< local changes, workspace-relative
  plDiversionOtherChanges m_OtherChanges; ///< someone else's changes, workspace-relative
  bool m_bOtherChangesKnown = false;     ///< false when the cloud API could not be reached

  plDynamicArray<plDiversionShelf> m_Shelves; ///< of every branch

  plDynamicArray<plDiversionLock> m_Locks; ///< the repository's hard locks, workspace-relative
  bool m_bLocksKnown = false;              ///< false when the cloud API could not be reached

  plDiversionUser m_User;   ///< the signed-in user; empty until the cloud API answered
  plString m_sRepoOwnerId;  ///< the repository owner's user id
  plString m_sAccessMode;   ///< the user's access mode in the repository, e.g. "OWNER"

  plDynamicArray<plDiversionBranch> m_Branches; ///< empty until the cloud API answered
  plDiversionCommit m_SyncedCommit;             ///< the commit the workspace is synced to, with its author
};

/// Polls the Diversion agent on its own thread and publishes plDiversionSnapshot objects for the main thread.
///
/// Cheap queries run every second (sync state, and the journal number that changes with every local change); the
/// change list is only fetched again when the journal moves. Who else is editing and the hard locks come from the
/// cloud API, every 20 seconds; the signed-in user and their access to the repository every 10 minutes. While sync is
/// paused the agent has no change list, so `dv diff` provides it, less often.
class plDiversionPoller : public plThread
{
public:
  explicit plDiversionPoller(plStringView sProjectDir);
  ~plDiversionPoller();

  void Stop();

  /// Asks for a full refresh as soon as possible, e.g. after a commit or after sync was paused or resumed.
  void Wake() { m_bWake = true; }

  using CommandDone = plDelegate<void(const plDiversionCli::Result&)>;

  /// Runs dv with these arguments in the workspace on the poller's thread, then refreshes. onDone gets the result on
  /// the main thread (see CallFinishedCommands); without it a failure shows up as the snapshot's message.
  void QueueCommand(plArrayPtr<const plStringView> args, CommandDone onDone = {});

  using AgentWork = std::function<void(plDiversionAgent& ref_agent)>;
  using WorkDone = std::function<void()>;

  /// Runs work with the poller's agent on its thread (a cloud request, e.g. a file's history), then onDone on the main
  /// thread (see CallFinishedCommands). work and onDone share whatever they capture; onDone reads what work wrote.
  void QueueAgentWork(AgentWork work, WorkDone onDone);

  /// Calls the onDone of commands and work that finished since the last call. Main thread.
  void CallFinishedCommands();

  /// Whether CallFinishedCommands has something to call.
  bool HasFinishedWork() const;

  /// Whether a queued command has not finished yet.
  bool IsBusy() const;

  /// Lists the shelves again soon (they change through commands and through the Diversion app).
  void RequestShelves()
  {
    m_bShelvesRequested = true;
    m_bWake = true;
  }

  /// The newest snapshot, or nullptr when it has not changed since uiKnownVersion.
  plSharedPtr<const plDiversionSnapshot> GetSnapshot(plUInt32& inout_uiKnownVersion) const;

private:
  virtual plUInt32 Run() override;

  /// Updates ref_state; returns whether anything the editor shows changed.
  bool Poll(plDiversionSnapshot& ref_state);
  bool PollChanges(plDiversionSnapshot& ref_state, bool bWake);

  /// Puts a freshly read change list into ref_state; returns whether it differs from the last one.
  bool TakeChanges(plDiversionSnapshot& ref_state, plDiversionStatus&& status);
  bool PollOtherChanges(plDiversionSnapshot& ref_state, bool bWake);
  bool PollLocks(plDiversionSnapshot& ref_state, bool bWake);
  bool PollAccess(plDiversionSnapshot& ref_state);
  bool PollBranches(plDiversionSnapshot& ref_state, bool bWake);
  bool PollSyncedCommit(plDiversionSnapshot& ref_state);
  bool PollConflicts(plDiversionSnapshot& ref_state);
  bool PollShelves(plDiversionSnapshot& ref_state);
  void RunQueuedWork();
  void Publish(const plDiversionSnapshot& state);
  void SleepFor(plTime duration);
  bool RunQueuedCommands(plDiversionSnapshot& ref_state);

  plString m_sProjectDir;
  plAtomicBool m_bStop = false;
  plAtomicBool m_bWake = true;

  mutable plMutex m_Mutex;
  plSharedPtr<const plDiversionSnapshot> m_pSnapshot;
  plUInt32 m_uiVersion = 0;
  struct Command
  {
    plHybridArray<plString, 8> m_Args;
    CommandDone m_OnDone;
    plDiversionCli::Result m_Result;
  };
  plDynamicArray<Command> m_Commands; ///< queued, guarded by m_Mutex
  plDynamicArray<Command> m_Finished; ///< done and with an m_OnDone, guarded by m_Mutex
  struct Work
  {
    AgentWork m_Work;
    WorkDone m_OnDone;
  };
  plDynamicArray<Work> m_Work;         ///< queued, guarded by m_Mutex
  plDynamicArray<WorkDone> m_WorkDone; ///< to call on the main thread, guarded by m_Mutex
  plAtomicInteger32 m_iPendingCommands = 0;
  plAtomicBool m_bShelvesRequested = true;
  plTime m_LastShelves;

  // worker-thread state
  plDiversionAgent m_Agent;
  plInt64 m_iLastJournal = -1;
  plTime m_LastOtherChanges;
  plTime m_LastLocks;
  plTime m_LastAccess;
  plTime m_LastBranches;
  plTime m_LastConflicts;
  plDynamicArray<plString> m_KnownConflicts; ///< what dv status last said
  plString m_sAccessRepoId; ///< the repository m_LastAccess asked about
  plTime m_LastPausedDiff;
};
