#include <plEditorPluginDiversion/EditorPluginDiversionPCH.h>

#include <plEditorPluginDiversion/Provider/DiversionPreferences.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plDiversionProjectPreferences, 1, plRTTIDefaultAllocator<plDiversionProjectPreferences>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("ShowBadges", m_bShowBadges)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_MEMBER_PROPERTY("ShowOthersEditing", m_bShowOthersEditing)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_MEMBER_PROPERTY("CheckIgnoreFile", m_bCheckIgnoreFile)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_MEMBER_PROPERTY("WarnOthersEditing", m_bWarnOthersEditing)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_MEMBER_PROPERTY("AskToLock", m_bAskToLock)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_MEMBER_PROPERTY("LockableFiles", m_sLockableFiles)->AddAttributes(new plDefaultValueAttribute(plStringView("*.plScene;*.plPrefab;*.plMeshAsset;*.plTextureAsset"))),
    PL_MEMBER_PROPERTY("ReposWithoutLocks", m_sReposWithoutLocks)->AddAttributes(new plHiddenAttribute()),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plDiversionProjectPreferences::plDiversionProjectPreferences()
  : plPreferences(Domain::Project, "Diversion")
{
}
