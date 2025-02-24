# Copyright 2020-2021 Huawei Technologies Co., Ltd
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
""" test control ops """
import numpy as np
import pytest

from mindspore import dtype as ms
from mindspore import Tensor
from mindspore import context
from mindspore import nn
from mindspore.common.parameter import Parameter, ParameterTuple
from mindspore.ops import composite as C
from mindspore.ops import operations as P

# from tests.vm_impl.math_ops_vm_impl import *
# from tests.vm_impl.vm_interface import *
# from tests.vm_impl import *
# context.set_context(save_graphs=True)


grad_by_list = C.GradOperation(get_by_list=True)
grad_all = C.GradOperation(get_all=True)


def test_while_grad():
    class MyWhileNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.max = P.ReduceMax()

        def construct(self, idx, end, x):
            while idx < end:
                part = x[idx, :, :]
                max_num = self.max(part)
                x[idx, :, 0:2] = max_num
                idx = idx + 1
            return x

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net

        def construct(self, *inputs):
            return grad_all(self.net)(*inputs)

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    while_net = MyWhileNet()
    net = GradNet(while_net)
    idx = Tensor(np.array(0), dtype=ms.int32)
    end = Tensor(np.array(2), dtype=ms.int32)
    x = Tensor(np.random.randn(2, 2, 2).astype(np.float32), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output[0].asnumpy(), pynative_output[0].asnumpy(), 0.0001, 0.0001)
    assert np.allclose(graph_output[1].asnumpy(), pynative_output[1].asnumpy(), 0.0001, 0.0001)
    assert np.allclose(graph_output[2].asnumpy(), pynative_output[2].asnumpy(), 0.0001, 0.0001)


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_while_with_const_param_grad():
    class MyWhileNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.mul = P.Mul()
            self.add = P.Add()

        def construct(self, x, y):
            while x < y:
                z = self.mul(x, x)
                x = self.add(z, 1)
            return x

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net

        def construct(self, *inputs):
            return grad_all(self.net)(*inputs)

    context.set_context(mode=context.GRAPH_MODE)
    while_net = MyWhileNet()
    net = GradNet(while_net)
    idx = Tensor([1.1], dtype=ms.float32)
    end = Tensor([8.0], dtype=ms.float32)
    graph_output = net(idx, end)
    expect_one = np.array([1.14433983e+02], dtype=np.float32)
    expect_two = np.array([0], dtype=np.float32)
    assert np.allclose(graph_output[0].asnumpy(), expect_one, 0.0001, 0.0001)
    assert np.allclose(graph_output[1].asnumpy(), expect_two, 0.0001, 0.0001)


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_while_with_variable_grad():
    class MyWhileNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.mul = P.Mul()
            self.add = P.Add()

        def construct(self, x, y):
            while x < y:
                z = self.mul(x, x)
                x = self.add(z, y)
            return x

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net

        def construct(self, *inputs):
            return grad_all(self.net)(*inputs)

    context.set_context(mode=context.GRAPH_MODE)
    while_net = MyWhileNet()
    net = GradNet(while_net)
    idx = Tensor([1.1], dtype=ms.float32)
    end = Tensor([8.0], dtype=ms.float32)
    graph_output = net(idx, end)
    expect_one = np.array([2.20000005e+00], dtype=np.float32)
    expect_two = np.array([1.00000000e+00], dtype=np.float32)
    assert np.allclose(graph_output[0].asnumpy(), expect_one, 0.0001, 0.0001)
    assert np.allclose(graph_output[1].asnumpy(), expect_two, 0.0001, 0.0001)


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_while_with_param_forward():
    class MyWhileNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.max = P.ReduceMax()
            self.param = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="weight")
            self.zero = Tensor(np.zeros(([2, 2, 2])), ms.float32)

        def construct(self, idx, end, x):
            out = self.zero
            while idx < end:
                part = x[idx, :, :]
                max_num = self.max(part)
                x[idx, :, 0:2] = max_num
                out = out + x + self.param
                idx = idx + 1
            return out

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    net = MyWhileNet()
    idx = Tensor(np.array(0), dtype=ms.int32)
    end = Tensor(np.array(2), dtype=ms.int32)
    x = Tensor(np.arange(8).reshape(2, 2, 2).astype(np.float32), dtype=ms.float32)
    graph_output = net(idx, end, x)
    expect = np.array([[[6, 8], [10, 12]], [[19, 22], [25, 28]]], dtype=np.int32)
    assert np.allclose(graph_output.asnumpy(), expect, 0.0001, 0.0001)


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_while_endless_case():
    """endless case when optimization"""

    class MyWhileNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.max = P.ReduceMax()
            self.param = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="weight")
            self.zero = Tensor(np.zeros(([2, 2, 2])), ms.float32)

        def construct(self, idx, end, x):
            out = self.zero
            while idx < end:
                part = x[idx, :, :]
                out = out + part
                idx = idx + 1
            return out

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    net = MyWhileNet()
    idx = Tensor(np.array(0), dtype=ms.int32)
    end = Tensor(np.array(2), dtype=ms.int32)
    x = Tensor(np.arange(8).reshape(2, 2, 2).astype(np.float32), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output.asnumpy(), pynative_output.asnumpy(), 0.0001, 0.0001)


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_while_with_param_grad():
    class MyWhileNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.max = P.ReduceMax()
            self.param = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="weight")
            self.zero = Tensor(np.zeros(([2, 2, 2])), ms.float32)

        def construct(self, idx, end, x):
            out = self.zero
            while idx < end:
                part = x[idx, :, :]
                max_num = self.max(part)
                x[idx, :, 0:2] = max_num
                out = out + x + self.param
                idx = idx + 1
            return out

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net
            self.weights = ParameterTuple(net.trainable_params())

        def construct(self, a, b, c):
            return grad_by_list(self.net, self.weights)(a, b, c)

    context.set_context(mode=context.GRAPH_MODE)
    while_net = MyWhileNet()
    net = GradNet(while_net)
    idx = Tensor(np.array(0), dtype=ms.int32)
    end = Tensor(np.array(2), dtype=ms.int32)
    x = Tensor(np.arange(8).reshape(2, 2, 2).astype(np.float32), dtype=ms.float32)
    graph_output = net(idx, end, x)
    expect = np.array([[[2, 2], [2, 2]], [[2, 2], [2, 2]]], dtype=np.int32)
    assert np.allclose(graph_output[0].asnumpy(), expect, 0.0001, 0.0001)


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_while_with_param_forward_with_const_branch():
    class MyWhileNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.max = P.ReduceMax()
            self.param = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="weight")
            self.zero = Tensor(np.zeros(([2, 2, 2])), ms.float32)
            self.reduce = P.ReduceSum()

        def construct(self, idx, end, x):
            out = self.zero
            while idx < end:
                if 2 > 1:
                    out = out + self.param
                else:
                    out = out + idx + self.param
                idx = idx + 1
            return out

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    while_net = MyWhileNet()
    net = while_net
    idx = Tensor(np.array(0), dtype=ms.int32)
    end = Tensor(np.array(4), dtype=ms.int32)
    x = Tensor(np.random.randn(2, 2, 2).astype(np.float32), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output.asnumpy(), pynative_output.asnumpy(), 0.0001, 0.0001)


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_while_opt_endless():
    """endless during optimization case"""

    class MyWhileNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.max = P.ReduceMax()
            self.param = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="weight")
            self.zero = Tensor(np.zeros(([2, 2, 2])), ms.float32)
            self.reduce = P.ReduceSum()
            self.addn = P.AddN()

        def construct(self, idx, end, x):
            addn1 = self.addn((x, x, x))
            out = addn1
            while idx < end:
                out = self.addn((out, addn1))
                idx = idx + 1
            out = self.addn((out, x))
            return out

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net

        def construct(self, *inputs):
            return grad_all(self.net)(*inputs)

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    while_net = MyWhileNet()
    net = GradNet(while_net)
    idx = Tensor(np.array(0), dtype=ms.int32)
    end = Tensor(np.array(4), dtype=ms.int32)
    x = Tensor(np.ones([2, 2, 2]).astype(np.float32) * 3, dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output[0].asnumpy(), pynative_output[0].asnumpy(), 0.0001, 0.0001)


