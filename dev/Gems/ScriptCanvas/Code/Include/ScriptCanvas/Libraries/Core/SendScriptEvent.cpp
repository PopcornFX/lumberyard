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

#include "SendScriptEvent.h"

#include <AzCore/Asset/AssetManager.h>

#include <ScriptCanvas/Core/SlotNames.h>
#include <ScriptCanvas/Libraries/Core/MethodUtility.h>

#include <ScriptEvents/ScriptEventsBus.h>

#include <ScriptCanvas/Core/ModifiableDatumView.h>

namespace ScriptCanvas
{
    namespace Nodes
    {
        namespace Core
        {
            SendScriptEvent::~SendScriptEvent()
            {
                ScriptEvents::ScriptEventNotificationBus::Handler::BusDisconnect();
            }

            void SendScriptEvent::OnInputSignal(const SlotId&)
            {
                if (!m_method)
                {
                    if (!m_asset.IsReady())
                    {
                        m_asset = AZ::Data::AssetManager::Instance().GetAsset<ScriptEvents::ScriptEventsAsset>(m_scriptEventAssetId, true, nullptr, true);
                    }

                    CreateSender(m_asset);
                }

                if (!m_method)
                {
                    AZStd::string error = AZStd::string::format("Script Event sender node called with no initialized method (%s::%s)!", m_busName.c_str(), m_eventName.c_str());
                    SCRIPTCANVAS_REPORT_ERROR((*this), error.c_str());
                }

                if (m_method)
                {
                    Node::DatumVector inputDatums = GatherDatumsForDescriptor(SlotDescriptors::DataIn());

                    if (m_method->GetNumArguments() != inputDatums.size())
                    {
                        SCRIPTCANVAS_REPORT_ERROR((*this), "The Script Event %s number of parameters %d do not correspond to the number of slots %zu in the Script Canvas node (%s). Make sure the node is updated in the Script Canvas graph.", m_method->m_name.c_str(), m_method->GetNumArguments(), inputDatums.size(), GetNodeName().c_str());
                        return;
                    }

                    AZStd::array<AZ::BehaviorValueParameter, BehaviorContextMethodHelper::MaxCount> params;
                    AZ::BehaviorValueParameter* paramFirst(params.begin());
                    AZ::BehaviorValueParameter* paramIter = paramFirst;
                    {
                        // all input should have been pushed into this node already
                        int argIndex(0);
                        for (const Datum* datum : inputDatums)
                        {
                            AZ::Outcome<AZ::BehaviorValueParameter, AZStd::string> inputParameter = datum->ToBehaviorValueParameter(*m_method->GetArgument(argIndex));
                            if (!inputParameter.IsSuccess())
                            {
                                SCRIPTCANVAS_REPORT_ERROR((*this), "BehaviorContext method input problem at parameter index %d: %s", argIndex, inputParameter.GetError().data());
                                return;
                            }

                            paramIter->Set(inputParameter.GetValue());
                            ++paramIter;
                            ++argIndex;
                        }

                        AZ_PROFILE_SCOPE_DYNAMIC(AZ::Debug::ProfileCategory::ScriptCanvas, "ScriptCanvas::ScriptEvents::OnInputSignal::Call %s::%s", m_busName.c_str(), m_eventName.c_str());
                        {
                            BehaviorContextMethodHelper::Call(*this, m_method, paramFirst, paramIter, m_resultSlotID); 
                        }
                    }
                }

                SignalOutput(GetSlotId("Out"));
            }

            ScriptCanvas::EBusBusId SendScriptEvent::GetBusId() const
            {
                return m_busId;
            }

            ScriptCanvas::EBusEventId SendScriptEvent::GetEventId() const
            {
                return m_eventId;
            }

            SlotId SendScriptEvent::GetBusSlotId() const
            {
                if (m_method && m_method->HasBusId())
                {
                    const int busIndex{ 0 };
                    const AZ::BehaviorParameter& busArgument = *m_method->GetArgument(busIndex);
                    const AZStd::string argumentTypeName = AZ::BehaviorContextHelper::IsStringParameter(busArgument) ? Data::GetName(Data::Type::String()) : Data::GetName(Data::FromAZType(busArgument.m_typeId));
                    const AZStd::string* argumentNamePtr = m_method->GetArgumentName(busIndex);
                    const AZStd::string argName = argumentNamePtr && !argumentNamePtr->empty()
                        ? *argumentNamePtr
                        : AZStd::string::format("%s:%2d", argumentTypeName.c_str(), busIndex);

                    return GetSlotId(argName);
                }

                return {};
            }

