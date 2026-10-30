#pragma once

#include <GuiFoundation/Action/BaseActions.h>

/// The Diversion menu in the menu bar, and the menu the status row's version control fields open.
class plDiversionActions
{
public:
  static void RegisterActions();
  static void UnregisterActions();

  /// Fills plVersionControl::StatusMenuMapping, which this registers.
  static void MapStatusMenu();

  /// Adds the Diversion menu to a menu bar mapping.
  static void MapMenuBar(plStringView sMapping);

  static plActionDescriptorHandle s_hMenu;
  static plActionDescriptorHandle s_hPauseSync;
  static plActionDescriptorHandle s_hRefresh;
  static plActionDescriptorHandle s_hOpenInDiversion;
  static plActionDescriptorHandle s_hCheckIgnoreFile;
  static plActionDescriptorHandle s_hSettings;
};

class plDiversionAction : public plButtonAction
{
  PL_ADD_DYNAMIC_REFLECTION(plDiversionAction, plButtonAction);

public:
  enum class Type
  {
    PauseSync,
    Refresh,
    OpenInDiversion,
    CheckIgnoreFile,
    Settings,
  };

  plDiversionAction(const plActionContext& context, const char* szName, Type type);
  ~plDiversionAction();

  virtual void Execute(const plVariant& value) override;

private:
  void UpdateState();

  Type m_Type;
  plEventSubscriptionID m_Subscription = 0;
};