@pytest.mark.skip(reason="not supported yet")
@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_no_while_call():
    class MyWhileNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.max = P.ReduceMax()
            self.param = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="weight")
            self.zero = Tensor(np.zeros(([2, 2, 2])), ms.float32)
            self.reduce = P.ReduceSum()

        def construct(self, idx, end, x):
            out = self.zero
            if 2 > 1:
                out = out + self.param
            else:
                out = out + idx + self.param
            return out

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    while_net = MyWhileNet()
    net = while_net
    idx = Tensor(np.array(0), dtype=ms.int32)
    end = Tensor(np.array(4), dtype=ms.int32)
    x = Tensor(np.random.randn(2, 2, 2).astype(np.float32), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output.asnumpy(), pynative_output.asnumpy(), 0.0001, 0.0001)


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_while_with_param_grad_with_const_branch():
    class MyWhileNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.max = P.ReduceMax()
            self.param = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="weight")
            self.zero = Tensor(np.zeros(([2, 2, 2])), ms.float32)
            self.reduce = P.ReduceSum()

        def construct(self, idx, end, x):
            out = self.zero
            while idx < end:
                if 2 > 1:
                    out = out + self.param
                else:
                    out = out + idx + self.param
                idx = idx + 1
            return out

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net
            self.weights = ParameterTuple(net.trainable_params())

        def construct(self, a, b, c):
            return grad_by_list(self.net, self.weights)(a, b, c)

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    while_net = MyWhileNet()
    net = GradNet(while_net)
    idx = Tensor(np.array(0), dtype=ms.int32)
    end = Tensor(np.array(4), dtype=ms.int32)
    x = Tensor(np.random.randn(2, 2, 2).astype(np.float32), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output[0].asnumpy(), pynative_output[0].asnumpy(), 0.0001, 0.0001)


