#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace vulkan {

struct Context;

inline constexpr uint32_t NO_BLOCK = UINT32_MAX;
inline constexpr VkDeviceSize DEFAULT_BLOCK_SIZE = 64ull * 1024 * 1024;

// What the memory is for. The allocator turns this into a memory type, so the
// same request lands in VRAM on a discrete GPU and in shared memory on an
// integrated one.
enum class MemoryUsage {
  // Only the GPU reads and writes it: textures, meshes, render targets.
  GpuOnly,
  // The CPU writes it and the GPU reads it: staging buffers, per-frame data.
  Upload,
  // The GPU writes it and the CPU reads it: query results, screenshots.
  Readback,
};

// Buffers and linear-tiling images must be kept apart from optimal-tiling
// images (bufferImageGranularity), so each kind gets its own blocks.
enum class ResourceKind {
  Linear,
  Optimal,
};

struct AllocatorConfig {
  // Upper bound. Small heaps (e.g. a 256 MiB BAR window) get smaller blocks.
  VkDeviceSize block_size = DEFAULT_BLOCK_SIZE;
};

struct FreeRange {
  VkDeviceSize offset = 0;
  VkDeviceSize size = 0;
};

// One vkAllocateMemory. Normal blocks hand out ranges from `free_ranges`;
// dedicated blocks hold exactly one resource.
struct MemoryBlock {
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
  VkDeviceSize used = 0;
  // Mapped once for the block's lifetime when the memory is host visible.
  void* mapped = nullptr;
  uint32_t memory_type = 0;
  ResourceKind kind = ResourceKind::Linear;
  bool isDedicated = false;
  // Sorted by offset. Adjacent ranges are always merged.
  std::vector<FreeRange> free_ranges;
};

// A range inside a block. The allocator owns the memory; holders of an
// Allocation only use it and hand it back with release().
struct Allocation {
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize offset = 0;
  VkDeviceSize size = 0;
  // Null unless the memory is host visible.
  void* mapped = nullptr;
  uint32_t block = NO_BLOCK;
};

struct AllocationRequest {
  VkMemoryRequirements requirements{};
  MemoryUsage usage = MemoryUsage::GpuOnly;
  ResourceKind kind = ResourceKind::Linear;
  // Forces a dedicated allocation. Large requests become dedicated anyway.
  bool isDedicated = false;
  // Attached to dedicated allocations so the driver knows the owner. At most one.
  VkBuffer buffer = VK_NULL_HANDLE;
  VkImage image = VK_NULL_HANDLE;
};

struct AllocatorStats {
  uint32_t block_count = 0;
  uint32_t dedicated_count = 0;
  uint32_t allocation_count = 0;
  // Total size of every VkDeviceMemory the allocator holds.
  VkDeviceSize reserved_bytes = 0;
  // Bytes handed out to allocations.
  VkDeviceSize used_bytes = 0;
};

// Free-list sub-allocator. Not thread safe.
struct Allocator {
  VkPhysicalDeviceMemoryProperties memory_properties{};
  VkDeviceSize block_size = DEFAULT_BLOCK_SIZE;
  VkDeviceSize max_allocation_size = 0;
  VkDeviceSize non_coherent_atom_size = 1;
  // Released blocks keep their slot (memory == VK_NULL_HANDLE) so indices
  // held by allocations stay valid.
  std::vector<MemoryBlock> blocks;
  uint32_t allocation_count = 0;
};

void create_allocator(Allocator& allocator, const Context& context,
                      const AllocatorConfig& config);
// Frees every block. Allocations still held become invalid.
void destroy_allocator(Allocator& allocator, const Context& context);

bool allocate(Allocator& allocator, const Context& context,
              const AllocationRequest& request, Allocation& allocation);
void release(Allocator& allocator, const Context& context, Allocation& allocation);

// Query requirements (including the driver's dedicated preference), allocate,
// and bind. On failure nothing is bound and `allocation` is empty.
bool allocate_buffer(Allocator& allocator, const Context& context, VkBuffer buffer,
                     MemoryUsage usage, Allocation& allocation);
bool allocate_image(Allocator& allocator, const Context& context, VkImage image,
                    MemoryUsage usage, ResourceKind kind, Allocation& allocation);

// Needed only for memory without HOST_COHERENT, and no-ops otherwise.
// flush after CPU writes, invalidate before CPU reads of GPU results.
void flush(const Allocator& allocator, const Context& context,
           const Allocation& allocation);
void invalidate(const Allocator& allocator, const Context& context,
                const Allocation& allocation);

AllocatorStats allocator_stats(const Allocator& allocator);

} // namespace vulkan
