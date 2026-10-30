#include <plEditorPluginDiversion/EditorPluginDiversionPCH.h>

#include <plEditorPluginDiversion/Provider/DiversionDifferenceMarks.h>
#include <plEditorPluginDiversion/Provider/DiversionProvider.h>

#include <EditorFramework/Document/GameObjectDocument.h>
#include <EditorUi/Runtime.h>
#include <EditorUi/Widgets.h>
#include <Foundation/IO/OSFile.h>
#include <Foundation/Serialization/AbstractObjectGraph.h>
#include <ToolsFoundation/Document/Document.h>
#include <ToolsFoundation/Object/DocumentObjectManager.h>
#include <ToolsFoundation/Selection/SelectionManager.h>

namespace
{
  /// At most this many differences are kept for the list.
  constexpr plUInt32 s_uiMaxDifferences = 300;

  /// At most this many objects get a box in the 3D views.
  constexpr plUInt32 s_uiMaxBoxes = 1000;

  /// Values longer than this as text are named, not shown.
  constexpr plUInt32 s_uiMaxValueLength = 40;

  /// The document's root object: no object anyone edits, and saving may give it a new guid.
  bool IsDocumentRoot(const plAbstractObjectNode& node)
  {
    return node.GetType().EndsWith("DocumentRoot");
  }

  bool IsGameObject(const plAbstractObjectNode* pNode)
  {
    return pNode != nullptr && pNode->GetType() == "plGameObject";
  }

  /// A value that only refers to other objects of the graph: children, components, embedded objects. What changed in
  /// those shows on them.
  bool IsReference(const plVariant& value)
  {
    if (value.IsA<plUuid>())
      return true;

    if (!value.IsA<plVariantArray>() || value.Get<plVariantArray>().IsEmpty())
      return false;

    for (const plVariant& element : value.Get<plVariantArray>())
    {
      if (!element.IsA<plUuid>())
        return false;
    }

    return true;
  }

  bool IsUpper(char c)
  {
    return c >= 'A' && c <= 'Z';
  }

  bool IsLower(char c)
  {
    return c >= 'a' && c <= 'z';
  }

  /// "LocalPosition" -> "Local Position", "plMeshComponent" -> "Mesh Component".
  plString MakeReadable(plStringView sName)
  {
    if (sName.StartsWith("pl") && sName.GetElementCount() > 2 && IsUpper(*(sName.GetStartPointer() + 2)))
      sName.Shrink(2, 0);

    plStringBuilder sResult;
    char previous = ' ';
    for (const char* p = sName.GetStartPointer(); p < sName.GetEndPointer(); ++p)
    {
      if (IsUpper(*p) && IsLower(previous))
        sResult.Append(" ");

      sResult.Append(plStringView(p, p + 1));
      previous = *p;
    }

    return sResult;
  }

  /// A name for an object of a document graph: its Name property, or its type.
  plString DescribeNode(const plAbstractObjectNode& node)
  {
    if (const plAbstractObjectNode::Property* pName = node.FindProperty("Name"); pName != nullptr && pName->m_Value.IsA<plString>() && !pName->m_Value.Get<plString>().IsEmpty())
      return pName->m_Value.Get<plString>();

    return MakeReadable(node.GetType());
  }

  void AppendNumber(double fValue, plStringBuilder& inout_sText)
  {
    plStringBuilder sNumber;
    sNumber.SetFormat("{}", plArgF(fValue, 3));

    if (sNumber.FindSubString(".") != nullptr)
    {
      while (sNumber.EndsWith("0"))
        sNumber.Shrink(0, 1);
      if (sNumber.EndsWith("."))
        sNumber.Shrink(0, 1);
    }

    inout_sText.Append(sNumber == "-0" ? plStringView("0") : sNumber.GetView());
  }

  void AppendNumbers(plArrayPtr<const float> values, plStringView sUnit, plStringBuilder& inout_sText)
  {
    inout_sText.Append("(");
    for (plUInt32 i = 0; i < values.GetCount(); ++i)
    {
      if (i > 0)
        inout_sText.Append(", ");

      AppendNumber(values[i], inout_sText);
      inout_sText.Append(sUnit);
    }
    inout_sText.Append(")");
  }

