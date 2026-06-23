/*!****************************************************************************************
 * \file core_tcl.cpp
 * \brief Tcl extension: in-process CORE read/write for Xschem (no external CLI tools).
 *****************************************************************************************/

#include "core_paths.h"
#include "xschem_bridge.h"

#include <tcl.h>

#include <optional>

#include <string>
#include <vector>

namespace {
namespace bridge = core::xschem_bridge;

std::string objToString(Tcl_Obj *obj) { return Tcl_GetString(obj); }

int setBridgeError(Tcl_Interp *interp, const bridge::Status &status)
{
    if (!status.errors.empty()) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(status.errors.front().c_str(), -1));
    } else {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("CORE operation failed", -1));
    }
    return TCL_ERROR;
}

void appendWarnings(Tcl_Interp *interp, const bridge::Status &status)
{
    if (status.warnings.empty()) {
        return;
    }
    Tcl_Obj *list = Tcl_NewListObj(0, nullptr);
    for (const std::string &msg : status.warnings) {
        Tcl_ListObjAppendElement(interp, list, Tcl_NewStringObj(msg.c_str(), -1));
    }
    Tcl_SetVar2Ex(interp, "::core_last_warnings", nullptr, list, TCL_GLOBAL_ONLY);
}

int CoreListCellsCmd(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "corePath");
        return TCL_ERROR;
    }

    bridge::Status status;
    const std::vector<std::string> cells = bridge::listCells(objToString(objv[1]), status);
    if (!status.ok) {
        return setBridgeError(interp, status);
    }

    Tcl_Obj *list = Tcl_NewListObj(0, nullptr);
    for (const std::string &cell : cells) {
        Tcl_ListObjAppendElement(interp, list, Tcl_NewStringObj(cell.c_str(), -1));
    }
    Tcl_SetObjResult(interp, list);
    appendWarnings(interp, status);
    return TCL_OK;
}

int CoreExportCellCmd(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "corePath cellName outputPath");
        return TCL_ERROR;
    }

    const bridge::Status status =
        bridge::exportCell(objToString(objv[1]), objToString(objv[2]), objToString(objv[3]));
    appendWarnings(interp, status);
    if (!status.ok) {
        return setBridgeError(interp, status);
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(objToString(objv[3]).c_str(), -1));
    return TCL_OK;
}

int CoreExportAllCmd(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "corePath outputDir");
        return TCL_ERROR;
    }

    std::size_t count = 0;
    const bridge::Status status = bridge::exportAll(objToString(objv[1]), objToString(objv[2]), count);
    appendWarnings(interp, status);
    if (!status.ok) {
        return setBridgeError(interp, status);
    }
    Tcl_SetObjResult(interp, Tcl_NewWideIntObj(static_cast<Tcl_WideInt>(count)));
    return TCL_OK;
}

int CoreFileNameCmd(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "cellName view");
        return TCL_ERROR;
    }

    const std::string cellName = objToString(objv[1]);
    const std::string viewText = objToString(objv[2]);
    const std::optional<core::ViewType> view = core::parseViewTypeName(viewText);
    if (!view.has_value()) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(("unknown view: " + viewText).c_str(), -1));
        return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, Tcl_NewStringObj(core::coreFileName(cellName, *view).c_str(), -1));
    return TCL_OK;
}

int CoreParsePathCmd(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "corePath");
        return TCL_ERROR;
    }

    const core::ParsedCorePath parsed = core::parseCoreFilePath(objToString(objv[1]));
    if (!parsed.valid) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("invalid CORE path", -1));
        return TCL_ERROR;
    }

    Tcl_Obj *list = Tcl_NewListObj(0, nullptr);
    Tcl_ListObjAppendElement(interp, list, Tcl_NewStringObj(parsed.cellName.c_str(), -1));
    Tcl_ListObjAppendElement(interp, list,
                             Tcl_NewStringObj(core::viewTypeToString(parsed.view).c_str(), -1));
    Tcl_SetObjResult(interp, list);
    return TCL_OK;
}

int CoreImportCmd(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    if (objc < 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "inputPath corePath ?-lib name? ?-cell name?");
        return TCL_ERROR;
    }

    core::XschemImporter::Options options;
    options.libName = "xschem";
    for (int i = 3; i < objc; i += 2) {
        if (i + 1 >= objc) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("missing value after flag", -1));
            return TCL_ERROR;
        }
        const std::string flag = objToString(objv[i]);
        const std::string value = objToString(objv[i + 1]);
        if (flag == "-lib") {
            options.libName = value;
        } else if (flag == "-cell") {
            options.cellName = value;
        } else {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(("unknown flag: " + flag).c_str(), -1));
            return TCL_ERROR;
        }
    }

    const bridge::Status status = bridge::importIntoCore(objToString(objv[1]), objToString(objv[2]), options);
    appendWarnings(interp, status);
    if (!status.ok) {
        return setBridgeError(interp, status);
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(objToString(objv[2]).c_str(), -1));
    return TCL_OK;
}

} // namespace

extern "C" int Core_Init(Tcl_Interp *interp)
{
    if (Tcl_InitStubs(interp, TCL_VERSION, 0) == nullptr) {
        return TCL_ERROR;
    }

    Tcl_CreateObjCommand(interp, "coreapi_list_cells", CoreListCellsCmd, nullptr, nullptr);
    Tcl_CreateObjCommand(interp, "coreapi_export_cell", CoreExportCellCmd, nullptr, nullptr);
    Tcl_CreateObjCommand(interp, "coreapi_export_all", CoreExportAllCmd, nullptr, nullptr);
    Tcl_CreateObjCommand(interp, "coreapi_import", CoreImportCmd, nullptr, nullptr);
    Tcl_CreateObjCommand(interp, "coreapi_core_file_name", CoreFileNameCmd, nullptr, nullptr);
    Tcl_CreateObjCommand(interp, "coreapi_parse_core_path", CoreParsePathCmd, nullptr, nullptr);

    if (Tcl_PkgProvide(interp, "Core", "1.0") == TCL_ERROR) {
        return TCL_ERROR;
    }
    return TCL_OK;
}
