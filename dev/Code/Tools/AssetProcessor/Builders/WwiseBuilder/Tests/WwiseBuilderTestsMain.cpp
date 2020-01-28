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

#include <AzTest/AzTest.h>
#include <Source/WwiseBuilderComponent.h>
#include <AzCore/IO/FileIO.h>
#include <AzCore/UnitTest/TestTypes.h>
#include <AzCore/UserSettings/UserSettingsComponent.h>
#include <AzFramework/StringFunc/StringFunc.h>
#include <AzToolsFramework/Application/ToolsApplication.h>

using namespace WwiseBuilder;
using namespace AZ;

class WwiseBuilderTests
    : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_app.Start(AZ::ComponentApplication::Descriptor());
        AZ::UserSettingsComponentRequestBus::Broadcast(&AZ::UserSettingsComponentRequests::DisableSaveOnFinalize);

        const char* dir = m_app.GetExecutableFolder();
        AZ::IO::FileIOBase::GetInstance()->SetAlias("@root@", dir);
        AZ::IO::FileIOBase::GetInstance()->SetAlias("@assets@", dir);
    }

    void TearDown() override
    {
        m_app.Stop();
    }

    AZStd::string GetRequestPath(AZStd::string_view fileName)
    {
        constexpr char requestPath[] = "Sounds/wwise/";
        return AZStd::string::format("%s%.*s", requestPath, fileName.size(), fileName.data());
    }

    AZStd::string GetTestFileAliasedPath(AZStd::string_view fileName)
    {
        constexpr char testFileFolder[] = "@root@/../Code/Tools/AssetProcessor/Builders/WwiseBuilder/Tests/Sounds/wwise/";
        return AZStd::string::format("%s%.*s", testFileFolder, fileName.size(), fileName.data());
    }

    AZStd::string GetTestFileFullPath(AZStd::string_view fileName)
    {
        AZStd::string aliasedPath = GetTestFileAliasedPath(fileName);
        char resolvedPath[AZ_MAX_PATH_LEN];
        AZ::IO::FileIOBase::GetInstance()->ResolvePath(aliasedPath.c_str(), resolvedPath, AZ_MAX_PATH_LEN);
        return AZStd::string(resolvedPath);
    }

    void TestFailureCase(AZStd::string_view fileName)
    {
        WwiseBuilderWorker worker;
        AssetBuilderSDK::ProductPathDependencySet resolvedPaths;

        AZStd::string relativeRequestPath = GetRequestPath(fileName);
        AZStd::string absoluteRequestPath = GetTestFileFullPath(fileName);

        AZ::Outcome<AZStd::string, AZStd::string> result = worker.GatherProductDependencies(absoluteRequestPath, relativeRequestPath, resolvedPaths);
        ASSERT_FALSE(result.IsSuccess());
        ASSERT_EQ(resolvedPaths.size(), 0);
    }

    void TestSuccessCase(AZStd::string_view fileName, AZStd::vector<const char*>& expectedDependencies, bool expectWarning = false)
    {
        WwiseBuilderWorker worker;
        AssetBuilderSDK::ProductPathDependencySet resolvedPaths;
        size_t referencedFilesCount = expectedDependencies.size();

        AssetBuilderSDK::ProductPathDependencySet expectedResolvedPaths;
        for (const char* path : expectedDependencies)
        {
            expectedResolvedPaths.emplace(path, AssetBuilderSDK::ProductPathDependencyType::ProductFile);
        }

        AZStd::string relativeRequestPath = GetRequestPath(fileName);
        AZStd::string absoluteRequestPath = GetTestFileFullPath(fileName);

        AZ::Outcome<AZStd::string, AZStd::string> result = worker.GatherProductDependencies(absoluteRequestPath, relativeRequestPath, resolvedPaths);
        ASSERT_TRUE(result.IsSuccess());
        ASSERT_EQ(!result.GetValue().empty(), expectWarning);
        ASSERT_EQ(resolvedPaths.size(), referencedFilesCount);
        if (referencedFilesCount > 0)
        {
            for (const AssetBuilderSDK::ProductPathDependency& dependency : expectedResolvedPaths)
            {
                ASSERT_TRUE(resolvedPaths.find(dependency) != resolvedPaths.end());
            }
        }
    }

    void TestSuccessCase(AZStd::string_view fileName, const char* expectedTexture, bool expectWarning = false)
    {
        AZStd::vector<const char*> expectedTextures;
        expectedTextures.push_back(expectedTexture);
        TestSuccessCase(fileName, expectedTextures, expectWarning);
    }

    void TestSuccessCaseNoDependencies(AZStd::string_view fileName, bool expectWarning = false)
    {
        AZStd::vector<const char*> expectedTextures;
        TestSuccessCase(fileName, expectedTextures, expectWarning);
    }