  /// A value as short text: quoted text, an enumeration's value, numbers, vectors, angles and colours. Empty when it has
  /// no short form.
  plString FormatValue(const plVariant& value)
  {
    plStringBuilder sText;

    if (value.IsA<plString>())
    {
      const plString& sValue = value.Get<plString>();

      // enumerations and flags are stored as "plBlendMode::Opaque" or "plFlags::A|plFlags::B"
      if (sValue.StartsWith("pl") && sValue.FindSubString("::") != nullptr && sValue.FindSubString(" ") == nullptr)
      {
        plHybridArray<plStringView, 4> parts;
        sValue.GetView().Split(false, parts, "|");
        for (plStringView sPart : parts)
        {
          if (const char* pColons = sPart.FindLastSubString("::"))
            sPart.SetStartPosition(pColons + 2);

          sText.AppendWithSeparator(" | ", sPart);
        }
      }
      else
      {
        sText.SetFormat("\"{}\"", sValue);
      }
    }
    else if (value.IsA<bool>())
    {
      sText = value.Get<bool>() ? "on" : "off";
    }
    else if (value.IsA<float>() || value.IsA<double>())
    {
      AppendNumber(value.ConvertTo<double>(), sText);
    }
    else if (value.IsNumber())
    {
      sText.SetFormat("{}", value.ConvertTo<plInt64>());
    }
    else if (value.IsA<plAngle>())
    {
      AppendNumber(value.Get<plAngle>().GetDegree(), sText);
      sText.Append("\xC2\xB0");
    }
    else if (value.IsA<plVec2>())
    {
      const plVec2 v = value.Get<plVec2>();
      const float values[] = {v.x, v.y};
      AppendNumbers(values, {}, sText);
    }
    else if (value.IsA<plVec3>())
    {
      const plVec3 v = value.Get<plVec3>();
      const float values[] = {v.x, v.y, v.z};
      AppendNumbers(values, {}, sText);
    }
    else if (value.IsA<plVec4>())
    {
      const plVec4 v = value.Get<plVec4>();
      const float values[] = {v.x, v.y, v.z, v.w};
      AppendNumbers(values, {}, sText);
    }
    else if (value.IsA<plQuat>())
    {
      plAngle x, y, z;
      value.Get<plQuat>().GetAsEulerAngles(x, y, z);
      const float values[] = {x.GetDegree(), y.GetDegree(), z.GetDegree()};
      AppendNumbers(values, "\xC2\xB0", sText);
    }
    else if (value.IsA<plColor>() || value.IsA<plColorGammaUB>())
    {
      const plColorGammaUB color = value.IsA<plColor>() ? plColorGammaUB(value.Get<plColor>()) : value.Get<plColorGammaUB>();
      sText.SetFormat("#{}{}{}", plArgU(color.r, 2, true, 16, true), plArgU(color.g, 2, true, 16, true), plArgU(color.b, 2, true, 16, true));
      if (color.a != 255)
        sText.AppendFormat("{}", plArgU(color.a, 2, true, 16, true));
    }

    if (sText.GetCharacterCount() > s_uiMaxValueLength)
      return {};

    return sText;
  }

  /// Who refers to whom in a document graph: the objects' parents and their children in order.
  struct Tree
  {
    plHashTable<plUuid, plUuid> m_Parents;
    plHashTable<plUuid, plDynamicArray<plUuid>> m_Children;
    plHybridArray<plUuid, 1> m_Roots;
  };

  void BuildTree(const plAbstractObjectGraph& graph, Tree& out_tree)
  {
    for (auto it : graph.GetAllNodes())
    {
      if (IsDocumentRoot(*it.Value()))
        out_tree.m_Roots.PushBack(it.Key());

      auto AddChild = [&](const plVariant& value)
      {
        if (value.IsA<plUuid>() && graph.GetNode(value.Get<plUuid>()) != nullptr)
        {
          out_tree.m_Parents[value.Get<plUuid>()] = it.Key();
          out_tree.m_Children[it.Key()].PushBack(value.Get<plUuid>());
        }
      };

      for (const plAbstractObjectNode::Property& prop : it.Value()->GetProperties())
      {
        if (prop.m_Value.IsA<plVariantArray>())
        {
          for (const plVariant& element : prop.m_Value.Get<plVariantArray>())
            AddChild(element);
        }
        else
        {
          AddChild(prop.m_Value);
        }
      }
    }
  }