            AZ::Outcome<void, AZStd::string> IsExposable(const AZ::BehaviorMethod& method)
            {
                if (method.GetNumArguments() > BehaviorContextMethodHelper::MaxCount)
                {
                    return AZ::Failure(AZStd::string("Too many arguments for a Script Canvas method"));
                }

                for (size_t argIndex(0), sentinel(method.GetNumArguments()); argIndex != sentinel; ++argIndex)
                {
                    if (const AZ::BehaviorParameter* argument = method.GetArgument(argIndex))
                    {
                        const Data::Type type = AZ::BehaviorContextHelper::IsStringParameter(*argument) ? Data::Type::String() : Data::FromAZType(argument->m_typeId);

                        if (!type.IsValid())
                        {
                            return AZ::Failure(AZStd::string::format("Argument type at index: %d is not valid in ScriptCanvas, TypeId: %s", argument->m_typeId.ToString<AZStd::string>().data()));
                        }

                        if ((argument->m_traits & AZ::BehaviorParameter::TR_THIS_PTR) && Data::IsValueType(type))
                        {
                            return AZ::Failure(AZStd::string::format("No member functions on value types, like, %s, are allowed in ScriptCanvas", Data::GetName(type).c_str()));
                        }
                    }
                    else
                    {
                        return AZ::Failure(AZStd::string::format("Missing argument at index: %d", argIndex));
                    }
                }

                return AZ::Success();
            }

            void SendScriptEvent::AddInputSlot(size_t slotIndex, size_t argIndex, const AZStd::string_view argName, const AZStd::string_view tooltip, AZ::BehaviorMethod* method, const AZ::BehaviorParameter* argument, AZ::Uuid slotKey, SlotIdMapping& populationMapping)
            {
                bool isNewSlot = true;

                ScriptCanvas::SlotId slotId;

                DataSlotConfiguration slotConfiguration;

                slotConfiguration.m_name = argName;
                slotConfiguration.m_toolTip = tooltip;
                slotConfiguration.SetConnectionType(ConnectionType::Input);
                slotConfiguration.m_addUniqueSlotByNameAndType = false;

                auto mappingIter = m_eventSlotMapping.find(slotKey);

                if (mappingIter != m_eventSlotMapping.end())
                {
                    slotConfiguration.m_slotId = mappingIter->second;
                    isNewSlot = false;
                }

                if (argument->m_typeId == azrtti_typeid<AZ::EntityId>())
                {
                    slotConfiguration.SetDefaultValue(ScriptCanvas::GraphOwnerId);

                    slotId = InsertSlot(aznumeric_cast<AZ::s64>(slotIndex), slotConfiguration, isNewSlot);
                }
                else
                {
                    slotConfiguration.ConfigureDatum(AZStd::move(Datum(*argument, Datum::eOriginality::Copy, nullptr)));
                    slotId = InsertSlot(aznumeric_cast<AZ::s64>(slotIndex), slotConfiguration, isNewSlot);

                    if (auto defaultValue = method->GetDefaultValue(argIndex))
                    {
                        ModifiableDatumView datumView;
                        FindModifiableDatumView(slotId, datumView);

                        if (datumView.IsValid() && Data::IsValueType(datumView.GetDataType()))
                        {
                            datumView.AssignToDatum(AZStd::move(Datum(defaultValue->m_value)));
                        }
                    }
                }

                AZ_Error("ScriptCanvas", populationMapping.find(slotKey) == populationMapping.end(), "Trying to map the same slot key twice inside of SendScriptEvent for assetId(%s).", m_scriptEventAssetId.ToString<AZStd::string>().c_str());
                populationMapping[slotKey] = slotId;
            }

            void SendScriptEvent::OnRegistered(const ScriptEvents::ScriptEvent& definition)
            {
                ScriptEvents::ScriptEventNotificationBus::Handler::BusDisconnect(m_scriptEventAssetId);

                for (const auto& methoodDefinition : definition.GetMethods())
                {
                    if (methoodDefinition.GetEventId() == m_eventId)
                    {
                        m_eventName = methoodDefinition.GetName();
                        break;
                    }
                }

                AZ::BehaviorMethod* method{};
                if (FindEvent(method, m_namespaces, m_eventName))
                {
                    ConfigureMethod(*method);
                }
            }

            void SendScriptEvent::ConfigureNode(const AZ::Data::AssetId& assetId, const ScriptCanvas::EBusEventId& eventId)
            {
                SlotIdMapping populationMapping;

                BuildNode(assetId, eventId, populationMapping);

                m_eventSlotMapping = AZStd::move(populationMapping);
            }

