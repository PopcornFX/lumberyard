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
#include "precompiled.h"
#include "ScriptEnginePython.h"

#include <llvm/Support/Path.h>

#if ENABLE_PYTHON
#include "Configuration.h"
#include <sstream>
#include <iomanip>


// This is not AZ code - AzCore is not used in the AzCoreGenerator project
#pragma warning(push)
#pragma warning(disable: 5033) // Disabling C++17 warning - warning C5033: 'register' is no longer a supported storage class which occurs in Python 2.7
// Undef _DEBUG to prevent python from trying to link with debug libs even when doing 
// a debug build. To force the use of a debug python, define USE_DEBUG_PYTHON and make 
// sure the build output directory has a python27_d.dll file and that the lib path 
// includes a directory with a python27_d.lib file.
// Using the WAF build system this can be accomplished by setting the USE_DEBUG_PYTHON 
// environment variable to the directory with a debug python build (such as 3rdParty\
// Python\2.7.10\win64_debug) before running lmbr_waf.
#ifndef USE_DEBUG_PYTHON
#undef _DEBUG
#endif
#include "Python.h"
#ifdef DEBUG
#define _DEBUG
#endif
#pragma warning(pop)

#ifdef Py_DEBUG
#define PYTHON_PATHS Configuration::PythonDebugPaths
#define PYTHON_HOME Configuration::PythonHomeDebug
#else
#define PYTHON_PATHS Configuration::PythonPaths
#define PYTHON_HOME Configuration::PythonHome
#endif // Py_DEBUG

namespace ScriptErrors
{
    const unsigned int ImportMainFailed = 100;
    const unsigned int ImportLauncherFailed = 200;
    const unsigned int InvalidSyntax = 300;
    const unsigned int LauncherError = 400;
    const unsigned int NoScripts = 500;
    const unsigned int RunScriptsError = 600;
    const unsigned int RunScriptsErrorWithReturnedValue = 700;
};

namespace
{
    bool g_runScriptsResult = false;
    
    struct UTF8Encoding {};
    struct ASCIIEncoding {};

    //! Prior to passing down text into python in order to run templates, we need to
    //! make sure to properly process escape sequences into the format JSON will
    //! understand.
    namespace StringEscape
    {
        template<typename Encoding>
        static typename std::enable_if<std::is_same<UTF8Encoding, Encoding>::value, std::string>::type Convert(const std::string& str)
        {
            std::stringstream ss;
            for (size_t i = 0; i < str.length(); ++i)
            {
                if (unsigned(str[i]) < '\x20' || str[i] == '\\' || str[i] == '"')
                {
                    ss << "\\u" << std::setfill('0') << std::setw(4) << std::hex << unsigned(str[i]);
                }
                else
                {
                    ss << str[i];
                }
            }
            return ss.str();
        }

        template<typename Encoding>
        static typename std::enable_if<std::is_same<ASCIIEncoding, Encoding>::value, std::string>::type Convert(const std::string& str)
        {
            std::stringstream ss;
            for (auto iter = str.cbegin(); iter != str.cend(); iter++)
            {
                switch (*iter)
                {
                case '\\':
                    ss << "\\\\";
                    break;
                case '"':
                    ss << "\\\"";
                    break;
                case '\'':
                    ss << "\\\'";
                    break;
                case '/':
                    ss << "\\/";
                    break;
                case '\b':
                    ss << "\\b";
                    break;
                case '\f':
                    ss << "\\f";
                    break;
                case '\n':
                    ss << "\\n";
                    break;
                case '\r':
                    ss << "\\r";
                    break;
                case '\t':
                    ss << "\\t";
                    break;
                default:
                    ss << *iter;
                    break;
                }
            }
            return ss.str();
        }
    }

