/*
* All or Portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
* its licensors.
*
* For complete copyright and license terms please see the LICENSE at the root of this
* distribution (the "License"). All use of this software is governed by the License,
* or, if provided, by the license below or the license accompanying this file. Do not
* remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*
*/

#include <qevent.h>

#include <AzCore/Component/Entity.h>

#include <GraphCanvas/GraphCanvasBus.h>
#include <GraphCanvas/Widgets/GraphCanvasEditor/GraphCanvasEditorDockWidget.h>
#include <StaticLib/GraphCanvas/Widgets/GraphCanvasEditor/ui_GraphCanvasEditorDockWidget.h>

namespace GraphCanvas
{
    /////////////////////
    // EditorDockWidget
    /////////////////////

    static int counter = 0;
    
    EditorDockWidget::EditorDockWidget(const EditorId& editorId, const QString& title, QWidget* parent)
        : AzQtComponents::StyledDockWidget(!title.isEmpty() ? title : QString("Window %1").arg(counter), parent)
        , m_editorId(editorId)
        , m_ui(new Ui::GraphCanvasEditorDockWidget())        
    {
        setAttribute(Qt::WA_DeleteOnClose);

        m_ui->setupUi(this);
        m_ui->graphicsView->SetEditorId(editorId);

        setAllowedAreas(Qt::DockWidgetArea::TopDockWidgetArea);

        // Create a new GraphCanvas scene for our GraphCanvasGraphicsView and
        // configure it with the proper EditorId.
        GraphCanvas::GraphCanvasRequestBus::BroadcastResult(m_sceneEntity, &GraphCanvas::GraphCanvasRequests::CreateSceneAndActivate);
        m_graphId = m_sceneEntity->GetId();
        GraphCanvas::SceneRequestBus::Event(m_graphId, &GraphCanvas::SceneRequests::SetEditorId, editorId);

        // Set the scene for our GraphCanvasGraphicsView.
        GraphCanvas::GraphCanvasGraphicsView* graphicsView = GetGraphicsView();
        graphicsView->SetScene(m_graphId);

        m_dockWidgetId = AZ::Entity::MakeId();
        EditorDockWidgetRequestBus::Handler::BusConnect(m_dockWidgetId);

        ++counter;
    }

    EditorDockWidget::~EditorDockWidget()
    {
        delete m_sceneEntity;
    }
    
    DockWidgetId EditorDockWidget::GetDockWidgetId() const
    {
        return m_dockWidgetId;
    }

    void EditorDockWidget::ReleaseBus()
    {
        ActiveEditorDockWidgetRequestBus::Handler::BusDisconnect(GetEditorId());
    }
    
    const EditorId& EditorDockWidget::GetEditorId() const
    {
        return m_editorId;
    }

    AZ::EntityId EditorDockWidget::GetViewId() const
    {
        return m_ui->graphicsView->GetViewId();
    }

    GraphId EditorDockWidget::GetGraphId() const
    {
        return m_graphId;
    }

    EditorDockWidget* EditorDockWidget::AsEditorDockWidget()
    {
        return this;
    }

    void EditorDockWidget::SetTitle(const AZStd::string& title)
    {
        setWindowTitle(title.c_str());
    }

    GraphCanvasGraphicsView* EditorDockWidget::GetGraphicsView() const
    {
        return m_ui->graphicsView;
    }

    void EditorDockWidget::closeEvent(QCloseEvent* closeEvent)
    {
        emit OnEditorClosed(this);

        AzQtComponents::StyledDockWidget::closeEvent(closeEvent);
    }

    void EditorDockWidget::SignalActiveEditor()
    {
        if (!ActiveEditorDockWidgetRequestBus::Handler::BusIsConnected())
        {
            ActiveEditorDockWidgetRequestBus::Event(m_editorId, &ActiveEditorDockWidgetRequests::ReleaseBus);
            ActiveEditorDockWidgetRequestBus::Handler::BusConnect(m_editorId);
        }
    }

#include <StaticLib/GraphCanvas/Widgets/GraphCanvasEditor/GraphCanvasEditorDockWidget.moc>
}