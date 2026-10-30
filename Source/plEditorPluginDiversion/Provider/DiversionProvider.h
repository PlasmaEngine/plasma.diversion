#pragma once

#include <plEditorPluginDiversion/Client/DiversionPoller.h>
#include <plEditorPluginDiversion/EditorPluginDiversionDLL.h>

#include <EditorFramework/Assets/AssetBrowserContextMenu.h>
#include <EditorFramework/Document/DocumentNotices.h>
#include <EditorFramework/VersionControl/VersionControl.h>
#include <Foundation/Containers/HashSet.h>
#include <Foundation/Types/UniquePtr.h>
#include <Foundation/Types/Uuid.h>
#include <ToolsFoundation/Document/DocumentManager.h>

#include <functional>

class plDiversionDifferenceMarks;
class plDiversionProjectPreferences;
class plPreferences;
struct plDocumentEvent;

/// Diversion as the editor's version control provider: serves projects that lie in a Diversion workspace.
///
/// A plDiversionPoller asks the local Diversion agent for the workspace's changes, sync state, who else is editing and
/// the hard locks; this class turns its snapshots into per-file states for the Asset Browser and the status row, and
/// into banners on open documents.
///
/// Locks: hard-lock actions show unless the repository's plan has none (decision D19): known from the user's plan when
/// they own the repository, otherwise from Diversion's first refusal, remembered per repository. The first edit of a
/// lockable file asks to lock it (D4), saving offers locks for unlocked files, and committing releases your locks on the
/// committed files unless you keep them (D24). The first edit of a file someone else is editing says so once (D23).
class PL_EDITORPLUGINDIVERSION_DLL plDiversionProvider : public plVersionControlProvider
{
public:
  plDiversionProvider();
  ~plDiversionProvider();

  static plDiversionProvider* GetSingleton() { return s_pSingleton; }

  // plVersionControlProvider
  plStringView GetName() const override { return "Diversion"; }
  bool Claims(plStringView sProjectDir) override;
  void Attach(plStringView sProjectDir) override;
  void Detach() override;
  void Update() override;
  void GetStatus(plVersionControlStatus& out_status) const override;
  void GetFileInfo(plStringView sAbsolutePath, plVersionControlFileInfo& out_info) const override;
  bool IsProviderFile(plStringView sAbsolutePath) const override;

  bool IsAttached() const { return m_pPoller != nullptr; }
  bool IsConnected() const;
  bool IsSyncPaused() const;

  void SetSyncPaused(bool bPaused);
  void Refresh();

  /// Opens the workspace in the Diversion desktop app (or the web app).
  void OpenInDiversion();

  /// Offers to add the folders the editor rebuilds locally to the workspace's .dvignore. bAlways asks even when the
  /// preference turned the check off, and says so when nothing is missing.
  void CheckIgnoreFile(bool bAlways);

  /// The workspace root's folder, the project's path below it ("Shatterfall"), empty when they are the same.
  plString GetProjectPrefix() const;

  /// The newest snapshot, null before the first one; GetSnapshotVersion changes with every new one.
  const plDiversionSnapshot* GetSnapshot() const { return m_pSnapshot.Borrow(); }
  plUInt32 GetSnapshotVersion() const { return m_uiSnapshotVersion; }

  /// Whether a commit, undo or shelf command is still running.
  bool IsBusy() const;

  /// Calls the callbacks of commands and requests that finished. Update() has this run after the frame; a caller that
  /// waits for a result outside the frame (an MCP tool) calls it itself.
  void DeliverResults();

  /// Reports a finished command: whether it worked, and dv's message (or the new commit id).
  using Done = plDelegate<void(bool bSucceeded, plStringView sMessage)>;

  /// Paths are workspace-relative; a moved file goes by its new path.
  void Commit(const plDynamicArray<plString>& paths, plStringView sMessage, Done onDone);

  /// Back to the last commit. New files among the paths stay unless bDeleteNew.
  void Undo(const plDynamicArray<plString>& paths, bool bDeleteNew, Done onDone);

  void Shelve(plStringView sName, const plDynamicArray<plString>& paths, Done onDone);
  void ApplyShelf(plStringView sShelfId, Done onDone);
  void DeleteShelf(plStringView sShelfId, Done onDone);

  /// \name Locks
  ///@{

  plDiversionLockSupport GetLockSupport() const { return m_LockSupport; }

  /// Whether lock actions show: connected, and the plan is not known to lack hard locks.
  bool CanUseLocks() const { return IsConnected() && m_LockSupport != plDiversionLockSupport::No; }

  /// Whether the user may force-release someone else's lock: repository owners and admins.
  bool CanForceUnlock() const;