    static PyObject* RegisterOutputFile(PyObject* /*self*/, PyObject* args)
    {
        const char* generatedFilePath = nullptr;
        PyObject* objectShouldAddToBuild = nullptr;
        bool shouldAddToBuild = false;
        // Parse out a string, optionally parse out an object (we'll cast it to a bool, if possible)
        if (!PyArg_ParseTuple(args, "s|O:RegisterOutputFile", &generatedFilePath, &objectShouldAddToBuild))
        {
            return NULL;
        }
        // Python did not add proper "bool" support via ParseTuple until 3.3
        // The recommended method is cast to an object and use the IsTrue method and assume false otherwise.
        if (objectShouldAddToBuild)
        {
            int isTrue = PyObject_IsTrue(objectShouldAddToBuild);
            if (isTrue == -1)
            {
                return NULL;
            }
            shouldAddToBuild = isTrue == 1;
        }

        CodeGenerator::Output::GeneratedFile(generatedFilePath, shouldAddToBuild);
        Py_RETURN_NONE;
    }

    static PyObject* OutputPrint(PyObject* /*self*/, PyObject* args)
    {
        const char* outputString = nullptr;
        if (!PyArg_ParseTuple(args, "s:OutputPrint", &outputString))
        {
            return NULL;
        }

        CodeGenerator::Output::Print(outputString);
        Py_RETURN_NONE;
    }

    static PyObject* OutputError(PyObject* /*self*/, PyObject* args)
    {
        const char* outputString = nullptr;
        if (!PyArg_ParseTuple(args, "s:OutputError", &outputString))
        {
            return NULL;
        }

        CodeGenerator::Output::Error(outputString);
        Py_RETURN_NONE;
    }

    static PyObject* RegisterDependencyFile(PyObject* /*self*/, PyObject* args)
    {
        const char* dependencyFilePath = nullptr;
        // Parse out a string
        if (!PyArg_ParseTuple(args, "s:RegisterDependencyFile", &dependencyFilePath))
        {
            return NULL;
        }

        CodeGenerator::Output::DependencyFile(dependencyFilePath);
        Py_RETURN_NONE;
    }

    static PyObject* SetRunScriptsResult(PyObject* /*self*/, PyObject* args)
    {
        PyObject* runScriptsResultObject = nullptr;
        if (!PyArg_ParseTuple(args, "O:SetRunScriptsResult", &runScriptsResultObject))
        {
            return NULL;
        }

        g_runScriptsResult = PyObject_IsTrue(runScriptsResultObject);
        Py_RETURN_NONE;
    }

    static PyMethodDef ExtensionMethods[] =
    {
        {"RegisterOutputFile", RegisterOutputFile, METH_VARARGS, "Takes a string absolute path to generated output file and boolean indicating if file should be injected into the build or not, returns nothing"},
        {"OutputPrint", OutputPrint, METH_VARARGS, "Takes a string and returns it from the code generator as informational output"},
        {"OutputError", OutputError, METH_VARARGS, "Takes a string and returns it from the code generator as error output"},
        {"RegisterDependencyFile", RegisterDependencyFile, METH_VARARGS, "Takes a string absolute path to raw dependency file, returns nothing"},
        {"SetRunScriptsResult", SetRunScriptsResult, METH_VARARGS, "Takes a boolean indicating whether run_scripts was successful or not, returns nothing"},
        // Add one entry per method here
        {NULL, NULL, 0, NULL}
    };
    
    static struct PyModuleDef ExtensionModDef =
    {
        PyModuleDef_HEAD_INIT,
        "azcg_extension", /* name of module */
        nullptr,          /* module documentation, may be NULL */
        -1,          /* size of per-interpreter state of the module, or -1 if the module keeps state in global variables. */
        ExtensionMethods
    };

    PyMODINIT_FUNC PyInitAzcgExtension()
    {
        return PyModule_Create(&ExtensionModDef);
    }
    
}

