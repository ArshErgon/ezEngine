#pragma once

#include <EditorFramework/EditorFrameworkDLL.h>
#include <GuiFoundation/PropertyGrid/Implementation/PropertyWidget.moc.h>

class QHBoxLayout;
class QComboBox;
class ezDynamicStringEnum;

class EZ_EDITORFRAMEWORK_DLL ezQtDynamicStringEnumPropertyWidget : public ezQtStandardPropertyWidget
{
  Q_OBJECT

public:
  ezQtDynamicStringEnumPropertyWidget();


protected slots:
  void on_CurrentEnum_changed(int iEnum);

protected:
  virtual void OnInit() override;
  virtual void InternalSetValue(const ezVariant& value) override;

protected:
  QComboBox* m_pWidget;
  QHBoxLayout* m_pLayout;
  ezDynamicStringEnum* m_pEnum = nullptr;
  ezInt32 m_iLastIndex = -1;
};

