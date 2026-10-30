#pragma once

#include <plEditorPluginDiversion/Provider/DiversionProvider.h>

#include <EditorFramework/GUI/TreeExtension.h>
#include <GuiFoundation/PropertyGrid/PropertyGridExtension.h>

/// Compare's marks outside the 3D views (D34, D35): in the Scene Tree of yours and of the old version a + / ~ / − letter
/// per object that differs, a dot on objects with one below them, what changed in the tooltip and an "Only
/// differences" filter; in the Properties panel a marker on each changed value with the other version's in its
/// tooltip. Registered while a project is attached.
class plDiversionDifferenceMarks : public plEditorUiTreeExtension, public plPropertyGridRowDecorator
{
public:
  plDiversionDifferenceMarks();

  static constexpr const char* FilterId = "diversion.differences";

  /// The marks' colours: added, removed, changed.
  static plColorGammaUB GetColor(plDiversionProvider::MarkKind kind);

  // plEditorUiTreeExtension
  virtual bool Decorate(const plDocumentObject* pObject, plEditorUiRowDecoration& inout_decoration) const override;
  virtual void GetFilters(const plDocument* pDocument, plDynamicArray<plEditorUiTreeFilter>& out_filters) const override;
  virtual bool MatchFilter(const plDocumentObject* pObject, plStringView sFilterId) const override;

  // plPropertyGridRowDecorator
  virtual bool GetState(const plPropertyGridModel::Row& row, bool& out_bActive, bool& out_bMixed, plStringBuilder& out_sTooltip) const override;
  virtual void Toggle(const plPropertyGridModel::Row& row) const override {}
  virtual plStringView GetIconResource() const override;

private:
  /// The comparison pObject's document takes part in and the object's guid in yours; null when none.
  static const plDiversionProvider::CompareInfo* Find(const plDocumentObject* pObject, bool& out_bOld, plUuid& out_guid);

  plString m_sIcon; ///< the changed values' marker
};
