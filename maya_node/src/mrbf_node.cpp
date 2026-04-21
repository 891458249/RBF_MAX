// =============================================================================
// maya_node/src/mrbf_node.cpp — Phase 2A Slice 11
// -----------------------------------------------------------------------------
// Real-predict implementation.  See mrbf_node.hpp for the contract and
// adapter_core.hpp for the attribute-marshalling helpers.
//
// Error-handling philosophy (Slice 11 D1/D2)
// ------------------------------------------
// compute() always returns MS::kSuccess (except for kUnknownParameter
// on plugs we do not own) so DG evaluation is never broken by a bad
// JSON path.  Failure modes surface through aStatusMessage +
// aIsLoaded=false + aOutputValues=zeros.  A single MGlobal warning is
// emitted per failing path to avoid log spam in playback.
// =============================================================================
#include "rbfmax/maya/mrbf_node.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <maya/MDataBlock.h>
#include <maya/MDataHandle.h>
#include <maya/MDoubleArray.h>
#include <maya/MFnDoubleArrayData.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnStringData.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MObject.h>
#include <maya/MPlug.h>

#include <maya/MPoint.h>        // Slice 13 — centers_for_viewport()

#include "rbfmax/kernel_functions.hpp"
#include "rbfmax/interpolator.hpp"
#include "rbfmax/maya/adapter_core.hpp"
#include "rbfmax/maya/plugin_info.hpp"
#include "rbfmax/types.hpp"     // Slice 13 — MatrixX / Eigen::Index

