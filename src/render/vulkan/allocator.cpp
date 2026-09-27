#include "render/vulkan/allocator.hpp"

#include "render/vulkan/context.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdio>
#include <print>
#include <utility>

namespace vulkan {

// Types that need extensions or special handling Blossom doesn't use.
static constexpr VkMemoryPropertyFlags UNSUPPORTED_FLAGS =
    VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT | VK_MEMORY_PROPERTY_PROTECTED_BIT |
    VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD |
    VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD;

// Vulkan guarantees alignments are powers of two.
static constexpr VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

static constexpr VkDeviceSize align_down(VkDeviceSize value, VkDeviceSize alignment) {
  return value & ~(alignment - 1);
}

// ---------------------------------------------------------------------------
// Memory type selection
// ---------------------------------------------------------------------------

struct UsageFlags {
  VkMemoryPropertyFlags required;
  VkMemoryPropertyFlags preferred;
  VkMemoryPropertyFlags avoided;
};

static UsageFlags usage_flags(MemoryUsage usage) {
  switch (usage) {
  case MemoryUsage::GpuOnly:
    // Discrete: VRAM without the small BAR window. Integrated: any shared type.
    return {0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT};
  case MemoryUsage::Upload:
    // Write-combined system memory on discrete, keeping VRAM free.
    return {VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT};
  case MemoryUsage::Readback:
    // Cached so CPU reads are fast; non-coherent types are handled by invalidate().
    return {VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT, 0};
  }
  return {};
}

static int score_memory_type(VkMemoryPropertyFlags flags, const UsageFlags& usage) {
  return std::popcount(flags & usage.preferred) - std::popcount(flags & usage.avoided);
}

// Allowed memory types for the request, best first. Later entries are
// fallbacks when a heap is out of memory.
static std::vector<uint32_t> candidate_types(const Allocator& allocator,
                                             uint32_t type_bits, MemoryUsage usage) {
  UsageFlags flags = usage_flags(usage);
  const VkPhysicalDeviceMemoryProperties& memory = allocator.memory_properties;

  std::vector<uint32_t> types;
  for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
    VkMemoryPropertyFlags type_flags = memory.memoryTypes[i].propertyFlags;
    if ((type_bits & (1u << i)) && (type_flags & flags.required) == flags.required &&
        !(type_flags & UNSUPPORTED_FLAGS)) {
      types.push_back(i);
    }
  }

  // Stable: drivers list faster types first, so ties keep that order.
  std::stable_sort(types.begin(), types.end(), [&](uint32_t a, uint32_t b) {
    return score_memory_type(memory.memoryTypes[a].propertyFlags, flags) >
           score_memory_type(memory.memoryTypes[b].propertyFlags, flags);
  });
  return types;
}

static VkMemoryPropertyFlags type_flags(const Allocator& allocator, uint32_t type) {
  return allocator.memory_properties.memoryTypes[type].propertyFlags;
}

