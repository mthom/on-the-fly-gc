#ifndef IMPL_DETAILS_HPP_INCLUDED
#define IMPL_DETAILS_HPP_INCLUDED

#include <atomic>

namespace otf_gc
{
  namespace impl_details
  {
    using underlying_header_t = uint64_t;
    using header_t = std::atomic<underlying_header_t>;    

    using underlying_log_ptr_t = void*;
    using log_ptr_t = std::atomic<underlying_log_ptr_t>;
    
    static constexpr underlying_header_t zeroed_header = 0;
    static constexpr underlying_log_ptr_t zeroed_log_ptr = 0;

    static constexpr uint64_t color_bits = 2;
    static constexpr uint64_t tag_bits = 8;

    static constexpr uint64_t header_tag_mask = ((1 << tag_bits) - 1) << color_bits;
    static constexpr uint64_t header_color_mask = 0x3;
    static constexpr std::size_t header_size = sizeof(header_t);
    static constexpr std::size_t log_ptr_size = sizeof(log_ptr_t);
    static constexpr std::size_t log_ptr_offset = 2*sizeof(std::size_t) + 2*sizeof(void*);
    static constexpr std::size_t search_depth = 32;
    static constexpr std::size_t segment_size = 64; // size of a segment in bytes.
    static constexpr uint64_t small_block_metadata_size = header_size + log_ptr_size;
    static constexpr uint64_t small_block_size_limit    = 6;
    static constexpr uint64_t split_bits                = 32;
    static constexpr uint64_t split_mask                = (1ULL << split_bits) - 1;
    static constexpr uint64_t split_switch_bits         = 32;
    static constexpr uint64_t split_switch_mask         = ((1ULL << split_switch_bits) - 1) << split_bits;    
    static constexpr uint64_t large_block_metadata_size = 2*sizeof(void*) + header_size + sizeof(std::size_t);
    static constexpr uint64_t large_obj_min_bits  = 10;
    static constexpr uint64_t large_obj_threshold = 1 << (large_obj_min_bits - 1);
    static constexpr std::size_t mark_tick_frequency = 64;
    static constexpr std::size_t pool_chunk_size = 64;
    static constexpr std::size_t small_size_classes = 7;
    static constexpr std::size_t tick_frequency = 32;
  }
}

#endif
