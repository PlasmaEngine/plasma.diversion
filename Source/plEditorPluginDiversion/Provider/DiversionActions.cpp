#include <plEditorPluginDiversion/EditorPluginDiversionPCH.h>

#include <plEditorPluginDiversion/Provider/DiversionActions.h>
#include <plEditorPluginDiversion/Provider/DiversionProvider.h>

#include <EditorFramework/Dialogs/PreferencesEngineDlg.h>
#include <EditorFramework/VersionControl/VersionControl.h>
#include <GuiFoundation/Action/ActionManager.h>
#include <GuiFoundation/Action/ActionMapManager.h>

plActionDescriptorHandle plDiversionActions::s_hMenu;
plActionDescriptorHandle plDiversionActions::s_hPauseSync;
plActionDescriptorHandle plDiversionActions::s_hRefresh;
plActionDescriptorHandle plDiversionActions::s_hOpenInDiversion;
plActionDescriptorHandle plDiversionActions::s_hCheckIgnoreFile;
plActionDescriptorHandle plDiversionActions::s_hSettings;

void plDiversionActions::RegisterActions()
{
  s_hMenu = PL_REGISTER_MENU("G.Diversion");
  s_hPauseSync = PL_REGISTER_ACTION_1("Diversion.PauseSync", plActionScope::Global, "Diversion", "", plDiversionAction, plDiversionAction::Type::PauseSync);
  s_hRefresh = PL_REGISTER_ACTION_1("Diversion.Refresh", plActionScope::Global, "Diversion", "", plDiversionAction, plDiversionAction::Type::Refresh);
  s_hOpenInDiversion = PL_REGISTER_ACTION_1("Diversion.OpenInDiversion", plActionScope::Global, "Diversion", "", plDiversionAction, plDiversionAction::Type::OpenInDiversion);
  s_hCheckIgnoreFile = PL_REGISTER_ACTION_1("Diversion.CheckIgnoreFile", plActionScope::Global, "Diversion", "", plDiversionAction, plDiversionAction::Type::CheckIgnoreFile);
  s_hSettings = PL_REGISTER_ACTION_1("Diversion.Settings", plActionScope::Global, "Diversion", "", plDiversionAction, plDiversionAction::Type::Settings);
}

void plDiversionActions::UnregisterActions()
{
  plActionMapManager::UnregisterActionMap(plVersionControl::StatusMenuMapping).IgnoreResult();

  plActionManager::UnregisterAction(s_hMenu);
  plActionManager::UnregisterAction(s_hPauseSync);
  plActionManager::UnregisterAction(s_hRefresh);
  plActionManager::UnregisterAction(s_hOpenInDiversion);
  plActionManager::UnregisterAction(s_hCheckIgnoreFile);
  plActionManager::UnregisterAction(s_hSettings);
}

void plDiversionActions::MapStatusMenu()
{
  if (plActionMapManager::RegisterActionMap(plVersionControl::StatusMenuMapping).Failed())
    return;

  plActionMap* pMap = plActionMapManager::GetActionMap(plVersionControl::StatusMenuMapping);
  pMap->MapAction(s_hPauseSync, "", 1.0f);
  pMap->MapAction(s_hRefresh, "", 2.0f);
  pMap->MapAction(s_hOpenInDiversion, "", 3.0f);
  pMap->MapAction(s_hSettings, "", 4.0f);
}

void plDiversionActions::MapMenuBar(plStringView sMapping)
{
  plActionMap* pMap = plActionMapManager::GetActionMap(sMapping);
  if (pMap == nullptr)
    return;

  // after Tools, before Help
  pMap->MapAction(s_hMenu, "", 6.5f);
  pMap->MapAction(s_hPauseSync, "G.Diversion", 1.0f);
  pMap->MapAction(s_hRefresh, "G.Diversion", 2.0f);
  pMap->MapAction(s_hOpenInDiversion, "G.Diversion", 3.0f);
  pMap->MapAction(s_hCheckIgnoreFile, "G.Diversion", 4.0f);
  pMap->MapAction(s_hSettings, "G.Diversion", 5.0f);
}

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plDiversionAction, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plDiversionAction::plDiversionAction(const plActionContext& context, const char* szName, Type type)
  : plButtonAction(context, szName, type == Type::PauseSync, "")
  , m_Type(type)
{
  m_Subscription = plVersionControl::s_Events.AddEventHandler([this](const plVersionControlEvent& e)
    {
      if (e.m_Type != plVersionControlEvent::Type::FilesChanged)
        UpdateState(); });

  UpdateState();
}

plDiversionAction::~plDiversionAction()
{
  plVersionControl::s_Events.RemoveEventHandler(m_Subscription);
}

void plDiversionAction::UpdateState()
{
  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  const bool bAttached = pProvider != nullptr && plVersionControl::GetActiveProvider() == pProvider;
  const bool bConnected = bAttached && pProvider->IsConnected();

  switch (m_Type)
  {
    case Type::PauseSync:
      SetEnabled(bConnected, false);
      SetChecked(bConnected && pProvider->IsSyncPaused(), false);
      break;

    case Type::Refresh:
    case Type::Settings:
      SetEnabled(bAttached, false);
      break;

    case Type::OpenInDiversion:
    case Type::CheckIgnoreFile:
      SetEnabled(bConnected, false);
      break;
  }

  TriggerUpdate();
}

void plDiversionAction::Execute(const plVariant& value)
{
  PL_IGNORE_UNUSED(value);

  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  if (pProvider == nullptr || !pProvider->IsAttached())
    return;

  switch (m_Type)
  {
    case Type::PauseSync:
      pProvider->SetSyncPaused(!pProvider->IsSyncPaused());
      break;

    case Type::Refresh:
      pProvider->Refresh();
      break;

    case Type::OpenInDiversion:
      pProvider->OpenInDiversion();
      break;

    case Type::CheckIgnoreFile:
      pProvider->CheckIgnoreFile(true);
      break;

    case Type::Settings:
      plPreferencesEngineDlg::Run();
      break;
  }
}
