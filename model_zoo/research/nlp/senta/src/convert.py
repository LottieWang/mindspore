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
'''
Convert paddle weight to ms
'''
import collections
import os
import paddle.fluid.dygraph as D
from paddle import fluid
from mindspore.train.serialization import save_checkpoint
from mindspore import Tensor

#
def build_params_map(attention_num=24):
    """
    build params map from paddle-paddle's ERNIE to transformer's BERT
    :return:
    """
    weight_map = collections.OrderedDict({
        'word_embedding': "bert.bert.bert_embedding_lookup.embedding_table",
        'pos_embedding': "bert.bert.bert_embedding_postprocessor.full_position_embeddings",
        'sent_embedding': "bert.bert.bert_embedding_postprocessor.embedding_table",
        'pre_encoder_layer_norm_scale': 'bert.bert.bert_embedding_postprocessor.layernorm.gamma',
        'pre_encoder_layer_norm_bias': 'bert.bert.bert_embedding_postprocessor.layernorm.beta',
    })
    # add attention layers
    for i in range(attention_num):
        weight_map[
            f'encoder_layer_{i}_multi_head_att_query_fc.w_0'] =\
            f'bert.bert.bert_encoder.layers.{i}.attention.attention.query_layer.weight'
        weight_map[
            f'encoder_layer_{i}_multi_head_att_query_fc.b_0'] =\
            f'bert.bert.bert_encoder.layers.{i}.attention.attention.query_layer.bias'
        weight_map[
            f'encoder_layer_{i}_multi_head_att_key_fc.w_0'] = \
            f'bert.bert.bert_encoder.layers.{i}.attention.attention.key_layer.weight'
        weight_map[
            f'encoder_layer_{i}_multi_head_att_key_fc.b_0'] =\
            f'bert.bert.bert_encoder.layers.{i}.attention.attention.key_layer.bias'
        weight_map[
            f'encoder_layer_{i}_multi_head_att_value_fc.w_0'] =\
            f'bert.bert.bert_encoder.layers.{i}.attention.attention.value_layer.weight'
        weight_map[
            f'encoder_layer_{i}_multi_head_att_value_fc.b_0'] =\
            f'bert.bert.bert_encoder.layers.{i}.attention.attention.value_layer.bias'
        weight_map[
            f'encoder_layer_{i}_multi_head_att_output_fc.w_0'] =\
            f'bert.bert.bert_encoder.layers.{i}.attention.output.dense.weight'
        weight_map[
            f'encoder_layer_{i}_multi_head_att_output_fc.b_0'] =\
            f'bert.bert.bert_encoder.layers.{i}.attention.output.dense.bias'
        weight_map[
            f'encoder_layer_{i}_post_att_layer_norm_scale'] =\
            f'bert.bert.bert_encoder.layers.{i}.attention.output.layernorm.gamma'
        weight_map[
            f'encoder_layer_{i}_post_att_layer_norm_bias'] =\
            f'bert.bert.bert_encoder.layers.{i}.attention.output.layernorm.beta'
        weight_map[f'encoder_layer_{i}_ffn_fc_0.w_0'] = f'bert.bert.bert_encoder.layers.{i}.intermediate.weight'
        weight_map[f'encoder_layer_{i}_ffn_fc_0.b_0'] = f'bert.bert.bert_encoder.layers.{i}.intermediate.bias'
        weight_map[f'encoder_layer_{i}_ffn_fc_1.w_0'] = f'bert.bert.bert_encoder.layers.{i}.output.dense.weight'
        weight_map[f'encoder_layer_{i}_ffn_fc_1.b_0'] = f'bert.bert.bert_encoder.layers.{i}.output.dense.bias'
        weight_map[
            f'encoder_layer_{i}_post_ffn_layer_norm_scale'] = \
            f'bert.bert.bert_encoder.layers.{i}.output.layernorm.gamma'
        weight_map[
            f'encoder_layer_{i}_post_ffn_layer_norm_bias'] = \
            f'bert.bert.bert_encoder.layers.{i}.output.layernorm.beta'
    # add pooler
    weight_map.update(
        {
            'pooled_fc.w_0': 'bert.bert.dense.weight',
            'pooled_fc.b_0': 'bert.bert.dense.bias',
            'mask_lm_trans_fc.w_0': 'bert.cls1.dense.weight',
            'mask_lm_trans_fc.b_0': 'bert.cls1.dense.bias',
            'mask_lm_trans_layer_norm_scale': 'bert.cls1.layernorm.gamma',
            'mask_lm_trans_layer_norm_bias': 'bert.cls1.layernorm.beta',
            'mask_lm_out_fc.b_0': 'bert.cls1.output_bias'
        }
    )
    return weight_map


input_dir = './roberta_skep_large_en/'
state_dict = []
w_map = build_params_map()
with fluid.dygraph.guard():
    paddle_paddle_params, _ = D.load_dygraph(os.path.join(input_dir, 'params'))
for weight_name, weight_value in paddle_paddle_params.items():
    if weight_name not in w_map.keys():
        continue
    if 'w_0' in weight_name \
            or 'post_att_layer_norm_scale' in weight_name \
            or 'post_ffn_layer_norm_scale' in weight_name \
            or 'cls_out_w' in weight_name:
        weight_value = weight_value.transpose()
    if weight_name.find('pos_embedding') > -1:
        weight_value = weight_value[:512]
    state_dict.append({'name': w_map[weight_name], 'data': Tensor(weight_value)})
    print(weight_name, '->', w_map[weight_name], weight_value.shape)
save_checkpoint(state_dict, 'roberta.ckpt')
