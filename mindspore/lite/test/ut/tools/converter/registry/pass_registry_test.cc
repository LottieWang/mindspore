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

#include <map>
#include <string>
#include <vector>
#include "common/common_test.h"
#include "backend/optimizer/common/pass.h"
#include "include/registry/model_parser_registry.h"
#include "include/registry/pass_registry.h"
#include "ops/fusion/add_fusion.h"
#include "ops/addn.h"
#include "ops/custom.h"
#include "tools/converter/model_parser.h"
#include "tools/converter/registry/pass_content.h"
#include "tools/optimizer/common/gllo_utils.h"
#include "ut/tools/converter/registry/model_parser_test.h"

using mindspore::converter::ConverterParameters;
using mindspore::converter::FmkType_CAFFE;
using mindspore::lite::registry::POSITION_BEGIN;
namespace mindspore {
class PassRegistryTest : public mindspore::CommonTest {
 public:
  PassRegistryTest() = default;
  void SetUp() override {
    REG_MODEL_PARSER(FmkType_CAFFE, TestModelParserCreator);
    auto model_parser = lite::registry::ModelParserRegistry::GetModelParser(FmkType_CAFFE);
    if (model_parser == nullptr) {
      return;
    }
    ConverterParameters converter_parameters;
    func_graph_ = model_parser->Parse(converter_parameters);
  }
  FuncGraphPtr func_graph_ = nullptr;
};

namespace opt {
// fuse add and add to addn.
class Test1Fusion : public Pass {
 public:
  Test1Fusion() : Pass("test1_fusion") {}
  bool CanFusion(const CNodePtr &cnode) {
    if (cnode == nullptr) {
      return false;
    }
    if (!opt::CheckPrimitiveType(cnode, prim::kPrimAddFusion)) {
      return false;
    }
    auto primc = GetValueNode<std::shared_ptr<ops::AddFusion>>(cnode->input(0));
    if (primc == nullptr) {
      return false;
    }
    if (primc->GetAttr(ops::kActivationType) != nullptr && primc->get_activation_type() != mindspore::NO_ACTIVATION) {
      return false;
    }
    size_t input_cnode_num = 0;
    for (size_t i = 1; i < cnode->size(); ++i) {
      auto input = cnode->input(i);
      if (!utils::isa<CNodePtr>(input)) {
        continue;
      }
      if (!opt::CheckPrimitiveType(input, prim::kPrimAddFusion)) {
        return false;
      }
      auto input_cnode = input->cast<CNodePtr>();
      auto add_primc = GetValueNode<std::shared_ptr<ops::AddFusion>>(input_cnode->input(0));
      if (add_primc == nullptr) {
        return false;
      }
      if (add_primc->GetAttr(ops::kActivationType) != nullptr &&
          add_primc->get_activation_type() != mindspore::NO_ACTIVATION) {
        return false;
      }
      ++input_cnode_num;
      continue;
    }
    return input_cnode_num > 0;
  }

  bool Run(const FuncGraphPtr &func_graph) override {
    if (func_graph == nullptr) {
      return false;
    }
    auto manager = func_graph->manager();
    if (manager == nullptr) {
      return false;
    }
    auto node_list = TopoSort(func_graph->get_return());
    for (auto &node : node_list) {
      if (!utils::isa<CNode>(node)) {
        continue;
      }
      auto cnode = node->cast<CNodePtr>();
      if (!CanFusion(cnode)) {
        continue;
      }
      std::vector<AnfNodePtr> inputs;
      for (size_t i = 1; i < cnode->size(); ++i) {
        auto input_node = cnode->input(i);
        if (!utils::isa<CNode>(input_node)) {
          inputs.push_back(input_node);
          continue;
        }
        auto input_cnode = input_node->cast<CNodePtr>();
        for (size_t j = 1; j < input_cnode->size(); ++j) {
          inputs.push_back(input_cnode->input(j));
        }
      }
      auto primc = std::make_shared<ops::AddN>();
      auto new_cnode = func_graph->NewCNode(primc, inputs);
      new_cnode->set_fullname_with_scope(cnode->fullname_with_scope());
      new_cnode->set_abstract(cnode->abstract()->Clone());
      manager->Replace(node, new_cnode);
    }
    return true;
  }
};

// convert addn to custom op
class Test2Fusion : public Pass {
 public:
  Test2Fusion() : Pass("test2_fusion") {}
  AnfNodePtr CreateCustomOp(const FuncGraphPtr func_graph, const CNodePtr &cnode) {
    if (cnode == nullptr) {
      return nullptr;
    }
    auto primc = std::make_shared<ops::Custom>();
    if (primc == nullptr) {
      return nullptr;
    }
    primc->set_type("Custom_AddN");
    std::map<std::string, std::vector<uint8_t>> custom_attrs;
    std::string input_num = std::to_string(3);
    std::vector<uint8_t> input_num_attr(input_num.begin(), input_num.end());
    custom_attrs["input_num"] = input_num_attr;
    std::string op_kind = "custom op";
    std::vector<uint8_t> op_kind_attr(op_kind.begin(), op_kind.end());
    custom_attrs["op_kind"] = op_kind_attr;
    primc->set_attr(custom_attrs);
    auto inputs = cnode->inputs();
    inputs.erase(inputs.begin());
    auto custom_cnode = func_graph->NewCNode(primc, inputs);
    custom_cnode->set_fullname_with_scope(cnode->fullname_with_scope());
    custom_cnode->set_abstract(cnode->abstract()->Clone());
    return custom_cnode;
  }

