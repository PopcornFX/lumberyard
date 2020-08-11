/*
* All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
* its licensors.
*
* For complete copyright and license terms please see the LICENSE at the root of this
* distribution (the "License"). All use of this software is governed by the License,
* or, if provided, by the license below or the license accompanying this file. Do not
* remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*
*/
#pragma once

AZ_PUSH_DISABLE_WARNING(4251 4800 4244, "-Wunknown-warning-option")
#include <QEvent>
#include <QGraphicsWidget>
#include <QObject>
AZ_POP_DISABLE_WARNING

#include <AzQtComponents/Components/Widgets/VectorInput.h>

#include <GraphCanvas/Components/NodePropertyDisplay/NodePropertyDisplay.h>
#include <GraphCanvas/Components/NodePropertyDisplay/VectorDataInterface.h>

#include <GraphCanvas/Components/MimeDataHandlerBus.h>
#include <GraphCanvas/Styling/StyleHelper.h>

class QGraphicsLinearLayout;

namespace GraphCanvas
{
    class GraphCanvasLabel;
    class VectorNodePropertyDisplay;

    class VectorEventFilter
        : public QObject
    {
        Q_OBJECT
    public:
        AZ_CLASS_ALLOCATOR(VectorEventFilter, AZ::SystemAllocator, 0);
        VectorEventFilter(VectorNodePropertyDisplay* owner);
        ~VectorEventFilter() = default;

        bool eventFilter(QObject *object, QEvent *event) override;

    private:
        VectorNodePropertyDisplay* m_owner;
    };
    
    class ReadOnlyVectorControl
        : public QGraphicsWidget
    {
    public:
        AZ_CLASS_ALLOCATOR(ReadOnlyVectorControl, AZ::SystemAllocator, 0);
        ReadOnlyVectorControl(int index, const VectorDataInterface& dataInterface);
        ~ReadOnlyVectorControl();
        
        void RefreshStyle(const AZ::EntityId& sceneId);
        void UpdateDisplay();

        int GetIndex() const;

        GraphCanvasLabel* GetTextLabel();
        const GraphCanvasLabel* GetTextLabel() const;
        
        GraphCanvasLabel* GetValueLabel();
        const GraphCanvasLabel* GetValueLabel() const;
    
    private:    
        QGraphicsLinearLayout* m_layout;
        
        GraphCanvasLabel* m_textLabel;
        GraphCanvasLabel* m_valueLabel;
        
        int m_index;
        
        const VectorDataInterface& m_dataInterface;
    };
    
    class VectorNodePropertyDisplay
        : public NodePropertyDisplay
    {
        friend class VectorEventFilter;

    public:
        AZ_CLASS_ALLOCATOR(VectorNodePropertyDisplay, AZ::SystemAllocator, 0);
        VectorNodePropertyDisplay(VectorDataInterface* dataInterface);
        virtual ~VectorNodePropertyDisplay();
    
        // DataSource
        void RefreshStyle() override;
        void UpdateDisplay() override;
        
        QGraphicsLayoutItem* GetDisabledGraphicsLayoutItem() override;
        QGraphicsLayoutItem* GetDisplayGraphicsLayoutItem() override;
        QGraphicsLayoutItem* GetEditableGraphicsLayoutItem() override;
        ////

        // DataSlotNotifications
        void OnDragDropStateStateChanged(const DragDropState& dragState);
        ////
    
    private:

        void OnFocusIn();
        void OnFocusOut();
        void SetupProxyWidget();
        void CleanupProxyWidget();
        
        void SubmitValue(int elementIndex, double newValue);
    
        Styling::StyleHelper m_styleHelper;
        VectorDataInterface*  m_dataInterface;
        
        GraphCanvasLabel*                           m_disabledLabel;
        AzQtComponents::VectorInput*                m_propertyVectorCtrl;
        QGraphicsProxyWidget*                       m_proxyWidget;
        
        QGraphicsWidget*                            m_displayWidget;
        AZStd::vector< ReadOnlyVectorControl* >     m_vectorDisplays;

        bool                                        m_releaseLayout;
    };
}