  /// The scene object a node belongs to (a component's game object); invalid for the document's own objects, such as
  /// its settings.
  plUuid FindOwner(const plAbstractObjectGraph& graph, const Tree& tree, const plUuid& guid)
  {
    const plUuid* pParent = tree.m_Parents.GetValue(guid);
    while (pParent != nullptr)
    {
      if (IsGameObject(graph.GetNode(*pParent)))
        return *pParent;

      pParent = tree.m_Parents.GetValue(*pParent);
    }

    return {};
  }

  void InsertAncestors(const Tree& tree, const plUuid& guid, plHashSet<plUuid>& inout_ancestors)
  {
    for (const plUuid* pParent = tree.m_Parents.GetValue(guid); pParent != nullptr; pParent = tree.m_Parents.GetValue(*pParent))
      inout_ancestors.Insert(*pParent);
  }
} // namespace

// The comparison's documents.

plDiversionProvider::CompareInfo* plDiversionProvider::FindComparison(const plDocument* pDocument, bool& out_bOld)
{
  if (pDocument == nullptr)
    return nullptr;

  if (CompareInfo* pInfo = m_CompareDocuments.GetValue(pDocument->GetGuid()))
  {
    out_bOld = true;
    return pInfo;
  }

  for (auto it : m_CompareDocuments)
  {
    if (it.Value().m_SourceDocument == pDocument->GetGuid())
    {
      out_bOld = false;
      return &it.Value();
    }
  }

  return nullptr;
}

plUuid plDiversionProvider::ToCompareGuid(const CompareInfo& info, const plUuid& guid, bool bOld)
{
  plUuid result = guid;
  if (bOld)
    result.CombineWithSeed(info.m_Seed);
  return result;
}

plUuid plDiversionProvider::FromCompareGuid(const CompareInfo& info, const plUuid& guid, bool bOld)
{
  plUuid result = guid;
  if (bOld)
    result.RevertCombinationWithSeed(info.m_Seed);
  return result;
}

void plDiversionProvider::DocumentManagerEventHandler(const plDocumentManager::Event& e)
{
  if (e.m_pDocument == nullptr)
    return;

  if (e.m_Type == plDocumentManager::Event::Type::DocumentOpened)
  {
    // an old version the last session left open (the layout reopens it): nothing compares it any more, so it goes
    if (!m_bOpeningCompareDocument && MakeKey(e.m_pDocument->GetDocumentPath()).StartsWith(MakeKey(GetCompareFolder())))
    {
      plEditorUiRuntime::RunAfterFrame([guid = e.m_pDocument->GetGuid()]()
        {
          if (plDocument* pDocument = plDocumentManager::GetDocumentByGuid(guid))
            pDocument->GetDocumentManager()->CloseDocument(pDocument); });
      return;
    }

    // yours again after a reload: its marks come back, for what it is now
    RefreshComparisonsOf(e.m_pDocument->GetGuid(), true);
    return;
  }

  if (e.m_Type == plDocumentManager::Event::Type::DocumentClosing && m_CompareDocuments.Contains(e.m_pDocument->GetGuid()))
  {
    // the old version goes, and with it the marks in yours
    const plUuid guid = e.m_pDocument->GetGuid();
    m_CompareDocuments[guid].m_bHighlight = false;
    ApplyHighlight(guid);
    m_CompareDocuments.Remove(guid);
    plDocumentNotices::NotifyChanged();
  }
}

void plDiversionProvider::RefreshComparisonsOf(const plUuid& sourceDocument, bool bApplyAlways)
{
  bool bChanged = false;
  for (auto it : m_CompareDocuments)
  {
    if (it.Value().m_SourceDocument != sourceDocument)
      continue;

    if (UpdateDifferences(it.Value()) || bApplyAlways)
    {
      ApplyHighlight(it.Key());
      bChanged = true;
    }
  }

  if (bChanged)
    plDocumentNotices::NotifyChanged();
}

// Marks.

