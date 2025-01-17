
#include "tim/fuse/batch_fuse.h"
#include "batch_fuse_context.h"
#include <algorithm>
#include <deque>
#include <cassert>

#include "tim/vx/context.h"
#include "tim/vx/graph.h"
#include "tim/vx/operation.h"
#include "ops/op_batch_fuse.h"
#include "ops/conv2d_batch_fuse.h"
#include "ops/pad_batch_fuse.h"
#include "ops/activation_batch_fuse.h"
#include "ops/elementwise_batch_fuse.h"
#include "ops/pool2d_batch_fuse.h"
#include "ops/reduce_batch_fuse.h"
#include "ops/transpose_batch_fuse.h"
#include "ops/reshape_batch_fuse.h"
#include "ops/concat_batch_fuse.h"

namespace tim {
namespace fuse {
namespace batch_fuse_impl {

void BatchFuseContext::MarkVisited(const std::shared_ptr<vx::Operation>& op) {
  if (visited_op_.end() !=
      std::find(visited_op_.begin(), visited_op_.end(), op)) {
    VSILOGW("The operation has been mark as visited.");
  } else {
    visited_op_.push_back(op);
  }
}

bool BatchFuseContext::IsVisited(
    const std::shared_ptr<vx::Operation>& op) const {
  return (visited_op_.end() !=
          std::find(visited_op_.begin(), visited_op_.end(), op));
}

bool BatchFuseContext::IsReadyForBatchFuse(
    const std::shared_ptr<vx::Operation>& op) const {
  for (const auto& tensor : op->impl()->InputsTensor()) {
    if (!tensor->IsConstTensor() && tensor->GetId() != (uint32_t)-1 &&
        (tensor_map_.end() == tensor_map_.find(tensor))) {
      return false;
    }
  }
  return true;
}

bool BatchFuseContext::IsReadyForGapInfer(
    const std::shared_ptr<vx::Operation>& op) const {
  for (const auto& tensor : op->impl()->InputsTensor()) {
    if (!tensor->IsConstTensor() && tensor->GetId() != (uint32_t)-1 &&
        (gap_infer_shape_map_.end() == gap_infer_shape_map_.find(tensor))) {
      return false;
    }
  }
  return true;
}

bool BatchFuseContext::IsReadyForCloneGraph(
    const std::shared_ptr<vx::Operation>& op) const {
  for (const auto& tensor : op->impl()->InputsTensor()) {
    if (!tensor->IsConstTensor() && tensor->GetId() != (uint32_t)-1 &&
        (clone_tensor_map_.end() == clone_tensor_map_.find(tensor))) {
      return false;
    }
  }
  return true;
}

void BatchFuseContext::UpdateTensorMap(
    const std::shared_ptr<vx::Tensor>& t_src,
    const std::shared_ptr<vx::Tensor>& t_batch_fuse) {
  tensor_map_[t_src] = t_batch_fuse;
}

void BatchFuseContext::UpdateCloneTensorMap(
    const std::shared_ptr<vx::Tensor>& t_src,
    const std::shared_ptr<vx::Tensor>& t_clone_graph) {
  clone_tensor_map_[t_src] = t_clone_graph;
}

void BatchFuseContext::UpdateForwardGap(
    const std::shared_ptr<vx::Tensor>& t_src,
    const std::array<uint32_t, 2>& pad) {
  forward_gap_map_[t_src] = pad;
}

void BatchFuseContext::UpdateGapInferShape(
    const std::shared_ptr<vx::Tensor>& t_src, const ShapeType& shape) {
  gap_infer_shape_map_[t_src] = shape;
}

void BatchFuseContext::UpdateProportion(
    const std::shared_ptr<vx::Tensor>& t_src, float pro) {
  proportion_map_[t_src] = pro;
}

void BatchFuseContext::UpdatePermAxisMap(
    const std::shared_ptr<vx::Tensor>& t_src,
    const std::vector<uint32_t>& p_a_m) {
  perm_axis_map_[t_src] = p_a_m;
}

std::shared_ptr<vx::Tensor> BatchFuseContext::GetMapedTensor(
    const std::shared_ptr<vx::Tensor>& t_src) const {
  auto it = tensor_map_.find(t_src);
  if (it != tensor_map_.end()) {
    return it->second;
  } else {
    VSILOGE("Tensor has not beed inserted in tensor map.");
    assert(false);
  }

  return nullptr;
}

std::shared_ptr<vx::Tensor> BatchFuseContext::GetCloneMapedTensor(
    const std::shared_ptr<vx::Tensor>& t_src) const {
  auto it = clone_tensor_map_.find(t_src);
  if (it != clone_tensor_map_.end()) {
    return it->second;
  } else {
    VSILOGE("Tensor has not beed inserted in tensor map.");
    assert(false);
  }

  return nullptr;
}

std::array<uint32_t, 2> BatchFuseContext::GetForwardGap(
    const std::shared_ptr<vx::Tensor>& t_src) const {
  auto it = forward_gap_map_.find(t_src);
  if (it != forward_gap_map_.end()) {
    return it->second;
  } else {
    VSILOGE("Tensor has not beed inserted in forward gap map.");
    assert(false);
  }

  return {};
}

ShapeType BatchFuseContext::GetGapInferShape(
    const std::shared_ptr<vx::Tensor>& t_src) const {
  auto it = gap_infer_shape_map_.find(t_src);
  if (it != gap_infer_shape_map_.end()) {
    return it->second;
  } else {
    VSILOGE("Tensor has not beed inserted in pad infer shape map.");
    assert(false);
  }

  return {};
}

float BatchFuseContext::GetProportion(
    const std::shared_ptr<vx::Tensor>& t_src) const {
  auto it = proportion_map_.find(t_src);
  if (it != proportion_map_.end()) {
    return it->second;
  } else {
    VSILOGE("Tensor has not beed inserted in proportion map.");
    assert(false);
  }

  return 0;
}

std::vector<uint32_t> BatchFuseContext::GetPermAxisMap(
    const std::shared_ptr<vx::Tensor>& t_src) const {
  auto it = perm_axis_map_.find(t_src);
  if (it != perm_axis_map_.end()) {
    return it->second;
  } else {
    VSILOGE("Tensor has not beed inserted in permute axis map.");
    assert(false);
  }

  return {};
}

#define REGIST_CLONE_GRAPH(op_idx, name, reduce_idx)          \
  case reduce_idx: {                                          \
    auto op_batch_fuse = std::make_shared<name##BatchFuse>(); \
    BatchFuseHandler_[op_idx][reduce_idx] = op_batch_fuse;    \
    op_batch_fuse->CloneGraph(next_tensors, op, ctx);         \
    break;                                                    \
  }

#define REGIST_REDUCE_CLONE_GRAPH(op_idx)                                    \
  case op_idx: {                                                             \
    auto reduce_type = op->impl()->node()->nn_param.reduce.type;             \
    switch (reduce_type) {                                                   \
      REGIST_CLONE_GRAPH(VSI_NN_OP_REDUCE, ReduceMean, VSI_NN_REDUCE_MEAN);  \
      REGIST_CLONE_GRAPH(VSI_NN_OP_REDUCE, ReduceMax, VSI_NN_REDUCE_MAX);    \
      REGIST_CLONE_GRAPH(VSI_NN_OP_REDUCE, ReduceMin, VSI_NN_REDUCE_MIN);    \
      REGIST_CLONE_GRAPH(VSI_NN_OP_REDUCE, ReduceProd, VSI_NN_REDUCE_PROD);  \
      REGIST_CLONE_GRAPH(VSI_NN_OP_REDUCE, ReduceAny, VSI_NN_REDUCE_ANY);    \
      REGIST_CLONE_GRAPH(VSI_NN_OP_REDUCE, ReduceSum, VSI_NN_REDUCE_SUM);    \
      default:                                                               \
        VSILOGW("Op %d: Default clone graph pass for reduce.", reduce_type); \
        assert(false);                                                       \
    }                                                                        \
    break;                                                                   \
  }

std::vector<std::shared_ptr<vx::Tensor>> HandleCloneGraph(
    std::shared_ptr<batch_fuse_impl::BatchFuseContext>& ctx,
    const std::shared_ptr<vx::Operation>& op,
    std::map<int32_t, std::map<int32_t, std::shared_ptr<OpBatchFuse>>>&
        BatchFuseHandler_) {
  auto op_id = op->impl()->kind_;
  std::vector<std::shared_ptr<vx::Tensor>> next_tensors;
  switch (op_id) {
    REGIST_CLONE_GRAPH(VSI_NN_OP_CONV2D, Conv2d, VSI_NN_OP_CONV2D);
    REGIST_CLONE_GRAPH(VSI_NN_OP_PAD, Pad, VSI_NN_OP_PAD);
    REGIST_CLONE_GRAPH(VSI_NN_OP_RELU, Relu, VSI_NN_OP_RELU);
    REGIST_CLONE_GRAPH(VSI_NN_OP_ADD, Add, VSI_NN_OP_ADD);
    REGIST_CLONE_GRAPH(VSI_NN_OP_POOL, Pool2d, VSI_NN_OP_POOL);
    REGIST_REDUCE_CLONE_GRAPH(VSI_NN_OP_REDUCE);
    REGIST_CLONE_GRAPH(VSI_NN_OP_PERMUTE, Transpose, VSI_NN_OP_PERMUTE);
    REGIST_CLONE_GRAPH(VSI_NN_OP_RESHAPE, Reshape, VSI_NN_OP_RESHAPE);
    REGIST_CLONE_GRAPH(VSI_NN_OP_RESHAPE2, Reshape, VSI_NN_OP_RESHAPE2);
    REGIST_CLONE_GRAPH(VSI_NN_OP_CONCAT, Concat, VSI_NN_OP_CONCAT);
  }
  return next_tensors;
}

std::vector<std::shared_ptr<vx::Tensor>> HandleBatchFuse(
    std::shared_ptr<batch_fuse_impl::BatchFuseContext>& ctx,
    const std::shared_ptr<vx::Operation>& op,
    std::map<int32_t, std::map<int32_t, std::shared_ptr<OpBatchFuse>>>&
        BatchFuseHandler_) {
  ctx->MarkVisited(op);
  std::vector<std::shared_ptr<vx::Tensor>> next_tensors;
  auto op_id = op->impl()->kind_;
  int32_t reduce_id = op_id;
  if (op_id == 59) {
    reduce_id = op->impl()->node()->nn_param.reduce.type;
  }
  BatchFuseHandler_[op_id][reduce_id]->OnInputs(next_tensors, op, ctx);
  BatchFuseHandler_[op_id][reduce_id]->OnOutputs(next_tensors, op, ctx);
  return next_tensors;
}

std::pair<std::vector<std::shared_ptr<vx::Tensor>>, bool> HandlePadInfernce(
    std::shared_ptr<batch_fuse_impl::BatchFuseContext>& ctx,
    const std::shared_ptr<vx::Operation>& op, bool Forward,
    std::map<int32_t, std::map<int32_t, std::shared_ptr<OpBatchFuse>>>&
        BatchFuseHandler_) {
  std::vector<std::shared_ptr<vx::Tensor>> next_tensors;
  bool need_backward = false;
  auto op_id = op->impl()->kind_;
  int32_t reduce_id = op_id;
  if (op_id == 59) {
    reduce_id = op->impl()->node()->nn_param.reduce.type;
  }
  if (Forward)
    need_backward = BatchFuseHandler_[op_id][reduce_id]->GapForwardInference(
        next_tensors, op, ctx);
  else
    need_backward = BatchFuseHandler_[op_id][reduce_id]->GapBackwardInference(
        next_tensors, op, ctx);
  return std::make_pair(next_tensors, need_backward);
}

}  // namespace batch_fuse_impl

std::pair<
    /*graph after batch fused*/
    std::shared_ptr<vx::Graph>,
    /* tensor mapping between source graph and graph after batch fused*/
    std::map<std::shared_ptr<vx::Tensor>, std::shared_ptr<vx::Tensor>>>
BatchFuse(const std::shared_ptr<vx::Graph>& src_graph,
          std::shared_ptr<vx::Context>& ctx, uint32_t fake_batch) {
  std::shared_ptr<vx::Graph> batch_fuse_graph = ctx->CreateGraph();
  std::shared_ptr<vx::Graph> clone_batch_graph = ctx->CreateGraph();
  auto batch_fuse_ctx = std::make_shared<batch_fuse_impl::BatchFuseContext>(
      src_graph, batch_fuse_graph, clone_batch_graph);
  batch_fuse_ctx->SetFakeBatch(fake_batch);
  batch_fuse_ctx->SetBatchAxis(3);
  batch_fuse_ctx->SetFuseAxes({1, 2});
  batch_fuse_ctx->SetChannelAxis(0);

  std::map<int32_t, std::map<int32_t, std::shared_ptr<OpBatchFuse>>>
      BatchFuseHandler_;

  std::map<std::shared_ptr<vx::Tensor>, std::shared_ptr<vx::Tensor>>
      graph_io_map;

  // For batch fuse from cloned graph
  std::deque<std::shared_ptr<vx::Tensor>> tensor_queue;

  // For gap inference
  std::deque<std::shared_ptr<vx::Tensor>> tensor_queue_pad_forward;
  std::deque<std::shared_ptr<vx::Tensor>> tensor_queue_pad_backward;

  //For clone graph from source graph
  std::deque<std::shared_ptr<vx::Tensor>> tensor_clone_queue;

  // The graph inputs and outputs need to be maped between source graph, clone graph adn batch fuse graph
  // tensor_in_src -> tensor_in_clone -> tensor_in_fuse

  auto graph_inputs = src_graph->InputsTensor();
  auto graph_outputs = src_graph->OutputsTensor();

  if (fake_batch == 1) {
    for (const auto& graph_input : graph_inputs) {
      graph_io_map[graph_input] = graph_input;
    }
    for (const auto& graph_output : graph_outputs) {
      graph_io_map[graph_output] = graph_output;
    }
    return std::make_pair(src_graph, graph_io_map);
  }

  for (const auto& t_src : graph_inputs) {
    //Initialize clone graph's tensor map
    auto input = clone_batch_graph->CreateTensor(t_src->GetSpec());
    batch_fuse_ctx->UpdateCloneTensorMap(t_src, input);

    tensor_clone_queue.push_back(t_src);
  }

  auto const_inputs = src_graph->GetConstantInputs();
  for (auto const_in : const_inputs) {
    //Initialize clone graph's constant tensor map

    //Copy data from tensor_in_src to tensor_in_clone
    std::vector<uint8_t> tmp(const_in->GetSpec().GetByteSize());
    const_in->CopyDataFromTensor(tmp.data());
    auto input =
        clone_batch_graph->CreateTensor(const_in->GetSpec(), tmp.data());
    batch_fuse_ctx->UpdateCloneTensorMap(const_in, input);
    tensor_clone_queue.push_back(const_in);
  }

  // Clone graph to multi batch
  while (!tensor_clone_queue.empty()) {
    auto tensor = tensor_clone_queue.front();
    tensor_clone_queue.pop_front();
    const auto& consumers = src_graph->GetConsumersOp(tensor);
    for (const auto& op : consumers) {
      if (op->impl()->kind_ != -1 && batch_fuse_ctx->IsReadyForCloneGraph(op)) {
        auto next_tensors = batch_fuse_impl::HandleCloneGraph(
            batch_fuse_ctx, op, BatchFuseHandler_);
        for (const auto& t : next_tensors) {
          tensor_clone_queue.push_back(t);
        }
      }
    }
  }

  auto graph_inputs_clone = clone_batch_graph->InputsTensor();
  for (const auto& t_src : graph_inputs_clone) {
    //Initialize batch fuse graph's tensor map
    auto input = batch_fuse_graph->CreateTensor(t_src->GetSpec());
    batch_fuse_ctx->UpdateTensorMap(t_src, input);
    batch_fuse_ctx->UpdateGapInferShape(t_src, t_src->GetShape());
    batch_fuse_ctx->UpdateForwardGap(t_src, {0, 0});

    tensor_queue.push_back(t_src);
    tensor_queue_pad_forward.push_back(t_src);
  }

  auto const_inputs_clone = clone_batch_graph->GetConstantInputs();
  for (auto const_in : const_inputs_clone) {
    //Initialize batch fuse graph's constant tensor map

    //Copy data from tensor_in_clone to tensor_in_fuse
    std::vector<uint8_t> tmp(const_in->GetSpec().GetByteSize());
    const_in->CopyDataFromTensor(tmp.data());
    auto input =
        batch_fuse_graph->CreateTensor(const_in->GetSpec(), tmp.data());
    batch_fuse_ctx->UpdateTensorMap(const_in, input);
    batch_fuse_ctx->UpdateGapInferShape(const_in, const_in->GetShape());
    batch_fuse_ctx->UpdateForwardGap(const_in, {0, 0});

    tensor_queue.push_back(const_in);
    tensor_queue_pad_forward.push_back(const_in);
  }

  while (!tensor_queue_pad_forward.empty()) {
    //bfs
    auto tensor = tensor_queue_pad_forward.front();
    tensor_queue_pad_forward.pop_front();

    const auto& consumers = clone_batch_graph->GetConsumersOp(tensor);
    for (const auto& op : consumers) {
      if (op->impl()->kind_ != -1 && batch_fuse_ctx->IsReadyForGapInfer(op)) {
        auto tensors_pad_forward_infer = batch_fuse_impl::HandlePadInfernce(
            batch_fuse_ctx, op, true, BatchFuseHandler_);  //forward gap infer
        if (tensors_pad_forward_infer.second) {
          //need_backward is true
          tensor_queue_pad_backward.push_back(tensor);
          while (!tensor_queue_pad_backward.empty()) {
            auto pad_tensor = tensor_queue_pad_backward.back();
            tensor_queue_pad_backward.pop_back();
            if (pad_tensor->GetSpec().attr_ == vx::TensorAttribute::INPUT) {
              // Input tensor has no producer, so push back current op's output tensor to continue to forward inference
              for (const auto& t : tensors_pad_forward_infer.first) {
                tensor_queue_pad_forward.push_back(t);  //next_tensors
              }
            } else {
              // Current tensor has producer
              const auto& producer =
                  clone_batch_graph->GetProducerOp(pad_tensor);

              // Start backward inference
              auto tensors_pad_backward_infer =
                  batch_fuse_impl::HandlePadInfernce(batch_fuse_ctx, producer,
                                                     false, BatchFuseHandler_);

              for (const auto& t : tensors_pad_backward_infer.first) {
                tensor_queue_pad_backward.push_back(t);  //former_tesnors
              }
              if (tensors_pad_backward_infer.first.empty()) {
                // Stop backward inference
                for (const auto& t : tensors_pad_forward_infer.first) {
                  // Start forward inference
                  tensor_queue_pad_forward.push_back(t);  //next_tensors
                }
              }
            }
          }
        } else {
          for (const auto& t : tensors_pad_forward_infer.first) {
            // Continue to forward inference
            tensor_queue_pad_forward.push_back(t);  //next_tensors
          }
        }
      }
    }
  }

  while (!tensor_queue.empty()) {
    auto tensor = tensor_queue.front();
    tensor_queue.pop_front();
    const auto& consumers = clone_batch_graph->GetConsumersOp(tensor);
    for (const auto& op : consumers) {
      if (!batch_fuse_ctx->IsVisited(op) && op->impl()->kind_ != -1 &&
          batch_fuse_ctx->IsReadyForBatchFuse(op)) {
        auto next_tensors = batch_fuse_impl::HandleBatchFuse(batch_fuse_ctx, op,
                                                             BatchFuseHandler_);
        for (const auto& t : next_tensors) {
          tensor_queue.push_back(t);
        }
      }
    }
  }

  // tensor_in_src -> tensor_in_clone -> tensor_in_fuse
  for (const auto& graph_input : graph_inputs) {
    auto clone_graph_input = batch_fuse_ctx->GetCloneMapedTensor(graph_input);
    auto batch_fuse_input = batch_fuse_ctx->GetMapedTensor(clone_graph_input);

    // tensor_in_src -> tensor_in_fuse
    graph_io_map[graph_input] = batch_fuse_input;
  }
  for (const auto& graph_output : graph_outputs) {
    auto clone_graph_output = batch_fuse_ctx->GetCloneMapedTensor(graph_output);
    auto batch_fuse_output = batch_fuse_ctx->GetMapedTensor(clone_graph_output);

    // tensor_in_src -> tensor_in_fuse
    graph_io_map[graph_output] = batch_fuse_output;
  }
  return std::make_pair(batch_fuse_graph, graph_io_map);
}
}  // namespace fuse
}  // namespace tim
