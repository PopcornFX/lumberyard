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

#include <AzToolsFramework/ToolsComponents/EditorComponentBase.h>

#include <LmbrCentral/Rendering/MeshComponentBus.h>

#include <System/ClothConfiguration.h>

namespace NvCloth
{
    //! Class for in-editor Cloth Component.
    class EditorClothComponent
        : public AzToolsFramework::Components::EditorComponentBase
        , public LmbrCentral::MeshComponentNotificationBus::Handler
    {
    public:
        AZ_EDITOR_COMPONENT(EditorClothComponent, "{2C99B4EF-8A5F-4585-89F9-86D50754DF7E}", AzToolsFramework::Components::EditorComponentBase);

        static void Reflect(AZ::ReflectContext* context);

        EditorClothComponent();

        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);

        // EditorComponentBase overrides
        void BuildGameEntity(AZ::Entity* gameEntity) override;

        // AZ::Component overrides
        void Activate() override;
        void Deactivate() override;

        // LmbrCentral::MeshComponentNotificationBus::Handler overrides
        void OnMeshCreated(const AZ::Data::Asset<AZ::Data::AssetData>& asset) override;
        void OnMeshDestroyed() override;

    private:
        ClothConfiguration m_config;

        // List of mesh nodes from the asset that contains cloth data.
        // This list is not serialized, it's compiled when the asset has been received via MeshComponentNotificationBus.
        MeshNodeList m_meshNodeList;

        AZStd::string m_previousMeshNode;

        bool m_assetSupportsSkinnedAnimation = false;
    };
} // namespace NvCloth
