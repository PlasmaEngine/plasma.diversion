#pragma once

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Strings/String.h>

/// Workspace-relative path lists for dv commands.
namespace plDiversionPaths
{
  /// The longest list of path arguments one dv call gets; Windows limits a whole command line to 32767 characters.
  constexpr plUInt32 MaxArgumentCharacters = 24000;

  /// The ticked paths as dv arguments. While they fit in MaxArgumentCharacters they are passed as they are; beyond
  /// that, a folder whose changes (of allChanged) are all ticked stands in for them. Paths use forward slashes.
  void MakeArguments(const plDynamicArray<plString>& ticked, const plDynamicArray<plString>& allChanged, plDynamicArray<plString>& out_arguments);
} // namespace plDiversionPaths