namespace PythonScripting
{
    namespace Configuration
    {
        using namespace llvm;
        static cl::OptionCategory PythonSettingsCategory("Python Settings");
        static cl::opt<std::string> PythonHome("python-home", cl::desc("PYTHONHOME equivalent, as the environment variable is ignored"), cl::cat(PythonSettingsCategory), cl::Required);
        static cl::opt<std::string> PythonHomeDebug("python-home-debug", cl::desc("PYTHONHOME equivalent for debug python, as the environment variable is ignored"), cl::cat(PythonSettingsCategory), cl::Optional);
        static cl::list<std::string> PythonPaths("python-path", cl::desc("Path to python libraries and scripts for utility to use"), cl::cat(PythonSettingsCategory), cl::ZeroOrMore);
        static cl::list<std::string> PythonDebugPaths("python-debug-path", cl::desc("Path to python debug libraries and scripts for utility to use in debug"), cl::cat(PythonSettingsCategory), cl::ZeroOrMore);
        static cl::list<std::string> CodeGenScript("codegen-script", cl::desc("Filename of the code generation script to invoke."), cl::cat(PythonSettingsCategory), cl::ZeroOrMore);
        static cl::OptionCategory TemplateSettingsCategory("Template Settings");
    }

    PythonScriptEngine::PythonScriptEngine(const char* programName)
        : m_programName(programName)
    {
    }

    int Py_FlushLine()
    {
        PyObject *f = PySys_GetObject("stdout");
        if (!f)
        {
            return 0;
        }
        return PyFile_WriteString("\n", f);
    }

    PyObject* OpenModule(const char* modulePath)
    {
        // Make an identifier for the open_code command, compiler doesn't like _Py_IDENTIFIER
        PyObject* PyId_open;
        PyId_open = PyUnicode_InternFromString("open_code");

        PyObject *ioMod, *openedFile;

        PyGILState_STATE gilState = PyGILState_Ensure();

        ioMod = PyImport_ImportModule("io");

        openedFile = PyObject_CallMethod(ioMod, "open", "s", modulePath);
        Py_INCREF(openedFile);

        Py_DECREF(ioMod);

        return openedFile;
    }

