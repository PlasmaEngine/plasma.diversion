#pragma once

#include <EditorUi/Widgets.h>
#include <Foundation/Math/Color8UNorm.h>

class plUiBoundImage;
class plUiButton;
class plUiElement;

/// Building blocks the Version Control panels share.
namespace plDiversionUi
{
  /// An image whose texture, tint and visibility come from bindings; an empty sTexture paints a solid rect.
  plUiBoundImage* BoundImage(plUiElement* pParent, plStringView sTexture, plStringView sTint, plStringView sVisible);

  /// Places an element at a fixed rect from its parent's top-left corner.
  void Place(plUiElement* pElement, float x, float y, float fWidth, float fHeight);

  /// A check box: the field well, the selection fill with the accent tick while sOnBinding is true (style pass D9).
  plUiButton* CheckBox(plUiElement* pParent, plStringView sAction, plStringView sOnBinding);

  /// A toolbar button with an icon and a label, right-aligned at fRight from the right edge; returns its width.
  float ToolButton(plUiElement* pParent, plEditorUi::Icon icon, plStringView sLabel, plStringView sAction, plStringView sDisabled, float fRight);

  /// A small button with a label in a list row, right-aligned at fRight from the row's right edge; returns its width.
  float RowButton(plUiElement* pParent, plStringView sLabel, plStringView sAction, plStringView sVisible, float fRight);

  /// The asset type's icon in its category colour, as the Asset Browser shows it; a plain document icon otherwise.
  void GetFileIcon(plStringView sAbsPath, plString& out_sIcon, plColorGammaUB& out_color);
} // namespace plDiversionUi
