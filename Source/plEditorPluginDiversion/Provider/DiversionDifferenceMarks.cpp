#include <plEditorPluginDiversion/EditorPluginDiversionPCH.h>

#include <plEditorPluginDiversion/Provider/DiversionDifferenceMarks.h>

#include <EditorUi/Widgets.h>
#include <ToolsFoundation/Object/DocumentObjectManager.h>

namespace
{
  plStringView LetterOf(plDiversionProvider::MarkKind kind)
  {
    switch (kind)
    {
      case plDiversionProvider::MarkKind::Added:
        return "+";
      case plDiversionProvider::MarkKind::Removed:
        return "\xE2\x88\x92"; // minus sign
      default:
        return "~";
    }
  }
} // namespace

plDiversionDifferenceMarks::plDiversionDifferenceMarks()
  : m_sIcon(plEditorUi::IconPath(plEditorUi::Icon::Clock))
{
}

plColorGammaUB plDiversionDifferenceMarks::GetColor(plDiversionProvider::MarkKind kind)
{
  switch (kind)
  {
    case plDiversionProvider::MarkKind::Added:
      return plColorGammaUB(0x73, 0xC3, 0x91);
    case plDiversionProvider::MarkKind::Removed:
      return plColorGammaUB(0xD7, 0x82, 0x78);
    default:
      return plColorGammaUB(0xE8, 0xB0, 0x4A);
  }
}

const plDiversionProvider::CompareInfo* plDiversionDifferenceMarks::Find(const plDocumentObject* pObject, bool& out_bOld, plUuid& out_guid)
{
  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  if (pProvider == nullptr || pObject == nullptr || pObject->GetDocumentObjectManager() == nullptr)
    return nullptr;

  const plDiversionProvider::CompareInfo* pInfo = pProvider->FindComparison(pObject->GetDocumentObjectManager()->GetDocument(), out_bOld);
  if (pInfo == nullptr || !pInfo->m_bTree)
    return nullptr;

  out_guid = plDiversionProvider::FromCompareGuid(*pInfo, pObject->GetGuid(), out_bOld);
  return pInfo;
}

bool plDiversionDifferenceMarks::Decorate(const plDocumentObject* pObject, plEditorUiRowDecoration& inout_decoration) const
{
  bool bOld = false;
  plUuid guid;
  const plDiversionProvider::CompareInfo* pInfo = Find(pObject, bOld, guid);
  if (pInfo == nullptr || !pInfo->m_bHighlight)
    return false;

  if (const plDiversionProvider::Mark* pMark = pInfo->m_Marks.GetValue(guid))
  {
    plObjectRowBadge& badge = inout_decoration.m_Badges.ExpandAndGetRef();
    badge.m_sText = LetterOf(pMark->m_Kind);
    badge.m_Color = GetColor(pMark->m_Kind);

    plStringBuilder sTooltip;
    switch (pMark->m_Kind)
    {
      case plDiversionProvider::MarkKind::Added:
        sTooltip = "Only in yours";
        break;
      case plDiversionProvider::MarkKind::Removed:
        sTooltip = "Only in the old version: yours removed it";
        break;
      default:
        sTooltip = bOld ? "Changed in yours" : "Changed";
        break;
    }

    if (!pMark->m_sDetail.IsEmpty())
      sTooltip.Append("\n", pMark->m_sDetail);

    inout_decoration.m_sTooltip = sTooltip;
    badge.m_sTooltip = sTooltip;
    return true;
  }

  // a difference further down, maybe in a collapsed group
  const plHashSet<plUuid>& ancestors = bOld ? pInfo->m_AncestorsInOld : pInfo->m_AncestorsInYours;
  if (!ancestors.Contains(guid))
    return false;

  plObjectRowBadge& dot = inout_decoration.m_Badges.ExpandAndGetRef();
  dot.m_bDot = true;
  dot.m_Color = GetColor(plDiversionProvider::MarkKind::Changed);
  dot.m_sTooltip = "Differences inside";
  return true;
}

void plDiversionDifferenceMarks::GetFilters(const plDocument* pDocument, plDynamicArray<plEditorUiTreeFilter>& out_filters) const
{
  plDiversionProvider* pProvider = plDiversionProvider::GetSingleton();
  bool bOld = false;
  const plDiversionProvider::CompareInfo* pInfo = pProvider != nullptr ? pProvider->FindComparison(pDocument, bOld) : nullptr;
  if (pInfo == nullptr || !pInfo->m_bTree)
    return;

  plEditorUiTreeFilter& filter = out_filters.ExpandAndGetRef();
  filter.m_sId = FilterId;
  filter.m_sGroup = "Version control";
  filter.m_sLabel = "Only differences";
  filter.m_Color = GetColor(plDiversionProvider::MarkKind::Changed);
}

bool plDiversionDifferenceMarks::MatchFilter(const plDocumentObject* pObject, plStringView sFilterId) const
{
  if (sFilterId != FilterId)
    return false;

  bool bOld = false;
  plUuid guid;
  const plDiversionProvider::CompareInfo* pInfo = Find(pObject, bOld, guid);
  return pInfo != nullptr && pInfo->m_Marks.Contains(guid);
}

bool plDiversionDifferenceMarks::GetState(const plPropertyGridModel::Row& row, bool& out_bActive, bool& out_bMixed, plStringBuilder& out_sTooltip) const
{
  // whole values only, not the elements of a list
  if (row.m_pProperty == nullptr || row.m_Items.IsEmpty() || row.m_Items[0].m_Index.IsValid())
    return false;

  for (const plPropertySelection& item : row.m_Items)
  {
    bool bOld = false;
    plUuid guid;
    const plDiversionProvider::CompareInfo* pInfo = Find(item.m_pObject, bOld, guid);
    if (pInfo == nullptr || !pInfo->m_bHighlight)
      continue;

    const auto* pChanges = pInfo->m_PropertyChanges.GetValue(guid);
    if (pChanges == nullptr)
      continue;

    for (const plDiversionProvider::PropertyChange& change : *pChanges)
    {
      if (change.m_sProperty != row.m_pProperty->GetPropertyName())
        continue;

      out_bActive = true;
      out_bMixed = false;

      const plString& sOther = bOld ? change.m_sNew : change.m_sOld;
      if (bOld)
        out_sTooltip = sOther.IsEmpty() ? plStringBuilder("Yours differs") : plStringBuilder("Yours: ", sOther);
      else if (sOther.IsEmpty())
        out_sTooltip.SetFormat("Differs from {}", pInfo->m_sLabel);
      else
        out_sTooltip.SetFormat("Was {} in {}", sOther, pInfo->m_sLabel);

      return true;
    }
  }

  return false;
}

plStringView plDiversionDifferenceMarks::GetIconResource() const
{
  return m_sIcon;
}