    unsigned int PythonScriptEngine::Initialize()
    {
        // Find the program path and get a relative path for python (We should either be in Bin64 or Bin64.Debug)
        InitializePython(const_cast<char*>(m_programName.c_str()));

        // Find the path to this executable, append launcher.py
        llvm::StringRef exePath = llvm::sys::path::parent_path(m_programName);
        std::string scriptPath = exePath.str();
        scriptPath.append(llvm::sys::path::get_separator());
        scriptPath.append("launcher.py");

        // Execute the core script, this will provide functions we can invoke later
        PyObject* PyFileObject = OpenModule(scriptPath.c_str());
        if (!PyFileObject)
        {
            CodeGenerator::Output::Error("Failed to load core script: %s", scriptPath.c_str());
            return CodeGenerator::ErrorCodes::ScriptError + ScriptErrors::ImportLauncherFailed;
        }
        Py_DECREF(PyFileObject);

        // This block of code was duplicated from Python C API - PyRunSimpleFileExFlags()
        // There is no known function in Python that allows for simple script execution with custom error object handling
        // The code has been updated to be code standards compliant, however implementation will remain as-is to preserve original functionality
        {
            PyObject* pyModuleMain;
            PyObject* pyDictMain;
            PyObject* pyResultObject = nullptr;
            bool setFileName = false;

            pyModuleMain = PyImport_AddModule("__main__");
            if (pyModuleMain == NULL)
            {
                // Probably missing __main__.py from scripts folder provided
                CodeGenerator::Output::Error("Python: Unable to import __main__ module");
                return CodeGenerator::ErrorCodes::ScriptError + ScriptErrors::ImportMainFailed;
            }
            // Todo - Refactor this to reduce duplication with the similar code in Invoke()
            // Also eliminate the need for goto.
            // https://issues.labcollab.net/browse/LMBR-23574
            Py_INCREF(pyModuleMain);
            pyDictMain = PyModule_GetDict(pyModuleMain);

            const int closeFileAfterRun = 1;
            const char mode[] = "r";
            FILE* fp = _Py_fopen(scriptPath.c_str(), mode);

            if (PyDict_GetItemString(pyDictMain, "__file__") == nullptr)
            {
                PyObject* pyFilenameString = PyUnicode_InternFromString(scriptPath.c_str());
                if (pyFilenameString == NULL)
                {
                    goto done;
                }
                if (PyDict_SetItemString(pyDictMain, "__file__", pyFilenameString) < 0)
                {
                    Py_DECREF(pyFilenameString);
                    goto done;
                }
                setFileName = true;
                Py_DECREF(pyFilenameString);
            }

            if (!fp)
            {
                CodeGenerator::Output::Error("Error opening file %s\n", scriptPath.c_str());
                return CodeGenerator::ErrorCodes::ScriptError + ScriptErrors::ImportMainFailed;
            }

            pyResultObject = PyRun_FileExFlags(fp, scriptPath.c_str(), Py_file_input, pyDictMain, pyDictMain, closeFileAfterRun, nullptr);

            if (pyResultObject == nullptr)
            {
                unsigned int scriptErrorNumber = 0;
                PyObject* pyErrType;
                PyObject* pyErrValue;
                PyObject* pyErrTraceback;
                PyErr_Fetch(&pyErrType, &pyErrValue, &pyErrTraceback);
                PyErr_NormalizeException(&pyErrType, &pyErrValue, &pyErrTraceback);

                if (PyErr_GivenExceptionMatches(pyErrType, PyExc_SyntaxError))
                {
                    PyObject* pyFileName = PyObject_GetAttrString(pyErrValue, "filename");
                    PyObject* pyLineNumber = PyObject_GetAttrString(pyErrValue, "lineno");
                    PyObject* pyLineNumberString = PyObject_Str(pyLineNumber);
                    PyObject* pyOffset = PyObject_GetAttrString(pyErrValue, "offset");
                    PyObject* pyOffsetString = PyObject_Str(pyOffset);
                    PyObject* pyText = PyObject_GetAttrString(pyErrValue, "text");
                    long offset = PyLong_AS_LONG(pyOffset);
                    if (offset > 0)
                    {
                        offset -= 1; // Offset by one since the first character is column 1, so no spaces
                    }
                    std::string offsetCaret = std::string(offset, ' ') + "^";
                    CodeGenerator::Output::Error("%s(%s,%s): SyntaxError: invalid syntax.\n%s%s\n", PyUnicode_AsUTF8(pyFileName), PyUnicode_AsUTF8(pyLineNumberString), PyUnicode_AsUTF8(pyOffsetString), PyUnicode_AsUTF8(pyText), offsetCaret.c_str());
                    scriptErrorNumber = ScriptErrors::InvalidSyntax;
                    Py_DECREF(pyText);
                    Py_DECREF(pyOffsetString);
                    Py_DECREF(pyOffset);
                    Py_DECREF(pyLineNumberString);
                    Py_DECREF(pyLineNumber);
                    Py_DECREF(pyFileName);
                }
                else
                {
                    PyObject* pyErrTypeStr = PyObject_Str(pyErrType);
                    PyObject* pyErrValueStr = PyObject_Str(pyErrValue);
                    PyObject* pyErrTracebackStr = PyObject_Str(pyErrTraceback);
                    const char* errTypeStr = PyUnicode_AsUTF8(pyErrTypeStr);
                    const char* errTypeValueStr = PyUnicode_AsUTF8(pyErrValueStr);
                    const char* errTracebackStr = PyUnicode_AsUTF8(pyErrTracebackStr);
                    CodeGenerator::Output::Error("Python Error %s: %s\n%s\n", errTypeStr, errTypeValueStr, errTracebackStr);
                    scriptErrorNumber = ScriptErrors::LauncherError;
                    Py_DECREF(pyErrTracebackStr);
                    Py_DECREF(pyErrValueStr);
                    Py_DECREF(pyErrTypeStr);
                }
                // End custom error handling code

                // Clear the error
                PyErr_Clear();

                return CodeGenerator::ErrorCodes::ScriptError + scriptErrorNumber;
            }
            
            Py_DECREF(pyResultObject);
            if (Py_FlushLine())
            {
                PyErr_Clear();
            }
done:
            if (setFileName && PyDict_DelItemString(pyDictMain, "__file__"))
            {
                PyErr_Clear();
            }
            Py_DECREF(pyModuleMain);
        }
        // End block of Python C API code

        return 0;
    }

