// DAO: this project's member functions of VulkanCommandProcessor, next to the
// upstream file (as gpu/d3d12/dao_command_processor.cc for Direct3D 12).
#include "xenia/gpu/vulkan/vulkan_command_processor.h"

#include <cstring>

#include "xenia/base/logging.h"
#include "xenia/gpu/vulkan/vulkan_shared_memory.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace gpu {
namespace vulkan {

bool VulkanCommandProcessor::DaoReadGpuWrittenMemory(
    uint32_t address, uint32_t length, uint32_t pixel_size_log2,
    std::vector<uint8_t>& out) {
  if (!length || !shared_memory_->IsRangeWrittenByGpu(address, length)) {
    return false;
  }
  // Resolves at the draw resolution scale are kept in the scaled buffer, not
  // read back here (no resolution scaling on this backend yet).
  if (texture_cache_->IsDrawResolutionScaled()) {
    return false;
  }
  if (!BeginSubmission(true)) {
    return false;
  }
  const VkBuffer readback_buffer = RequestReadbackBuffer(length);
  if (readback_buffer == VK_NULL_HANDLE) {
    return false;
  }
  const ui::vulkan::VulkanDevice* const vulkan_device = GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  shared_memory_->Use(VulkanSharedMemory::Usage::kRead);
  VkBufferCopy copy_region;
  copy_region.srcOffset = address;
  copy_region.dstOffset = 0;
  copy_region.size = length;
  deferred_command_buffer_.CmdVkCopyBuffer(shared_memory_->buffer(),
                                           readback_buffer, 1, &copy_region);
  // The fence alone doesn't make the copy visible to the host.
  VkBufferMemoryBarrier buffer_memory_barrier;
  buffer_memory_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  buffer_memory_barrier.pNext = nullptr;
  buffer_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  buffer_memory_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  buffer_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  buffer_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  buffer_memory_barrier.buffer = readback_buffer;
  buffer_memory_barrier.offset = 0;
  buffer_memory_barrier.size = length;
  deferred_command_buffer_.CmdVkPipelineBarrier(
      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr,
      1, &buffer_memory_barrier, 0, nullptr);

  // Submits and waits for the GPU.
  if (!AwaitAllQueueOperationsCompletion()) {
    XELOGE("VulkanCommandProcessor: Failed to wait for the GPU memory read-back");
    return false;
  }
  void* mapping = nullptr;
  if (dfn.vkMapMemory(device, memexport_readback_buffer_memory_, 0, length, 0,
                      &mapping) != VK_SUCCESS ||
      !mapping) {
    XELOGE("VulkanCommandProcessor: Failed to map the GPU memory read-back buffer");
    return false;
  }
  out.resize(length);
  std::memcpy(out.data(), mapping, length);
  dfn.vkUnmapMemory(device, memexport_readback_buffer_memory_);
  return true;
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