plString plDiversionProvider::DescribeDifferences(const CompareInfo& info)
{
  // it could not be read
  if (info.m_uiAdded + info.m_uiRemoved + info.m_uiChanged == 0 && !info.m_Differences.IsEmpty())
    return info.m_Differences[0];

  if (!info.m_bTree)
  {
    if (info.m_uiAdded + info.m_uiRemoved + info.m_uiChanged == 0)
      return "The same objects and properties as yours.";

    plStringBuilder s;
    s.SetFormat("Compared with yours: {} added, {} removed and {} changed objects.", info.m_uiAdded, info.m_uiRemoved, info.m_uiChanged);
    return s;
  }

  plUInt32 uiCount[3] = {};
  for (auto it : info.m_Marks)
    ++uiCount[static_cast<plUInt32>(it.Value().m_Kind)];

  if (info.m_Marks.IsEmpty())
    return info.m_uiSettingsChanged > 0 ? "The same objects as yours; its settings differ." : "The same objects and properties as yours.";

  plStringBuilder s;
  s.SetFormat("Compared with yours: {} added, {} removed and {} changed objects", uiCount[static_cast<plUInt32>(MarkKind::Added)], uiCount[static_cast<plUInt32>(MarkKind::Removed)], uiCount[static_cast<plUInt32>(MarkKind::Changed)]);
  s.Append(info.m_uiSettingsChanged > 0 ? ", and its settings differ." : ".");

  if (info.m_iStep >= 0 && info.m_iStep < static_cast<plInt32>(info.m_Steps.GetCount()))
  {
    const Mark* pMark = info.m_Marks.GetValue(info.m_Steps[info.m_iStep]);
    s.AppendFormat("  {} of {}: {}", info.m_iStep + 1, info.m_Steps.GetCount(), pMark != nullptr ? pMark->m_sName.GetView() : plStringView());
  }

  return s;
}

void plDiversionProvider::AddDifferenceActions(const plUuid& compareDocument, const CompareInfo& info, plDocumentNotice& ref_notice)
{
  const bool bShown = info.m_bHighlight;
  plDocumentNotice::Action& toggle = ref_notice.m_Actions.ExpandAndGetRef();
  toggle.m_sLabel = bShown ? "Hide differences" : "Highlight differences";
  toggle.m_Callback = [compareDocument, bShown]()
  {
    if (plDiversionProvider* pProvider = plDiversionProvider::GetSingleton())
      pProvider->SetHighlight(compareDocument, !bShown);
  };

  if (info.m_Steps.IsEmpty())
    return;

  for (bool bForward : {false, true})
  {
    plDocumentNotice::Action& step = ref_notice.m_Actions.ExpandAndGetRef();
    step.m_sLabel = bForward ? "Next" : "Previous";
    step.m_Callback = [compareDocument, bForward]()
    {
      if (plDiversionProvider* pProvider = plDiversionProvider::GetSingleton())
        pProvider->StepDifference(compareDocument, bForward);
    };
  }
}

void plDiversionProvider::AddComparedNotice(plDocument* pDocument, plDynamicArray<plDocumentNotice>& ref_notices) const
{
  for (auto it : m_CompareDocuments)
  {
    const CompareInfo& info = it.Value();
    if (info.m_SourceDocument != pDocument->GetGuid() || !info.m_bTree || info.m_Marks.IsEmpty())
      continue;

    plDocumentNotice& notice = ref_notices.ExpandAndGetRef();
    notice.m_Tone = plDocumentNotice::Tone::Info;
    notice.m_sIcon = plEditorUi::IconPath(plEditorUi::Icon::Clock);
    plStringBuilder sText;
    sText.SetFormat("Yours, compared with {}.", info.m_sLabel);
    notice.m_sText = sText;
    notice.m_sDetail = info.m_bHighlight ? "The marks show what it has that the old version has not, and what changed; the old version marks what yours removed." : "The differences are hidden.";
    AddDifferenceActions(it.Key(), info, notice);
    return;
  }
}