  /// Whether the lock prompts are about the file: the kinds in the preferences (scenes, prefabs, meshes, textures).
  bool IsLockable(plStringView sAbsolutePath) const;

  /// Takes, releases or (bForce) force-releases the lock on a file of the workspace.
  void Lock(plStringView sAbsolutePath, Done onDone);
  void Unlock(plStringView sAbsolutePath, bool bForce, Done onDone);

  /// Releases your locks among the workspace-relative paths, e.g. the files just committed (D24).
  void ReleaseOwnLocks(const plDynamicArray<plString>& paths);

  /// Asks before force-releasing someone else's lock, then does it.
  void AskAndForceUnlock(plStringView sAbsolutePath);

  /// Opens the file's document read-only, or turns the open one read-only.
  static void OpenReadOnly(plStringView sAbsolutePath);

  /// Asks, then takes one file back to the last commit; a new file is only deleted when ticked (D15).
  void AskAndUndo(plStringView sAbsolutePath, bool bAdded);

  ///@}

  /// \name Branches and sync (the title bar's chips)
  ///@{

  void ShowMenu(Menu menu, plEditorUiWindowHost& ref_host, const plVec2I32& vScreenPosition) override;
  plString DescribeIncomingChange(plStringView sAbsolutePath) const override;

  /// Every local change of the newest snapshot: what a commit of everything would include.
  void GetPendingFiles(plDynamicArray<plString>& out_files) const override;

  /// Switches the workspace to a branch: unsaved documents are saved or dropped first (D26), pending changes are
  /// shelved or taken along (D12).
  void SwitchBranch(plStringView sBranch);

  /// Asks for a name and creates a branch from the current commit, switching to it unless told not to.
  void CreateBranch();

  /// Switches without asking: pending changes are shelved or taken along. For callers that asked already (MCP).
  void Checkout(plStringView sBranch, bool bShelve, Done onDone);

  /// Whether the agent's version is in the range the package was tested with (D30).
  bool IsVersionTested() const;

  static constexpr const char* TestedVersions = "1.6.567 to 1.6.x";

  ///@}
  /// \name History and conflicts
  ///@{

  using HistoryDone = std::function<void(bool bSucceeded, const plDynamicArray<plDiversionCommit>& commits, plStringView sError)>;

  /// The commits that changed a file on the current branch, newest first.
  void RequestHistory(plStringView sAbsolutePath, HistoryDone onDone);

  /// Brings the file back as it was in a commit: a change in the workspace like any other (D27).
  void Restore(plStringView sAbsolutePath, plStringView sCommitId, Done onDone);

  /// Opens the file as it was in a ref (a commit, a branch) read-only, beside the current document, with a banner
  /// that counts the object-level differences. A scene or prefab marks them in both documents' trees and 3D views,
  /// with Previous / Next; other documents list them.
  void Compare(plStringView sAbsolutePath, plStringView sRef, plStringView sLabel);

  /// Whether the document is an old version that Compare opened.
  bool IsCompareDocument(const plDocument* pDocument) const { return pDocument != nullptr && m_CompareDocuments.Contains(pDocument->GetGuid()); }

  struct Conflict
  {
    plString m_sAbsPath;
    plString m_sBackupAbsPath; ///< your unsynced version, for a backup conflict
    bool m_bHeld = false;      ///< synced edits clash with an incoming commit; the workspace stays behind
  };

  /// The conflicts in the current snapshot, found once per snapshot: the status, asked for several times a frame,
  /// counts them.
  void GetConflicts(plDynamicArray<Conflict>& out_conflicts) const;

  /// Resolves a conflict for your version or for theirs, after asking (D28). A held conflict is resolved for every
  /// held file at once: Diversion updates the workspace as a whole.
  void AskAndResolve(const Conflict& conflict, bool bKeepMine);

  /// The other side of a conflict, read-only beside yours.
  void CompareConflict(const Conflict& conflict);

  ///@}

  /// The workspace-relative path of a file in the workspace; empty outside it.
  plString ToWorkspacePath(plStringView sAbsolutePath) const;
  plString ToAbsolutePath(plStringView sWorkspacePath) const;

  /// "10:42" today, "20 Oct 10:42" before; local time.
  static plString FormatClock(plTimestamp time);

  /// "5 min ago", "3 h ago", "2 days ago".
  static plString FormatAgo(plTimestamp time);

private:
  void ApplySnapshot(const plDiversionSnapshot& snapshot);
  void FindConflicts(plDynamicArray<Conflict>& out_conflicts) const;
  void UpdateLockSupport(const plDiversionSnapshot& snapshot);
  void RunLockCommand(plArrayPtr<const plStringView> args, Done onDone);
  void LockRefusedByPlan();

