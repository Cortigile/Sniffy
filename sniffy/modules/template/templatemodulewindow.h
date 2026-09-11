#ifndef TEMPLATEMODULEWINDOW_H
#define TEMPLATEMODULEWINDOW_H

#include <QWidget>
#include <QScrollArea>
#include <QDebug>
#include <QVBoxLayout>

#include "gui/widgetcontrolmodule.h"
#include "gui/widgetseparator.h"
#include "gui/widgetdial.h"
#include "gui/widgetdialrange.h"
#include "gui/widgetbuttons.h"
#include "gui/widgetlabel.h"
#include "gui/widgetselection.h"
#include "gui/widgetchart.h"
#include "gui/widgettab.h"
#include "gui/widgetlabelarea.h"

#include "templatemoduleconfig.h"

namespace Ui {
class TemplateModuleWindow;
}

class TemplateModuleWindow : public QWidget
{
    Q_OBJECT

public:
    explicit TemplateModuleWindow(TemplateModuleConfig *config, QWidget *parent = nullptr);
    ~TemplateModuleWindow();

    void restoreGUIAfterStartup();

private:
    Ui::TemplateModuleWindow *ui;
    TemplateModuleConfig *config;
};

#endif // TEMPLATEMODULEWINDOW_H