void plDiversionProvider::ApplyHighlight(const plUuid& compareDocument)
{
  const CompareInfo* pInfo = m_CompareDocuments.GetValue(compareDocument);
  if (pInfo == nullptr)
    return;

  for (bool bOld : {false, true})
  {
    plDocument* pDocument = plDocumentManager::GetDocumentByGuid(bOld ? compareDocument : pInfo->m_SourceDocument);
    plGameObjectDocument* pGameDocument = plDynamicCast<plGameObjectDocument*>(pDocument);
    if (pGameDocument == nullptr)
      continue;

    plDynamicArray<plUuid> objects;
    plDynamicArray<plColor> colors;
    if (pInfo->m_bTree && pInfo->m_bHighlight)
    {
      for (auto it : pInfo->m_Marks)
      {
        // an object only one version has
        const MarkKind kind = it.Value().m_Kind;
        if (kind == (bOld ? MarkKind::Added : MarkKind::Removed) || objects.GetCount() >= s_uiMaxBoxes)
          continue;

        objects.PushBack(ToCompareGuid(*pInfo, it.Key(), bOld));
        colors.PushBack(plColor(plDiversionDifferenceMarks::GetColor(kind)));
      }
    }

    pGameDocument->SetObjectHighlights(objects, colors);
    plEditorUiTreeExtensionRegistry::RequestRefresh(pDocument);
  }

  plPropertyGridExtension::NotifyChanged();
}

void plDiversionProvider::SetHighlight(const plUuid& compareDocument, bool bShow)
{
  CompareInfo* pInfo = m_CompareDocuments.GetValue(compareDocument);
  if (pInfo == nullptr)
    return;

  pInfo->m_bHighlight = bShow;
  if (!bShow)
    pInfo->m_iStep = -1;

  ApplyHighlight(compareDocument);
  plDocumentNotices::NotifyChanged();
}

void plDiversionProvider::StepDifference(const plUuid& compareDocument, bool bForward)
{
  CompareInfo* pInfo = m_CompareDocuments.GetValue(compareDocument);
  if (pInfo == nullptr || pInfo->m_Steps.IsEmpty())
    return;

  if (!pInfo->m_bHighlight)
  {
    pInfo->m_bHighlight = true;
    ApplyHighlight(compareDocument);
  }

  const plInt32 iCount = static_cast<plInt32>(pInfo->m_Steps.GetCount());
  if (bForward)
    pInfo->m_iStep = (pInfo->m_iStep + 1) % iCount;
  else
    pInfo->m_iStep = pInfo->m_iStep <= 0 ? iCount - 1 : pInfo->m_iStep - 1;

  const plUuid guid = pInfo->m_Steps[pInfo->m_iStep];
  const Mark* pMark = pInfo->m_Marks.GetValue(guid);

  // selected and framed in both, where it is there
  for (bool bOld : {false, true})
  {
    plDocument* pDocument = plDocumentManager::GetDocumentByGuid(bOld ? compareDocument : pInfo->m_SourceDocument);
    plGameObjectDocument* pGameDocument = plDynamicCast<plGameObjectDocument*>(pDocument);
    if (pGameDocument == nullptr)
      continue;

    const bool bThere = pMark != nullptr && pMark->m_Kind != (bOld ? MarkKind::Added : MarkKind::Removed);
    const plDocumentObject* pObject = bThere ? pDocument->GetObjectManager()->GetObject(ToCompareGuid(*pInfo, guid, bOld)) : nullptr;
    if (pObject == nullptr)
    {
      pDocument->GetSelectionManager()->Clear();
      continue;
    }

    pDocument->GetSelectionManager()->SetSelection(pObject);
    pGameDocument->TriggerShowSelectionInScenegraph();
    pGameDocument->TriggerFocusOnSelection(true);
  }

  plDocumentNotices::NotifyChanged();
}

// Finding the differences.

bool plDiversionProvider::UpdateDifferences(CompareInfo& inout_info)
{
  plFileStats stats;
  if (plOSFile::GetFileStats(inout_info.m_sSourcePath, stats).Failed())
    stats.m_LastModificationTime = plTimestamp::MakeInvalid();

  if (inout_info.m_bDiffed && stats.m_LastModificationTime.Compare(inout_info.m_SourceModified, plTimestamp::CompareMode::Identical))
    return false;

  inout_info.m_SourceModified = stats.m_LastModificationTime;
  inout_info.m_bDiffed = true;
  inout_info.m_Differences.Clear();
  inout_info.m_uiAdded = 0;
  inout_info.m_uiRemoved = 0;
  inout_info.m_uiChanged = 0;
  inout_info.m_uiSettingsChanged = 0;
  inout_info.m_Marks.Clear();
  inout_info.m_AncestorsInYours.Clear();
  inout_info.m_AncestorsInOld.Clear();
  inout_info.m_Steps.Clear();
  inout_info.m_PropertyChanges.Clear();
  DiffDocuments(inout_info.m_sRawPath, inout_info.m_sSourcePath, inout_info);

  if (inout_info.m_iStep >= static_cast<plInt32>(inout_info.m_Steps.GetCount()))
    inout_info.m_iStep = -1;

  return true;
}

