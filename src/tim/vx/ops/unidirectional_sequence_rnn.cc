/****************************************************************************
*
*    Copyright (c) 2022 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************/
#include "tim/vx/ops/unidirectional_sequence_rnn.h"

#include "builtin_op_impl.h"
#include "vsi_nn_pub.h"

namespace tim {
namespace vx {
namespace ops {

vsi_nn_activation_e downcast_act_type(UnidirectionalSequenceRnn::ActivationType act) {
    switch (act) {
        case UnidirectionalSequenceRnn::ActivationType::kRELU:
            return VSI_NN_ACT_RELU;
        case UnidirectionalSequenceRnn::ActivationType::kRELU1:
            return VSI_NN_ACT_RELU1;
        case UnidirectionalSequenceRnn::ActivationType::kRELU6:
            return VSI_NN_ACT_RELU6;
        case UnidirectionalSequenceRnn::ActivationType::kTANH:
            return VSI_NN_ACT_TANH;
        case UnidirectionalSequenceRnn::ActivationType::kSIGMOID:
            return VSI_NN_ACT_SIGMOID;
        case UnidirectionalSequenceRnn::ActivationType::kHARDSIGMOID:
            return VSI_NN_ACT_HARD_SIGMOID;
        default: {
            VSILOGW("Not supported activition type for RNN = %d", static_cast<int32_t>(act));
            return VSI_NN_ACT_NONE;
        }
    }
}

UnidirectionalSequenceRnn::UnidirectionalSequenceRnn(
    Graph* graph, 
    ActivationType act_type,
    bool time_major)
    : DirectMapOp(graph, VSI_NN_OP_UNIDIRECTIONAL_SEQUENCE_RNN),
      act_type_(act_type) {

  this->impl()->node()->nn_param.unidirectional_sequence_rnn.time_major = time_major;
  this->impl()->node()->nn_param.unidirectional_sequence_rnn.activation =
      downcast_act_type(act_type);
}

std::shared_ptr<Operation> UnidirectionalSequenceRnn::Clone(std::shared_ptr<Graph>& graph) const {
    auto cloned_op =
        graph->CreateOperation<tim::vx::ops::UnidirectionalSequenceRnn>(
            act_type_,
            this->impl()->node()->nn_param.unidirectional_sequence_rnn.time_major);
    return cloned_op;
}

}
}  // namespace vx
}  // namespace tim