# Copyright 2021 Huawei Technologies Co., Ltd
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
import pytest
from mindspore import context
from mindspore import Tensor, nn
from mindspore.ops import composite as C
from mindspore.common import dtype as mstype
from mindspore.common.parameter import Parameter

grad_all = C.GradOperation(get_all=True)
context.set_context(device_target="Ascend")


class IfInIfNet(nn.Cell):
    def __init__(self):
        super().__init__()
        self.param_a = Parameter(Tensor(5, mstype.int32), name='a')
        self.param_b = Parameter(Tensor(4, mstype.int32), name='b')

    def construct(self, x):
        if self.param_a > self.param_b:
            x += 10
            if x > self.param_a:
                self.param_b += 1
                x += self.param_a
        return x


class IfInIfNet1(nn.Cell):
    def __init__(self):
        super().__init__()
        self.param_a = Parameter(Tensor(5, mstype.int32), name='a')
        self.param_b = Parameter(Tensor(4, mstype.int32), name='b')

    def construct(self, x):
        if self.param_a > self.param_b:
            out = self.func(x)
        else:
            out = self.func(self.param_a)
        out += self.param_b
        return out

    def func(self, x):
        x += 10
        if x > self.param_a:
            self.param_b += 1
            x += self.param_a
        return x


class IfInIfNet2(nn.Cell):
    def __init__(self):
        super().__init__()
        self.param_a = Parameter(Tensor(5, mstype.int32), name='a')
        self.param_b = Parameter(Tensor(4, mstype.int32), name='b')

    def construct(self, x):
        if self.check(self.param_a, self.param_b):
            out = self.func(x)
        else:
            out = x
        out += self.param_b
        return out

    def func(self, x):
        x += 10
        if x > self.param_a:
            self.param_b += 1
            x += self.param_a
        return x

    def check(self, x, y):
        if x < y:
            self.param_b += 1
            return True
        self.param_b -= 1
        return False


class IfInIfNet3(nn.Cell):
    def __init__(self):
        super().__init__()
        self.param_a = Parameter(Tensor(5, mstype.int32), name='a')
        self.param_b = Parameter(Tensor(4, mstype.int32), name='b')

    def construct(self, x):
        if self.func(x) > self.param_a:
            out = x
        else:
            out = self.param_a
        out += self.param_b
        return out

    def func(self, x):
        x += 10
        if x > self.param_a:
            self.param_b += 1
            x += self.param_a
        return x


class GradNet(nn.Cell):
    def __init__(self, net):
        super(GradNet, self).__init__()
        self.net = net

    def construct(self, *inputs):
        return grad_all(self.net)(*inputs)


def control_flow_if_in_if(input_net, x):
    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    net = input_net()
    grad_net = GradNet(net)
    graph_forward_res = net(x)
    graph_backward_res = grad_net(x)

    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    net = input_net()
    grad_net = GradNet(net)
    pynative_forward_res = net(x)
    pynative_backward_res = grad_net(x)

    assert graph_forward_res == pynative_forward_res
    assert graph_backward_res == pynative_backward_res


def test_if_in_if():
    x = Tensor(2, mstype.int32)
    control_flow_if_in_if(IfInIfNet, x)


@pytest.mark.skip(reason="not supported side effect")
def test_if_in_if_01():
    x = Tensor(2, mstype.int32)
    control_flow_if_in_if(IfInIfNet1, x)


@pytest.mark.skip(reason="not supported side effect")
def test_if_in_if_02():
    x = Tensor(2, mstype.int32)
    control_flow_if_in_if(IfInIfNet2, x)


def test_if_in_if_03():
    x = Tensor(2, mstype.int32)
    control_flow_if_in_if(IfInIfNet3, x)