@pytest.mark.skip(reason="not supported yet")
@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_for_while_with_param_grad_with_const_branch():
    class MyWhileNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.max = P.ReduceMax()
            self.param = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="weight")
            self.zero = Tensor(np.zeros(([2, 2, 2])), ms.float32)
            self.reduce = P.ReduceSum()
            self.start = Tensor(np.array(0), dtype=ms.int32)

        def construct(self, idx, end, x):
            out = self.zero
            for _ in range(0, 2):
                idx = self.start
                while idx < end:
                    if 2 > 1:
                        out = out + self.param
                    else:
                        out = out + idx + self.param
                    idx = idx + 1
            return out

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net
            self.weights = ParameterTuple(net.trainable_params())

        def construct(self, a, b, c):
            return grad_by_list(self.net, self.weights)(a, b, c)

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    while_net = MyWhileNet()
    net = GradNet(while_net)
    idx = Tensor(np.array(0), dtype=ms.int32)
    end = Tensor(np.array(4), dtype=ms.int32)
    x = Tensor(np.random.randn(2, 2, 2).astype(np.float32), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output[0].asnumpy(), pynative_output[0].asnumpy(), 0.0001, 0.0001)


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_for_while_with_param_grad_basic():
    class MyWhileNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.max = P.ReduceMax()
            self.param = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="weight")
            self.zero = Tensor(np.zeros(([2, 2, 2])), ms.float32)
            self.reduce = P.ReduceSum()
            self.start = Tensor(np.array(0), dtype=ms.int32)

        def construct(self, idx, end, x):
            out = self.zero
            for _ in range(0, 2):
                idx = self.start
                while idx < end:
                    out = out + self.param
                    idx = idx + 1
            return out

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net
            self.weights = ParameterTuple(net.trainable_params())

        def construct(self, a, b, c):
            return grad_by_list(self.net, self.weights)(a, b, c)

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    while_net = MyWhileNet()
    net = GradNet(while_net)
    idx = Tensor(np.array(0), dtype=ms.int32)
    end = Tensor(np.array(4), dtype=ms.int32)
    x = Tensor(np.random.randn(2, 2, 2).astype(np.float32), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output[0].asnumpy(), pynative_output[0].asnumpy(), 0.0001, 0.0001)


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_for_while_with_param_grad_normal():
    class MyWhileNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.max = P.ReduceMax()
            self.param = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="weight")
            self.zero = Tensor(np.zeros(([2, 2, 2])), ms.float32)
            self.reduce = P.ReduceSum()
            self.start = Tensor(np.array(0), dtype=ms.int32)

        def construct(self, idx, end, x):
            out = x
            for _ in range(0, 2):
                idx = self.start
                while idx < end:
                    out = out + self.param
                    idx = idx + 1
            return out

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net
            self.weights = ParameterTuple(net.trainable_params())

        def construct(self, a, b, c):
            return grad_by_list(self.net, self.weights)(a, b, c)

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    while_net = MyWhileNet()
    net = GradNet(while_net)
    idx = Tensor(np.array(0), dtype=ms.int32)
    end = Tensor(np.array(4), dtype=ms.int32)
    x = Tensor(np.random.randn(2, 2, 2).astype(np.float32), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output[0].asnumpy(), pynative_output[0].asnumpy(), 0.0001, 0.0001)


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_while_with_param_basic_grad():
    class MyWhileNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.max = P.ReduceMax()
            self.param = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="weight")
            self.zero = Tensor(np.zeros(([2, 2, 2])), ms.float32)
            self.t2 = Tensor(np.array(2), dtype=ms.float32)

        def construct(self, idx, end, x):
            out = self.zero
            while idx < end:
                out = out + self.param
                idx = idx + 1
            return out + self.param

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net
            self.weights = ParameterTuple(net.trainable_params())

        def construct(self, a, b, c):
            return grad_by_list(self.net, self.weights)(a, b, c)

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    while_net = MyWhileNet()
    net = GradNet(while_net)
    idx = Tensor(np.array(0), dtype=ms.int32)
    end = Tensor(np.array(3), dtype=ms.int32)
    x = Tensor(np.random.randn(2, 2, 2).astype(np.float32), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output[0].asnumpy(), pynative_output[0].asnumpy(), 0.0001, 0.0001)


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_while_with_param_basic_grad_mul():
    class MyWhileNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.max = P.ReduceMax()
            self.param = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="weight")
            self.zero = Tensor(np.ones(([2, 2, 2])), ms.float32)
            self.t2 = Tensor(np.array(2), dtype=ms.float32)

        def construct(self, idx, end, x):
            out = self.zero
            while idx < end:
                out = out * self.param
                idx = idx + 1
            return out + self.param

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net
            self.weights = ParameterTuple(net.trainable_params())

        def construct(self, a, b, c):
            return grad_by_list(self.net, self.weights)(a, b, c)

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    while_net = MyWhileNet()
    net = GradNet(while_net)
    idx = Tensor(np.array(0), dtype=ms.int32)
    end = Tensor(np.array(3), dtype=ms.int32)
    x = Tensor(np.random.randn(2, 2, 2).astype(np.float32), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output[0].asnumpy(), pynative_output[0].asnumpy(), 0.0001, 0.0001)


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_while_with_param_basic_grad_two():
    class MyWhileNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.max = P.ReduceMax()
            self.param = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="weight")
            self.weight = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="loss")
            self.zero = Tensor(np.zeros(([2, 2, 2])), ms.float32)
            self.t2 = Tensor(np.array(2), dtype=ms.float32)

        def construct(self, idx, end, x):
            out = self.zero
            while idx < end:
                out = out + self.param + self.weight
                idx = idx + 1
            return out + self.param

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net
            self.weights = ParameterTuple(net.trainable_params())

        def construct(self, a, b, c):
            return grad_by_list(self.net, self.weights)(a, b, c)

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    while_net = MyWhileNet()
    net = GradNet(while_net)
    idx = Tensor(np.array(0), dtype=ms.int32)
    end = Tensor(np.array(3), dtype=ms.int32)
    x = Tensor(np.random.randn(2, 2, 2).astype(np.float32), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output[0].asnumpy(), pynative_output[0].asnumpy(), 0.0001, 0.0001)
    assert np.allclose(graph_output[1].asnumpy(), pynative_output[1].asnumpy(), 0.0001, 0.0001)


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_while_with_param_basic_grad_three():
    class MyWhileNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.max = P.ReduceMax()
            self.param = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="weight")
            self.weight = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="loss")
            self.key = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="key")
            self.zero = Tensor(np.zeros(([2, 2, 2])), ms.float32)
            self.t2 = Tensor(np.array(2), dtype=ms.float32)

        def construct(self, idx, end, x):
            out = self.zero
            while idx < end:
                out = out + self.param + self.weight + self.key
                idx = idx + 1
            return out + self.param

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net
            self.weights = ParameterTuple(net.trainable_params())

        def construct(self, a, b, c):
            return grad_by_list(self.net, self.weights)(a, b, c)

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    while_net = MyWhileNet()
    net = GradNet(while_net)
    idx = Tensor(np.array(0), dtype=ms.int32)
    end = Tensor(np.array(3), dtype=ms.int32)
    x = Tensor(np.random.randn(2, 2, 2).astype(np.float32), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output[0].asnumpy(), pynative_output[0].asnumpy(), 0.0001, 0.0001)
    assert np.allclose(graph_output[1].asnumpy(), pynative_output[1].asnumpy(), 0.0001, 0.0001)
    assert np.allclose(graph_output[2].asnumpy(), pynative_output[2].asnumpy(), 0.0001, 0.0001)


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_while_if_with_param_grad():
    class MyWhileNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.max = P.ReduceMax()
            self.param = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="weight")
            self.zero = Tensor(np.zeros(([2, 2, 2])), ms.float32)
            self.t2 = Tensor(np.array(2), dtype=ms.float32)

        def construct(self, idx, end, x):
            out = self.zero
            while idx < end:
                if self.max(out) < self.max(x):
                    out = out + self.param * 2
                else:
                    out = out + self.param
                idx = idx + 1
            return out + self.param

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net
            self.weights = ParameterTuple(net.trainable_params())

        def construct(self, a, b, c):
            return grad_by_list(self.net, self.weights)(a, b, c)

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    while_net = MyWhileNet()
    net = GradNet(while_net)
    idx = Tensor(np.array(0), dtype=ms.int32)
    end = Tensor(np.array(3), dtype=ms.int32)
    x = Tensor(np.ones([2, 2, 2]).astype(np.float32), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output[0].asnumpy(), pynative_output[0].asnumpy(), 0.0001, 0.0001)