namespace rbfmax {
namespace maya {

// -----------------------------------------------------------------------------
//  Static members
// -----------------------------------------------------------------------------

const MString mRBFNode::kTypeName{"mRBFNode"};
const MTypeId mRBFNode::kTypeId{kNodeTypeIdValue};

MObject mRBFNode::aJsonPath;
MObject mRBFNode::aReloadTrigger;
MObject mRBFNode::aQueryPoint;
MObject mRBFNode::aInputValue;
MObject mRBFNode::aOutputValue;
MObject mRBFNode::aOutputValues;
MObject mRBFNode::aIsLoaded;
MObject mRBFNode::aNCenters;
MObject mRBFNode::aDimInput;
MObject mRBFNode::aDimOutput;
MObject mRBFNode::aKernelType;
MObject mRBFNode::aStatusMessage;

// -----------------------------------------------------------------------------
//  Ctor / Dtor (explicit to keep unique_ptr to incomplete type working)
// -----------------------------------------------------------------------------

mRBFNode::mRBFNode()  = default;
mRBFNode::~mRBFNode() = default;

void* mRBFNode::creator() {
    return new mRBFNode();
}

// -----------------------------------------------------------------------------
//  initialize — declare every attribute and wire attributeAffects
// -----------------------------------------------------------------------------

MStatus mRBFNode::initialize() {
    MStatus st;
    MFnNumericAttribute nAttr;
    MFnTypedAttribute   tAttr;
    MFnStringData       strData;
    MFnDoubleArrayData  daData;

    // ---- Input: jsonPath (string) ----
    const MObject emptyString = strData.create("", &st);
    if (!st) return st;
    aJsonPath = tAttr.create("jsonPath", "jp", MFnData::kString, emptyString, &st);
    if (!st) return st;
    tAttr.setStorable(true);
    tAttr.setKeyable(false);
    tAttr.setReadable(true);
    tAttr.setWritable(true);

    // ---- Input: reloadTrigger (int, default 0) ----
    aReloadTrigger = nAttr.create("reloadTrigger", "rt",
                                  MFnNumericData::kInt, 0, &st);
    if (!st) return st;
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    nAttr.setReadable(true);
    nAttr.setWritable(true);

    // ---- Input: queryPoint (double[]) ----
    const MObject emptyDblArray = daData.create(MDoubleArray(), &st);
    if (!st) return st;
    aQueryPoint = tAttr.create("queryPoint", "qp",
                               MFnData::kDoubleArray, emptyDblArray, &st);
    if (!st) return st;
    tAttr.setStorable(true);
    tAttr.setKeyable(false);
    tAttr.setReadable(true);
    tAttr.setWritable(true);

    // ---- Input: aInputValue (double) — legacy 10A ----
    aInputValue = nAttr.create("inputValue", "iv",
                               MFnNumericData::kDouble, 0.0, &st);
    if (!st) return st;
    nAttr.setKeyable(true);
    nAttr.setStorable(true);
    nAttr.setReadable(true);
    nAttr.setWritable(true);

    // ---- Output: aOutputValue (double) — legacy 10A ----
    aOutputValue = nAttr.create("outputValue", "ov",
                                MFnNumericData::kDouble, 0.0, &st);
    if (!st) return st;
    nAttr.setKeyable(false);
    nAttr.setStorable(false);
    nAttr.setReadable(true);
    nAttr.setWritable(false);

    // ---- Output: outputValues (double[]) ----
    aOutputValues = tAttr.create("outputValues", "ovs",
                                 MFnData::kDoubleArray, emptyDblArray, &st);
    if (!st) return st;
    tAttr.setStorable(false);
    tAttr.setKeyable(false);
    tAttr.setReadable(true);
    tAttr.setWritable(false);

    // ---- Output state attrs ----
    aIsLoaded = nAttr.create("isLoaded", "isl",
                             MFnNumericData::kBoolean, false, &st);
    if (!st) return st;
    nAttr.setStorable(false);
    nAttr.setKeyable(false);
    nAttr.setReadable(true);
    nAttr.setWritable(false);

    aNCenters = nAttr.create("nCenters", "nc",
                             MFnNumericData::kInt, 0, &st);
    if (!st) return st;
    nAttr.setStorable(false);
    nAttr.setKeyable(false);
    nAttr.setReadable(true);
    nAttr.setWritable(false);

    aDimInput = nAttr.create("dimInput", "di",
                             MFnNumericData::kInt, 0, &st);
    if (!st) return st;
    nAttr.setStorable(false);
    nAttr.setKeyable(false);
    nAttr.setReadable(true);
    nAttr.setWritable(false);

    aDimOutput = nAttr.create("dimOutput", "do",
                              MFnNumericData::kInt, 0, &st);
    if (!st) return st;
    nAttr.setStorable(false);
    nAttr.setKeyable(false);
    nAttr.setReadable(true);
    nAttr.setWritable(false);

    aKernelType = tAttr.create("kernelType", "kt",
                               MFnData::kString, emptyString, &st);
    if (!st) return st;
    tAttr.setStorable(false);
    tAttr.setKeyable(false);
    tAttr.setReadable(true);
    tAttr.setWritable(false);

    aStatusMessage = tAttr.create("statusMessage", "sm",
                                  MFnData::kString, emptyString, &st);
    if (!st) return st;
    tAttr.setStorable(false);
    tAttr.setKeyable(false);
    tAttr.setReadable(true);
    tAttr.setWritable(false);

    // ---- Register ----
    st = addAttribute(aJsonPath);       if (!st) return st;
    st = addAttribute(aReloadTrigger);  if (!st) return st;
    st = addAttribute(aQueryPoint);     if (!st) return st;
    st = addAttribute(aInputValue);     if (!st) return st;
    st = addAttribute(aOutputValue);    if (!st) return st;
    st = addAttribute(aOutputValues);   if (!st) return st;
    st = addAttribute(aIsLoaded);       if (!st) return st;
    st = addAttribute(aNCenters);       if (!st) return st;
    st = addAttribute(aDimInput);       if (!st) return st;
    st = addAttribute(aDimOutput);      if (!st) return st;
    st = addAttribute(aKernelType);     if (!st) return st;
    st = addAttribute(aStatusMessage);  if (!st) return st;

    // ---- attributeAffects ----
    // Outputs that depend on load-state follow jsonPath + reloadTrigger;
    // outputValues additionally depends on queryPoint.  The legacy 10A
    // outputValue depends only on inputValue.
    const MObject load_inputs[]  = { aJsonPath, aReloadTrigger };
    const MObject state_outputs[] = { aIsLoaded, aNCenters, aDimInput,
                                      aDimOutput, aKernelType,
                                      aStatusMessage, aOutputValues };
    for (const MObject& in : load_inputs) {
        for (const MObject& out : state_outputs) {
            st = attributeAffects(in, out); if (!st) return st;
        }
    }
    st = attributeAffects(aQueryPoint, aOutputValues); if (!st) return st;
    st = attributeAffects(aInputValue, aOutputValue);  if (!st) return st;

    return MS::kSuccess;
}

// -----------------------------------------------------------------------------
//  try_load — parse JSON into a fresh RBFInterpolator.
// -----------------------------------------------------------------------------
//
// Writes diagnostic state into the status attributes on every code
// path so a downstream UI can display something useful.  Emits exactly
// one MGlobal warning per failing path (warned_about_current_path_
// resets when path or reloadTrigger changes).

namespace {

// Write a string into a string-typed MDataHandle (MFnStringData).
MStatus set_string_handle(MDataHandle& h, const MString& s) {
    MFnStringData strData;
    MStatus st;
    MObject strObj = strData.create(s, &st);
    if (!st) return st;
    h.setMObject(strObj);
    return MS::kSuccess;
}

}  // namespace

void mRBFNode::try_load(const std::string& path,
                        int                trigger,
                        MDataBlock&        data) {
    MStatus st;

    // Default-clear state attrs first so all failure paths end with a
    // fully-populated output block.
    MDataHandle hIsLoaded  = data.outputValue(aIsLoaded, &st);
    MDataHandle hNCenters  = data.outputValue(aNCenters, &st);
    MDataHandle hDimIn     = data.outputValue(aDimInput, &st);
    MDataHandle hDimOut    = data.outputValue(aDimOutput, &st);
    MDataHandle hKernel    = data.outputValue(aKernelType, &st);
    MDataHandle hStatus    = data.outputValue(aStatusMessage, &st);

    hIsLoaded.setBool(false);
    hNCenters.setInt(0);
    hDimIn.setInt(0);
    hDimOut.setInt(0);
    set_string_handle(hKernel, MString(""));

    auto warn_once = [&](const MString& msg) {
        if (!warned_about_current_path_) {
            MGlobal::displayWarning(MString("mRBFNode: ") + msg);
            warned_about_current_path_ = true;
        }
    };

    if (!validate_json_path(path)) {
        interp_.reset();
        loaded_path_ = path;
        last_reload_trigger_ = trigger;
        const MString why = MString(
            "jsonPath invalid or not readable: '") + MString(path.c_str())
            + MString("'");
        set_string_handle(hStatus, why);
        warn_once(why);
        return;
    }

    // Attempt the load.  RBFInterpolator::load returns false on any
    // parse / schema / IO error; it internally wraps try/catch so we
    // do not need to here.
    auto fresh = std::unique_ptr<rbfmax::RBFInterpolator>(
        new rbfmax::RBFInterpolator());
    const bool ok = fresh->load(path);
    if (!ok || !fresh->is_fitted()) {
        interp_.reset();
        loaded_path_ = path;
        last_reload_trigger_ = trigger;
        const MString why = MString(
            "RBFInterpolator::load failed (parse, schema, or IO error): '")
            + MString(path.c_str()) + MString("'");
        set_string_handle(hStatus, why);
        warn_once(why);
        return;
    }

    // Success.
    interp_ = std::move(fresh);
    loaded_path_ = path;
    last_reload_trigger_ = trigger;
    warned_about_current_path_ = false;

    hIsLoaded.setBool(true);
    hNCenters.setInt(static_cast<int>(interp_->n_centers()));
    hDimIn.setInt(static_cast<int>(interp_->dim()));
    hDimOut.setInt(static_cast<int>(interp_->kernel_params().type  // dummy deref
                                    == rbfmax::KernelType::kGaussian
                                    ? 0 : 0));
    // dimOutput = M (weights.cols()) — we need a public way to read M.
    // Phase 1 does not expose weights directly, but for Slice 11's
    // "Helper" purpose we use predict on a zero vector and take its
    // size — predict() returns a zero-cost M-sized VectorX whose .size()
    // equals M (guarded by is_fitted above).  This costs O(N) at load
    // time, acceptable as load is off the hot path.
    {
        rbfmax::VectorX probe = rbfmax::VectorX::Zero(interp_->dim());
        rbfmax::VectorX y_probe = interp_->predict(probe);
        hDimOut.setInt(static_cast<int>(y_probe.size()));
    }
    set_string_handle(hKernel,
        MString(rbfmax::kernel_type_to_string(interp_->kernel_params().type)));
    set_string_handle(hStatus, MString("OK"));
}

// -----------------------------------------------------------------------------
//  compute
// -----------------------------------------------------------------------------

MStatus mRBFNode::compute(const MPlug& plug, MDataBlock& data) {
    MStatus st;

    // Legacy 10A path: aOutputValue depends only on aInputValue.
    if (plug == aOutputValue) {
        MDataHandle hIn  = data.inputValue(aInputValue, &st);
        if (!st) return st;
        const double x = hIn.asDouble();
        const double y = static_cast<double>(
            hello_transform(static_cast<rbfmax::Scalar>(x)));
        MDataHandle hOut = data.outputValue(aOutputValue, &st);
        if (!st) return st;
        hOut.setDouble(y);
        data.setClean(plug);
        return MS::kSuccess;
    }

    // Slice 11 path: any of the load-tracking or predict outputs.
    const bool is_load_or_predict_output =
        (plug == aOutputValues)   || (plug == aIsLoaded)  ||
        (plug == aNCenters)       || (plug == aDimInput)  ||
        (plug == aDimOutput)      || (plug == aKernelType) ||
        (plug == aStatusMessage);
    if (!is_load_or_predict_output) {
        return MS::kUnknownParameter;
    }

    try {
        // Read inputs.
        MDataHandle hJsonPath = data.inputValue(aJsonPath, &st);
        if (!st) return st;
        const std::string path = std::string(hJsonPath.asString().asChar());

        MDataHandle hTrigger = data.inputValue(aReloadTrigger, &st);
        if (!st) return st;
        const int trigger = hTrigger.asInt();

        // Reload if path or trigger changed.
        if (path != loaded_path_ || trigger != last_reload_trigger_) {
            // path changed — reset the "already warned" flag so the
            // new path gets its own (single) warning on failure.
            if (path != loaded_path_) {
                warned_about_current_path_ = false;
            }
            try_load(path, trigger, data);
        }

        // Produce outputValues.  Layout:
        //   * interp_ == nullptr (load failed / empty path): write
        //     empty array; downstream can distinguish via aIsLoaded.
        //   * interp_ != nullptr: read queryPoint, call predict,
        //     write result.
        MDataHandle hQuery = data.inputValue(aQueryPoint, &st);
        if (!st) return st;
        MFnDoubleArrayData queryData(hQuery.data(), &st);
        if (!st) return st;
        const MDoubleArray queryArr = queryData.array();

        std::vector<double> queryStd(static_cast<std::size_t>(queryArr.length()));
        for (unsigned int i = 0; i < queryArr.length(); ++i) {
            queryStd[i] = queryArr[i];
        }

        MDoubleArray outArr;
        if (interp_ != nullptr && interp_->is_fitted()
            && static_cast<int>(queryArr.length()) == interp_->dim()) {
            const rbfmax::VectorX q = double_vector_to_eigen(queryStd);
            const rbfmax::VectorX y = interp_->predict(q);
            for (Eigen::Index i = 0; i < y.size(); ++i) {
                outArr.append(static_cast<double>(y(i)));
            }
        }
        // else: outArr stays empty (size 0) — downstream code should
        // gate on aIsLoaded and aDimInput.

        // Write outputValues as MFnDoubleArrayData.
        MFnDoubleArrayData outData;
        MObject outObj = outData.create(outArr, &st);
        if (!st) return st;
        MDataHandle hOut = data.outputValue(aOutputValues, &st);
        if (!st) return st;
        hOut.setMObject(outObj);

        // Mark all outputs clean in one sweep.
        data.setClean(aOutputValues);
        data.setClean(aIsLoaded);
        data.setClean(aNCenters);
        data.setClean(aDimInput);
        data.setClean(aDimOutput);
        data.setClean(aKernelType);
        data.setClean(aStatusMessage);
        return MS::kSuccess;

    } catch (const std::exception& ex) {
        // Any unexpected C++ exception escaping from the kernel gets
        // translated into a warning + empty outputs.  compute returns
        // success so DG evaluation continues.
        MDataHandle hStatus = data.outputValue(aStatusMessage, &st);
        set_string_handle(hStatus,
            MString("mRBFNode compute exception: ") + MString(ex.what()));
        if (!warned_about_current_path_) {
            MGlobal::displayWarning(
                MString("mRBFNode: compute threw: ") + MString(ex.what()));
            warned_about_current_path_ = true;
        }
        return MS::kSuccess;
    }
}

// -----------------------------------------------------------------------------
//  Slice 13 — Viewport 2.0 accessors (Path B: read by mRBFDrawOverride
//                                     via mRBFShape.sourceNode connection)
// -----------------------------------------------------------------------------
//
// These are additive, read-only, const-qualified; they never touch the
// DG and never modify Phase 2A state.  Safe to call from the draw
// thread because every read is either a pointer null-check or a fresh
// Eigen matrix copy into a new std::vector<MPoint>.

bool mRBFNode::is_loaded() const noexcept {
    return interp_ != nullptr;
}

std::vector<MPoint> mRBFNode::centers_for_viewport() const {
    std::vector<MPoint> out;
    if (!interp_) {
        return out;
    }
    const ::rbfmax::MatrixX& C = interp_->centers();
    const Eigen::Index N = C.rows();
    const Eigen::Index D = C.cols();
    out.reserve(static_cast<std::size_t>(N));
    for (Eigen::Index i = 0; i < N; ++i) {
        const double x = (D > 0) ? static_cast<double>(C(i, 0)) : 0.0;
        const double y = (D > 1) ? static_cast<double>(C(i, 1)) : 0.0;
        const double z = (D > 2) ? static_cast<double>(C(i, 2)) : 0.0;
        out.emplace_back(x, y, z);
    }
    return out;
}

}  // namespace maya
}  // namespace rbfmax
