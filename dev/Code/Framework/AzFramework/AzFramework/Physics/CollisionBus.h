﻿/*
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

#include <AzCore/Component/ComponentBus.h>
#include <AzFramework/Physics/Collision.h>

namespace Physics
{
    //! CollisionRequests configures global project-level collision filtering settings.
    //! This is equivalent to setting values via the UI.
    class CollisionRequests
        : public AZ::EBusTraits
    {
    public:

        virtual ~CollisionRequests() {}

        /// Gets a collision layer by name. The Default layer is returned if the layer name was not found.
        virtual CollisionLayer GetCollisionLayerByName(const AZStd::string& layerName) = 0;

        /// Looks up the name of a collision layer
        virtual AZStd::string GetCollisionLayerName(const Physics::CollisionLayer& layer) = 0;

        /// Tries to find a collision layer which matches layerName. 
        /// Returns true if it was found and the result is stored in collisionLayer, otherwise false. 
        virtual bool TryGetCollisionLayerByName(const AZStd::string& layerName, CollisionLayer& collisionLayer) = 0;

        /// Gets a collision group by name. The All group is returned if the group name was not found.
        virtual CollisionGroup GetCollisionGroupByName(const AZStd::string& groupName) = 0;

        /// Tries to find a collision group which matches groupName. 
        /// Returns true if it was found, and the group is stored in collisionGroup, otherwise false.
        virtual bool TryGetCollisionGroupByName(const AZStd::string& groupName, CollisionGroup& collisionGroup) = 0;

        /// Looks up a name from a collision group
        virtual AZStd::string GetCollisionGroupName(const CollisionGroup& collisionGroup) = 0;

        /// Gets a collision group by id.
        virtual CollisionGroup GetCollisionGroupById(const CollisionGroups::Id& groupId) = 0;

        /// Sets the layer name by index.
        virtual void SetCollisionLayerName(int index, const AZStd::string& layerName) = 0;

        /// Creates a new collision group preset with corresponding groupName.
        virtual void CreateCollisionGroup(const AZStd::string& groupName, const Physics::CollisionGroup& group) = 0;
    };

    using CollisionRequestBus = AZ::EBus<CollisionRequests>;

    //! CollisionFilteringRequests configures filtering settings per entity.
    class CollisionFilteringRequests
        : public AZ::ComponentBus
    {
    public:
        static void Reflect(AZ::ReflectContext* context);

        //! Sets the collision layer on an entity.
        //! layerName should match a layer defined in the PhysX cConfiguration window.
        //! Colliders with a matching colliderTag will be updated. Specify the empty tag to update all colliders.
        virtual void SetCollisionLayer(const AZStd::string& layerName, const AZ::Crc32& colliderTag) = 0;

        //! Gets the collision layer name for a collider on an entity
        //! If the collision layer can't be found, an empty string is returned.
        //! Note: Multiple colliders on an entity are currently not supported.
        virtual AZStd::string GetCollisionLayerName() = 0;

        //! Sets the collision group on an entity.
        //! groupName should match a group defined in the PhysX configuration window.
        //! Colliders with a matching colliderTag will be updated. Specify the empty tag to update all colliders.
        virtual void SetCollisionGroup(const AZStd::string& groupName, const AZ::Crc32& colliderTag) = 0;

        //! Gets the collision group name for a collider on an entity. 
        //! If the collision group can't be found, an empty string is returned.
        //! Note: Multiple colliders on an entity are currently not supported.
        virtual AZStd::string GetCollisionGroupName() = 0;

        //! Toggles a single collision layer on or off on an entity.
        //! layerName should match a layer defined in the PhysX configuration window.
        //! Colliders with a matching colliderTag will be updated. Specify the empty tag to update all colliders.
        virtual void ToggleCollisionLayer(const AZStd::string& layerName, const AZ::Crc32& colliderTag, bool enabled) = 0;
    };
    using CollisionFilteringRequestBus = AZ::EBus<CollisionFilteringRequests>;

} // namespace Physics