@pytest.mark.skip(reason="not supported yet")
@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_while_with_param_grad_not_enter_while():
    class MyWhileNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.max = P.ReduceMax()
            self.param = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="weight")
            self.zero = Tensor(np.zeros(([2, 2, 2])), ms.float32)

        def construct(self, idx, end, x):
            out = self.zero
            while idx < end:
                out = out + self.param * 3
                idx = idx + 1
            return out + self.param

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net
            self.weights = ParameterTuple(net.trainable_params())

        def construct(self, a, b, c):
            return grad_by_list(self.net, self.weights)(a, b, c)

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    while_net = MyWhileNet()
    net = GradNet(while_net)
    idx = Tensor(np.array(3), dtype=ms.int32)
    end = Tensor(np.array(0), dtype=ms.int32)
    x = Tensor(np.random.randn(2, 2, 2).astype(np.float32), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output[0].asnumpy(), pynative_output[0].asnumpy(), 0.0001, 0.0001)


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_with_param_if_by_if_forward():
    class MyIfByIfNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.max = P.ReduceMax()
            self.param = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="weight")
            self.zero = Tensor(np.zeros(([2, 2, 2])), ms.float32)

        def construct(self, a, b, x):
            out = self.zero
            if a < b:
                out = out + x + self.param
            else:
                out = out + x
            if a == b:
                out = out + x * 3 + self.param
            else:
                out = out + x * 2
            return out

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    if_net = MyIfByIfNet()
    net = if_net
    idx = Tensor(np.array(0), dtype=ms.int32)
    end = Tensor(np.array(4), dtype=ms.int32)
    x = Tensor(np.ones([2, 2, 2]).astype(np.float32), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output.asnumpy(), pynative_output.asnumpy(), 0.0001, 0.0001)


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_with_param_if_by_if_grad_inputs():
    class MyIfByIfNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.max = P.ReduceMax()
            self.param = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="weight")
            self.zero = Tensor(np.zeros(([2, 2, 2])), ms.float32)

        def construct(self, a, b, x):
            out = self.zero
            if a < b:
                out = out + x + self.param * 4
            if a == b:
                out = out + x * 3 + self.param * 3
            return out

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net

        def construct(self, *inputs):
            return grad_all(self.net)(*inputs)

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    if_net = MyIfByIfNet()
    net = GradNet(if_net)
    idx = Tensor(np.array(0), dtype=ms.int32)
    end = Tensor(np.array(0), dtype=ms.int32)
    x = Tensor(np.random.randn(2, 2, 2).astype(np.float32), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output[0].asnumpy(), pynative_output[0].asnumpy(), 0.0001, 0.0001)
    assert np.allclose(graph_output[1].asnumpy(), pynative_output[1].asnumpy(), 0.0001, 0.0001)
    assert np.allclose(graph_output[2].asnumpy(), pynative_output[2].asnumpy(), 0.0001, 0.0001)


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_with_param_if_by_if_grad_parameter():
    class MyIfByIfNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.max = P.ReduceMax()
            self.param = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="weight")
            self.zero = Tensor(np.zeros(([2, 2, 2])), ms.float32)

        def construct(self, a, b, x):
            out = self.zero
            if a < b:
                out = out + x + self.param * 2
            if a == b:
                out = out + x * 3 + self.param
            return out

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net
            self.weights = ParameterTuple(net.trainable_params())

        def construct(self, *inputs):
            return grad_by_list(self.net, self.weights)(*inputs)

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    if_net = MyIfByIfNet()
    net = GradNet(if_net)
    idx = Tensor(np.array(0), dtype=ms.int32)
    end = Tensor(np.array(2), dtype=ms.int32)
    x = Tensor(np.random.randn(2, 2, 2).astype(np.float32), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output[0].asnumpy(), pynative_output[0].asnumpy(), 0.0001, 0.0001)


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_with_param_if_by_if_grad_param_excute_null():
    class MyIfByIfNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.max = P.ReduceMax()
            self.param = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="weight")
            self.zero = Tensor(np.zeros(([2, 2, 2])), ms.float32)

        def construct(self, a, b, x):
            out = self.zero
            if a < b:
                out = out + x + self.param * 2
            return out

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net
            self.weights = ParameterTuple(net.trainable_params())

        def construct(self, *inputs):
            return grad_by_list(self.net, self.weights)(*inputs)

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    if_net = MyIfByIfNet()
    net = GradNet(if_net)
    idx = Tensor(np.array(4), dtype=ms.int32)
    end = Tensor(np.array(0), dtype=ms.int32)
    x = Tensor(np.random.randn(2, 2, 2).astype(np.float32), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output[0].asnumpy(), pynative_output[0].asnumpy(), 0.0001, 0.0001)


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_if_by_if_return_inside_grad():
    class MyIfByIfNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.max = P.ReduceMax()
            self.param = Parameter(Tensor(np.arange(2 * 2 * 2).reshape((2, 2, 2)), ms.float32), name="weight")
            self.zero = Tensor(np.zeros(([2, 2, 2])), ms.float32)

        def construct(self, a, b, x):
            out = self.zero
            if a < b:
                return out + x + self.param
            if a == b:
                return out + self.param * 2
            return out + self.param * 3

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net
            self.weights = ParameterTuple(net.trainable_params())

        def construct(self, *inputs):
            return grad_by_list(self.net, self.weights)(*inputs)

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    if_net = MyIfByIfNet()
    net = GradNet(if_net)
    idx = Tensor(np.array(1), dtype=ms.int32)
    end = Tensor(np.array(0), dtype=ms.int32)
    x = Tensor(np.random.randn(2, 2, 2).astype(np.float32), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output[0].asnumpy(), pynative_output[0].asnumpy(), 0.0001, 0.0001)


