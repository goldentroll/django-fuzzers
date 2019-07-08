#include <climits>
#include <cstdlib>
#include <iomanip>
#include <libgen.h>
#include <sstream>
#include <vector>
#include <string>
#include <optional>
#include <Python.h>

#ifndef PYTHON_HARNESS_PATH
#error "Define PYTHON_HARNESS_PATH"
#endif

#define PY_SSIZE_T_CLEAN
#include <Python.h>

void* pFunc = nullptr;
extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
    const std::string argv0 = (*argv)[0];

    std::string scriptRootPath;

    std::vector<uint8_t> program;
    FILE* fp = fopen(PYTHON_HARNESS_PATH, "rb");
    if ( fp == nullptr ) {
        printf("Fatal error: Cannot open script: %s\n", PYTHON_HARNESS_PATH);
        abort();
    }

    fseek (fp, 0, SEEK_END);
    long length = ftell(fp);
    if ( length < 1 ) {
        printf("Fatal error: Cannot retrieve script file size\n");
        abort();
    }
    fseek (fp, 0, SEEK_SET);
    program.resize(length);
    if ( fread(program.data(), 1, length, fp) != static_cast<size_t>(length) ) {
        printf("Fatal error: Cannot read script\n");
        abort();
    }
    fclose(fp);

    std::string code = std::string(program.data(), program.data() + program.size());

    {
        /* Resolve script root path */
        char resolved_path[PATH_MAX+1];
        if ( realpath(PYTHON_HARNESS_PATH, resolved_path) == nullptr ) {
            printf("Fatal error: Cannot resolve full script path\n");
            abort();
        }
        scriptRootPath = std::string(dirname(resolved_path));
    }

    {
        wchar_t *program = Py_DecodeLocale(argv0.c_str(), nullptr);
        Py_SetProgramName(program);
        PyMem_RawFree(program);
    }

    Py_Initialize();

    {
        std::string setArgv0;
        setArgv0 += "import sys";
        setArgv0 += "\n";
        setArgv0 += "sys.argv[0] = '" + std::string(PYTHON_HARNESS_PATH) + "'\n";
        if ( PyRun_SimpleString(setArgv0.c_str()) != 0 ) {
            printf("Fatal: Cannot set argv[0]\n");
            PyErr_PrintEx(1);
            abort();
        }
    }

    {
        std::string setPYTHONPATH;
        setPYTHONPATH += "import sys";
        setPYTHONPATH += "\n";
        setPYTHONPATH += "sys.path.append('" + scriptRootPath + "')\n";
        setPYTHONPATH += "\n";
        if ( PyRun_SimpleString(setPYTHONPATH.c_str()) != 0 ) {
            printf("Fatal: Cannot set PYTHONPATH\n");
            PyErr_PrintEx(1);
            abort();
        }
    }

    PyObject *pValue, *pModule, *pLocal;

    pModule = PyModule_New("fuzzermod");
    PyModule_AddStringConstant(pModule, "__file__", "");
    pLocal = PyModule_GetDict(pModule);
    pValue = PyRun_String(code.c_str(), Py_file_input, pLocal, pLocal);

    if ( pValue == nullptr ) {
        printf("Fatal: Cannot create Python function from string\n");
        PyErr_PrintEx(1);
        abort();
    }
    Py_DECREF(pValue);

    pFunc = PyObject_GetAttrString(pModule, "FuzzerRunOne");

    if (pFunc == nullptr || !PyCallable_Check(static_cast<PyObject*>(pFunc))) {
        printf("Fatal: FuzzerRunOne not defined or not callable\n");
        abort();
    }

    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(uint8_t* data, size_t size) {
    std::optional<std::vector<uint8_t>> ret = std::nullopt;

    PyObject *pArgs, *pValue;

    pArgs = PyTuple_New(1);
    pValue = PyBytes_FromStringAndSize((const char*)data, size);
    PyTuple_SetItem(pArgs, 0, pValue);

    pValue = PyObject_CallObject(static_cast<PyObject*>(pFunc), pArgs);

    if ( pValue == nullptr ) {
        /* Abort on unhandled exception */
        PyErr_PrintEx(1);
        abort();
    }

    if ( PyBytes_Check(pValue) ) {
        /* Retrieve output */

        uint8_t* output;
        Py_ssize_t outputSize;
        if ( PyBytes_AsStringAndSize(pValue, (char**)&output, &outputSize) != -1) {
            /* Return output */
            ret = std::vector<uint8_t>(output, output + outputSize);
            goto end;
        } else {
            /* TODO this should not happen */
        }

    }

end:
    Py_DECREF(pValue);
    Py_DECREF(pArgs);
    //return ret;
    return 0;
}