            void SendScriptEvent::BuildNode(const AZ::Data::AssetId& assetId, const ScriptCanvas::EBusEventId& eventId, SlotIdMapping& populationMapping)
            {
                m_scriptEventAssetId = assetId;

                GetGraph()->AddDependentAsset(GetEntityId(), azrtti_typeid<ScriptEvents::ScriptEventsAsset>(), m_scriptEventAssetId);

                m_ignoreReadyEvent = true;
                AZ::Data::AssetBus::Handler::BusConnect(assetId);
                m_ignoreReadyEvent = false;

                AZ::Data::Asset<ScriptEvents::ScriptEventsAsset> asset = AZ::Data::AssetManager::Instance().GetAsset<ScriptEvents::ScriptEventsAsset>(assetId);

                const ScriptEvents::ScriptEvent& definition = asset.Get()->m_definition;
                
                // If no name has been serialized, this is a new node, so initialize it to the Script Event's definition values.
                if (m_busId == ScriptCanvas::EBusBusId())
                {
                    m_busId = asset.Get()->GetBusId();
                    m_eventId = eventId;

                    m_version = definition.GetVersion();
                    m_definition = definition;
                }

                AZStd::string busName = definition.GetName();
                AZStd::string ebusEventName;

                for (auto methodDefinition : definition.GetMethods())
                {
                    if (eventId == methodDefinition.GetEventId())
                    {
                        ebusEventName = methodDefinition.GetName();
                        break;
                    }
                }

                Namespaces emptyNamespaces;
    
                ScriptEvents::ScriptEventBus::BroadcastResult(m_scriptEvent, &ScriptEvents::ScriptEventRequests::RegisterScriptEvent, assetId, m_version);

                AZ::BehaviorMethod* method{};
                if (!FindEvent(method, emptyNamespaces, ebusEventName))
                {
                    AZ_Error("Script Canvas", IsUpdating(), "The Script Event %s::%s could not be found", busName.c_str(), ebusEventName.data());
                    return;
                }

                m_busName = busName;
                m_eventName = ebusEventName;

                if (m_version == 0)
                {
                    m_version = definition.GetVersion();
                }

                auto isExposableOutcome = IsExposable(*method);
                AZ_Warning("ScriptCanvas", isExposableOutcome.IsSuccess(), "BehaviorContext Method %s is no longer exposable to ScriptCanvas: %s", isExposableOutcome.GetError().data());
                ConfigureMethod(*method);

                size_t slotOffset = GetSlots().size();

                if (method->HasResult())
                {
                    if (const AZ::BehaviorParameter* result = method->GetResult())
                    {
                        if (!result->m_typeId.IsNull() && result->m_typeId != azrtti_typeid<void>())
                        {
                            // Arbitrary UUID for result slots.
                            // Doesn't need to be globally unique, as each method will only have
                            // a single output.
                            //
                            // Should this change we'll need a new way of generating this.
                            AZ::Uuid slotKey = AZ::Uuid("{C7E99974-D1C0-4108-B731-120AF000060C}");

                            Data::Type outputType(AZ::BehaviorContextHelper::IsStringParameter(*result) ? Data::Type::String() : Data::FromAZType(result->m_typeId));
                            // multiple outs will need out value names
                            const AZStd::string resultSlotName(AZStd::string::format("Result: %s", Data::GetName(outputType).c_str()));

                            DataSlotConfiguration slotConfiguration;

                            slotConfiguration.m_name = resultSlotName.c_str();
                            slotConfiguration.SetConnectionType(ConnectionType::Output);
                            slotConfiguration.SetType(outputType);

                            auto slotIdIter = m_eventSlotMapping.find(slotKey);

                            bool isNewSlot = true;

                            if (slotIdIter != m_eventSlotMapping.end())
                            {
                                isNewSlot = false;
                                slotConfiguration.m_slotId = slotIdIter->second;
                            }

                            m_resultSlotID = InsertSlot(aznumeric_cast<AZ::s64>(slotOffset), slotConfiguration, isNewSlot);
                            ++slotOffset;

                            populationMapping[slotKey] = m_resultSlotID;
                        }
                    }
                }

                ScriptEvents::Method scriptEventMethod;
                bool methodFound = definition.FindMethod(method->m_name, scriptEventMethod);
                
                size_t argIndex = 0;
                
                // Address slot (BusId)
                if (method->HasBusId())
                {
                    if (const AZ::BehaviorParameter* argument = method->GetArgument(argIndex))
                    {
                        AZ::Uuid slotKey = definition.GetAddressTypeProperty().GetId();
                        AddInputSlot(slotOffset + argIndex, argIndex, GetSourceSlotName(), method->GetArgumentToolTip(argIndex)->c_str(), method, argument, slotKey, populationMapping);
                    }
                    ++argIndex;
                }

                // Input parameter slots
                const auto& parameters = scriptEventMethod.GetParameters();
                for (size_t parameterIndex = 0; parameterIndex != parameters.size(); ++parameterIndex, ++argIndex)
                {
                    const ScriptEvents::Parameter& parameter = parameters[parameterIndex];
                    AZ::Uuid slotKey = parameter.GetNameProperty().GetId();
                    const AZ::BehaviorParameter* argument = method->GetArgument(argIndex);
                    const AZStd::string& argName = parameter.GetName();
                    const AZStd::string& argumentTooltip = parameter.GetTooltip();

                    if (argument)
                    {
                        AddInputSlot(slotOffset + argIndex, argIndex, argName, argumentTooltip, method, argument, slotKey, populationMapping);
                    }
                }

                PopulateNodeType();
            }