void plDiversionProvider::DiffDocuments(plStringView sOldPath, plStringView sNewPath, CompareInfo& inout_info)
{
  plUniquePtr<plAbstractObjectGraph> pOldHeader, pOld, pOldTypes;
  plUniquePtr<plAbstractObjectGraph> pNewHeader, pNew, pNewTypes;
  if (plDocument::ReadDocument(sOldPath, pOldHeader, pOld, pOldTypes).Failed() || plDocument::ReadDocument(sNewPath, pNewHeader, pNew, pNewTypes).Failed())
  {
    inout_info.m_Differences.PushBack("The two versions could not be read for a comparison.");
    return;
  }

  // the copy's guids derive from it
  if (const plAbstractObjectNode* pHeader = pOldHeader->GetNodeByName("Header"))
  {
    if (const plAbstractObjectNode::Property* pId = pHeader->FindProperty("DocumentID"); pId != nullptr && pId->m_Value.IsA<plUuid>())
      inout_info.m_OldDocumentId = pId->m_Value.Get<plUuid>();
  }

  Tree oldTree, newTree;
  BuildTree(*pOld, oldTree);
  BuildTree(*pNew, newTree);

  auto Add = [&](plStringView sLine)
  {
    if (inout_info.m_Differences.GetCount() < s_uiMaxDifferences)
      inout_info.m_Differences.PushBack(sLine);
  };

  // a scene object's own difference
  auto MarkObject = [&](const plUuid& guid, const plAbstractObjectNode& node, MarkKind kind) -> Mark&
  {
    Mark& mark = inout_info.m_Marks[guid];
    mark.m_Kind = kind;
    mark.m_sName = DescribeNode(node);
    return mark;
  };

  // a difference of something a scene object has, e.g. a component: the object counts as changed, unless only one
  // version has it anyway
  auto MarkOwner = [&](const plAbstractObjectGraph& graph, const Tree& tree, const plAbstractObjectGraph& otherGraph, const plUuid& guid, plStringView sLine)
  {
    const plUuid owner = FindOwner(graph, tree, guid);
    if (!owner.IsValid())
    {
      ++inout_info.m_uiSettingsChanged;
      return;
    }

    if (otherGraph.GetNode(owner) == nullptr)
      return;

    Mark& mark = inout_info.m_Marks[owner];
    mark.m_Kind = MarkKind::Changed;
    mark.m_sName = DescribeNode(*graph.GetNode(owner));

    if (mark.m_sDetail.GetCharacterCount() < 2000)
      mark.m_sDetail = plStringBuilder(mark.m_sDetail, mark.m_sDetail.IsEmpty() ? "" : "\n", sLine);
  };

  // what yours has that the old version has not, what it lost, and what changed in between
  for (auto it : pNew->GetAllNodes())
  {
    const plAbstractObjectNode& node = *it.Value();
    if (IsDocumentRoot(node))
      continue;

    const plAbstractObjectNode* pOldNode = pOld->GetNode(it.Key());
    if (pOldNode == nullptr)
    {
      ++inout_info.m_uiAdded;
      Add(plStringBuilder("+ ", DescribeNode(node), " (", node.GetType(), ")"));

      if (IsGameObject(&node))
        MarkObject(it.Key(), node, MarkKind::Added);
      else
        MarkOwner(*pNew, newTree, *pOld, it.Key(), plStringBuilder("+ ", MakeReadable(node.GetType())));
      continue;
    }

    plStringBuilder sChanged, sLines;
    for (const plAbstractObjectNode::Property& prop : node.GetProperties())
    {
      const plAbstractObjectNode::Property* pOldProp = pOldNode->FindProperty(prop.m_sPropertyName);
      if (pOldProp != nullptr && pOldProp->m_Value == prop.m_Value)
        continue;

      // children and components are told apart by their own differences
      if (IsReference(prop.m_Value) || (pOldProp != nullptr && IsReference(pOldProp->m_Value)))
        continue;

      PropertyChange& change = inout_info.m_PropertyChanges[it.Key()].ExpandAndGetRef();
      change.m_sProperty = prop.m_sPropertyName;
      change.m_sNew = FormatValue(prop.m_Value);
      if (pOldProp != nullptr)
        change.m_sOld = FormatValue(pOldProp->m_Value);

      plStringBuilder sLine = MakeReadable(prop.m_sPropertyName);
      if (!change.m_sOld.IsEmpty() && !change.m_sNew.IsEmpty())
        sLine.Append(" ", change.m_sOld, " \xE2\x86\x92 ", change.m_sNew);

      sChanged.AppendWithSeparator(", ", sLine);
    }

    if (sChanged.IsEmpty())
      continue;

    ++inout_info.m_uiChanged;
    Add(plStringBuilder("~ ", DescribeNode(node), ": ", sChanged));

    if (IsGameObject(&node))
    {
      Mark& mark = inout_info.m_Marks[it.Key()];
      mark.m_Kind = MarkKind::Changed;
      mark.m_sName = DescribeNode(node);
      mark.m_sDetail = mark.m_sDetail.IsEmpty() ? plString(sChanged) : plString(plStringBuilder(sChanged, "\n", mark.m_sDetail));
    }
    else
    {
      MarkOwner(*pNew, newTree, *pOld, it.Key(), plStringBuilder(MakeReadable(node.GetType()), ": ", sChanged));
    }
  }

  for (auto it : pOld->GetAllNodes())
  {
    const plAbstractObjectNode& node = *it.Value();
    if (IsDocumentRoot(node) || pNew->GetNode(it.Key()) != nullptr)
      continue;

    ++inout_info.m_uiRemoved;
    Add(plStringBuilder("- ", DescribeNode(node), " (", node.GetType(), ")"));

    if (IsGameObject(&node))
      MarkObject(it.Key(), node, MarkKind::Removed);
    else
      MarkOwner(*pOld, oldTree, *pNew, it.Key(), plStringBuilder("\xE2\x88\x92 ", MakeReadable(node.GetType())));
  }

  // the dots on their parents, in the version each is in
  for (auto it : inout_info.m_Marks)
  {
    if (it.Value().m_Kind != MarkKind::Removed)
      InsertAncestors(newTree, it.Key(), inout_info.m_AncestorsInYours);
    if (it.Value().m_Kind != MarkKind::Added)
      InsertAncestors(oldTree, it.Key(), inout_info.m_AncestorsInOld);
  }

  // Previous / Next in tree order: yours, with what it removed after the objects it was beside; an added or removed
  // group is one step
  auto Visit = [&](auto& self, const plUuid& guid, const plUuid& oldGuid) -> void
  {
    if (const plDynamicArray<plUuid>* pChildren = newTree.m_Children.GetValue(guid))
    {
      for (const plUuid& child : *pChildren)
      {
        if (!IsGameObject(pNew->GetNode(child)))
          continue;

        const Mark* pMark = inout_info.m_Marks.GetValue(child);
        if (pMark != nullptr)
          inout_info.m_Steps.PushBack(child);

        if (pMark == nullptr || pMark->m_Kind != MarkKind::Added)
          self(self, child, child);
      }
    }

    if (const plDynamicArray<plUuid>* pOldChildren = oldTree.m_Children.GetValue(oldGuid))
    {
      for (const plUuid& child : *pOldChildren)
      {
        const Mark* pMark = inout_info.m_Marks.GetValue(child);
        if (pMark != nullptr && pMark->m_Kind == MarkKind::Removed && IsGameObject(pOld->GetNode(child)))
          inout_info.m_Steps.PushBack(child);
      }
    }
  };

  for (plUInt32 i = 0; i < newTree.m_Roots.GetCount(); ++i)
    Visit(Visit, newTree.m_Roots[i], i < oldTree.m_Roots.GetCount() ? oldTree.m_Roots[i] : plUuid());
}