@pytest.mark.level1
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_if_by_if_forward():
    class MyIfByIfNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.add = P.Add()
            self.sub = P.Sub()
            self.mul = P.Mul()
            self.div = P.RealDiv()

        def construct(self, a, b, x):
            if a < b:
                a = self.add(a, b)
            else:
                a = self.sub(a, b)
            if a == x:
                a = self.mul(a, b)
            else:
                a = self.div(a, b)
            if b == x:
                b = self.add(a, b)
            else:
                b = self.add(a, x)
            a = a * b
            out = a + b + x
            return out

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    if_net = MyIfByIfNet()
    net = if_net
    idx = Tensor(np.array(2), dtype=ms.float32)
    end = Tensor(np.array(3), dtype=ms.float32)
    x = Tensor(np.array(4), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output.asnumpy(), pynative_output.asnumpy(), 0.0001, 0.0001)


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_if_by_if_forward_control_tuple_switch():
    """tuple_get from  switch op will generate new switch inside to eliminate tuple_get"""

    class Branch3Net(nn.Cell):
        def __init__(self):
            super().__init__()
            self.add = P.Add()
            self.sub = P.Sub()
            self.mul = P.Mul()
            self.div = P.RealDiv()

        def construct(self, a, b, x):
            if b == x:
                b = self.add(a, b)
            else:
                b = self.add(a, x)
            return a, b, x

    class Branch2Net(nn.Cell):
        def __init__(self):
            super().__init__()
            self.add = P.Add()
            self.sub = P.Sub()
            self.mul = P.Mul()
            self.div = P.RealDiv()
            self.net = Branch3Net()

        def construct(self, a, b, x):
            if a == x:
                a = self.mul(a, b)
            else:
                a = self.div(a, b)
            return self.net(a, b, x)

    class MyIfByIfNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.add = P.Add()
            self.sub = P.Sub()
            self.mul = P.Mul()
            self.div = P.RealDiv()
            self.net = Branch2Net()

        def construct(self, a, b, x):
            if a < b:
                a = self.add(a, b)
            else:
                a = self.sub(a, b)
            a, b, x = self.net(a, b, x)
            a = a * b
            out = a + b + x
            return out

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    if_net = MyIfByIfNet()
    net = if_net
    idx = Tensor(np.array(2), dtype=ms.float32)
    end = Tensor(np.array(3), dtype=ms.float32)
    x = Tensor(np.array(0), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output.asnumpy(), pynative_output.asnumpy(), 0.0001, 0.0001)


