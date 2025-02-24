# Copyright 2020 Huawei Technologies Co., Ltd
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ============================================================================

import numpy as np
import pytest
from mindspore.common import dtype as mstype
from mindspore import nn
from mindspore import Tensor
from mindspore.ops import composite as C
from mindspore import context
from mindspore.common.parameter import Parameter

context.set_context(mode=context.GRAPH_MODE, save_graphs=False, device_target="GPU")


class ForwardNet(nn.Cell):
    def __init__(self, max_cycles=10):
        super(ForwardNet, self).__init__()
        self.max_cycles = max_cycles
        self.zero = Tensor(np.array(0), mstype.int32)
        self.i = Tensor(np.array(0), mstype.int32)
        self.weight = Parameter(Tensor(np.array(0), mstype.int32))

    def construct(self, x, y):
        out = self.zero
        i = self.i
        while x < y:
            self.weight = x
            while i < self.max_cycles:
                out = x * y + out
                i = i + 1
                self.weight = i
            x = x + 1
        if out < 20:
            self.weight = out
            out = out - 20
        return out, self.weight


class BackwardNet(nn.Cell):
    def __init__(self, net):
        super(BackwardNet, self).__init__(auto_prefix=False)
        self.forward_net = net
        self.grad = C.GradOperation(get_all=True)

    def construct(self, *inputs):
        grads = self.grad(self.forward_net)(*inputs)
        return grads


def test_forward():
    x = Tensor(np.array(1), mstype.int32)
    y = Tensor(np.array(3), mstype.int32)
    # Graph Mode
    context.set_context(mode=context.GRAPH_MODE)
    graph_forward_net = ForwardNet(max_cycles=3)
    graph_mode_out = graph_forward_net(x, y)
    # Pynative Mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_forward_net = ForwardNet(max_cycles=3)
    pynative_mode_out = pynative_forward_net(x, y)
    assert graph_mode_out == pynative_mode_out


@pytest.mark.skip(reason="not supported side effect")
def test_backward():
    x = Tensor(np.array(1), mstype.int32)
    y = Tensor(np.array(3), mstype.int32)
    # Graph Mode
    context.set_context(mode=context.GRAPH_MODE)
    graph_forward_net = ForwardNet(max_cycles=3)
    graph_backward_net = BackwardNet(graph_forward_net)
    graph_mode_grads = graph_backward_net(x, y)
    # Pynative Mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_forward_net = ForwardNet(max_cycles=3)
    pynative_backward_net = BackwardNet(pynative_forward_net)
    pynative_mode_grads = pynative_backward_net(x, y)
    assert graph_mode_grads == pynative_mode_grads


class ForwardNetNoAssign(nn.Cell):
    def __init__(self, max_cycles=10):
        super(ForwardNetNoAssign, self).__init__()
        self.max_cycles = max_cycles
        self.zero = Tensor(np.array(0), mstype.int32)
        self.i = Tensor(np.array(0), mstype.int32)
        self.weight = Parameter(Tensor(np.array(0), mstype.int32))

    def construct(self, x, y):
        out = self.zero
        i = self.i
        while x < y:
            while i < self.max_cycles:
                out = x * y + out
                i = i + 1
            x = x + 1
        if out < 20:
            out = out - 20
        return out


class BackwardNetNoAssign(nn.Cell):
    def __init__(self, net):
        super(BackwardNetNoAssign, self).__init__(auto_prefix=False)
        self.forward_net = net
        self.grad = C.GradOperation(get_all=True)

    def construct(self, *inputs):
        grads = self.grad(self.forward_net)(*inputs)
        return grads


# This test case has a problem of evaluator endless loop.
@pytest.mark.level0
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_backward_no_assign():
    x = Tensor(np.array(1), mstype.int32)
    y = Tensor(np.array(3), mstype.int32)
    # Graph Mode
    context.set_context(mode=context.GRAPH_MODE)
    graph_forward_net = ForwardNetNoAssign(max_cycles=3)
    graph_backward_net = BackwardNetNoAssign(graph_forward_net)
    graph_mode_grads = graph_backward_net(x, y)
    # Pynative Mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_forward_net = ForwardNetNoAssign(max_cycles=3)
    pynative_backward_net = BackwardNetNoAssign(pynative_forward_net)
    pynative_mode_grads = pynative_backward_net(x, y)
    assert graph_mode_grads == pynative_mode_grads