  bool Run(const FuncGraphPtr &func_graph) override {
    if (func_graph == nullptr) {
      return false;
    }
    auto manager = func_graph->manager();
    if (manager == nullptr) {
      return false;
    }
    auto node_list = TopoSort(func_graph->get_return());
    for (auto &node : node_list) {
      if (!utils::isa<CNode>(node)) {
        continue;
      }
      if (!opt::CheckPrimitiveType(node, prim::kPrimAddN)) {
        continue;
      }
      auto cnode = node->cast<CNodePtr>();
      auto custome_cnode = CreateCustomOp(func_graph, cnode);
      if (custome_cnode == nullptr) {
        return false;
      }
      manager->Replace(node, custome_cnode);
    }
    return true;
  }
};

class TestFusion : public Pass {
 public:
  TestFusion() : Pass("test_fusion") {}
  bool Run(const FuncGraphPtr &func_graph) override {
    if (func_graph == nullptr) {
      return false;
    }
    auto manager = Manage(func_graph, true);
    if (manager == nullptr) {
      return false;
    }
    auto test1_fusion = std::make_shared<Test1Fusion>();
    if (!test1_fusion->Run(func_graph)) {
      return false;
    }
    auto test2_fusion = std::make_shared<Test2Fusion>();
    if (!test2_fusion->Run(func_graph)) {
      return false;
    }
    return true;
  }
};

REG_PASS(TestFusion, TestFusion)
REG_SCHEDULED_PASS(POSITION_BEGIN, {"TestFusion"})
}  // namespace opt

TEST_F(PassRegistryTest, TestRegistry) {
  auto &passes = lite::PassStoreRoomInfo();
  auto &assigned_passes = lite::ExternalAssignedPassesInfo();
  ASSERT_EQ(assigned_passes.size(), 1);
  auto pass_names = assigned_passes[POSITION_BEGIN];
  ASSERT_EQ(pass_names.size(), 1);
  auto begin_pass = passes[pass_names.front()];
  ASSERT_NE(begin_pass, nullptr);
  auto begin_pass_test = std::dynamic_pointer_cast<opt::TestFusion>(begin_pass);
  ASSERT_NE(begin_pass_test, nullptr);
  ASSERT_NE(func_graph_, nullptr);
  auto res = begin_pass_test->Run(func_graph_);
  ASSERT_EQ(res, true);
  auto cnode_list = func_graph_->GetOrderedCnodes();
  ASSERT_EQ(cnode_list.size(), 2);
  bool is_custom = opt::CheckPrimitiveType(cnode_list.front(), prim::kPrimCustom);
  ASSERT_EQ(is_custom, true);
  auto custome_prim = GetValueNode<std::shared_ptr<ops::Custom>>(cnode_list.front()->input(0));
  ASSERT_NE(custome_prim, nullptr);
  auto type = custome_prim->get_type();
  ASSERT_EQ(type, std::string("Custom_AddN"));
  bool is_return = opt::CheckPrimitiveType(cnode_list.back(), prim::kPrimReturn);
  ASSERT_EQ(is_return, true);
}
}  // namespace mindspore