@pytest.mark.level0
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_if_by_if_forward_control_inside_net():
    class Branch3Net(nn.Cell):
        def __init__(self):
            super().__init__()
            self.add = P.Add()
            self.sub = P.Sub()
            self.mul = P.Mul()
            self.div = P.RealDiv()

        def construct(self, a, b, x):
            if b == x:
                b = self.add(a, b)
            else:
                b = self.add(a, x)
            a = a * b
            out = a + b + x
            return out

    class Branch2Net(nn.Cell):
        def __init__(self):
            super().__init__()
            self.add = P.Add()
            self.sub = P.Sub()
            self.mul = P.Mul()
            self.div = P.RealDiv()
            self.net = Branch3Net()

        def construct(self, a, b, x):
            if a == x:
                a = self.mul(a, b)
            else:
                a = self.div(a, b)
            return self.net(a, b, x)

    class MyIfByIfNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.add = P.Add()
            self.sub = P.Sub()
            self.mul = P.Mul()
            self.div = P.RealDiv()
            self.net = Branch2Net()

        def construct(self, a, b, x):
            if a < b:
                a = self.add(a, b)
            else:
                a = self.sub(a, b)
            out = self.net(a, b, x)
            return out

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    if_net = MyIfByIfNet()
    net = if_net
    idx = Tensor(np.array(2), dtype=ms.float32)
    end = Tensor(np.array(3), dtype=ms.float32)
    x = Tensor(np.array(0), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output.asnumpy(), pynative_output.asnumpy(), 0.0001, 0.0001)


