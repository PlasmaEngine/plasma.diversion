#include <plEditorPluginDiversion/EditorPluginDiversionPCH.h>

#include <plEditorPluginDiversion/Panels/PanelWidgets.h>

#include <EditorFramework/Assets/AssetCurator.h>
#include <Foundation/Math/ColorScheme.h>
#include <GameUiPlugin/Elements/UiElements.h>

plUiBoundImage* plDiversionUi::BoundImage(plUiElement* pParent, plStringView sTexture, plStringView sTint, plStringView sVisible)
{
  plUiBoundImage* image = PL_DEFAULT_NEW(plUiBoundImage);
  image->m_BrushMode = sTexture.IsEmpty() ? plUiBrushMode::Solid : plUiBrushMode::Image;
  image->m_sTextureBinding = sTexture;
  image->m_sTintBinding = sTint;
  image->m_sVisibleBinding = sVisible;
  image->m_Color = plColorGammaUB(255, 255, 255, 255);
  pParent->m_Children.PushBack(image);
  return image;
}

void plDiversionUi::Place(plUiElement* pElement, float x, float y, float fWidth, float fHeight)
{
  pElement->m_vAnchorMin = pElement->m_vAnchorMax = plVec2::MakeZero();
  pElement->m_vOffsets = plVec4(x, y, fWidth, fHeight);
}

plUiButton* plDiversionUi::CheckBox(plUiElement* pParent, plStringView sAction, plStringView sOnBinding)
{
  using namespace plEditorUi;

  plUiButton* button = Button(pParent, "", sAction);
  button->ClearChildren();
  button->m_Color = plColorGammaUB(0, 0, 0, 0);
  button->m_HoverColor = plColorGammaUB(0, 0, 0, 0);
  button->m_PressedColor = plColorGammaUB(0, 0, 0, 0);

  plUiPanel* box = Panel(button);
  box->m_vAnchorMin = box->m_vAnchorMax = plVec2(0.5f);
  box->m_vOffsets = plVec4(-7, -7, 14, 14);
  box->m_Color = Field();
  box->m_fBorderWidth = 1;
  box->m_BorderColor = Border();
  box->m_vCornerRadius = plVec4(3);

  plUiPanel* on = Panel(box);
  on->m_Color = Selection();
  on->m_fBorderWidth = 1;
  on->m_BorderColor = plColorGammaUB(Accent().r, Accent().g, Accent().b, 140);
  on->m_vCornerRadius = plVec4(3);
  on->m_sVisibleBinding = sOnBinding;

  plUiImage* mark = SvgImage(on, IconPath(Icon::Check));
  mark->m_vAnchorMin = mark->m_vAnchorMax = plVec2(0.5f);
  mark->m_vOffsets = plVec4(-5, -5, 10, 10);
  mark->m_Color = Accent();
  return button;
}

float plDiversionUi::ToolButton(plUiElement* pParent, plEditorUi::Icon icon, plStringView sLabel, plStringView sAction, plStringView sDisabled, float fRight)
{
  using namespace plEditorUi;

  const float fWidth = MeasureText(sLabel, FontSize - 1) + 38.0f;

  plUiButton* button = Button(pParent, "", sAction);
  button->ClearChildren();
  button->m_vAnchorMin = button->m_vAnchorMax = plVec2(1, 0.5f);
  button->m_vOffsets = plVec4(-fRight - fWidth, -11, fWidth, 22);
  button->m_Color = Raised();
  button->m_HoverColor = Border();
  button->m_fBorderWidth = 1;
  button->m_BorderColor = Border();
  button->m_vCornerRadius = plVec4(3);
  button->m_sDisabledBinding = sDisabled;

  plUiImage* image = SvgImage(button, IconPath(icon));
  Place(image, 9, 5, 12, 12);
  image->m_Color = Muted();

  plUiText* text = Text(button, sLabel);
  Fill(text, plVec4(27, 0, 6, 0));
  text->m_uiFontSize = FontSize - 1;

  return fWidth;
}

float plDiversionUi::RowButton(plUiElement* pParent, plStringView sLabel, plStringView sAction, plStringView sVisible, float fRight)
{
  using namespace plEditorUi;

  const float fWidth = MeasureText(sLabel, FontSize - 2) + 18.0f;

  plUiButton* button = Button(pParent, "", sAction);
  button->ClearChildren();
  button->m_vAnchorMin = button->m_vAnchorMax = plVec2(1, 0.5f);
  button->m_vOffsets = plVec4(-fRight - fWidth, -11, fWidth, 22);
  button->m_Color = Raised();
  button->m_HoverColor = Border();
  button->m_fBorderWidth = 1;
  button->m_BorderColor = Border();
  button->m_vCornerRadius = plVec4(3);
  button->m_sVisibleBinding = sVisible;
  button->m_sDisabledBinding = "Busy";

  plUiText* text = Text(button, sLabel);
  Fill(text);
  text->m_HAlign = plUiTextHAlign::Center;
  text->m_uiFontSize = FontSize - 2;

  return fWidth;
}

void plDiversionUi::GetFileIcon(plStringView sAbsPath, plString& out_sIcon, plColorGammaUB& out_color)
{
  out_sIcon = ":/GuiFoundation/Icons/Document.svg";
  out_color = plEditorUi::Dim();

  auto pSubAsset = plAssetCurator::GetSingleton()->FindSubAsset(sAbsPath);
  if (!pSubAsset.isValid())
    return;

  const plAssetDocumentTypeDescriptor* pDesc = pSubAsset->m_pAssetInfo->m_pDocumentTypeDescriptor;
  out_sIcon = pDesc->m_sIcon;
  if (pDesc->m_sAssetCategory.IsEmpty())
    out_color = plEditorUi::Muted();
  else
    out_color = plColorScheme::GetCategoryColor(pDesc->m_sAssetCategory, plColorScheme::CategoryColorUsage::OverlayIcon);
}