  void DocumentNoticesRequested(const plDocumentNotices::Request& request);
  void AddLockNotice(plDocument* pDocument, const plVersionControlFileInfo& info, plDynamicArray<plDocumentNotice>& ref_notices) const;
  void AddEditingNotice(plDocument* pDocument, const plVersionControlFileInfo& info, plDynamicArray<plDocumentNotice>& ref_notices) const;

  /// For a file that holds the workspace back: whose commit it meets, and Open Conflicts. Returns whether it added one.
  bool AddConflictNotice(plDocument* pDocument, const plVersionControlFileInfo& info, plDynamicArray<plDocumentNotice>& ref_notices) const;
  void DocumentEventHandler(const plDocumentEvent& e);
  void FirstEdit(const plDocument* pDocument);
  void AskToLock(plStringView sAbsolutePath);
  void SaveRequested(plDocumentManager::SaveRequest& ref_request);
  void ContextMenuRequested(const plAssetBrowserContextMenuEvent& e);

  void ShowBranchMenu(plEditorUiWindowHost& ref_host, const plVec2I32& vScreenPosition);
  void ShowSyncMenu(plEditorUiWindowHost& ref_host, const plVec2I32& vScreenPosition);
  void RunCheckout(plStringView sBranch, bool bShelve, const plDynamicArray<plString>& reopen);

  /// Notes that a command of the editor's own is about to rewrite a file (an empty path: any file of the workspace), so
  /// the reload banner names it ("restoring dv.commit.7") and not the last commit the workspace received.
  void NoteOwnChange(plStringView sAbsolutePath, plStringView sDescription);

public:
  /// How a scene object differs from the other version: the Scene Tree's letter and the 3D views' colour.
  enum class MarkKind : plUInt8
  {
    Added,   ///< only in yours
    Removed, ///< only in the old version
    Changed, ///< in both, with other values or components
  };

  struct Mark
  {
    MarkKind m_Kind = MarkKind::Changed;
    plString m_sName;   ///< the object's name, or its type
    plString m_sDetail; ///< what changed, a line each: "Local Position (4, 0, 2) → (5.5, 0, 2)", "+ Mesh Component"
  };

  /// A property whose value differs, for the Properties panel.
  struct PropertyChange
  {
    plString m_sProperty;
    plString m_sOld; ///< the values as text, empty when there is no short form
    plString m_sNew;
  };

  /// What a Compare document shows in its banner, and marks in both documents. Object guids are yours: the copy's are
  /// the old version's combined with m_Seed.
  struct CompareInfo
  {
    plString m_sSourcePath; ///< the file it is an old version of
    plString m_sLabel;      ///< "dv.commit.11 by Sam", "Theirs (dv.commit.14)"
    plString m_sRef;
    plString m_sName;                       ///< in the copy's file name and tab, "Main (theirs)"; the ref when empty
    plString m_sRawPath;                    ///< the old version's bytes as they came, to compare with
    plTimestamp m_SourceModified;           ///< of the file the differences were found with
    bool m_bDiffed = false;
    plDynamicArray<plString> m_Differences; ///< "+ Lantern_03", "~ Well: Position"
    plUInt32 m_uiAdded = 0;
    plUInt32 m_uiRemoved = 0;
    plUInt32 m_uiChanged = 0;
    bool m_bConflict = false; ///< opened for a conflict: the banner offers Keep mine / Keep theirs

    plUuid m_SourceDocument;  ///< yours, open beside it; its guid stays the same through a reload
    plUuid m_OldDocumentId;   ///< the old version's own document guid, as in its file
    plUuid m_Seed;            ///< the copy's object guids are the old version's combined with it
    bool m_bTree = false;     ///< a scene or prefab: the differences are marked in its tree and views, not listed
    bool m_bHighlight = true; ///< the marks show
    plUInt32 m_uiSettingsChanged = 0; ///< differences outside the scene objects, e.g. in the scene's settings

    plHashTable<plUuid, Mark> m_Marks;                                         ///< scene objects that differ
    plHashSet<plUuid> m_AncestorsInYours;                                      ///< objects with a mark below them in yours
    plHashSet<plUuid> m_AncestorsInOld;                                        ///< and in the old version
    plDynamicArray<plUuid> m_Steps;                                            ///< the marks Previous / Next go through, in tree order
    plInt32 m_iStep = -1;                                                      ///< into m_Steps; -1 before the first
    plHashTable<plUuid, plHybridArray<PropertyChange, 2>> m_PropertyChanges; ///< by object (components and the like too)
  };

  /// The comparison a document takes part in, as the old version or as yours (out_bOld), null when none does.
  CompareInfo* FindComparison(const plDocument* pDocument, bool& out_bOld);