    bool PythonScriptEngine::HasScripts() const
    {
        return Configuration::CodeGenScript.size() > 0;
    }

    std::list<std::string> PythonScriptEngine::GetScripts() const
    {
        return std::list<std::string>(begin(Configuration::CodeGenScript), end(Configuration::CodeGenScript));
    }

    unsigned int PythonScriptEngine::Invoke(const char* jsonString, const char* inputFileName, const char* inputPath, const char* outputPath)
    {
        if (!HasScripts())
        {
            CodeGenerator::Output::Error("No python driver scripts provided");
            return CodeGenerator::ErrorCodes::ScriptError + ScriptErrors::NoScripts;
        }

        // Generate comma delimited string of scripts to run in python
        std::string scriptsToRun = "";
        for (auto& script : Configuration::CodeGenScript)
        {
            scriptsToRun.append(script);
            scriptsToRun.append(",");
        }
        scriptsToRun = scriptsToRun.substr(0, scriptsToRun.size() - 1);

        std::stringstream scriptCommand;
        // Python
        // def Run(scripts, dataObject, inputPath, outputPath, inputFile):
        scriptCommand << "SetRunScriptsResult(run_scripts(\'" << scriptsToRun.c_str() << "\', \'" << StringEscape::Convert<ASCIIEncoding>(jsonString) << "\', \'" << inputPath << "\', \'" << outputPath << "\', \'" << inputFileName << "\'))" << std::endl;

        PyCompilerFlags pyCompilerFlags;
        //pyCompilerFlags.cf_flags = 0;
        // Unsure if InspectFlag is necessary for our error handling
        pyCompilerFlags.cf_flags = Py_InspectFlag;

        // This block of code was duplicated from Python C API - PyRun_String()
        // There is no known function in Python that allows for simple script execution with custom error object handling
        // The code has been updated to be code standards compliant, however implementation will remain as-is to preserve original functionality
        {
            PyObject* pyModuleMain;
            PyObject* pyDictMain;
            PyObject* pyResultObject;
            pyModuleMain = PyImport_AddModule("__main__");
            if (pyModuleMain == nullptr)
            {
                // Probably missing __main__.py from scripts folder provided
                CodeGenerator::Output::Error("Python: Unable to import __main__ module");
                return CodeGenerator::ErrorCodes::ScriptError + ScriptErrors::ImportMainFailed;
            }
            pyDictMain = PyModule_GetDict(pyModuleMain);

            // Run our script
            pyResultObject = PyRun_StringFlags(scriptCommand.str().c_str(), Py_single_input, pyDictMain, pyDictMain, &pyCompilerFlags);

            // Error handling
            if (!pyResultObject)
            {
                // Null result object means that Python execution failed internally
                // Custom error handling (not from Python C API)
                // str() the exception (might give a decent error string for now)
                 
                PyObject* pyErrType;
                PyObject* pyErrValue;
                PyObject* pyErrTraceback;
                PyErr_Fetch(&pyErrType, &pyErrValue, &pyErrTraceback);
                PyErr_NormalizeException(&pyErrType, &pyErrValue, &pyErrTraceback);

                unsigned int scriptErrorCode = 0;
                if (PyErr_GivenExceptionMatches(pyErrType, PyExc_SyntaxError))
                {
                    PyObject* pyFileName = PyObject_GetAttrString(pyErrValue, "filename");
                    PyObject* pyLineNumber = PyObject_GetAttrString(pyErrValue, "lineno");
                    PyObject* pyLineNumberString = PyObject_Str(pyLineNumber);
                    CodeGenerator::Output::Error("Python syntax error during run_scripts.\nSyntax error at %s:%s\n", PyUnicode_AsUTF8(pyFileName), PyUnicode_AsUTF8(pyLineNumberString));
                    scriptErrorCode = ScriptErrors::InvalidSyntax;
                    Py_DECREF(pyLineNumberString);
                    Py_DECREF(pyLineNumber);
                    Py_DECREF(pyFileName);
                }
                else
                {
                    PyObject* pyErrStr = PyObject_Str(pyErrValue);
                    CodeGenerator::Output::Error("Python returned an error during run_scripts: %s\n", PyUnicode_AsUTF8(pyErrStr));
                    scriptErrorCode = ScriptErrors::RunScriptsError;
                    Py_DECREF(pyErrStr);
                }
                // End custom error handling

                // Clear the error
                PyErr_Clear();
                return CodeGenerator::ErrorCodes::ScriptError + scriptErrorCode;
            }
            else if (!g_runScriptsResult)
            {
                return CodeGenerator::ErrorCodes::ScriptError + ScriptErrors::RunScriptsErrorWithReturnedValue;
            }

            // Decrement reference since it was incremented for us by PyRun_StringFlags
            Py_DECREF(pyResultObject);
            pyResultObject = nullptr;
            if (Py_FlushLine())
            {
                PyErr_Clear();
            }
        }
        // End Python C API code

        return 0;
    }

