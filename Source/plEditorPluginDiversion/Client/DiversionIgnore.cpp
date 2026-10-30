#include <plEditorPluginDiversion/EditorPluginDiversionPCH.h>

#include <plEditorPluginDiversion/Client/DiversionIgnore.h>

namespace
{
  /// A pattern reduced to the folder it names: no leading "/" or "**/", no trailing "/", "/*" or "/**". Sets
  /// out_bAnywhere for a pattern that matches at any depth (a plain name, or one starting with "**/").
  plStringBuilder Reduce(plStringView sPattern, bool& out_bAnywhere)
  {
    plStringBuilder s = sPattern;
    s.Trim(" \t\r");

    out_bAnywhere = false;
    if (s.StartsWith("**/"))
    {
      out_bAnywhere = true;
      s.Shrink(3, 0);
    }

    // "/AssetCache" names the folder at the root only
    const bool bAnchored = s.StartsWith("/");
    while (s.StartsWith("/"))
      s.Shrink(1, 0);

    bool bChanged = true;
    while (bChanged)
    {
      bChanged = false;
      for (plStringView sSuffix : {plStringView("/**"), plStringView("/*"), plStringView("/")})
      {
        if (s.EndsWith(sSuffix))
        {
          // the suffixes are ASCII, so bytes are characters
          s.Shrink(0, sSuffix.GetElementCount());
          bChanged = true;
        }
      }
    }

    // .gitignore: a pattern with no slash left in it matches a name at any level
    if (!bAnchored && s.FindSubString("/") == nullptr)
      out_bAnywhere = true;

    return s;
  }

  bool Covers(plStringView sPattern, plStringView sFolder)
  {
    bool bAnywhere = false;
    const plStringBuilder sReduced = Reduce(sPattern, bAnywhere);
    if (sReduced.IsEmpty())
      return false;

    if (sReduced.IsEqual_NoCase(sFolder))
      return true;

    if (!bAnywhere)
      return false;

    // the folder's own name, or its path below some parent
    const plStringView sName = plPathUtils::GetFileNameAndExtension(sFolder);
    if (sReduced.IsEqual_NoCase(sName))
      return true;

    return sFolder.EndsWith_NoCase(plStringBuilder("/", sReduced));
  }
} // namespace

void plDiversionIgnore::GetWantedLines(plStringView sProjectPrefix, plDynamicArray<plString>& out_lines)
{
  out_lines.Clear();

  for (plStringView sFolder : {plStringView("AssetCache"), plStringView("Plugins")})
  {
    plStringBuilder sLine;
    if (!sProjectPrefix.IsEmpty())
      sLine.Append(sProjectPrefix, "/");

    sLine.Append(sFolder, "/");
    out_lines.PushBack(sLine);
  }
}

void plDiversionIgnore::FindMissingLines(plStringView sIgnoreFile, plStringView sProjectPrefix, plDynamicArray<plString>& out_missing)
{
  out_missing.Clear();

  plDynamicArray<plString> wanted;
  GetWantedLines(sProjectPrefix, wanted);

  plHybridArray<plStringView, 64> lines;
  sIgnoreFile.Split(false, lines, "\n");

  for (const plString& sWanted : wanted)
  {
    if (!IsFolderIgnored(sIgnoreFile, sWanted))
      out_missing.PushBack(sWanted);
  }
}

bool plDiversionIgnore::IsFolderIgnored(plStringView sIgnoreFile, plStringView sFolder)
{
  plStringBuilder sTrimmed = sFolder;
  sTrimmed.Trim("/");

  plHybridArray<plStringView, 64> lines;
  sIgnoreFile.Split(false, lines, "\n");

  for (plStringView sLine : lines)
  {
    sLine.Trim(" \t\r");

    // comments and negations never ignore anything
    if (sLine.IsEmpty() || sLine.StartsWith("#") || sLine.StartsWith("!"))
      continue;

    if (Covers(sLine, sTrimmed))
      return true;
  }

  return false;
}

plString plDiversionIgnore::AppendLines(plStringView sIgnoreFile, const plDynamicArray<plString>& lines, plStringView sComment)
{
  const plStringView sNewline = sIgnoreFile.FindSubString("\r\n") != nullptr ? plStringView("\r\n") : plStringView("\n");

  plStringBuilder sResult = sIgnoreFile;
  if (!sResult.IsEmpty() && !sResult.EndsWith("\n"))
    sResult.Append(sNewline);

  if (!sResult.IsEmpty())
    sResult.Append(sNewline);

  sResult.Append("# ", sComment, sNewline);

  for (const plString& sLine : lines)
    sResult.Append(sLine, sNewline);

  return sResult;
}