  /// An object of a document of the comparison by the guid it has in yours, and back.
  static plUuid ToCompareGuid(const CompareInfo& info, const plUuid& guid, bool bOld);
  static plUuid FromCompareGuid(const CompareInfo& info, const plUuid& guid, bool bOld);

  /// Shows or hides the marks of a comparison in both documents' 3D views and trees.
  void SetHighlight(const plUuid& compareDocument, bool bShow);

  /// Selects the next (or previous) difference in both documents and frames it in their views.
  void StepDifference(const plUuid& compareDocument, bool bForward);

private:
  /// Where Compare puts the old versions: in the project's AssetCache, which holds no assets and which version control
  /// ignores.
  static plString GetCompareFolder();

  void OpenCompareDocument(const plDynamicArray<plUInt8>& content, CompareInfo&& info);
  void AddCompareNotice(plDocument* pDocument, plDynamicArray<plDocumentNotice>& ref_notices) const;
  void ShowDifferences(const CompareInfo& info) const;
  void Resolve(const Conflict& conflict, bool bKeepMine);

  /// Sends a comparison's marks to the 3D views of both documents (none while hidden) and repaints their trees.
  void ApplyHighlight(const plUuid& compareDocument);

  /// Compares again the comparisons of yours that changed on disk, and marks anew (all of them with bApplyAlways).
  void RefreshComparisonsOf(const plUuid& sourceDocument, bool bApplyAlways);

  /// The banner's second line: the counts, and which difference Previous / Next is at.
  static plString DescribeDifferences(const CompareInfo& info);

  /// Highlight / Hide differences, Previous and Next.
  static void AddDifferenceActions(const plUuid& compareDocument, const CompareInfo& info, plDocumentNotice& ref_notice);

  /// Yours, while an old version is compared with it.
  void AddComparedNotice(plDocument* pDocument, plDynamicArray<plDocumentNotice>& ref_notices) const;

  void DocumentManagerEventHandler(const plDocumentManager::Event& e);

  /// Compares two document files object by object: objects one has and the other not, and the properties they differ
  /// in. Names come from a "Name" property where there is one. Scene objects get a mark (MarkDifferences).
  static void DiffDocuments(plStringView sOldPath, plStringView sNewPath, CompareInfo& inout_info);

  /// Compares again when yours changed on disk since (saved, reloaded, restored). True when it did.
  static bool UpdateDifferences(CompareInfo& inout_info);
  void Run(plArrayPtr<const plStringView> args, Done onDone, bool bCommit);
  void GetAllChangedPaths(plDynamicArray<plString>& out_paths) const;
  void PreferencesChanged(plPreferences* pPreferences);

  /// Lower case, forward slashes: Windows paths compare without regard to case.
  static plString MakeKey(plStringView sAbsolutePath);

  static plDiversionProvider* s_pSingleton;

  plString m_sProjectDir;
  plUniquePtr<plDiversionPoller> m_pPoller;
  plSharedPtr<const plDiversionSnapshot> m_pSnapshot;
  plUInt32 m_uiSnapshotVersion = 0;

  plHashTable<plString, plVersionControlFileInfo> m_Files; ///< by MakeKey(), only files in the project folder
  plDynamicArray<Conflict> m_Conflicts;                     ///< of m_pSnapshot
  plUInt32 m_uiChangedFiles = 0;
  plUInt32 m_uiChangedFilesElsewhere = 0; ///< in the workspace, outside the project folder
  plUInt32 m_uiLockedByYou = 0;           ///< files of the project you hold a lock on

  plDiversionProjectPreferences* m_pPreferences = nullptr;
  bool m_bIgnoreChecked = false;

  plDiversionLockSupport m_LockSupport = plDiversionLockSupport::Unknown;
  bool m_bAskToLockThisSession = true; ///< "Don't ask again this session" turns it off
  plHashSet<plString> m_EditedFiles;   ///< keys of files edited since the project opened

  plHashTable<plUuid, CompareInfo> m_CompareDocuments; ///< open read-only versions, by document guid
  plUniquePtr<plDiversionDifferenceMarks> m_pDifferenceMarks; ///< their marks in the Scene Tree and the Properties panel
  bool m_bOpeningCompareDocument = false;                    ///< while OpenCompareDocument opens one

  struct OwnChange
  {
    plString m_sDescription;
    plTime m_Until; ///< the note is stale after this
  };
  plHashTable<plString, OwnChange> m_OwnChanges; ///< by MakeKey(); see NoteOwnChange
  OwnChange m_OwnChangeOfAll;                    ///< a branch switch rewrites any file

  bool m_bVersionWarned = false;                       ///< the untested version went to the log once
  bool m_bDeliveryQueued = false;                      ///< DeliverResults waits for the end of the frame
};