private:
    AzToolsFramework::ToolsApplication m_app;
};

TEST_F(WwiseBuilderTests, WwiseBuilder_EmptyFile_ExpectFailure)
{
    // Should fail in WwiseBuilderWorker::GatherProductDependencies, when checking for the size of the file.
    TestFailureCase("test_bank1.bnk");
}

TEST_F(WwiseBuilderTests, WwiseBuilder_MalformedMetadata_ParsingFailed_ExpectFailure)
{
    // Should fail in WwiseBuilderWorker::GatherProductDependencies, trying to parse the json file.
    TestFailureCase("test_bank2.bnk");
}

TEST_F(WwiseBuilderTests, WwiseBuilder_MalformedMetadata_NoRootObject_ExpectFailure)
{
    // Should fail in WwiseBuilderWorker::GatherProductDependencies after calling Internal::GetDependenciesFromMetadata,
    //  which should return an AZ::Failure when the json data's root element isn't an object.
    TestFailureCase("test_bank3.bnk");
}

TEST_F(WwiseBuilderTests, WwiseBuilder_MalformedMetadata_DependencyFieldWrongType_ExpectFailure)
{
    // Should fail in WwiseBuilderWorker::GatherProductDependencies after calling Internal::GetDependenciesFromMetadata,
    //  which should return an AZ::Failure when the dependency field is not an array.
    TestFailureCase("test_bank4.bnk");
}

TEST_F(WwiseBuilderTests, WwiseBuilder_InitBank_NoMetadata_NoDependencies)
{
    TestSuccessCaseNoDependencies("init.bnk");
}

TEST_F(WwiseBuilderTests, WwiseBuilder_ContentBank_NoMetadata_NoDependencies)
{
    // Should generate a warning after trying to find metadata for the given bank, when the bank is not the init bank. 
    //  Warning should be about not being able to generate full dependency information without the metadata file.
    TestSuccessCaseNoDependencies("test_doesNotExist.bnk", true);
}

TEST_F(WwiseBuilderTests, WwiseBuilder_ContentBank_OneDependency)
{
    TestSuccessCase("test_bank5.bnk", "Sounds/wwise/init.bnk");
}

TEST_F(WwiseBuilderTests, WwiseBuilder_ContentBank_MultipleDependencies)
{
    AZStd::vector<const char*> expectedPaths = {
        "Sounds/wwise/init.bnk",
        "Sounds/wwise/test_bank2.bnk",
        "Sounds/wwise/12345678.wem"
    };
    TestSuccessCase("test_bank6.bnk", expectedPaths);
}

TEST_F(WwiseBuilderTests, WwiseBuilder_ContentBank_DependencyArrayNonexistent_NoDependencies)
{
    // Should generate a warning when trying to get dependency info from metadata file, but the dependency field does 
    //  not an empty array. Warning should be describing that a dependency on the init bank was added by default, but 
    //  the full dependency list could not be generated.
    TestSuccessCaseNoDependencies("test_bank7.bnk", true);
}

TEST_F(WwiseBuilderTests, WwiseBuilder_ContentBank_NoElementsInDependencyArray_NoDependencies)
{
    // Should generate a warning when trying to get dependency info from metadata file, but the dependency field is 
    //  an empty array. Warning should be describing that a dependency on the init bank was added by default, but the
    //  full dependency list could not be generated.
    TestSuccessCaseNoDependencies("test_bank8.bnk", true);
}

TEST_F(WwiseBuilderTests, WwiseBuilder_ContentBank_MissingInitBankDependency_MultipleDependencies)
{
    // Should generate a warning when trying to get dependency info from metadata file, but the dependency info in the 
    //  metadata doesn't include the init bank. Warning should be describing that a dependency on the init bank was 
    //  added by default.
    AZStd::vector<const char*> expectedPaths = {
        "Sounds/wwise/init.bnk",
        "Sounds/wwise/test_bank2.bnk",
        "Sounds/wwise/12345678.wem"
    };
    TestSuccessCase("test_bank9.bnk", expectedPaths, true);
}

AZ_UNIT_TEST_HOOK();