static bool is_non_coherent(VkMemoryPropertyFlags flags) {
  return (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
         !(flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

// One eighth of the heap keeps small heaps (BAR windows) from being eaten by
// a single block.
static VkDeviceSize block_size_for(const Allocator& allocator, uint32_t type) {
  const VkPhysicalDeviceMemoryProperties& memory = allocator.memory_properties;
  VkDeviceSize heap_size = memory.memoryHeaps[memory.memoryTypes[type].heapIndex].size;
  VkDeviceSize size = std::min(allocator.block_size, heap_size / 8);
  if (allocator.max_allocation_size > 0) {
    size = std::min(size, allocator.max_allocation_size);
  }
  return size;
}

// ---------------------------------------------------------------------------
// Free list
// ---------------------------------------------------------------------------

// First fit. Any alignment gap in front of the allocation and any space left
// after it stay in the free list.
static bool take_range(MemoryBlock& block, VkDeviceSize size, VkDeviceSize alignment,
                       VkDeviceSize& offset) {
  for (size_t i = 0; i < block.free_ranges.size(); ++i) {
    FreeRange range = block.free_ranges[i];
    VkDeviceSize start = align_up(range.offset, alignment);
    VkDeviceSize end = start + size;
    VkDeviceSize range_end = range.offset + range.size;
    if (end > range_end) {
      continue;
    }

    auto it = block.free_ranges.erase(block.free_ranges.begin() +
                                      static_cast<std::ptrdiff_t>(i));
    if (end < range_end) {
      it = block.free_ranges.insert(it, {end, range_end - end});
    }
    if (start > range.offset) {
      block.free_ranges.insert(it, {range.offset, start - range.offset});
    }

    offset = start;
    return true;
  }
  return false;
}

static void return_range(MemoryBlock& block, VkDeviceSize offset, VkDeviceSize size) {
  auto it = std::lower_bound(
      block.free_ranges.begin(), block.free_ranges.end(), offset,
      [](const FreeRange& range, VkDeviceSize value) { return range.offset < value; });
  it = block.free_ranges.insert(it, {offset, size});

  auto next = it + 1;
  if (next != block.free_ranges.end() && it->offset + it->size == next->offset) {
    it->size += next->size;
    block.free_ranges.erase(next);
  }
  if (it != block.free_ranges.begin()) {
    auto previous = it - 1;
    if (previous->offset + previous->size == it->offset) {
      previous->size += it->size;
      block.free_ranges.erase(it);
    }
  }
}

// ---------------------------------------------------------------------------
// Blocks
// ---------------------------------------------------------------------------

static bool create_block(Allocator& allocator, const Context& context, uint32_t type,
                         ResourceKind kind, VkDeviceSize size, bool isDedicated,
                         VkBuffer buffer, VkImage image, uint32_t& index) {
  VkMemoryDedicatedAllocateInfo dedicated_info{};
  dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
  dedicated_info.buffer = buffer;
  dedicated_info.image = image;

  VkMemoryAllocateInfo allocate_info{};
  allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocate_info.allocationSize = size;
  allocate_info.memoryTypeIndex = type;
  if (isDedicated && (buffer != VK_NULL_HANDLE || image != VK_NULL_HANDLE)) {
    allocate_info.pNext = &dedicated_info;
  }

  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkResult result = vkAllocateMemory(context.device, &allocate_info, nullptr, &memory);
  if (result != VK_SUCCESS) {
    // Out of memory is expected here; allocate() falls back to the next type.
    if (result != VK_ERROR_OUT_OF_DEVICE_MEMORY && result != VK_ERROR_OUT_OF_HOST_MEMORY) {
      std::println(stderr, "[allocator] vkAllocateMemory failed: {}",
                   static_cast<int>(result));
    }
    return false;
  }

  void* mapped = nullptr;
  if (type_flags(allocator, type) & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
    result = vkMapMemory(context.device, memory, 0, VK_WHOLE_SIZE, 0, &mapped);
    if (result != VK_SUCCESS) {
      std::println(stderr, "[allocator] vkMapMemory failed: {}", static_cast<int>(result));
      vkFreeMemory(context.device, memory, nullptr);
      return false;
    }
  }

  MemoryBlock block{};
  block.memory = memory;
  block.size = size;
  block.mapped = mapped;
  block.memory_type = type;
  block.kind = kind;
  block.isDedicated = isDedicated;
  if (!isDedicated) {
    block.free_ranges.push_back({0, size});
  }

  auto slot = std::find_if(allocator.blocks.begin(), allocator.blocks.end(),
                           [](const MemoryBlock& b) { return b.memory == VK_NULL_HANDLE; });
  if (slot != allocator.blocks.end()) {
    *slot = std::move(block);
    index = static_cast<uint32_t>(slot - allocator.blocks.begin());
  } else {
    allocator.blocks.push_back(std::move(block));
    index = static_cast<uint32_t>(allocator.blocks.size() - 1);
  }
  return true;
}

static void destroy_block(Allocator& allocator, const Context& context, uint32_t index) {
  MemoryBlock& block = allocator.blocks[index];
  // Freeing also unmaps.
  if (block.memory != VK_NULL_HANDLE) {
    vkFreeMemory(context.device, block.memory, nullptr);
  }
  block = {};
}

static void fill_allocation(Allocator& allocator, uint32_t index, VkDeviceSize offset,
                            VkDeviceSize size, Allocation& allocation) {
  MemoryBlock& block = allocator.blocks[index];
  block.used += size;
  allocator.allocation_count += 1;

  allocation.memory = block.memory;
  allocation.offset = offset;
  allocation.size = size;
  allocation.mapped = block.mapped ? static_cast<char*>(block.mapped) + offset : nullptr;
  allocation.block = index;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void create_allocator(Allocator& allocator, const Context& context,
                      const AllocatorConfig& config) {
  allocator = {};
  allocator.block_size = config.block_size;
  allocator.non_coherent_atom_size = context.properties.limits.nonCoherentAtomSize;
  vkGetPhysicalDeviceMemoryProperties(context.physical_device, &allocator.memory_properties);

  VkPhysicalDeviceVulkan11Properties vulkan11{};
  vulkan11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
  VkPhysicalDeviceProperties2 properties{};
  properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  properties.pNext = &vulkan11;
  vkGetPhysicalDeviceProperties2(context.physical_device, &properties);
  allocator.max_allocation_size = vulkan11.maxMemoryAllocationSize;
}

void destroy_allocator(Allocator& allocator, const Context& context) {
  if (allocator.allocation_count > 0) {
    std::println(stderr, "[allocator] destroyed with {} live allocations",
                 allocator.allocation_count);
  }
  for (uint32_t i = 0; i < allocator.blocks.size(); ++i) {
    destroy_block(allocator, context, i);
  }
  allocator = {};
}

bool allocate(Allocator& allocator, const Context& context,
              const AllocationRequest& request, Allocation& allocation) {
  allocation = {};

  std::vector<uint32_t> types = candidate_types(
      allocator, request.requirements.memoryTypeBits, request.usage);
  if (types.empty()) {
    std::println(stderr, "[allocator] no memory type fits the request");
    return false;
  }

  for (uint32_t type : types) {
    VkDeviceSize size = request.requirements.size;
    VkDeviceSize alignment = request.requirements.alignment;
    // Keep flush/invalidate ranges from touching a neighbour's memory.
    if (is_non_coherent(type_flags(allocator, type))) {
      alignment = std::max(alignment, allocator.non_coherent_atom_size);
      size = align_up(size, allocator.non_coherent_atom_size);
    }

    VkDeviceSize block_size = block_size_for(allocator, type);
    uint32_t index = NO_BLOCK;

    if (request.isDedicated || size > block_size / 2) {
      if (create_block(allocator, context, type, request.kind, size, true, request.buffer,
                       request.image, index)) {
        fill_allocation(allocator, index, 0, size, allocation);
        return true;
      }
      continue;
    }

    for (uint32_t i = 0; i < allocator.blocks.size(); ++i) {
      MemoryBlock& block = allocator.blocks[i];
      VkDeviceSize offset = 0;
      if (block.memory != VK_NULL_HANDLE && !block.isDedicated &&
          block.memory_type == type && block.kind == request.kind &&
          take_range(block, size, alignment, offset)) {
        fill_allocation(allocator, i, offset, size, allocation);
        return true;
      }
    }

    if (create_block(allocator, context, type, request.kind, block_size, false,
                     VK_NULL_HANDLE, VK_NULL_HANDLE, index)) {
      VkDeviceSize offset = 0;
      take_range(allocator.blocks[index], size, alignment, offset);
      fill_allocation(allocator, index, offset, size, allocation);
      return true;
    }
  }

  std::println(stderr, "[allocator] out of memory for a {} byte request",
               request.requirements.size);
  return false;
}

void release(Allocator& allocator, const Context& context, Allocation& allocation) {
  if (allocation.block == NO_BLOCK) {
    return;
  }

  MemoryBlock& block = allocator.blocks[allocation.block];
  block.used -= allocation.size;
  allocator.allocation_count -= 1;

  if (block.isDedicated) {
    destroy_block(allocator, context, allocation.block);
  } else {
    return_range(block, allocation.offset, allocation.size);
    if (block.used == 0) {
      destroy_block(allocator, context, allocation.block);
    }
  }
  allocation = {};
}

bool allocate_buffer(Allocator& allocator, const Context& context, VkBuffer buffer,
                     MemoryUsage usage, Allocation& allocation) {
  VkBufferMemoryRequirementsInfo2 info{};
  info.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2;
  info.buffer = buffer;

  VkMemoryDedicatedRequirements dedicated{};
  dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
  VkMemoryRequirements2 requirements{};
  requirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
  requirements.pNext = &dedicated;
  vkGetBufferMemoryRequirements2(context.device, &info, &requirements);

  AllocationRequest request{};
  request.requirements = requirements.memoryRequirements;
  request.usage = usage;
  request.kind = ResourceKind::Linear;
  request.isDedicated =
      dedicated.prefersDedicatedAllocation || dedicated.requiresDedicatedAllocation;
  request.buffer = buffer;

  if (!allocate(allocator, context, request, allocation)) {
    return false;
  }

  VkResult result =
      vkBindBufferMemory(context.device, buffer, allocation.memory, allocation.offset);
  if (result != VK_SUCCESS) {
    std::println(stderr, "[allocator] vkBindBufferMemory failed: {}",
                 static_cast<int>(result));
    release(allocator, context, allocation);
    return false;
  }
  return true;
}

bool allocate_image(Allocator& allocator, const Context& context, VkImage image,
                    MemoryUsage usage, ResourceKind kind, Allocation& allocation) {
  VkImageMemoryRequirementsInfo2 info{};
  info.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
  info.image = image;

  VkMemoryDedicatedRequirements dedicated{};
  dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
  VkMemoryRequirements2 requirements{};
  requirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
  requirements.pNext = &dedicated;
  vkGetImageMemoryRequirements2(context.device, &info, &requirements);

  AllocationRequest request{};
  request.requirements = requirements.memoryRequirements;
  request.usage = usage;
  request.kind = kind;
  request.isDedicated =
      dedicated.prefersDedicatedAllocation || dedicated.requiresDedicatedAllocation;
  request.image = image;

  if (!allocate(allocator, context, request, allocation)) {
    return false;
  }

  VkResult result =
      vkBindImageMemory(context.device, image, allocation.memory, allocation.offset);
  if (result != VK_SUCCESS) {
    std::println(stderr, "[allocator] vkBindImageMemory failed: {}",
                 static_cast<int>(result));
    release(allocator, context, allocation);
    return false;
  }
  return true;
}

static bool non_coherent_range(const Allocator& allocator, const Allocation& allocation,
                               VkMappedMemoryRange& range) {
  if (allocation.block == NO_BLOCK) {
    return false;
  }
  const MemoryBlock& block = allocator.blocks[allocation.block];
  if (!is_non_coherent(type_flags(allocator, block.memory_type))) {
    return false;
  }

  VkDeviceSize atom = allocator.non_coherent_atom_size;
  VkDeviceSize begin = align_down(allocation.offset, atom);
  VkDeviceSize end = std::min(align_up(allocation.offset + allocation.size, atom), block.size);

  range = {};
  range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  range.memory = block.memory;
  range.offset = begin;
  range.size = end - begin;
  return true;
}

void flush(const Allocator& allocator, const Context& context,
           const Allocation& allocation) {
  VkMappedMemoryRange range{};
  if (non_coherent_range(allocator, allocation, range)) {
    vkFlushMappedMemoryRanges(context.device, 1, &range);
  }
}

void invalidate(const Allocator& allocator, const Context& context,
                const Allocation& allocation) {
  VkMappedMemoryRange range{};
  if (non_coherent_range(allocator, allocation, range)) {
    vkInvalidateMappedMemoryRanges(context.device, 1, &range);
  }
}

AllocatorStats allocator_stats(const Allocator& allocator) {
  AllocatorStats stats{};
  stats.allocation_count = allocator.allocation_count;
  for (const MemoryBlock& block : allocator.blocks) {
    if (block.memory == VK_NULL_HANDLE) {
      continue;
    }
    stats.block_count += 1;
    stats.dedicated_count += block.isDedicated ? 1 : 0;
    stats.reserved_bytes += block.size;
    stats.used_bytes += block.used;
  }
  return stats;
}

} // namespace vulkan