            void SendScriptEvent::InitializeResultSlotId()
            {
                if (m_method && m_method->HasResult())
                {
                    if (const AZ::BehaviorParameter* result = m_method->GetResult())
                    {
                        if (!result->m_typeId.IsNull() && result->m_typeId != azrtti_typeid<void>())
                        {
                            Data::Type outputType(AZ::BehaviorContextHelper::IsStringParameter(*result) ? Data::Type::String() : Data::FromAZType(result->m_typeId));
                            // multiple outs will need out value names
                            const AZStd::string resultSlotName(AZStd::string::format("Result: %s", Data::GetName(outputType).c_str()));

                            Slot* slot = GetSlotByName(resultSlotName);

                            if (slot)
                            {
                                m_resultSlotID = slot->GetId();
                            }
                        }
                    }
                }
            }

            void SendScriptEvent::OnScriptEventReady(const AZ::Data::Asset<ScriptEvents::ScriptEventsAsset>& asset)
            {
                if (!IsConfigured())
                {
                    m_asset = asset;
                    CreateSender(asset);
                }

                if (!m_ignoreReadyEvent)
                {
                    RegisterScriptEvent(asset);
                }
            }

            bool SendScriptEvent::RegisterScriptEvent(AZ::Data::Asset<ScriptEvents::ScriptEventsAsset> asset)
            {
                if (!m_scriptEvent)
                {
                    if (!ScriptEvents::ScriptEventNotificationBus::Handler::BusIsConnectedId(m_scriptEventAssetId))
                    {
                        ScriptEvents::ScriptEventNotificationBus::Handler::BusConnect(m_scriptEventAssetId);
                    }

                    ScriptEvents::ScriptEventBus::BroadcastResult(m_scriptEvent, &ScriptEvents::ScriptEventRequests::RegisterScriptEvent, m_scriptEventAssetId, m_version);
                    if (m_scriptEvent)
                    {
                        m_scriptEvent->Init(m_scriptEventAssetId);
                        m_busName = m_scriptEvent->GetBusName();
                    }

                    return IsConfigured();
                }

                return false;
            }

            bool SendScriptEvent::CreateSender(AZ::Data::Asset<ScriptEvents::ScriptEventsAsset> asset)
            {
                if (IsConfigured())
                {
                    return true;
                }
                
                RegisterScriptEvent(asset);

                if (asset && asset.IsReady())
                {
                    const ScriptEvents::ScriptEvent& definition = asset.Get()->m_definition;
                    const AZStd::string& name = definition.GetName();

                    AZ::BehaviorMethod* method{};
                    if (m_version != definition.GetVersion())
                    {
                        // The desired version of the event was not found, check if we have a more up to date version
                        ScriptEvents::Method methodDefinition;
                        if (definition.FindMethod(m_eventId, methodDefinition))
                        {
                            if (FindEvent(method, m_namespaces, methodDefinition.GetName()))
                            {
                                ConfigureMethod(*method);
                                InitializeResultSlotId();
                            }
                        }
                    }
                    else
                    {
                        // The desired version of the event was not found, check if we have a more up to date version
                        ScriptEvents::Method methodDefinition;
                        if (definition.FindMethod(m_eventId, methodDefinition))
                        {
                            if (FindEvent(method, m_namespaces, methodDefinition.GetName()))
                            {
                                ConfigureMethod(*method);
                                InitializeResultSlotId();
                                
                                return true;
                            }
                        }
                    }
                }

                return false;
            }

