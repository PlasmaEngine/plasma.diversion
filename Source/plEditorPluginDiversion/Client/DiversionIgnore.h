#pragma once

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Strings/String.h>

/// The lines a Plasma project needs in its workspace's .dvignore, which uses .gitignore patterns.
namespace plDiversionIgnore
{
  /// The folders the editor rebuilds locally and that should never sync: transform output and the package store.
  /// sProjectPrefix is the project folder relative to the workspace root, empty when they are the same folder.
  void GetWantedLines(plStringView sProjectPrefix, plDynamicArray<plString>& out_lines);

  /// The wanted lines that no line of sIgnoreFile covers yet.
  void FindMissingLines(plStringView sIgnoreFile, plStringView sProjectPrefix, plDynamicArray<plString>& out_missing);

  /// Whether a line of sIgnoreFile ignores the workspace-relative folder sFolder.
  bool IsFolderIgnored(plStringView sIgnoreFile, plStringView sFolder);

  /// sIgnoreFile with the lines appended under a comment, keeping its line endings.
  plString AppendLines(plStringView sIgnoreFile, const plDynamicArray<plString>& lines, plStringView sComment = "Plasma: transform output and installed packages, rebuilt on every machine");
} // namespace plDiversionIgnore
