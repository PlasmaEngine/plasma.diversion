#include <plEditorPluginDiversion/EditorPluginDiversionPCH.h>

#include <plEditorPluginDiversion/Client/DiversionPaths.h>

#include <Foundation/Containers/HashSet.h>
#include <Foundation/Containers/HashTable.h>

namespace
{
  plUInt32 CountCharacters(const plDynamicArray<plString>& paths)
  {
    plUInt32 uiCount = 0;
    for (const plString& sPath : paths)
      uiCount += sPath.GetElementCount() + 3; // the space and the quotes around it
    return uiCount;
  }

  /// Calls func with each folder above sPath, nearest first ("a/b/c.txt" -> "a/b", "a").
  template <typename FUNC>
  void ForEachFolder(plStringView sPath, FUNC func)
  {
    const char* szEnd = sPath.GetEndPointer();
    while (true)
    {
      const char* szSlash = nullptr;
      for (const char* sz = sPath.GetStartPointer(); sz < szEnd; ++sz)
      {
        if (*sz == '/')
          szSlash = sz;
      }

      if (szSlash == nullptr)
        return;

      func(plStringView(sPath.GetStartPointer(), szSlash));
      szEnd = szSlash;
    }
  }
} // namespace

void plDiversionPaths::MakeArguments(const plDynamicArray<plString>& ticked, const plDynamicArray<plString>& allChanged, plDynamicArray<plString>& out_arguments)
{
  out_arguments = ticked;

  if (CountCharacters(ticked) <= MaxArgumentCharacters)
    return;

  // changes below each folder, all and ticked
  plHashTable<plString, plUInt32> all;
  plHashTable<plString, plUInt32> tickedBelow;

  for (const plString& sPath : allChanged)
    ForEachFolder(sPath, [&](plStringView sFolder) { all[sFolder]++; });

  for (const plString& sPath : ticked)
    ForEachFolder(sPath, [&](plStringView sFolder) { tickedBelow[sFolder]++; });

  // each ticked path goes as its highest folder whose changes are all ticked, or as itself
  plHashSet<plString> added;
  out_arguments.Clear();

  for (const plString& sPath : ticked)
  {
    plString sArgument = sPath;

    ForEachFolder(sPath, [&](plStringView sFolder)
      {
        const plUInt32* pAll = all.GetValue(sFolder);
        const plUInt32* pTicked = tickedBelow.GetValue(sFolder);
        if (pAll != nullptr && pTicked != nullptr && *pAll == *pTicked)
          sArgument = sFolder; });

    if (!added.Contains(sArgument))
    {
      added.Insert(sArgument);
      out_arguments.PushBack(sArgument);
    }
  }
}
