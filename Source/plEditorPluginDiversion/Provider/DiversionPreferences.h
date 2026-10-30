#pragma once

#include <EditorFramework/Preferences/Preferences.h>

/// The Diversion page of the editor's preferences, per project and per user.
class plDiversionProjectPreferences : public plPreferences
{
  PL_ADD_DYNAMIC_REFLECTION(plDiversionProjectPreferences, plPreferences);

public:
  plDiversionProjectPreferences();

  /// Modified / Added / Deleted letters on Asset Browser tiles.
  bool m_bShowBadges = true;

  /// The eye marker on files someone else is editing or changed on another branch.
  bool m_bShowOthersEditing = true;

  /// Offer to add AssetCache/ and Plugins/ to the workspace's .dvignore when the project opens.
  bool m_bCheckIgnoreFile = true;

  /// A banner on a document someone else is editing or changed on another branch. Opening is never blocked.
  bool m_bWarnOthersEditing = true;

  /// Ask to lock a lockable file on its first edit, and offer locks when saving (plans with hard locks).
  bool m_bAskToLock = true;

  /// The file kinds the lock prompts are about, separated by semicolons.
  plString m_sLockableFiles = "*.plScene;*.plPrefab;*.plMeshAsset;*.plTextureAsset";

  /// Repositories whose plan refused a hard lock, separated by semicolons; their lock actions stay hidden.
  plString m_sReposWithoutLocks;
};