    void PythonScriptEngine::Shutdown()
    {
        Py_Finalize();
    }

    void PythonScriptEngine::InitializePython(char* programName)
    {
        Py_SetProgramName(Py_DecodeLocale(programName, nullptr));
        Py_SetPythonHome(Py_DecodeLocale(PYTHON_HOME.c_str(), nullptr));

        PyImport_AppendInittab("azcg_extension", PyInitAzcgExtension);

        // Init without importing site.py (it's not in the current folder)
        Py_NoSiteFlag = 1;

        // Might be necessary at some point, this SHOULD prevent PYTHONHOME and PYTHONPATH from being found in the environment, which we overwrite anyway
        //Py_IgnoreEnvironmentFlag = 1;

        // Don't write pyc files which can conflict with each other since build servers and the like share drives
        Py_DontWriteBytecodeFlag = 1;

        // Show all imports into Python, useful for debugging just where it is able to import something from and which paths it is trying first
        //Py_VerboseFlag = INT_MAX;

        Py_Initialize();

        // Find the path to this executable
        std::string exePath = llvm::sys::path::parent_path(programName).str();

        // Set the sys.path to empty, we will append only our environment to it later
        std::string sysPathModify = "import sys\nsys.path = []\n";

        // Add the path to this executable to the front of sys.path
        PYTHON_PATHS.push_back(exePath.c_str());

        // Set our paths into sys.path
        for (auto& pythonPath : PYTHON_PATHS)
        {
            if (CodeGenerator::GlobalConfiguration::g_enableVerboseOutput)
            {
                CodeGenerator::Output::Print("Adding python path: %s\n", pythonPath.c_str());
            }

            std::replace(pythonPath.begin(), pythonPath.end(), '\\', '/');

            sysPathModify += "sys.path.append('";
            sysPathModify += pythonPath;
            sysPathModify += "')\n";
        }

        PyRun_SimpleString(sysPathModify.c_str());

        // Now import site.py
        PyImport_ImportModule("site");
        PyImport_ImportModule("encodings");
    }
}

#endif // #if ENABLE_PYTHON