@pytest.mark.level1
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_if_by_if_forward_use_namespace():
    class MyIfByIfNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.add = P.Add()
            self.sub = P.Sub()
            self.mul = P.Mul()
            self.div = P.RealDiv()

        def construct(self, a, b, x):
            if a < b:
                a = P.Add()(a, b)
            else:
                a = P.Sub()(a, b)
            if a == x:
                a = P.Mul()(a, b)
            else:
                a = P.RealDiv()(a, b)
            if b == x:
                b = P.Add()(a, b)
            else:
                b = P.Add()(a, x)
            a = a * b
            out = a + b + x
            return out

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    if_net = MyIfByIfNet()
    net = if_net
    idx = Tensor(np.array(2), dtype=ms.float32)
    end = Tensor(np.array(3), dtype=ms.float32)
    x = Tensor(np.array(0), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output.asnumpy(), pynative_output.asnumpy(), 0.0001, 0.0001)


@pytest.mark.level1
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_if_by_if_forward_use_global_op():
    class MyIfByIfNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.add = P.Add()
            self.sub = P.Sub()
            self.mul = P.Mul()
            self.div = P.RealDiv()

        def construct(self, a, b, x):
            add = P.Add()
            sub = P.Sub()
            mul = P.Mul()
            div = P.RealDiv()
            if a < b:
                a = add(a, b)
            else:
                a = sub(a, b)
            if a == x:
                a = mul(a, b)
            else:
                a = div(a, b)
            if b == x:
                b = add(a, b)
            else:
                b = add(a, x)
            a = a * b
            out = a + b + x
            return out

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    if_net = MyIfByIfNet()
    net = if_net
    idx = Tensor(np.array(2), dtype=ms.float32)
    end = Tensor(np.array(3), dtype=ms.float32)
    x = Tensor(np.array(0), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output.asnumpy(), pynative_output.asnumpy(), 0.0001, 0.0001)


@pytest.mark.level1
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_for_with_if_by_if_forward():
    class MyIfByIfNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.add = P.Add()
            self.sub = P.Sub()

        def construct(self, a, b, x):
            for _ in range(0, 4):
                if a < b:
                    a = self.add(a, b)
                else:
                    b = self.sub(b, x)
            a = a * b
            out = a + b + x
            return out

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    if_net = MyIfByIfNet()
    net = if_net
    idx = Tensor(np.array(2), dtype=ms.float32)
    end = Tensor(np.array(3), dtype=ms.float32)
    x = Tensor(np.array(0), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output.asnumpy(), pynative_output.asnumpy(), 0.0001, 0.0001)


@pytest.mark.level1
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_for_with_if_by_if_forward_namespace():
    class MyIfByIfNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.add = P.Add()
            self.sub = P.Sub()
            self.mul = P.Mul()
            self.div = P.RealDiv()

        def construct(self, a, b, x):
            for _ in range(0, 6):
                if a < b:
                    a = P.Add()(a, b)
                else:
                    b = P.Sub()(b, x)
            a = a * b
            out = a + b + x
            return out

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    if_net = MyIfByIfNet()
    net = if_net
    idx = Tensor(np.array(2), dtype=ms.float32)
    end = Tensor(np.array(3), dtype=ms.float32)
    x = Tensor(np.array(0), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output.asnumpy(), pynative_output.asnumpy(), 0.0001, 0.0001)


@pytest.mark.level1
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_if_by_if_forward_const_branch_inner():
    class MyIfByIfNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.add = P.Add()
            self.sub = P.Sub()
            self.mul = P.Mul()
            self.div = P.RealDiv()

        def construct(self, a, b, x):
            add = P.Add()
            sub = P.Sub()
            mul = P.Mul()
            div = P.RealDiv()
            if a < b:
                a = add(a, b)
            else:
                a = sub(a, b)
            if 2 > 1:
                a = mul(a, b)
            else:
                a = div(a, b)
            if b == x:
                b = add(a, b)
            else:
                b = add(a, x)
            a = a * b
            out = a + b + x
            return out

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    if_net = MyIfByIfNet()
    net = if_net
    idx = Tensor(np.array(2), dtype=ms.float32)
    end = Tensor(np.array(3), dtype=ms.float32)
    x = Tensor(np.array(0), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output.asnumpy(), pynative_output.asnumpy(), 0.0001, 0.0001)


