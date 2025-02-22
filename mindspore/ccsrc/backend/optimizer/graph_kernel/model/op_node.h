/**
 * Copyright 2021 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef MINDSPORE_CCSRC_BACKEND_OPTIMIZER_GRAPH_KERNEL_MODEL_OP_NODE_H_
#define MINDSPORE_CCSRC_BACKEND_OPTIMIZER_GRAPH_KERNEL_MODEL_OP_NODE_H_

#include <memory>
#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_map>
#include <functional>

#include "backend/optimizer/graph_kernel/model/node.h"
#include "backend/kernel_compiler/common_utils.h"
#include "ir/dtype/type.h"

namespace mindspore {
namespace opt {
namespace graphkernel {
#define CHECK_ATTR(attrs, attr_name)                                                              \
  do {                                                                                            \
    if (attrs.count(attr_name) == 0) {                                                            \
      MS_LOG(EXCEPTION) << "The attr [" << attr_name << "] does not exist in [" << #attrs << "]"; \
    }                                                                                             \
  } while (0)

class PrimOp : public Node {
 public:
  enum ComputeType {
    RESHAPE,
    ELEMWISE,
    BROADCAST,
    REDUCE,
    OPAQUE,
  };

  PrimOp(const std::string &op, const std::string &node_name, ComputeType compute)
      : Node({{}, TypeId::kNumberTypeBegin, kOpFormat_DEFAULT}, node_name), op_(op), compute_type_(compute) {}

  virtual void Check(const NodePtrList &inputs, const DAttrs &attrs);
  virtual void CheckShape(const NodePtrList &inputs, const DAttrs &attrs) {}
  virtual void CheckType(const NodePtrList &inputs, const DAttrs &attrs);
  virtual void CheckFormat(const NodePtrList &inputs, const DAttrs &attrs);

  virtual void Infer(const NodePtrList &inputs, const DAttrs &attrs);
  virtual NodePtr InferValue(const NodePtrList &inputs, const DAttrs &attrs, const std::string &op);
  virtual DShape InferShape(const NodePtrList &inputs, const DAttrs &attrs) { return inputs[0]->shape; }
  virtual TypeId InferType(const NodePtrList &inputs, const DAttrs &attrs) { return inputs[0]->type; }
  virtual DFormat InferFormat(const NodePtrList &inputs, const DAttrs &attrs) { return inputs[0]->format; }

  void Dump(std::ostringstream &os) const override;
  NType NodeType() override { return NType::Primitive; }

  const std::string &op() const { return op_; }
  ComputeType compute_type() const { return compute_type_; }

 protected:
  std::string op_;
  ComputeType compute_type_;
};
using PrimOpPtr = std::shared_ptr<PrimOp>;

class ElemwiseOp : public PrimOp {
 public:
  ElemwiseOp(const std::string &op, const std::string &node_name) : PrimOp(op, node_name, ELEMWISE) {}

  void Infer(const NodePtrList &inputs, const DAttrs &attrs) override;
  DShape InferShape(const NodePtrList &inputs, const DAttrs &attrs) override;
  DFormat InferFormat(const NodePtrList &inputs, const DAttrs &attrs) override;
};

class CastOp : public ElemwiseOp {
 public:
  CastOp(const std::string &op, const std::string &node_name) : ElemwiseOp("Cast", node_name) {}

  TypeId InferType(const NodePtrList &inputs, const DAttrs &attrs) override;
};

class InplaceAssignOp : public ElemwiseOp {
 public:
  InplaceAssignOp(const std::string &op, const std::string &node_name) : ElemwiseOp("InplaceAssign", node_name) {}

  DShape InferShape(const NodePtrList &inputs, const DAttrs &attrs) override { return inputs[2]->shape; }
  TypeId InferType(const NodePtrList &inputs, const DAttrs &attrs) override { return inputs[2]->type; }
  DFormat InferFormat(const NodePtrList &inputs, const DAttrs &attrs) override { return inputs[2]->format; }
};

class SelectOp : public ElemwiseOp {
 public:
  SelectOp(const std::string &op, const std::string &node_name) : ElemwiseOp("Select", node_name) {}

  void CheckType(const NodePtrList &inputs, const DAttrs &attrs) override;
  TypeId InferType(const NodePtrList &inputs, const DAttrs &attrs) override { return inputs[1]->type; }
};

class CompareOp : public ElemwiseOp {
 public:
  CompareOp(const std::string &op, const std::string &node_name) : ElemwiseOp(op, node_name) {}

  TypeId InferType(const NodePtrList &inputs, const DAttrs &attrs) override { return TypeId::kNumberTypeBool; }
};

class LessOp : public CompareOp {
 public:
  LessOp(const std::string &op, const std::string &node_name) : CompareOp("Less", node_name) {}
};

class EqualOp : public CompareOp {
 public:
  EqualOp(const std::string &op, const std::string &node_name) : CompareOp("Equal", node_name) {}
};

class LessEqualOp : public CompareOp {
 public:
  LessEqualOp(const std::string &op, const std::string &node_name) : CompareOp("LessEqual", node_name) {}
};

class GreaterOp : public CompareOp {
 public:
  GreaterOp(const std::string &op, const std::string &node_name) : CompareOp("Greater", node_name) {}
};

class GreaterEqualOp : public CompareOp {
 public:
  GreaterEqualOp(const std::string &op, const std::string &node_name) : CompareOp("GreaterEqual", node_name) {}
};

class ReshapeOp : public PrimOp {
 public:
  ReshapeOp(const std::string &op, const std::string &node_name) : PrimOp(op, node_name, RESHAPE) {}

  DShape InferShape(const NodePtrList &inputs, const DAttrs &attrs) override;
  DFormat InferFormat(const NodePtrList &inputs, const DAttrs &attrs) override {
    return attrs.find("format") == attrs.end() ? kOpFormat_DEFAULT
                                               : GetValue<std::string>(attrs.find("format")->second);
  }
};

class BroadcastToOp : public PrimOp {
 public:
  BroadcastToOp(const std::string &op, const std::string &node_name) : PrimOp(op, node_name, BROADCAST) {}

  DShape InferShape(const NodePtrList &inputs, const DAttrs &attrs) override;
};

class ReduceOp : public PrimOp {
 public:
  ReduceOp(const std::string &op, const std::string &node_name) : PrimOp(op, node_name, REDUCE) {}

  void Check(const NodePtrList &inputs, const DAttrs &attrs) override;

  DShape InferShape(const NodePtrList &inputs, const DAttrs &attrs) override;
  DFormat InferFormat(const NodePtrList &inputs, const DAttrs &attrs) override { return kOpFormat_DEFAULT; };
};

class OpaqueOp : public PrimOp {
 public:
  OpaqueOp(const std::string &op, const std::string &node_name) : PrimOp(op, node_name, OPAQUE) {}
};

class Conv2dOp : public OpaqueOp {
 public:
  Conv2dOp(const std::string &op, const std::string &node_name) : OpaqueOp("Conv2D", node_name) {}

  DShape InferShape(const NodePtrList &inputs, const DAttrs &attrs) override;
  TypeId InferType(const NodePtrList &inputs, const DAttrs &attrs) override;
};

class TransposeOp : public OpaqueOp {
 public:
  TransposeOp(const std::string &op, const std::string &node_name) : OpaqueOp("Transpose", node_name) {}

  DShape InferShape(const NodePtrList &inputs, const DAttrs &attrs) override;
  DFormat InferFormat(const NodePtrList &inputs, const DAttrs &attrs) override;
};

class MatMulOp : public OpaqueOp {
 public:
  MatMulOp(const std::string &op, const std::string &node_name) : OpaqueOp("MatMul", node_name) {}

  DShape InferShape(const NodePtrList &inputs, const DAttrs &attrs) override;
  TypeId InferType(const NodePtrList &inputs, const DAttrs &attrs) override;
};

class PadAkgOp : public OpaqueOp {
 public:
  PadAkgOp(const std::string &op, const std::string &node_name) : OpaqueOp("PadAkg", node_name) {}

  DShape InferShape(const NodePtrList &inputs, const DAttrs &attrs) override;
};

class UnPadAkgOp : public OpaqueOp {
 public:
  UnPadAkgOp(const std::string &op, const std::string &node_name) : OpaqueOp("UnPadAkg", node_name) {}

  DShape InferShape(const NodePtrList &inputs, const DAttrs &attrs) override;
};

class CImagOp : public ElemwiseOp {
 public:
  CImagOp(const std::string &op, const std::string &node_name) : ElemwiseOp("CImag", node_name) {}

  void CheckType(const NodePtrList &inputs, const DAttrs &attrs) override {
    if (inputs[0]->type != TypeId::kNumberTypeComplex64) {
      throw GKException("CImag's input[0] should be complex64");
    }
  };

  TypeId InferType(const NodePtrList &inputs, const DAttrs &attrs) override { return TypeId::kNumberTypeFloat32; }
};

class CRealOp : public ElemwiseOp {
 public:
  CRealOp(const std::string &op, const std::string &node_name) : ElemwiseOp("CReal", node_name) {}

  void CheckType(const NodePtrList &inputs, const DAttrs &attrs) override {
    if (inputs[0]->type != TypeId::kNumberTypeComplex64) {
      throw GKException("CReal's input[0] should be complex64");
    }
  };

  TypeId InferType(const NodePtrList &inputs, const DAttrs &attrs) override { return TypeId::kNumberTypeFloat32; }
};

class ComplexOp : public ElemwiseOp {
 public:
  ComplexOp(const std::string &op, const std::string &node_name) : ElemwiseOp("Complex", node_name) {}

  void CheckType(const NodePtrList &inputs, const DAttrs &attrs) override;

  TypeId InferType(const NodePtrList &inputs, const DAttrs &attrs) override { return TypeId::kNumberTypeComplex64; }
};
}  // namespace graphkernel
}  // namespace opt
}  // namespace mindspore
#endif