            bool SendScriptEvent::IsOutOfDate() const
            {
                bool isOutOfDate = false;

                AZ::Data::Asset<ScriptEvents::ScriptEventsAsset> assetData = AZ::Data::AssetManager::Instance().GetAsset<ScriptEvents::ScriptEventsAsset>(GetAssetId());

                if(assetData)
                {
                    ScriptEvents::ScriptEvent& definition = assetData.Get()->m_definition;

                    if(GetVersion() != definition.GetVersion())
                    {
                        isOutOfDate = true;
                    }
                }
                else
                {
                    // If we don't have any asset data. We are definitely out of date.
                    return true;
                }

                return isOutOfDate;
            }

            UpdateResult SendScriptEvent::OnUpdateNode()
            {
                for(auto mapPair : m_eventSlotMapping)
                {
                    Slot* slot = GetSlot(mapPair.second);

                    if (slot == nullptr)
                    {
                        continue;
                    }

                    const bool removeConnections = false;
                    RemoveSlot(mapPair.second, removeConnections);
                }

                ScriptCanvas::EBusEventId eventId = m_eventId;

                m_busId = ScriptCanvas::EBusBusId();
                m_eventId = ScriptCanvas::EBusEventId();

                m_eventMap.clear();
                m_scriptEvent.reset();

                m_ebus = nullptr;
                m_method = nullptr;

                m_version = 0;

                SlotIdMapping populationMapping;

                BuildNode(m_scriptEventAssetId, eventId, populationMapping);

                m_eventSlotMapping = AZStd::move(populationMapping);

                if (m_method == nullptr)
                {
                    return UpdateResult::DeleteNode;
                }
                else
                {
                    return UpdateResult::DirtyGraph;
                }
            }

            AZStd::string SendScriptEvent::GetUpdateString() const
            {
                if (m_method)
                {
                    return AZStd::string::format("Updated ScriptEvent (%s)", m_definition.GetName().c_str());
                }
                else
                {
                    return AZStd::string::format("Deleted ScriptEvent (%s)", m_asset.GetId().ToString<AZStd::string>().c_str());
                }
            }

            void SendScriptEvent::OnDeactivate()
            {
                m_method = nullptr;
                m_scriptEvent = nullptr;
                m_ebus = nullptr;

                ScriptEvents::ScriptEventNotificationBus::Handler::BusDisconnect();
                AZ::Data::AssetBus::Handler::BusDisconnect();

                ScriptEventBase::OnDeactivate();
            }

            void SendScriptEvent::ConfigureMethod(AZ::BehaviorMethod& method)
            {
                if (IsConfigured())
                {
                    return;
                }

                m_method = &method;
                m_eventName = method.m_name;
            }

            bool SendScriptEvent::FindEvent(AZ::BehaviorMethod*& outMethod, const Namespaces& namespaces, AZStd::string_view eventName)
            {
                if (m_scriptEvent == nullptr)
                {
                    return false;
                }                

                m_ebus = m_scriptEvent->GetBehaviorBus();

                if (m_ebus == nullptr)
                {
                    return false;
                }

                const auto& sender = m_ebus->m_events.find(eventName);

                if (sender == m_ebus->m_events.end())
                {
                    AZ_Error("Script Canvas", IsUpdating(), "No event by name of %s found in the ebus %s", eventName.data(), m_scriptEvent->GetBusName().c_str());
                    return false;
                }

                AZ::EBusAddressPolicy addressPolicy
                    = (m_ebus->m_idParam.m_typeId.IsNull() || m_ebus->m_idParam.m_typeId == AZ::AzTypeInfo<void>::Uuid())
                    ? AZ::EBusAddressPolicy::Single
                    : AZ::EBusAddressPolicy::ById;

                AZ::BehaviorMethod* method
                    = m_ebus->m_queueFunction
                    ? (addressPolicy == AZ::EBusAddressPolicy::ById ? sender->second.m_queueEvent : sender->second.m_queueBroadcast)
                    : (addressPolicy == AZ::EBusAddressPolicy::ById ? sender->second.m_event : sender->second.m_broadcast);

                if (!method)
                {
                    AZ_Error("Script Canvas", false, "Queue function mismatch in %s-%s", eventName.data(), m_scriptEvent->GetBusName().c_str());
                    return false;
                }

                outMethod = method;
                return true;
            }
        }
    }
}

#include <Include/ScriptCanvas/Libraries/Core/SendScriptEvent.generated.cpp>