@pytest.mark.level1
@pytest.mark.platform_arm_ascend_training
@pytest.mark.platform_x86_ascend_training
@pytest.mark.env_onecard
def test_if_by_if_forward_all_const_branch():
    class MyIfByIfNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.add = P.Add()
            self.sub = P.Sub()
            self.mul = P.Mul()
            self.div = P.RealDiv()

        def construct(self, a, b, x):
            add = P.Add()
            sub = P.Sub()
            mul = P.Mul()
            div = P.RealDiv()
            if 2 < 12:
                a = add(a, b)
            else:
                a = sub(a, b)
            if 2 > 1:
                a = mul(a, b)
            else:
                a = div(a, b)
            if 2 == 1:
                b = add(a, b)
            else:
                b = add(a, x)
            a = a * b
            out = a + b + x
            return out

    # graph mode
    context.set_context(mode=context.GRAPH_MODE)
    if_net = MyIfByIfNet()
    net = if_net
    idx = Tensor(np.array(2), dtype=ms.float32)
    end = Tensor(np.array(3), dtype=ms.float32)
    x = Tensor(np.array(0), dtype=ms.float32)
    graph_output = net(idx, end, x)
    # pynative mode
    context.set_context(mode=context.PYNATIVE_MODE)
    pynative_output = net(idx, end, x)
    assert np.allclose(graph_output.asnumpy(), pynative_output.asnumpy(), 0.0001, 0.0001)


@pytest.mark.level1
@pytest.mark.platform_x86_cpu
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_if_const_grad():
    class MyNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.add = P.Add()

        def construct(self, *inputs):
            out = self.add(*inputs)
            return out

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net
            self.weights = ParameterTuple(net.trainable_params())

        def construct(self, *inputs):
            a = 1
            b = 2
            if a > 0:
                b = 1
            a += b
            return grad_by_list(self.net, self.weights)(*inputs)

    context.set_context(mode=context.GRAPH_MODE)
    my_net = MyNet()
    net = GradNet(my_net)
    a = Tensor(np.array(0), dtype=ms.int32)
    b = Tensor(np.array(1), dtype=ms.int32)
    net(a, b)


@pytest.mark.level1
@pytest.mark.platform_x86_cpu
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_if_by_if_const_grad():
    class MyNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.add = P.Add()

        def construct(self, *inputs):
            out = self.add(*inputs)
            return out

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net
            self.weights = ParameterTuple(net.trainable_params())

        def construct(self, *inputs):
            a = 1
            b = 2
            if a > 0:
                b = 1
            if a < 0:
                b = 0
            if a == 0:
                b = 3
            a += b
            return grad_by_list(self.net, self.weights)(*inputs)

    context.set_context(mode=context.GRAPH_MODE)
    my_net = MyNet()
    net = GradNet(my_net)
    a = Tensor(np.array(0), dtype=ms.int32)
    b = Tensor(np.array(1), dtype=ms.int32)
    net(a, b)


@pytest.mark.level1
@pytest.mark.platform_x86_cpu
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_while_const_grad():
    class MyNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.add = P.Add()

        def construct(self, *inputs):
            out = self.add(*inputs)
            return out

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net
            self.weights = ParameterTuple(net.trainable_params())

        def construct(self, *inputs):
            a = 1
            while a > 1:
                a = a - 1
            return grad_by_list(self.net, self.weights)(*inputs)

    context.set_context(mode=context.GRAPH_MODE)
    my_net = MyNet()
    net = GradNet(my_net)
    a = Tensor(np.array(0), dtype=ms.int32)
    b = Tensor(np.array(1), dtype=ms.int32)
    net(a, b)


@pytest.mark.level1
@pytest.mark.platform_x86_cpu
@pytest.mark.platform_x86_gpu_training
@pytest.mark.env_onecard
def test_if_by_while_const_grad():
    class MyNet(nn.Cell):
        def __init__(self):
            super().__init__()
            self.add = P.Add()

        def construct(self, *inputs):
            out = self.add(*inputs)
            return out

    class GradNet(nn.Cell):
        def __init__(self, net):
            super(GradNet, self).__init__()
            self.net = net
            self.weights = ParameterTuple(net.trainable_params())

        def construct(self, *inputs):
            a = 1
            b = 2
            if a > 0:
                b = 0
            while a > 1:
                a = a - 1
            a += b
            return grad_by_list(self.net, self.weights)(*inputs)

    context.set_context(mode=context.GRAPH_MODE)
    my_net = MyNet()
    net = GradNet(my_net)
    a = Tensor(np.array(0), dtype=ms.int32)
    b = Tensor(np.array(1), dtype=ms.int32)
    net(a, b)
