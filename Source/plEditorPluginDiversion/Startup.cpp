#include <plEditorPluginDiversion/EditorPluginDiversionPCH.h>

#include <plEditorPluginDiversion/Provider/DiversionActions.h>
#include <plEditorPluginDiversion/Provider/DiversionProvider.h>

#include <EditorFramework/Actions/ProjectActions.h>
#include <EditorFramework/VersionControl/VersionControl.h>
#include <EditorUi/Runtime.h>
#include <Foundation/Configuration/Plugin.h>

// The editor half of the Diversion package: registers Diversion as a version control provider, which serves projects
// inside a Diversion workspace, and adds the Diversion menu. Its MCP tools live in plEditorPluginDiversionMcp, loaded
// only once the MCP package is: a hard link would make this plugin fail to load in every project without plasma.mcp.

static plDiversionProvider* s_pProvider = nullptr;

namespace
{
  constexpr const char* s_szMcpPlugin = "plEditorPluginPlasmaMcp";
  constexpr const char* s_szDiversionMcpPlugin = "plEditorPluginDiversionMcp";

  // Owns the deferred load; destroying it on unload cancels a load that has not run yet.
  plEditorUiTimerGroup* s_pDeferredLoadContext = nullptr;

  bool IsPluginLoaded(plStringView sName)
  {
    plDynamicArray<plPlugin::PluginInfo> infos;
    plPlugin::GetAllPluginInfos(infos);

    for (const auto& info : infos)
    {
      if (info.m_sName.GetFileName() == sName)
        return true;
    }

    return false;
  }

  void LoadMcpToolsIfPossible()
  {
    if (IsPluginLoaded(s_szMcpPlugin) && !IsPluginLoaded(s_szDiversionMcpPlugin) && plPlugin::ExistsPluginFile(s_szDiversionMcpPlugin))
      plPlugin::LoadPlugin(s_szDiversionMcpPlugin).IgnoreResult();
  }

  void PluginEventHandler(const plPluginEvent& e)
  {
    // loading a plugin broadcasts plugin events, and this handler runs inside such a broadcast: the load waits
    if (e.m_EventType == plPluginEvent::AfterPluginChanges && s_pDeferredLoadContext != nullptr)
    {
      s_pDeferredLoadContext->Start(plTime::MakeZero(), []()
        { LoadMcpToolsIfPossible(); });
    }
  }
} // namespace

static void MapProjectMenu(plStringView sMapping)
{
  plDiversionActions::MapMenuBar(sMapping);
}

PL_PLUGIN_ON_LOADED()
{
  plDiversionActions::RegisterActions();
  plDiversionActions::MapStatusMenu();
  plProjectActions::AddProjectMenuMapper(&MapProjectMenu);

  s_pProvider = PL_DEFAULT_NEW(plDiversionProvider);
  plVersionControl::RegisterProvider(s_pProvider);

  s_pDeferredLoadContext = PL_DEFAULT_NEW(plEditorUiTimerGroup);
  plPlugin::Events().AddEventHandler(PluginEventHandler);
}

PL_PLUGIN_ON_UNLOADED()
{
  plPlugin::Events().RemoveEventHandler(PluginEventHandler);
  PL_DEFAULT_DELETE(s_pDeferredLoadContext);

  plVersionControl::UnregisterProvider(s_pProvider);
  PL_DEFAULT_DELETE(s_pProvider);

  plProjectActions::RemoveProjectMenuMapper(&MapProjectMenu);
  plDiversionActions::UnregisterActions();
}
