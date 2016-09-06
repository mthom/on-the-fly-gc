#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "atomic_list.hpp"
#include "impl_details.hpp"
#include "gc.hpp"
#include "mutator.hpp"

using namespace std;

namespace otf_gc
{
  inline size_t mutator::binary_log(int sz)
  {
    assert(sz >= 8);

    sz--;
    sz |= sz >> 1;
    sz |= sz >> 2;
    sz |= sz >> 4;
    sz |= sz >> 8;
    sz |= sz >> 16;
    
    sz -= (sz >> 1) & 0x5555555555555555ULL;
    sz = (sz & 0x3333333333333333ULL) + ((sz >> 2) & 0x3333333333333333ULL);
    sz = (sz + (sz >> 4)) & 0x0f0f0f0f0f0f0f0fULL;
    
    return (sz * 0x0101010101010101ULL) >> 56;
  }

  inline impl_details::underlying_header_t
  mutator::create_header(impl_details::underlying_header_t desc)
  {
    using namespace impl_details;
    return static_cast<underlying_header_t>(alloc_color.c) | (desc << color_bits);
  }

  inline bool mutator::transfer_small_blocks_from_collector(size_t power)
  {
    stub_list stubs = gc::collector->small_free_lists[power-3].exchange(nullptr, std::memory_order_relaxed);
    
    if(stubs) {
      fixed_managers[power-3].append(std::move(stubs));
      return true;
    }

    return false;
  }

  inline bool mutator::transfer_large_blocks_from_collector()
  {
    large_block_list blocks = gc::collector->large_free_list.exchange(nullptr, std::memory_order_relaxed);
    
    if(blocks) {
      variable_manager.append(std::move(blocks));
      
      return true;
    }

    return false;
  }

  void* mutator::allocate_small(size_t power, impl_details::underlying_header_t desc)
  {
    void* ptr = fixed_managers[power-3].get_block();

    if(!ptr) {
      if(transfer_small_blocks_from_collector(power))
	ptr = fixed_managers[power-3].get_block();
      
      if(!ptr) {	
	void* blk = fixed_managers[power-3].get_new_block();
	allocation_dump.push_front(blk);
	ptr = blk;
      }
    }

    new(ptr) impl_details::log_ptr_t(nullptr);
    new(reinterpret_cast<header_t*>(reinterpret_cast<std::ptrdiff_t>(ptr) + log_ptr_size))
      impl_details::header_t(create_header(desc));
    
    return ptr;
  }

  void* mutator::allocate_large(size_t power,
				impl_details::underlying_header_t desc,
				size_t num_log_ptrs)
  {
    block_cursor blk_c(variable_manager.get_block(power));

    if(blk_c.null_block()) {
      if(transfer_large_blocks_from_collector())
	blk_c = variable_manager.get_block(power);
      
      if(blk_c.null_block()) {
	blk_c = aligned_alloc(alignof(impl_details::header_t), 1 << power);
	allocation_dump.push_front(reinterpret_cast<void*>(blk_c.start()));
		
	blk_c.size() = power;
	blk_c.split() = 0;		

	variable_manager.push_front_used(reinterpret_cast<void*>(blk_c.start()), power);
      }
    }

    blk_c.num_log_ptrs() = num_log_ptrs;
    blk_c.recalculate();
      
    for(size_t i = 0; i < num_log_ptrs; ++i)
      new(blk_c.log_ptr(i)) impl_details::log_ptr_t(nullptr);
    
    new(blk_c.header()) impl_details::header_t(create_header(desc));
    
    return reinterpret_cast<void*>(blk_c.start());
  }

  stub_list mutator::vacate_small_used_list(size_t i)
  {
    return fixed_managers[i].release_used_list();
  }

  large_block_list mutator::vacate_large_used_list()
  {
    return variable_manager.release_used_list();
  }

  void* mutator::allocate(int raw_sz, impl_details::underlying_header_t desc, size_t num_log_ptrs)
  {
    using namespace impl_details;
    
    if(raw_sz + small_block_metadata_size <= large_obj_threshold) {
      void* p = allocate_small(mutator::binary_log(raw_sz + small_block_metadata_size), desc);
      
      return reinterpret_cast<void*>(reinterpret_cast<std::ptrdiff_t>(p) + small_block_metadata_size);
    } else {
      size_t preamble_sz = large_block_metadata_size + num_log_ptrs * log_ptr_size;
      void *p = allocate_large(mutator::binary_log(raw_sz + preamble_sz), desc, num_log_ptrs);
      
      return reinterpret_cast<void*>(reinterpret_cast<std::ptrdiff_t>(p) + preamble_sz);
    }
  }
}
